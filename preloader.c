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

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arch.h"
#include "ipc.h"
#include "load.h"
#include "log.h"
#include "preloader.h"
#include "reaper.h"
#include "util.h"


#ifndef PID_PATH
#define PID_PATH "/tmp"
#endif

/**
 * Preloader arguments.
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
 * @brief Setup most of the things that should be done before
 * our child/real process execute.
 *
 * @param conn_fd Main connection socket fd.
 * @param stdout_fd Stdout socket fd.
 * @param stderr_fd Stderr socket fd.
 * @param stdin_fd Stdin socket fd.
 * @param cwd_argv Current work dir + argument list.
 *
 * @return Returns the new argv the child process should have.
 */
static char* setup_child(int conn_fd, int stdout_fd, int stderr_fd,
	int stdin_fd, char *cwd_argv)
{
	setenv("LD_BIND_NOW", "", 1);

	/* Close server listening socket on client. */
	ipc_finish();

	/* Close log file, because we do not need to
	 * inherit it. */
	log_close();

	/* Deallocates reaper data structures. */
	reaper_finish();

	/* Redirect stdout and stderr to the socket. */
	dup2(stdin_fd,  STDIN_FILENO);
	dup2(stdout_fd, STDOUT_FILENO);
	dup2(stderr_fd, STDERR_FILENO);
	ipc_close(4, stdin_fd, stdout_fd, stderr_fd, conn_fd);

	/* Re-enable line-buffering again for stdout. */
	setvbuf(stdout, NULL, _IOLBF, 0);

	/* Set the current directory. */
	if (chdir(cwd_argv) < 0)
		die("Unable to chdir to: %s, aborting...\n", cwd_argv);

	/* Restore default signal handler. */
	signal(SIGTERM, SIG_DFL);
	return (cwd_argv);
}

/**
 * @brief *This* is where occurs the main loop.
 *
 * daemon_main() is responsible to accept connections, receive
 * its arguments, fork a new process (to be normally executed)
 * and the proceeds to handle a next connection.
 *
 * @param argc Argument count pointer, this is the new child
               argc.
 *
 * @return Returns the new argv the child process should have.
 *
 * @note This routine is called from arch_pre_daemon_main(),
 * where the new argc/argv are actually changed, when this
 * function returns.
 */
char* daemon_main(int *argc)
{
	char *cwd_argv = NULL;
	int stdout_fd;
	int stderr_fd;
	int stdin_fd;
	int conn_fd;
	pid_t pid;

	ipc_init(args.port);
	reaper_init();

	while (1)
	{
		conn_fd  = ipc_wait_conn();
		cwd_argv = ipc_recv_msg(conn_fd, argc);

		if (!cwd_argv)
			die("unable to receive message\n");

		if (ipc_wait_fds(&stdout_fd, &stderr_fd, &stdin_fd) < 0)
		{
			log_info("Error while waiting for fds, skipping...\n");
			goto again;
		}

		/* If child. */
		if ((pid = fork()) == 0)
			return setup_child(conn_fd, stdout_fd, stderr_fd,
				stdin_fd, cwd_argv);
		else
			reaper_add_child(pid, conn_fd);

		/* Send child PID. */
		ipc_send_int32((int32_t)pid, conn_fd);

	again:
		/* Keep conn_fd as our reaper will close the connection. */
		ipc_close(3, stdin_fd, stdout_fd, stderr_fd);
		free(cwd_argv);
	}

	_exit(0);
}

/**
 * @brief Parse the preloader arguments through env vars
 */
static void parse_args(void)
{
	char *env;

	/* Check port. */
	if ((env = getenv("PRELOADER_PORT")) != NULL)
	{
		if (str2int(&args.port, env) < 0 ||
			(args.port < 0 || args.port > 65535))
		{
			die("Invalid port (%s)\n", env);
		}
	}

	/* Check for log level. */
	if ((env = getenv("PRELOADER_LOG_LVL")) != NULL)
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
	if ((env = getenv("PRELOADER_LOG_FILE")) != NULL)
	{
		args.log_file = strdup(env);
		args.log_fd = -1;
	}

	/* Check daemon. */
	if (getenv("PRELOADER_DAEMONIZE"))
	{
		args.daemonize = 1;
		args.log_fd = -1;
	}

	/* Check if should load a given file too. */
	if ((env = getenv("PRELOADER_LOAD_FILE")) != NULL)
		args.load_file = strdup(env);
}

/**
 * @brief Preloader's signal handler
 */
static void sig_handler(int sig)
{
	((void)sig);
	/*
	 * It sounds stupid (and maybe it is) to handle SIGTERM
	 * just once to reset the signal immediately... but it
	 * was the way I found to kill this process and the
	 * dummy process peacefully.
	 */
	signal(SIGTERM, SIG_DFL);
	kill(0, SIGTERM);
}

/**
 * @brief Creates a 'daemon' process.
 */
static void daemonize(void)
{
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
 * @brief Preloader library entrypoint
 */
void __attribute__ ((constructor)) my_init(void)
{
	parse_args();

	/* Check if we're already running, if so, do nothing. */
	if (!read_and_check_pid(args.pid_path, args.port))
		return;

	/* Initialize logs. */
	if (log_init(&args) < 0)
		die("Unable to initialize logging, plese check your parameters "
			"and try again!\n");

	/* Daemon. */
	if (args.daemonize)
		daemonize();

	/* Spawns a dummy process so our reaper always have some
	 * child to wait for. */
	if (!fork())
		pause();

	/* PID file. */
	if (create_pid(args.pid_path, args.port) < 0)
		die("Unable to create pid file, aborting...\n");

	log_info("Initializing...\n");

	/* Setup signals. */
	signal(SIGTERM, sig_handler);

	/* Read a load file, if specified. */
	if (args.load_file)
		load_file(args.load_file);

	/* Setup arch-dependent things. */
	arch_setup();
}

void __attribute__ ((destructor)) my_fini(void){}
