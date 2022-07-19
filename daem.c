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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "ipc.h"
#include "util.h"

#define PID_FILE "/tmp/daem_3636.pid"

#define IDX_ATEXIT_PTR 1
#define IDX_ARGC       3
#define IDX_ARGV       4

static uint8_t bck_start[1 + 1 + 10 + 2] = {0};
static uintptr_t addr_start;

/**
 *
 */
static char* daemon_main(int *argc)
{
	uint64_t t0, t1;
	pid_t pid;
	char *argv;
	int conn_fd;

	printf("dmain: %d\n", (int)getpid());

	ipc_init();

	while (1)
	{
		conn_fd = ipc_wait_conn();

t0 = time_ms();
		argv = ipc_recv_msg(conn_fd, argc);

		if (!argv)
			die("unable to receive message\n");

		if ((pid = fork()) == 0)
			return (argv);
		else
			wait(NULL);
t1 = time_ms();

		printf("build time: %dms\n", t1-t0);

		_exit(0);
	}

	_exit(0);
}

/**
 *
 */
static void change_argv(int argc, char *cwd_argv, volatile uint64_t *sp)
{
	char *p;    /* Character pointer.  */
	int count;  /* Current argv pos.   */
	char *argv; /* Argv pointer.       */

	printf("cwd: %s (pid: %d)\n", cwd_argv, (int)getpid());

	/* Skip CWD. */
	for (p = cwd_argv; *p != '\0'; p++);
	p++; /* skip CWD-nul char. */

	/* Set argv nicely. */
	for (count = 0, argv = p; count < argc; p++)
	{
		if (*p == '\0')
		{
			sp[count++] = (uint64_t)argv;
			argv = p + 1;
		}
	}


	for (count = 0; count < argc; count++)
		printf("change_argv[%d] = %s\n", count, (char*)sp[count]);
}

/**
 *
 */
static void pre_daemon_main(void)
{
	volatile uint64_t *stack;
	volatile uint64_t *sp;
	uintptr_t atexit_ptr;
	char *cwd_argv;
	int argc;
	int i;

	stack = NULL;
	sp = NULL;

	/* Get stack pointer. */
	__asm__ __volatile__ ("mov %%rsp, %%rax" : "=a" (stack));

	/* Attempt to find the return address. */
	for (i = 0, sp = stack; i < 15; i++, sp++)
	{
		if (stack[i] == addr_start + (sizeof bck_start))
		{
			sp = &stack[i];
			break;
		}
	}

	if (i == 15)
		die("Unable to find the return address, "
			"I can't proceed\n");

	/* Backup atexit pointer. */
	atexit_ptr = sp[IDX_ATEXIT_PTR];

	/*
	 * Start our daemon and waits to return: when a new clang
	 * invocation should be made and we need to return
	 * nicely.
	 */
	cwd_argv = daemon_main(&argc);

	/* Change argc. */
	if ((int)sp[IDX_ARGC] < argc)
		die("PRELOADed lib has argc (%d) less than the required (%d) argc!\n"
			"Please launch with a greater argc!\n", sp[IDX_ARGC], argc);

	//dump_stack(sp);

	sp[IDX_ARGC] = argc;

	/* ====================== Change argv. ====================== */
	change_argv(argc, cwd_argv, &sp[IDX_ARGV]);

	/*
	 * Shift envp and auxv too .... errr... nope...
	 *
	 * Yes, that sounds strange, but it seems that glibc uses other
	 * means to find the environment variables, and... if we shift
	 * positions, functions like getenv() may not be able to find
	 * the variables in the expected positions.
	 */

	/*
	 * Finally our work is almost done, we need to X more things:
	 * 1) Restore _start original content (mprotect pensar!)
	 * 2) Fix the return address, so _start can be 'restarted' from
	 *    the beginning.
	 * 3) 'pop' and restore %rdx (atexit_ptr) value.
	 */

	/* 1) Restore original content. */
	memcpy((char*)addr_start, bck_start, sizeof bck_start);

	/* 2) Fix return address. */
	sp[0] -= sizeof bck_start;

	//dump_stack(sp);

	/*
	 * 3) 'pop' & restore %rdx.
	 *
	 * I'm definitely not proud of doing this, but it was the
	 * only way I could find to restore the original contents
	 * of %rdx and leave the stack the way it was before,
	 * _without_ the contents of rdx on the stack.
	 */
	__asm__ __volatile__
	(
		"leaveq\n"
		"mov (%%rsp),  %%rax\n" /* backup return address.          */
		"pop  %%rbx\n"  /* temporarily discard return address.     */
		"pop  %%rbx\n"  /* discard %rdx previously saved on stack. */
		"pop  %%rbx\n"  /* discard our stub value to align stack.  */
		"push %%rax\n"  /* restore return address.                 */
		"retq\n"
		:
		: "d" (atexit_ptr) /* load %rdi with atexit address. */
	);
}

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
 *
 */
static int patch_start(uintptr_t start)
{
	int i;
	uint8_t patch[sizeof bck_start] = {
		/* push %rdx (just to align the stack) */
		0x52,
		/* push %rdx */
		0x52,
		/* movabs $imm64, %rax. */
		0x48, 0xb8,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* callq *%rax. */
		0xff, 0xd0
	};

	union
	{
		int64_t addr;
		uint8_t val[8];
	} u_addr;

	/* Backup the bytes that we're overwritten. */
	memcpy(bck_start, (void*)start, sizeof patch);

	/* Calculate rel-offset. */
	u_addr.addr = (uint64_t)pre_daemon_main;

	/* Patch: movabs $imm64, %rax. */
	for (i = 0; i < 8; i++)
		patch[4 + i] = u_addr.val[i];

	memcpy((void*)start, patch, sizeof patch);
	return (0);
}

/**
 *
 */
void __attribute__ ((constructor)) my_init(void)
{
	COMPILE_TIME_ASSERT(sizeof(void*) == sizeof(uintptr_t));

	/* Check if we're already running. */
	if (!read_and_check_pid(PID_FILE))
		return;
	/* We should execute. */
	else
		if (create_pid(PID_FILE) < 0)
			exit(1);

	printf("Initializing...\n");

	addr_start = getauxval(AT_ENTRY);
	if (!addr_start)
		die("Unable to get AT_ENTRY, aborting...\n");

	printf("AT_ENTRY: %" PRIx64"\n", addr_start);

	if (make_rwx(addr_start, sizeof bck_start) < 0)
		die("Unable to set entry point as RWX!...\n");

	patch_start(addr_start);
}

void __attribute__ ((destructor)) my_fini(void){}
