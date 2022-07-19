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

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/mman.h>

#include "ipc.h"
#include "util.h"

static int sv_fd;

/**
 *
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
 * @brief Read a chunk of bytes and return the next byte
 * belonging to the frame.
 *
 * @param nd Websocket Frame Data.
 *
 * @return Returns the byte read, or -1 if error.
 *
 * @attention This is part of the internal API and is documented just
 * for completeness.
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

/* ==================================================================
 * Public IPC routines
 * ==================================================================*/

/**
 *
 */
int ipc_init(void)
{
	struct sockaddr_in server;
	int reuse;

	sv_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sv_fd < 0)
		die("Cant start IPC!\n");

	reuse = 1;
	if (setsockopt(sv_fd, SOL_SOCKET, SO_REUSEADDR,
		(const char *)&reuse, sizeof(reuse)) < 0)
		die("setsockopt(SO_REUSEADDR) failed!\n");

	/* Prepare the sockaddr_in structure. */
	memset((void*)&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	server.sin_port = htons(SV_PORT);

	/* Bind. */
	if (bind(sv_fd, (struct sockaddr *)&server, sizeof(server)) < 0)
		die("Bind failed\n");

	/* Listen. */
	if (listen(sv_fd, SV_MAX_CLIENTS) < 0)
		die("Unable to listen at port %d\n", SV_PORT);

	return (0);
}

/**
 *
 */
void ipc_finish(void)
{

}

/**
 *
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
 *
 */
char *ipc_recv_msg(int conn, int *argc_p)
{
	int i;
	int32_t argc;
	char *cwd_argv;
	int32_t amnt_bytes;
	uint8_t tmp[4] = {0};
	struct net_data nd = {0};

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
		d_err("Unable to receive argc from %d!\n", conn);
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
		d_err("Unable to receive amnt_bytes from %d!\n", conn);
		return (NULL);
	}

	/* Read CWD and argv. */
	cwd_argv = mmap(0, amnt_bytes, PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

	if (cwd_argv == MAP_FAILED)
	{
		d_err("Cant allocate memory (%d bytes)!\n", amnt_bytes);
		return (NULL);
	}

	for (i = 0; i < amnt_bytes && !nd.error; i++)
		cwd_argv[i] = next_byte(&nd);

	if (nd.error)
	{
		d_err("Failed while receiving cwd_argv argument from %d!\n", conn);
		goto err0;
	}

	*argc_p = argc;
	return (cwd_argv);
err0:
	munmap(cwd_argv, amnt_bytes);
	return (NULL);
}
