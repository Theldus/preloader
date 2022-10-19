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
#include <stdarg.h>
#include <poll.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ipc.h"
#include "log.h"
#include "preloader.h"

static int sv_fd;

#define TIMEOUT_MS 128

/**
 * @brief Given a 32-bit message, decodes the content
 * as a int32_t number.
 *
 * @param msg Content to be decoded.
 *
 * @return Returns message as uint32_t.
 */
static inline int32_t msg_to_int32(uint8_t *msg)
{
	int32_t msg_int;
	/* Decodes as big-endian. */
	msg_int = (msg[3] << 0) | (msg[2] << 8) | (msg[1] << 16) |
		(msg[0] << 24);
	return (msg_int);
}

/**
 * @brief Given a 32-bit message, encodes the content
 * to be sent.
 *
 * @param msg Message to be encoded.
 * @param msg Target buffer.
 */
static inline void int32_to_msg(int32_t msg, uint8_t *msg_buff)
{
	/* Encodes as big-endian. */
	msg_buff[0] = (msg >> 24);
	msg_buff[1] = (msg >> 16);
	msg_buff[2] = (msg >>  8);
	msg_buff[3] = (msg >>  0);
}

/**
 * @brief Check for error on a given pollfd.
 *
 * @param p Pollfd to be checked.
 *
 * @return Returns 0 if success, 1 otherwise.
 */
static inline int event_error(struct pollfd *p)
{
	int ev = p->events;
	if ((ev & POLLHUP) ||
		(ev & POLLERR) ||
		(ev & POLLNVAL))
		return (1);
	return (0);
}

/**
 * @brief Given a file descriptor @p fd and a timeout
 * @p timeout_ms, waits up to @p timeout_ms for a new
 * message on @p fd.
 *
 * @param fd Target fd to wait for an accept.
 * @param timeout_ms Timeout (in milliseconds) to wait.
 *
 * @return On success, returns 0, otherwise, -1.
 */
static int recv_timeout(int fd, int timeout_ms)
{
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = POLLIN;

	if (poll(&pfd, 1, timeout_ms) <= 0 || event_error(&pfd))
		return (-1);

	return (0);
}

/* ==================================================================
 * Public IPC routines
 * ==================================================================*/

/**
 * @brief Initiates the server and puts it to listening
 * to the configured port/id.
 *
 * @return Always 0.
 */
int ipc_init(struct args *args)
{
	struct sockaddr_un server;

	/* Validate path. */
	if (strlen(args->pid_path) + sizeof "/preloader_65535.sock" >
		sizeof(server.sun_path))
	{
		die("Socket path exceeds maximum allowed! (%zu)\n",
			sizeof(server.sun_path));
	}

	sv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sv_fd < 0)
		die("Cant start IPC!\n");

	/* Prepare the sockaddr_un structure. */
	memset((void*)&server, 0, sizeof(server));
	server.sun_family = AF_UNIX;
	snprintf(server.sun_path, sizeof server.sun_path - 1,
		"%s/preloader_%d.sock", args->pid_path, args->port);

	/* Remove sock file if already exists. */
	unlink(server.sun_path);

	/* Bind. */
	if (bind(sv_fd, (struct sockaddr *)&server, sizeof(server)) < 0)
		die("Bind failed\n");

	/* Listen. */
	if (listen(sv_fd, SV_MAX_CLIENTS) < 0)
		die("Unable to listen at path (%s)\n", server.sun_path);

	return (0);
}

/**
 * @brief Releases all IPC resources.
 */
void ipc_finish(void)
{
	close(sv_fd);
}

/**
 * @brief Wait for a new connection (no timeout).
 *
 * @return Returns the client file descriptor.
 */
int ipc_wait_conn(void)
{
	int cli_fd;

	cli_fd = accept(sv_fd, NULL, NULL);
	if (cli_fd < 0)
		die("Failed while accepting connections, aborting...\n");

	return (cli_fd);
}

/* Amount of bytes before the actual cwd_argv. */
#define ARGC_AMNT 8

