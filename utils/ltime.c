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

#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <gelf.h>
#include <libelf.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/wait.h>

/*
 * This is LTime.
 *
 * LTime (Load Time) is another preloader auxiliary tool that aims to
 * obtain the times of a given program with and without the preloader.
 * LTime supports multiple files at once, as well as multiple folders,
 * for recursive analysis.
 *
 * The main idea is that ltime can be used to investigate potential
 * programs that can be accelerated with preloader. LTime is fast
 * enough to run system wide (~5k ELF executables) in about 30 minutes.
 *
 * Usage:
 *  ./ltime <folder-or-file> <folder-or-file> ....
 * Like:
 *  ./ltime clang ffprobe
 *  ./ltime /usr/bin/clang
 *  ./ltime /usr/bin /bin /home bin1 bin2 /path/bin1
 *
 * Output:
 *  $ ./ltime foo
 *  "foo", 21.916714 ms, 1.291591 ms
 *
 * First column:  file analyzed
 * Second column: time without preloader (normal run)
 * Third column:  time with preloader
 *
 * Real-world scenario: Find the top-5 ELF files with the longer
 * load times without preloader:
 *
 * 1) Get everything first:
 *  $ ./ltime / | tee allfiles.txt
 *
 * 2) Sort by second column in descending order
 *  $ sort -t',' -rg -k2 allfiles.txt | head -n5 | column -ts','
 *  "/usr/bin/mplayer"                          52.136383 ms   1.689277 ms
 *  "/usr/bin/ffprobe"                          49.085384 ms   1.490358 ms
 *  "/usr/lib64/qt5/libexec/QtWebEngineProcess" 45.864020 ms   1.565409 ms
 *  "/usr/bin/SDLvncviewer"                     41.423378 ms   1.487050 ms
 *  "/usr/bin/ffmpeg"                           37.896008 ms   1.410382 ms
 *
 * Notes:
 * 1) The search do not cross mount-points, so each desired mount need to be
 * specified one by one. If your /home belongs to the root partition (/), there
 * is no need to do that, but only: ./finder /.
 *
 * 2) Finder deps: libelf.
 *
 * 3) Please note that the times are relative to the load time of the
 * dynamic libraries, not the (complete) execution of the program.
 *
 * Preloader only becomes useful if the load time consumes a good
 * portion of the total execution time, examples like:
 *
 *  $ time foo <parameters>  # doing some useful computation
 *  real    0m0.051s
 *  user    0m0.040s
 *  sys     0m0.011s
 *
 *  $ ./ltime foo
 *  "foo", 38.517846 ms, 1.702319 ms
 *
 * It can be seen that 51ms was spent for a useful computation, where
 * 38ms is spent on load time... great opportunity for preloading.
 *
 * Implementation notes:
 * How it works is very simple: for each file to be analyzed, ltime
 * temporarily copies the binary to /tmp (or TMPDIR), and adds three
 * instructions to the program's entry point that basically just
 * terminate the program's execution.
 *
 * (Since the dynamic loader has already loaded all libraries at
 * this point, it is safe to terminate the program as soon as it
 * reaches the entrypoint. This is *not true* for Musl, and
 * libraries are loaded _after_ the entrypoint, so Musl is not
 * supported).
 *
 * Once this is done, the times are obtained with the execution
 * with and without the preloader.
 *
 * Links:
 * [0]: https://github.com/Theldus/preloader
 */

#define VERBOSE 1

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

/* Patches. */

/* mov $60, %rax # NR_exit.
 * mov $0,  %rdi
 * syscall */
static const unsigned char patch_amd64[] = {
	0x48,0xc7,0xc0,0x3c,0x00,0x00,0x00,0x48,
	0xc7,0xc7,0x00,0x00,0x00,0x00,0x0f,0x05
};

/* mov $0x1, %eax # NR_exit.
 * xor %ebx, %ebx
 * int $0x80 */
static const unsigned char patch_i386[] = {
	0xb8,0x01,0x00,0x00,0x00,0x31,0xdb,0xcd,0x80
};

/* mov x8, #93 # NR_exit.
 * mov x0, #0
 * svc 0 */
static const unsigned char patch_aarch64[] = {
	0x00,0x00,0x80,0xd2,0xa8,0x0b,0x80,0xd2,
	0x01,0x00,0x00,0xd4
};

