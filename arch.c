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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "arch.h"
#include "log.h"
#include "util.h"

/* Environment variables pointer. */
extern char **environ;

/* Private auxiliary vector. */
static struct auxv_t {
	unsigned long type;
	unsigned long value;
} *auxv = NULL;

/**
 *
 */
static int make_rwx(uintptr_t p, size_t min_size)
{
	uintptr_t aligned_addr;
	size_t target_size;
	long page_size;

	page_size    = sysconf(_SC_PAGESIZE);
	aligned_addr = p & ~(page_size - 1);
	target_size  = page_size;

	if ((p - aligned_addr) < min_size)
		target_size = page_size * 2;

	if (mprotect((void*)aligned_addr, target_size,
		PROT_READ|PROT_WRITE|PROT_EXEC) < 0)
	{
		perror("make_rwx:");
		return (-1);
	}
	return (0);
}

/**
 * @brief Create a local copy of the auxiliary-vector.
 *
 * The old approach (reading from /proc/self/auxv) is not
 * always feasible: qemu-user shares the same /proc/self/auxv
 * pointer with the one obtained from main(), which implies
 * that auxv (even via /proc) can be changed when via qemu-user.
 *
 * To work around this, it's best to back up the auxv before
 * changing it, and then use the local copy instead of the one
 * provided by libc.
 */
static void init_local_auxv(void)
{
	size_t cur_size;
	ssize_t r;
	char *a;
	int fd;

	#define READ_SIZE 512

	cur_size = READ_SIZE;

	fd = open("/proc/self/auxv", O_RDONLY);
	if (fd < 0)
		die("No /proc/self/auxv available, giving up...\n");

	auxv = calloc(1, READ_SIZE);
	if (!auxv)
	{
		close(fd);
		die("Unable to allocated auxv!\n");
	}

	a = (char*)auxv;

	while ((r = read(fd, a, READ_SIZE)) == READ_SIZE)
	{
		a = realloc(auxv, cur_size + READ_SIZE);
		if (!a)
			die("Unable to realloc auxv!\n");

		auxv      = (struct auxv_t *)a;
		a        += cur_size;
		cur_size += READ_SIZE;
	}
	close(fd);
}

/**
 * @brief Retrieve a value from the auxiliary vector
 *
 * @param type Type to be retrieved.
 *
 * @return On success, returns the value corresponding
 * to @p type. Otherwise, returns 0.
 *
 * @note This function was made to replace the original
 * getauxval(): as with 'environ', auxv is also shifted
 * to the left and the reference that libc makes of it
 * no longer makes sense.
 *
 * Since there is no way to 'fix' the auxv pointer (and
 * this is quite library dependent), this function 'fixes'
 * getauxval() by replacing the original with another of
 * the same signature, which reads data directly from
 * /proc/self/auxv, which should always work.
 */
__attribute__((visibility("default")))
unsigned long getauxval(unsigned long type)
{
	struct auxv_t *a;

	if (!auxv)
		init_local_auxv();

	for (a = auxv; a->type; a++)
		if (a->type == type)
			return (a->value);

	return (0);
}

/**
 *
 */
void arch_change_argv(int argc, char *cwd_argv, uintptr_t *sp)
{
	char *p;          /* Character pointer.  */
	int count;        /* Current argv pos.   */
	char *argv;       /* Argv pointer.       */
	uintptr_t *dest;
	uintptr_t *src;
	uintptr_t *len;

	/* Skip CWD. */
	for (p = cwd_argv; *p != '\0'; p++);
	p++; /* skip CWD-nul char. */

	/* Set argv nicely. */
	for (count = 0, argv = p; count < argc; p++)
	{
		if (*p == '\0')
		{
			sp[count++] = (uintptr_t)argv;
			argv = p + 1;
		}
	}

	dest = &sp[count];

	/* Advance pointer until find a NUL, this is our source. */
	for (src = dest; *src; src++);

	/* Advance until find two NULs, this is our length. */
	for (len = src + 1; *len; len++);
	for (len = len + 1; *len; len++);

	/*
	 * Once the argv, envp and auxv were shifted to the left,
	 * the reference glibc made to 'envp' no longer makes
	 * sense and needs to be updated. This isn't exactly
	 * pretty, but it's the only way I've found to keep the
	 * argument list (when getting to _start) as expected
	 * _and_ not messing up glibc.
	 *
	 * Although I make references to glibc here, this
	 * should (or should) work in other libs such as Bionic.
	 */
	environ = (char **)&dest[1];

	/* Move envp and auxv into the parameter list. */
	while (dest <= len)
		*dest++ = *src++;
}

/**
 *
 */
void arch_validate_argc(int old_argc, int new_argc)
{
	if (old_argc < new_argc)
		die("PRELOADed lib has argc (%d) less than the required (%d) argc!\n"
			"Please launch with a greater argc!\n", old_argc, new_argc);
}

/**
 *
 */
void arch_setup(void)
{
	COMPILE_TIME_ASSERT(sizeof(void*) == sizeof(uintptr_t));

	uintptr_t arch_addr_start;

	/* Get _start/entry point address. */
	if (!(arch_addr_start = getauxval(AT_ENTRY)))
		die("Unable to get AT_ENTRY, aborting...\n");

	log_info("AT_ENTRY: %" PRIxPTR"\n", arch_addr_start);

	/* Make _start RWX. */
	if (make_rwx(arch_addr_start, 512) < 0)
		die("Unable to set entry point as RWX!...\n");

	/* Patch _start/entry point. */
	arch_patch_start(arch_addr_start);
}