/**
 * @brief Receives the file descriptors (stdout, stdin and
 * stderr), the current work directory, and the command-line
 * arguments given to preloader_cli.
 *
 * @param conn_fd Client connection.
 * @param out Client stdout fd.
 * @param err Client stderr fd.
 * @param in  Client stdin fd.
 * @param argc_p Argument count pointer.
 *
 * @return Returns the current work directory and the
 * argument list.
 */
char* ipc_recv_msg(
	int conn_fd, int *out, int *err, int *in, int *argc_p)
{
	char buff[CMSG_SPACE(3 * sizeof(int))];
	struct cmsghdr *cmsghdr;
	struct msghdr msghdr;
	char buff_data[128];
	uint32_t rem_bytes;
	char *cwd_argv, *p;
	struct iovec iov;
	int fds[3];
	ssize_t nr;

	/* Fill message header and our I/O vec. */
	memset(&msghdr, 0, sizeof(msghdr));
	msghdr.msg_iov = &iov;
	msghdr.msg_iovlen = 1;
	iov.iov_base = buff_data;
	iov.iov_len = sizeof(buff_data);

	/* Set 'msghdr' fields that describe ancillary data */
	msghdr.msg_control = buff;
	msghdr.msg_controllen = sizeof(buff);

	/* Wait up to TIMEOUT_MS to receive something. */
	if (recv_timeout(conn_fd, TIMEOUT_MS) < 0)
		return (NULL);

	/* Receive real & ancillary data. */
	nr = recvmsg(conn_fd, &msghdr, 0);

	/*
	 * We should receive at least 8 bytes:
	 * 4 bytes: argc
	 * 4 bytes: amnt of bytes remaining
	 *
	 * Yes... I'm assuming I'll always be able to receive
	 * at least 8 bytes... recvmsg doesnt guarantee this,
	 * thik of better solution....
	 */
	if (nr < ARGC_AMNT)
		return (NULL);

	/* Save our argc + amnt. */
	*argc_p   = msg_to_int32((uint8_t*)buff_data);
	rem_bytes = msg_to_int32((uint8_t*)buff_data + 4);

	/* Check if the fds were received. */
	cmsghdr = CMSG_FIRSTHDR(&msghdr);

	if (cmsghdr == NULL ||
		cmsghdr->cmsg_len   != CMSG_LEN(sizeof(int) * 3) ||
		cmsghdr->cmsg_level != SOL_SOCKET ||
		cmsghdr->cmsg_type  != SCM_RIGHTS)
	{
		return (NULL);
	}

	/* Copy the fds into the proper place. */
	memcpy(&fds, CMSG_DATA(cmsghdr), sizeof(int) * 3);
	*out = fds[0];
	*err = fds[1];
	*in  = fds[2];

	/* Read CWD and argv. */
	cwd_argv = malloc(rem_bytes - 8);
	if (!cwd_argv)
		log_crit("Cant allocate memory (%d bytes)!\n", rem_bytes);

	rem_bytes -= nr;

	if (nr)
		memcpy(cwd_argv, buff_data + ARGC_AMNT, nr - ARGC_AMNT);
	p = cwd_argv + (nr - ARGC_AMNT);

	/* Fill cwd_argv. */
	while (rem_bytes)
	{
		nr = recv(conn_fd, p, rem_bytes, 0);
		if (nr <= 0)
			goto out0;

		rem_bytes -= nr;
		p += nr;
	}

	return (cwd_argv);
out0:
	free(cwd_argv);
	return (NULL);
}

/**
 * @brief For a given int32_t @p value, send to the
 * socket pointed by @p fd.
 *
 * @return Returns 1 if success, 0 otherwise.
 */
int ipc_send_int32(int32_t value, int fd)
{
	uint8_t buff[4];
	int32_to_msg(value, buff);
	return (send(fd, buff, sizeof buff, 0) == sizeof buff);
}

/**
 * Closes an arbitrary amount of file descriptors
 * specified in @p num.
 */
void ipc_close(int num, ...)
{
	int i;
	va_list ap;

	va_start(ap, num);
	for (i = 0; i < num; i++)
		close(va_arg(ap, int));
	va_end(ap);
}