/* mov r7, #1 # NR_exit.
 * mov r0, #0
 * swi 0 */
static const unsigned char patch_arm[] = {
	0x01,0x70,0xa0,0xe3,0x00,0x00,0xa0,0xe3,
	0x00,0x00,0x00,0xef
};

/* Some data about the ELF file. */
static Elf *elf;
static int nruns;
static int fd_elf;
static regex_t regex;
static char target_file[PATH_MAX];

/**
 * @brief Given a file, open the ELF file and initialize
 * its data structure.
 *
 * @param file Path of the file to be opened, if any.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int open_elf(const char *file)
{
	Elf_Kind ek;

	if ((fd_elf = open(file, O_RDWR, 0)) < 0)
		errto(out1, "Unable to open %s!\n", file);

	if ((elf = elf_begin(fd_elf, ELF_C_READ, NULL)) == NULL)
		errto(out2, "elf_begin() failed!\n");

	ek = elf_kind(elf);
	if (ek != ELF_K_ELF)
		errto(out2, "File \"%s\" is not an ELF file!\n", file);

	return (0);
out2:
	close(fd_elf);
out1:
	elf_end(elf);
	return (-1);
}

/**
 * @brief Given an opened ELF file, closes all the
 * resources.
 *
 * @param elf Opened ELF file.
 */
static void close_elf(void)
{
	if (elf)
	{
		elf_end(elf);
		elf = NULL;
	}
	if (fd_elf > 0)
	{
		close(fd_elf);
		fd_elf = -1;
	}
}

/**
 * @brief For a given @p file, get its entry point, machine type,
 * and check if it is dynamic or not.
 *
 * @param file File to be checked.
 * @param machine Returned machine type.
 * @param is_dyn Returns 1 if dynamic, 0 if not.
 *
 * @return Returns the file offset if success, -1 if error.
 */
static off_t get_entry_offset(char *file, int *machine, int *is_dyn)
{
	GElf_Ehdr ehdr;
	GElf_Addr entry;
	GElf_Phdr phdr;
	size_t i, num;
	off_t foff;

	foff = -1;

	if (open_elf(file) < 0)
		goto out0;

	gelf_getehdr(elf, &ehdr);

	/* Get entry point. */
	entry = ehdr.e_entry;
	*machine = ehdr.e_machine;

	/* Iterate over the program headers to find the section
	 * containing the address of the entry point. */
	if (elf_getphdrnum(elf, &num) < 0)
		goto out0;

	/* Get entry offset. */
	for (i = 0; i < num; i++)
	{
		gelf_getphdr(elf, i, &phdr);
		if (phdr.p_type != PT_LOAD)
			continue;

		if (entry >= phdr.p_vaddr &&
			entry < phdr.p_vaddr + phdr.p_memsz)
		{
			foff = entry - phdr.p_vaddr + phdr.p_offset;
			break;
		}
	}

	/*
	 * Check if its dynamic
	 *
	 * Note: Ideally we should also check for static-pie
	 * executables, but I'm too lazy to do that...
	 * homework for who are reading this =).
	 */
	for (*is_dyn = 0, i = 0; i < num; i++)
	{
		gelf_getphdr(elf, i, &phdr);
		if (phdr.p_type == PT_DYNAMIC)
		{
			*is_dyn = 1;
			break;
		}
	}

	/* Return error if binary is not dynamic. */
	if (!*is_dyn)
		return (-1);

out0:
	return (foff);
}

