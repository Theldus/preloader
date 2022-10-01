/*
 * MIT License
 *
 * Copyright (c) 2022 Davidson Francis <davidsondfgl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <ftw.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <gelf.h>

#include "khash.h"

/*
 * This is Finder.
 * Finder is a ldd-like tool made specifically for preloader[0]: it searches
 * for the dependencies of one or more files (recursively, if folder) and
 * counts how many libraries and relocations each binary has.
 *
 * It is also possible to display the list of dependencies if DUMP_LIBS = 1.
 *
 * Think of it as a 'bloat detector': with the amount of dependencies on
 * each executable and relocations, you can get an idea of how big a
 * program is for the system and how long it can take to load.
 *
 * Usage:
 * ./finder <folder-or-file> <folder-or-file2> <folder-or-fileN...>
 *
 * Output:
 * "foo",4,1532
 * "bar",190,233467
 *
 * where:
 * first column:  ELF name.
 * second column: amount of shared libs/dependencies.
 * third column:  amount of relocations.
 *
 * Real-world scenario: Find the top-5 ELF files with the most amount of
 * dynamic libraries.
 *
 # 1) Get everything first:
 * $ ./finder / /home | tee allfiles.txt
 *
 * 2) Sort by second column in descending order:
 * $ sort -t',' -rg -k2 allfiles.txt | head -n5 | column -ts','
 * "/usr/bin/mplayer"                           219  218822
 * "/usr/lib64/qt5/libexec/QtWebEngineProcess"  196  615522
 * "/usr/bin/SDLvncviewer"                      190  171352
 * "/usr/bin/ffprobe"                           187  198403
 * "/usr/bin/ffplay"                            187  198472
 *
 * If sort by relocation amount:
 * $ sort -t',' -rg -k3 allfiles.txt | head -n5 | column -ts','
 * "/usr/bin/wireshark"         74   1480265
 * "/usr/bin/tshark"            45   1362767
 * "/usr/bin/rawshark"          45   1362226
 * "/usr/bin/sharkd"            42   1360746
 * "/opt/google/chrome/chrome"  92   820895
 *
 * Notes:
 * 1) The search do not cross mount-points, so each desired mount need to be
 * specified one by one. If your /home belongs to the root partition (/), there
 * is no need to do that, but only: ./finder /.
 *
 * 2) Finder deps: libelf.
 *
 * Links:
 * [0]: https://github.com/Theldus/preloader
 */

#define DUMP_LIBS    0
#define PRINT_HEADER 0
#define VERBOSE      0

/* Hashmap for our libs. */
KHASH_SET_INIT_STR(lib)

/* Fancy macros. */
#if VERBOSE == 1
#define errxit(...) \
	do { \
		fprintf(stderr, __VA_ARGS__); \
		exit(EXIT_FAILURE); \
	} while (0)

#define errto(lbl, ...) \
	do { \
		fprintf(stderr, __VA_ARGS__); \
		goto lbl; \
	} while (0)
#else
#define errxit(...) exit(EXIT_FAILURE)
#define errto(lbl, ...) goto lbl
#endif

/* ELF magic. */
static const unsigned char elf_magic[4] = {0x7f,0x45,0x4c,0x46};

/* Some possible search path for the libraries. */
static const char *search_path[] = {
	/* Slackware search path. */
	"/usr/lib64", "/lib64", "/usr/local/lib64",
	"/usr/x86_64-slackware-linux/lib64",
	/* Ubuntu search path. */
	"/usr/local/lib/i386-linux-gnu",
	"/lib/i386-linux-gnu",
	"/usr/local/lib/i686-linux-gnu",
	"/lib/i686-linux-gnu",
	"/usr/lib/i686-linux-gnu",
	"/usr/local/lib",
	"/usr/local/lib/x86_64-linux-gnu",
	"/lib/x86_64-linux-gnu",
	"/usr/lib/x86_64-linux-gnu",
	/* Raspberry-Pi (ARM32) search path. */
	"/opt/vc/lib",
	"/usr/local/lib/arm-linux-gnueabihf",
	"/lib/arm-linux-gnueabihf",
	"/usr/lib/arm-linux-gnueabihf",
	"/usr/lib/arm-linux-gnueabihf/libfakeroot",
	"/usr/local/lib"
};

/* Some data about the ELF file. */
struct open_elf
{
	Elf *e;
	int fd;
	Elf_Data *strtab_data;
};

static int dump_dyn_libs(struct open_elf *elf, Elf_Scn *scn, GElf_Shdr *shdr,
	khash_t(lib) *seen_list, uint64_t *rel_amnt);
