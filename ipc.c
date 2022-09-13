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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "ipc.h"
#include "log.h"

static int sv_fd;
static int stdout_fd;
static int stderr_fd;
static int stdin_fd;

#define TIMEOUT_MS 128

/**
 * Structure that holds network-related data in order to
 * read an arbitrary amount of bytes from the client.
 */
struct net_data
{
	/* Buffer. */
	uint8_t buff[1024];
	/* Buffer current position. */
	size_t cur_pos;
	/* Amount of read bytes. */
	size_t amt_read;
	/* Client fd. */
	int client;
	/* Error flag. */
	int error;
};

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
 * @brief Read a chunk of bytes and return the next byte
 * belonging to the frame.
 *
 * @param nd Websocket Frame Data.
 *
 * @return Returns the byte read, or -1 if error.
 */
static inline int next_byte(struct net_data *nd)
{
	ssize_t n;

	/* If empty or full. */
	if (nd->cur_pos == 0 || nd->cur_pos == nd->amt_read)
	{
		if ((n = recv(nd->client, nd->buff, sizeof(nd->buff), 0)) <= 0)
		{
			nd->error = 1;
			return (-1);
		}
		nd->amt_read = (size_t)n;
		nd->cur_pos = 0;
	}
	return (nd->buff[nd->cur_pos++]);
}

/**
 * @brief Puts the server to listen for a given port
 * @p port.
 *
 * @param port Port to be listened.
 * @param fd Returned file descriptor.
 */
static void listen_port(uint16_t port, int *fd)
{
	struct sockaddr_in server;
	int reuse;

	*fd = socket(AF_INET, SOCK_STREAM, 0);
	if (*fd < 0)
		die("Cant start IPC!\n");

	reuse = 1;
	if (setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR,
		(const char *)&reuse, sizeof(reuse)) < 0)
		die("setsockopt(SO_REUSEADDR) failed!\n");

	/* Prepare the sockaddr_in structure. */
	memset((void*)&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	server.sin_port = htons(port);

	/* Bind. */
	if (bind(*fd, (struct sockaddr *)&server, sizeof(server)) < 0)
		die("Bind failed\n");

	/* Listen. */
	if (listen(*fd, SV_MAX_CLIENTS) < 0)
		die("Unable to listen at port %d\n", port);
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
 * @p timeout_ms, waits up to @p timeout_ms for a
 * connection or give up.
 *
 * This is used to wait for the file descriptors belonging
 * to I/O: stdout, stderr and stdin, since a client might
 * not never connect to them and then, hangs the server.
 *
 * @param fd Target fd to wait for an accept.
 * @param timeout_ms Timeout (in milliseconds) to wait.
 *
 * @return If success, returns the accepted fd. On error
 * or timeout, returns -1.
 */
static int accept_timeout(int fd, int timeout_ms)
{
	struct sockaddr_in cli;
	struct pollfd pfd;
	size_t len;

	pfd.fd = fd;
	pfd.events = POLLIN;

	len = sizeof(struct sockaddr_in);

	if (poll(&pfd, 1, timeout_ms) <= 0 || event_error(&pfd))
		return (-1);

	return accept(fd, (struct sockaddr *)&cli, (socklen_t *)&len);
}

/* ==================================================================
 * Public IPC routines
 * ==================================================================*/

/**
 * @brief Listen to all ports given an initial
 * port @p port.
 *
 * @return Always 0.
 */
int ipc_init(int port)
{
	listen_port(port + 0, &sv_fd);
	listen_port(port + 1, &stdout_fd);
	listen_port(port + 2, &stderr_fd);
	listen_port(port + 3, &stdin_fd);
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
	struct sockaddr_in client;
	size_t len;
	int cli_fd;

	len = sizeof(struct sockaddr_in);

	cli_fd = accept(sv_fd, (struct sockaddr *)&client, (socklen_t *)&len);
	if (cli_fd < 0)
		die("Failed while accepting connections, aborting...\n");

	return (cli_fd);
}

/**
 * @brief Wait for all I/O sockets to establish a connection,
 * each of them with TIMEOUT_MS.
 *
 * @return Returns 0 if all of them are connected, -1 if one
 * of them were not able to connect in time.
 */
int ipc_wait_fds(int *out, int *err, int *in)
{
	/* Accept stdout, stderr and stdin with TIMEOUT_MS ms. */
	if ((*out = accept_timeout(stdout_fd, TIMEOUT_MS)) < 0)
		return (-1);
	if ((*err = accept_timeout(stderr_fd, TIMEOUT_MS)) < 0)
		return (-1);
	if ((*in  = accept_timeout(stdin_fd,  TIMEOUT_MS)) < 0)
		return (-1);
	return (0);
}

/**
 * @brief Receives the main control-message from the client,
 * containing: argc count, current work dir, and argument
 * list.
 *
 * @param conn Connection file descriptor.
 * @param argc_p Argument count pointer.
 *
 * @return On success, returns a NUL-separated string
 * containing: CWD NUL argv[0] NUL argv[1] NUL ...
 *             argv[argc-1] NUL.
 * On error, returns NULL.
 */
char *ipc_recv_msg(int conn, int *argc_p)
{
	int i;
	int32_t argc;
	char *cwd_argv;
	int32_t amnt_bytes;
	uint8_t tmp[4] = {0};
	struct net_data nd;

	memset(&nd, 0, sizeof nd);
	nd.client = conn;

	/*
	 * Our 'protocol':
	 * First message:
	 *   4 bytes, big-endian: argc
	 * Second message:
	 *   4 bytes, big-endian: total amount of bytes to be read next.
	 * Third-message (without spaces):
	 *   current work dir NUL argv[0] NUL argv[1] NUL ... argv[argc-1] NUL
	 */

	/* Read argc. */
	tmp[0] = next_byte(&nd);
	tmp[1] = next_byte(&nd);
	tmp[2] = next_byte(&nd);
	tmp[3] = next_byte(&nd);
	argc = msg_to_int32(tmp);
	if (nd.error)
	{
		log_err("Unable to receive argc from %d!\n", conn);
		return (NULL);
	}

	/* Read amnt_bytes. */
	tmp[0] = next_byte(&nd);
	tmp[1] = next_byte(&nd);
	tmp[2] = next_byte(&nd);
	tmp[3] = next_byte(&nd);
	amnt_bytes = msg_to_int32(tmp);
	if (nd.error)
	{
		log_err("Unable to receive amnt_bytes from %d!\n", conn);
		return (NULL);
	}

	/* Read CWD and argv. */
	cwd_argv = malloc(amnt_bytes);
	if (!cwd_argv)
	{
		log_err("Cant allocate memory (%d bytes)!\n", amnt_bytes);
		return (NULL);
	}

	for (i = 0; i < amnt_bytes && !nd.error; i++)
		cwd_argv[i] = next_byte(&nd);

	if (nd.error)
	{
		log_err("Failed while receiving cwd_argv argument from %d!\n", conn);
		goto err0;
	}

	*argc_p = argc;
	return (cwd_argv);
err0:
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