/**
 * @brief Patch the current file (target_file) accordingly
 * to the architecture supported.
 *
 * @param off File offset to apply patch.
 * @param machine Machine type.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int patch_file(off_t off, int machine)
{
	const unsigned char *patch;
	size_t sp;

	switch (machine)
	{
		case EM_X86_64:
			patch = patch_amd64;
			sp = sizeof(patch_amd64);
			break;
		case EM_386:
			patch = patch_i386;
			sp = sizeof(patch_i386);
			break;
		case EM_AARCH64:
			patch = patch_aarch64;
			sp = sizeof(patch_aarch64);
			break;
		case EM_ARM:
			patch = patch_arm;
			sp = sizeof(patch_arm);
			break;
		default:
			fprintf(stderr, "Error, architecture not identified!\n");
			return (-1);
	}

	if (lseek(fd_elf, off, SEEK_SET) < 0)
		return (-1);

	if (write(fd_elf, patch, sp) != (ssize_t)sp)
		return (-1);

	return (0);
}

/**
 * @brief Given a file, copy it to /tmp (or TMPDIR,
 * if it exists).
 *
 * @param file File to be copied.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static ssize_t copy_to_tmp(const char *file)
{
	char *t, *base, *tmp;
	struct stat st = {0};
	int fdi, fdo;
	ssize_t ret;

	ret = -1;

	if ((t = getenv("TMPDIR")) == NULL)
		t = "/tmp";

	if ((fdi = open(file, O_RDONLY)) < 0)
		return (-1);

	tmp  = strdup(file);
	base = basename(tmp);
	snprintf(target_file, sizeof target_file - 1, "%s/%s", t, base);
	free(tmp);

	if ((fdo = creat(target_file, 0755)) < 0)
		goto out0;

	fstat(fdi, &st);
	ret = sendfile(fdo, fdi, NULL, st.st_size);

	if (ret != st.st_size)
		goto out0;

	ret = 0;
out0:
	close(fdo);
	close(fdi);
	return (ret);
}

/**
 * @brief Get the file path and mode for the specified
 * file @p file, depending whether the file exists in
 * the informed path or in the PATH.
 *
 * @param file File to get the retrieved path.
 * @param mode File mode retrieved.
 *
 * @return If successful, returns the file path;
 * otherwise, returns NULL.
 */
static char *get_file_path(char *file, mode_t *mode)
{
	char *t, *path, *cpath, tfile[PATH_MAX - 1] = {0};
	struct stat st;
	char *tokptr;

	if (!stat(file, &st))
	{
		*mode = st.st_mode;
		return (file);
	}

	/* Check against PATH if the file is there. */
	path   = t = strdup(getenv("PATH"));
	tokptr = NULL;

	for (cpath = strtok_r(path, ":", &tokptr); cpath != NULL;
		 cpath = strtok_r(NULL, ":", &tokptr))
	{
		snprintf(tfile, PATH_MAX - 1, "%s/%s", cpath, file);

		if (!stat(tfile, &st))
		{
			*mode = st.st_mode;
			strncpy(target_file, tfile, sizeof target_file - 1);
			free(t);
			return (target_file);
		}
	}

	free(t);
	return (NULL);
}

/**
 * @brief For a given file path, check if the given
 * file is an ELF file or not.
 *
 * @param path File to be checked.
 *
 * @return Returns 0 if ELF, -1 otherwise.
 */
static int is_elf(const char *path)
{
	unsigned char buff[4] = {0};
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

	ret = 0;
out:
	close(fd);
	return (ret);
}

/**
 * @brief Starts a preloader daemon for the contents
 * of the variable 'target_file'.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int start_daemon(void)
{
	struct stat st;
	int wstatus;
	char *path;
	pid_t pid;

	path = realpath("../libpreloader.so", NULL);
	if (!path || stat(path, &st) < 0)
	{
		free(path);
		path = realpath("libpreloader.so", NULL);
		if (!path || stat(path, &st) < 0)
		{
			free(path);
			path = "/usr/local/lib/libpreloader.so";
			if (stat(path, &st) < 0)
				return (-1);
		}
	}

	if ((pid = fork()) == 0)
	{
		putenv("LD_BIND_NOW=1");
		putenv("PRELOADER_DAEMONIZE=1");
		setenv("LD_PRELOAD", path, 1);
		execlp(target_file, target_file, NULL);
		exit(1);
	}
	free(path);

	waitpid(pid, &wstatus, 0);

	if (WIFEXITED(wstatus))
	{
		if (WEXITSTATUS(wstatus))
			return (-1);
	}
	else
		return (-1);

	return (0);
}

/**
 * @brief Stops a running preloader daemon.
 *
 * @return Returns 0 if successfully stopped, -1 otherwise.
 */