static int dump_elf(int fd, const char *file, khash_t(lib) *seen_list,
	uint64_t *rel_amnt);

/**
 * @brief Given a library name, tries to found the appropriate
 * path for the library.
 *
 * E.g: foo.so -> /usr/lib/foo.so
 *
 * @param lib Library name.
 *
 * @return If found, returns a pointer containing the full
 * library path. If not found, returns NULL.
 */
static char* get_lib_path(const char *lib)
{
	static char path[PATH_MAX] = {0};
	struct stat st;
	size_t i;

	for (i = 0; i < sizeof search_path / sizeof search_path[0]; i++)
	{
		snprintf(path, PATH_MAX, "%s/%s", search_path[i], lib);
		if (!stat(path, &st))
			return (path);
	}

	return (NULL);
}

/**
 * @brief Given a FD or file, open the ELF file and initialize
 * its data structure.
 *
 * @param fd   File descriptor of an already opened file, if any.
 * @param file Path of the file to be opened, if any.
 * @param elf  Data structure where we should save some
 *             data about the ELF file.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int open_elf(int fd, const char *file, struct open_elf *elf)
{
	Elf_Kind ek;

	if (!elf)
		return (-1);

	if (fd > 0)
	{
		lseek(fd, 0, SEEK_SET);
		elf->fd = fd;
	}
	else
		if ((elf->fd = open(file, O_RDONLY, 0)) < 0)
			errto(out1, "Unable to open %s!\n", file);

	if ((elf->e = elf_begin(elf->fd, ELF_C_READ, NULL)) == NULL)
		errto(out2, "elf_begin() failed!\n");

	ek = elf_kind(elf->e);
	if (ek != ELF_K_ELF)
		errto(out2, "File \"%d\" (fd: %d) is not an ELF file!\n", file, fd);

	return (0);
out2:
	close(elf->fd);
out1:
	elf_end(elf->e);
	return (-1);
}

/**
 * @brief Given an opened ELF file, closes all the
 * resources.
 *
 * @param elf Opened ELF file.
 */
static void close_elf(struct open_elf *elf)
{
	if (!elf)
		return;
	if (elf->e)
		elf_end(elf->e);
	if (elf->fd > 0)
		close(elf->fd);
}

/**
 * @brief Give an ELF file, find the next section of type
 * @p sh_type and return it.
 *
 * @param elf     Opened ELF file.
 * @param scn     ELF section to be resumed from (NULL if
 *                from the beginning).
 * @param shdr    Elf section header.
 * @param sh_type Section type.
 *
 * @return Returns a pointer to the found section, NULL
 * if not found.
 */
static Elf_Scn *find_section(struct open_elf *elf, Elf_Scn *scn,
	GElf_Shdr *shdr, GElf_Word sh_type)
{
	while ((scn = elf_nextscn(elf->e, scn)) != NULL)
	{
		if (gelf_getshdr(scn, shdr) != shdr)
			continue;

		if (shdr->sh_type == sh_type)
			return (scn);
	}
	return (NULL);
}

/**
 * @brief Given an ELF file, get the amount of relocations present
 * in that file, i.e: the amount of entries for all sections of
 * type SHT_RELA and SHT_REL.
 *
 * @param elf Opened ELF file.
 *
 * @return Returns the amount of relocations found, 0 if none.
 */
static uint64_t get_relocs_amnt(struct open_elf *elf)
{
	Elf_Scn *scn;
	GElf_Shdr shdr;
	uint64_t size;

	scn  = NULL;
	size = 0;
	while ((scn = elf_nextscn(elf->e, scn)) != NULL)
	{
		if (gelf_getshdr(scn, &shdr) != &shdr)
			continue;

		if (shdr.sh_type != SHT_RELA && shdr.sh_type != SHT_REL)
			continue;

		size += (shdr.sh_size / shdr.sh_entsize);
	}
	return (size);
}

/**
 * @Brief Given a opened ELF file, find a load its strtab content.
 *
 * @param elf Opened ELF file.
 *
 * @return Returns 1 if success, 0 otherwise.
 */
static int load_strtab(struct open_elf *elf)
{
	Elf_Scn *scn;
	GElf_Shdr shdr;

	scn = NULL;
	if ((scn = find_section(elf, scn, &shdr, SHT_STRTAB)) == NULL)
		errto(out0, "Unable to find strtab section!\n");

	elf->strtab_data = elf_getdata(scn, NULL);
	if (!elf->strtab_data)
		errto(out0, "Unable to find strtab data!\n");

	return (1);
out0:
	return (0);
}

