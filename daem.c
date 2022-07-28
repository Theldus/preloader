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
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "ipc.h"
#include "log.h"
#include "util.h"
#include "load.h"

#ifndef PID_PATH
#define PID_PATH "/tmp"
#endif

#define IDX_ATEXIT_PTR 1
#define IDX_ARGC       3
#define IDX_ARGV       4

static uint8_t bck_start[1 + 1 + 10 + 2] = {0};
static uintptr_t addr_start;

/**
 *
 */
struct args args = {
	.port      = SV_DEFAULT_PORT,
	.pid_path  = PID_PATH,
	.log_lvl   = LOG_LVL_INFO,
	.log_file  = NULL,
	.log_fd    = STDERR_FILENO,
	.load_file = NULL,
};

/**
 *
 */
static char* daemon_main(int *argc)
{
	char *cwd_argv;
	int conn_fd;

	ipc_init(args.port);

	while (1)
	{
		conn_fd  = ipc_wait_conn();
		cwd_argv = ipc_recv_msg(conn_fd, argc);

		if (!cwd_argv)
			die("unable to receive message\n");

		if (fork() == 0)
		{
			unsetenv("LD_BIND_NOW");

			/* Close server listening socket on client. */
			ipc_finish();

			/* Close log file, because we do not need to
			 * inherit it. */
			log_close();

			/* Redirect stdout and stderr to the socket. */
			dup2(conn_fd, STDIN_FILENO);
			dup2(conn_fd, STDOUT_FILENO);
			dup2(conn_fd, STDERR_FILENO);
			close(conn_fd);

			/* Re-enable line-buffering again for stdout. */
			setvbuf(stdout, NULL, _IOLBF, 0);

			/* Set the current directory. */
			chdir(cwd_argv);

			/* Restore signal for SIGCHLD here. */
			signal(SIGCHLD, SIG_DFL);
			return (cwd_argv);
		}

		close(conn_fd);
		free(cwd_argv);
	}

	_exit(0);
}

/**
 *
 */
static void change_argv(int argc, char *cwd_argv, volatile uintptr_t *sp)
{
	char *p;    /* Character pointer.  */
	int count;  /* Current argv pos.   */
	char *argv; /* Argv pointer.       */

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
}

/**
 *
 */
static void pre_daemon_main(void)
{
	volatile uintptr_t *stack;
	volatile uintptr_t *sp;
	uintptr_t atexit_ptr;
	char *cwd_argv;
	int argc;
	int i;

	#define MAX_LOOKUP 15

	/* Get stack pointer. */
	__asm__ __volatile__ ("mov %%rsp, %%rax" : "=a" (stack));

	/* Attempt to find the return address. */
	for (i = 0, sp = stack; i < MAX_LOOKUP; i++, sp++)
		if (stack[i] == addr_start + (sizeof bck_start))
			break;

	if (i == MAX_LOOKUP)
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
			"Please launch with a greater argc!\n", (int)sp[IDX_ARGC], argc);

	sp[IDX_ARGC] = argc;

	/* ====================== Change argv. ====================== */
	change_argv(argc, cwd_argv, &sp[IDX_ARGV]);

	/*
	 * Finally our work is almost done, we need to X more things:
	 * 1) Restore _start original content
	 * 2) Fix the return address, so _start can be 'restarted' from
	 *    the beginning.
	 * 3) 'pop' and restore %rdx (atexit_ptr) value.
	 */

	/* 1) Restore original content. */
	memcpy((char*)addr_start, bck_start, sizeof bck_start);

	/* 2) Fix return address. */
	sp[0] -= sizeof bck_start;

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
static void parse_args(void)
{
	char *env;

	/* Check port. */
	if ((env = getenv("DAEM_PORT")) != NULL)
	{
		if (str2int(&args.port, env) < 0 ||
			(args.port < 0 || args.port > 65535))
		{
			die("Invalid port (%s)\n", env);
		}
	}

	/* Check for log level. */
	if ((env = getenv("DAEM_LOG_LVL")) != NULL)
	{
		if (!strcmp(env, "info"))
			args.log_lvl = LOG_LVL_INFO;
		else if (!strcmp(env, "err"))
			args.log_lvl = LOG_LVL_ERR;
		else if (!strcmp(env, "crit"))
			args.log_lvl = LOG_LVL_CRIT;
		else if (!strcmp(env, "all"))
			args.log_lvl = LOG_LVL_ALL;
		else
			die("Unrecognized log_lvl (%s), supported ones are: \n"
				"  info, err, crit and all!\n", env);
	}

	/* Check log file. */
	if ((env = getenv("DAEM_LOG_FILE")) != NULL)
	{
		args.log_file = strdup(env);
		args.log_fd = -1;
	}

	/* Check daemon. */
	if (getenv("DAEM_DAEMONIZE"))
	{
		args.daemonize = 1;
		args.log_fd = -1;
	}

	/* Check if should load a given file too. */
	if ((env = getenv("DAEM_LOAD_FILE")) != NULL)
		args.load_file = strdup(env);
}

/**
 *
 */
static void daemonize(void)
{
	int fd;

	/* Fork and let the parent dies. */
	if (fork() != 0)
		exit(0);

	/* Create a new session. */
	setsid();

	/*
	 * We can't close our fd's here because our children need
	 * to inherit them to redirect I/O to the socket.
	 */
}

/**
 *
 */
void __attribute__ ((constructor)) my_init(void)
{
	COMPILE_TIME_ASSERT(sizeof(void*) == sizeof(uintptr_t));

	parse_args();

	/* Check if we're already running, if so, do nothing. */
	if (!read_and_check_pid(PID_PATH, args.port))
		return;

	/* Initialize logs. */
	if (log_init(&args) < 0)
		die("Unable to initialize logging, plese check your parameters "
			"and try again!\n");

	/* Daemon. */
	if (args.daemonize)
		daemonize();

	/* PID file. */
	if (create_pid(PID_PATH, args.port) < 0)
		die("Unable to create pid file, aborting...\n");

	log_info("Initializing...\n");

	/* Setup signals. */
	signal(SIGCHLD, SIG_IGN);

	/* Read a load file, if specified. */
	if (args.load_file)
		load_file(args.load_file);

	/* Read our '_start' address. */
	if (!(addr_start = getauxval(AT_ENTRY)))
		die("Unable to get AT_ENTRY, aborting...\n");

	log_info("AT_ENTRY: %" PRIx64"\n", addr_start);

	if (make_rwx(addr_start, sizeof bck_start) < 0)
		die("Unable to set entry point as RWX!...\n");

	patch_start(addr_start);
}

void __attribute__ ((destructor)) my_fini(void){}