static int stop_daemon(void)
{
	char *tmp, pid_file[PATH_MAX];
	char buff[16] = {0};
	pid_t pid;
	ssize_t r;
	int ret;
	int fd;
	int i;

	ret = -1;

	if (!(tmp = getenv("TMPDIR")))
		tmp = "/tmp";

	if (strlen(tmp) + sizeof "/preloader_3636.pid" > sizeof pid_file)
		errto(out0, "pid_file exceeds path capacity!\n");

	snprintf(pid_file, sizeof pid_file - 1, "%s/preloader_3636.pid", tmp);

	if ((fd = open(pid_file, O_RDONLY)) < 0)
		errto(out0, "PID file (%s) not found!\n", pid_file);

	if ((r = read(fd, buff, sizeof buff)) < 0)
		errto(out1, "Unable to read complete file!\n");

	for (pid = 0, i = 0; i < r; i++)
	{
		if (buff[i] < '0' || buff[i] > '9')
			errto(out1, "Malformed pid file!\n");
		else
		{
			pid *= 10;
			pid += (buff[i] - '0');
		}
	}

	/* Try to kill the process. */
	if (kill(pid, SIGTERM) < 0)
		errto(out1, "Unable to kill daemon, maybe its not running?\n");

	ret = 0;
out1:
	close(fd);
	unlink(pid_file);
out0:
	return (ret);
}

/**
 * @brief Starts a new process a measure its execution time.
 *
 * @param preload Should run prealoder_cli?
 *
 * @return If success, returns the elapsed time
 * (in milliseconds), otherwise, returns -1.
 */
static double spawn_child_ms(int preload)
{
	struct timespec ts1, ts2;
	char *proc, *arg1;
	struct stat st;
	int wstatus;
	pid_t pid;
	double ms;

	proc = target_file;
	arg1 = target_file;
	if (preload)
	{
		arg1 = target_file;
		proc = "../preloader_cli";
		if (stat(proc, &st) < 0)
		{
			proc = "./preloader_cli";
			if (stat(proc, &st) < 0)
			{
				proc = "/usr/local/bin/preloader_cli";
				if (stat(proc, &st) < 0)
					return (-1);
			}
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &ts1);
		if ((pid = fork()) == 0)
		{
			execlp(proc, proc, arg1, NULL);
			exit(1);
		}
		waitpid(pid, &wstatus, 0);
	clock_gettime(CLOCK_MONOTONIC, &ts2);

	ms = ((ts2.tv_sec - ts1.tv_sec)*1000) +
		((double)(ts2.tv_nsec - ts1.tv_nsec)/1000000);

	/* If abnormal exit (!= 0) or got signaled.
	 * return a negative time, to indicate failure. */
	if (WIFEXITED(wstatus))
	{
		if (WEXITSTATUS(wstatus))
			ms = -1;
	}
	else
		ms = -1;

	return (ms);
}

/**
 * @brief Benchmark a given file @p tfile both normally
 * and with preloader.
 *
 * The measured time is sent to stdout.
 *
 * @param orig_file Original parameter as-is.
 * @param tfile Processed file (considering PATH).
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int handle_file(const char *orig_file, const char *tfile)
{
	double ms_normal, ms_pre, ms_tmp;
	int machine;
	off_t foff;
	int is_dyn;
	int ret;
	int i;

	ret = -1;

	/* Check if ELF. */
	if (is_elf(tfile) < 0)
		errto(out0, "%s its not an ELF file!\n", tfile);

	/* Copy our target file to TMP. */
	if (copy_to_tmp(tfile) < 0)
		errto(out0, "Unable to copy file: %s...\n", tfile);

	/* Get file entry offset and machine type. */
	if ((foff = get_entry_offset(target_file, &machine, &is_dyn)) < 0)
		errto(out1, "Unable to get file offset or binary is static!\n");

	/* Patch file for the appropriate architecture. */
	if (patch_file(foff, machine) < 0)
		errto(out1, "Unable to patch file!\n");
	close_elf();

	/* =============== TIMMINGS START =============== */
	if (start_daemon() < 0)
		errto(out1, "Unable to start preloader daemon!\n");
	usleep(250*1000); /* Wait for daemon start. */

	/* Cache file first. */
	spawn_child_ms(0);

	/* -- Normal run -- */
	for (ms_normal = 0, i = 0; i < nruns; i++)
	{
		if ((ms_tmp = spawn_child_ms(0)) < 0)
			errto(out2, "Error while during normal run, idx: %d\n", i);

		ms_normal += ms_tmp;
	}
	ms_normal /= nruns;

	/* -- Preloader run -- */
	for (ms_pre = 0, i = 0; i < nruns; i++)
	{
		if ((ms_tmp = spawn_child_ms(1)) < 0)
			errto(out2, "Error while during preloader run, idx: %d\n", i);

		ms_pre += ms_tmp;
	}
	ms_pre /= nruns;

#if VERBOSE == 1
	printf("file: \"%s\", w/o: %f ms, w/ preloader: %f ms\n",
		orig_file, ms_normal, ms_pre);
#else
	printf("\"%s\", %f ms, %f ms\n", orig_file, ms_normal, ms_pre);
#endif

	ret = 0;
out2:
	stop_daemon();
out1:
	unlink(target_file);
	close_elf();
out0:
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
	if (!regexec(&regex, path, 0, NULL, 0))
		return (0);

	/* Check if ELF and handle it. */
	handle_file(path, path);
	return (0);
}