/**
 * @brief Given a library name, try to find the proper
 * path and then add to the seen_list table.
 *
 * @param lib_needed Library name.
 * @param seen_list Hashtable representing the list of already seen
                    libraries at the moment.
 * @param path_lib  Pointer where the complete library path will
 *                  be saved.
 *
 * @return Returns -1 if error, 0 if already present, 1 if added
 * with success.
 */
static int add_lib_to_seen(const char *lib_needed,
	khash_t(lib) *seen_list, char **path_lib)
{
	char *path;
	khint_t k;
	int ret;

	/* Try to add in our hash table. */
	path = get_lib_path(lib_needed);
	if (!path)
		return (-1); /* path not found. */

	k = kh_put(lib, seen_list, path, &ret);
	if (ret < 0)
		errxit("Unable to put lib in seen list, aborting!\n");

	/* Already present, skip this one. */
	else if (ret == 0)
		return (0);

	/* Not present, add the lib. */
	path = strdup(path);
	if (!path)
		errxit("Unable to dup string!\n");

	kh_key(seen_list, k) = path;
	*path_lib = path;
	return (1);
}

/**
 * @brief For a given opened ELF file, dump its content
 * recursively.
 *
 * @param elf Opened ELF file.
 * @param scn Elf dynamic section to be dumped.
 * @param shdr Section header for the given section.
 * @param seen_list Hashtable representing the list of already seen
                    libraries at the moment.
 * @param rel_amnt  Total relocation count.
 *
 * @return Returns -1 if error, 0 if success.
 */
static int dump_dyn_libs(struct open_elf *elf, Elf_Scn *scn, GElf_Shdr *shdr,
	khash_t(lib) *seen_list, uint64_t *rel_amnt)
{
	GElf_Dyn  dyn_entry;
	Elf_Data *dyn_data;
	char *lib_name;     /* library name: libc.so. */
	char *path_lib;     /* full library path: /usr/lib64/libc.so. */
	int count;
	int i;

	dyn_data = elf_getdata(scn, NULL);
	if (!dyn_data)
		errto(out0, "Unable to retrieve data from dynamic section!\n");

	count = shdr->sh_size / shdr->sh_entsize;

	/* Close our ELF fd. */
	close(elf->fd); elf->fd = -1;

	for (i = 0; i < count; i++)
	{
		if (!gelf_getdyn(dyn_data, i, &dyn_entry))
			continue;

		if (dyn_entry.d_tag != DT_NEEDED)
			continue;

		lib_name = (char *)elf->strtab_data->d_buf +
			dyn_entry.d_un.d_val;

		/*
		 * Try to add lib to seem list:
		 * If:   0: already exists
		 * If: < 0: unable to get the current path, skip too
		 * If: > 0: not exists, added with success.
		 */
		if (add_lib_to_seen(lib_name, seen_list, &path_lib) <= 0)
			continue;

		dump_elf(-1, path_lib, seen_list, rel_amnt);
	}
	return (0);
out0:
	return (-1);
}

/**
 * @brief Given a file (fd or path), dump its content: relocation
 * amount and recursive list of libraries.
 *
 * @param fd Opened fd, if any.
 * @param file File path, if any.
 * @param seen_list Hashtable representing the list of already seen
                    libraries at the moment.
 * @param rel_amnt  Total relocation count.
 *
 * @return Returns -1 if error and 0 if success.
 */
static int dump_elf(int fd, const char *file, khash_t(lib) *seen_list,
	uint64_t *rel_amnt)
{
	struct open_elf op_elf = {0};
	GElf_Shdr shdr;
	Elf_Scn *scn;
	int ret;

	ret = -1;

	if (open_elf(fd, file, &op_elf) < 0)
		return (-1);

	if (!load_strtab(&op_elf))
		goto out;

	/* Increment total rel amount. */
	*rel_amnt += get_relocs_amnt(&op_elf);

	/* Dump dyn-libs. */
	scn = NULL;
	while ((scn = find_section(&op_elf, scn, &shdr, SHT_DYNAMIC)))
		dump_dyn_libs(&op_elf, scn, &shdr, seen_list, rel_amnt);

	ret = 0;
out:
	close_elf(&op_elf);
	return (ret);
}

/**
 * @brief For a given file path, check if the given
 * file is an ELF file or not.
 *
 * @param path File to be checked.
 *
 * @return Returns a negative number if not an ELF,
 * and the file descriptor of the opened ELF file
 * otherwise.
 *
 * @note If @p path is an ELF file, the opened fd is not
 * closed here.
 */
static int is_elf(const char *path)
{
	unsigned char buff[17] = {0}; /* including part of the ELF header. */
	int ret;
	int fd;

	ret = -1;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return (ret);

	if (read(fd, buff, sizeof buff) != sizeof buff)
		goto out;

	/* Check signature. */
	if (memcmp(buff, elf_magic, sizeof elf_magic))
		goto out;

	/*
	 * Check header.
	 * 2 == executable, 3 == shared object.
	 */
	if (buff[16] != 2 && buff[16] != 3)
		goto out;

	return (fd);
out:
	close(fd);
	return (ret);
}

/**
 * @brief For a given path, check if it is an ELF file
 * and then dump is content.
 *
 * @param path Potential ELF file to be analyzed.
 */
static void handle_possible_elf(const char *path)
{
	int fd;
	char *l;
	khint_t k;
	uint64_t rel_amnt;
	khash_t(lib) *seen_list;

	rel_amnt = 0;

	/* Skip if not ELF file. */
	if ((fd = is_elf(path)) < 0)
		return;

	/* Initialize our hash map. */
	seen_list = kh_init(lib);
	if (!seen_list)
		errxit("Unable to create the seen table!\n");

	/* Open ELF file. */
	if (dump_elf(fd, path, seen_list, &rel_amnt) < 0)
		return;

	printf("\"%s\",%d,%d\n",
		path, (int)kh_size(seen_list), (int)rel_amnt);

#if DUMP_LIBS == 1
	printf("\nlibs:\n");
#endif

	/* Dump our seen list. */
	for (k = 0; k < kh_end(seen_list); k++)
	{
		if (!kh_exist(seen_list, k))
			continue;

		l = (char*)kh_key(seen_list, k);
#if DUMP_LIBS == 1
		puts(l);
#endif
		free(l);
	}

#if DUMP_LIBS == 1
	printf("\n");
#endif

	kh_destroy(lib, seen_list);
}

/**
 * @brief Roughly check if a given path is a library
 * or not.
 *
 * @param path Path to be checked.
 *
 * @return Returns 1 if a possible library, 0 otherwise.
 */
static int is_lib(const char *path)
{
	char *p2 = strdup(path);
	char *base;
	size_t len;
	int ret;

	ret  = 0;
	base = basename(p2);
	len  = strlen(base);

	/* Must be at least lib[letter].so, or 7 characters. */
	if (len < 7)
		goto out;

	if (!strncmp(base, "lib", 3))
		if (strstr(base, ".so"))
			ret = 1;
out:
	free(p2);
	return (ret);
}

/**
 * @brief nftw() handler, called each time a new file
 * is discovered.
 *
 * @param filepath File path to be analyzed.
 * @param info File path stat structure.
 * @param typeflag File path type.
 * @param pathinfo File path additional infos.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int do_check(const char *path, const struct stat *info,
	const int typeflag, struct FTW  *pathinfo)
{
	((void)pathinfo);

	/* Ignore everything that is not a file or is empty. */
	if (typeflag != FTW_F || !info->st_size)
		return (0);

	/* Skip non executables. */
	if (!(info->st_mode & S_IXUSR))
		return (0);

	/* Skip libs. */
	if (is_lib(path))
		return (0);

	/* Check if ELF and handle it. */
	handle_possible_elf(path);
	return (0);
}

/* Main. */
int main(int argc, char **argv)
{
	struct stat path_stat;
	char *path;
	int res;
	int i;

	if (argc < 2)
		errxit("Usage: %s <root-folder-to-search OR file>", argv[0]);

	if (elf_version(EV_CURRENT) == EV_NONE)
		errxit("Unable to initialize libelf\n");

#if PRINT_HEADER == 1
	/* Print header, even if we not found anything. */
	printf("binary_file sh_libs_amnt total_reloc_amnt\n");
#endif

	for (i = 1; i < argc; i++)
	{
		path = argv[i];

		if (stat(path, &path_stat) != 0)
			errxit("Unable to stat path: %s\n", path);

		if (S_ISREG(path_stat.st_mode))
			handle_possible_elf(path);
		else if (S_ISDIR(path_stat.st_mode))
		{
			res = nftw(path, do_check, 10, FTW_PHYS|FTW_MOUNT);
			if (res)
				errxit("NFTW error!\n");
		}
		else
			errxit("Parameter (%s) is not a regular file nor directory!\n");
	}

	return (0);
}