/* Usage. */
static void usage(const char *prg)
{
	fprintf(stderr,
		"Usage: %s [-r <num_runs>] [<program-name-or-path>...]\n"
		"Options: \n"
		"  -r <num_runs> How many times to run to get the average\n",
		prg);
	exit(EXIT_FAILURE);
}

/**
 * Safe string-to-int routine that takes into account:
 * - Overflow and Underflow
 * - No undefined behavior
 *
 * Taken from https://stackoverflow.com/a/12923949/3594716
 * and slightly adapted: no error classification, because
 * I don't need to know, error is error.
 *
 * @param out Pointer to integer.
 * @param s String to be converted.
 *
 * @return Returns 0 if success and a negative number otherwise.
 */
static int str2int(int *out, const char *s)
{
	char *end;
	if (s[0] == '\0' || isspace(s[0]))
		return (-1);
	errno = 0;

	long l = strtol(s, &end, 10);

	/* Both checks are needed because INT_MAX == LONG_MAX is possible. */
	if (l > INT_MAX || (errno == ERANGE && l == LONG_MAX))
		return (-1);
	if (l < INT_MIN || (errno == ERANGE && l == LONG_MIN))
		return (-1);
	if (*end != '\0')
		return (-1);

	*out = l;
	return (0);
}

/**
 * Handle command-line arguments accordingly
 * with their position.
 */
static int handle_args(int argc, char **argv)
{
	int idx_files;

	idx_files = 1;
	nruns = 1;

	/* Valid num args. */
	if (argc < 2)
		errto(err0, "At least <program-name-or-path> is required!\n");

	/* Get number of runs. */
	if (!strcmp(argv[1], "-r"))
	{
		if (argc < 4)
			usage(argv[0]);

		if (str2int(&nruns, argv[2]) < 0)
			errto(err0, "Parameter '%s' is not a valid number!\n",
				argv[2]);

		if (!nruns)
			errto(err0, "Parameter '%s' must be greater than 0!\n",
				argv[2]);

		idx_files = 3;
	}

	return (idx_files);
err0:
	usage(argv[0]);
	return (0);
}

/* Main. */
int main(int argc, char **argv)
{
	int i;
	int res;
	mode_t mode;
	int idx_files;
	char *file_path;

	/* Init libelf. */
	if (elf_version(EV_CURRENT) == EV_NONE)
		errxit("Unable to initialize libelf\n");

	/* Init our regex to skip libs. */
	if (regcomp(&regex, ".+\\.so(\\.[0-9]+)*$", REG_EXTENDED) != 0)
		errto(out0, "Failed to to compile regex!\n");

	idx_files = handle_args(argc, argv);

	for (i = idx_files; i < argc; i++)
	{
		file_path = get_file_path(argv[i], &mode);
		if (!file_path)
			errto(out0, "ELF file (%s) not found!\n", argv[i]);

		if (S_ISREG(mode))
			handle_file(argv[i], file_path);
		else if (S_ISDIR(mode))
		{
			res = nftw(file_path, do_check, 10, FTW_PHYS|FTW_MOUNT);
			if (res)
				errto(out0, "NFTW error, path: %s\n", file_path);
		}
		else
			errto(out0, "Parametr (%s) is not a regular file!\n", file_path);
	}
out0:
	regfree(&regex);
	return (0);
}
