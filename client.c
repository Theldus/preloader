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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PRG_NAME "client"

#define die(...) \
	do { \
		fprintf(stderr, __VA_ARGS__); \
		exit(EXIT_FAILURE); \
	} while (0)

#define SV_PORT 3636

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
 * @brief Given a 32-bit message, encodes the content
 * to be sent.
 *
 * @param msg
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

/**
 *
 */
static ssize_t send_all(
	int conn, const void *buf, size_t len, int flags)
{
	const char *p;
	ssize_t ret;

	if (conn < 0)
		return (-1);

	p = buf;
	while (len)
	{
		ret = send(conn, p, len, flags);
		if (ret == -1)
			return (-1);
		p += ret;
		len -= ret;
	}
	return (0);
}

/**
 *
 */
struct run_data
{
	uint8_t argc[4];
	uint8_t amt_bytes[4];
	char *cwd_argv;
};

/**
 *
 */
static ssize_t prepare_data(struct run_data *rd, int argc, char **argv)
{
	int i;
	uint32_t amnt;
	char cwd[4096] = {0};
	char *p;

	/* If argv[0] == 'client', we should use argv[1]+. */
	if (strstr(argv[0], PRG_NAME))
	{
		argc--;
		argv++;
	}

	int32_to_msg(argc, rd->argc);

	/* Get current working directory. */
	if (!getcwd(cwd, sizeof(cwd)))
		return (-1);

	amnt = (uint32_t)strlen(cwd);
	for (i = 0; i < argc; i++)
		amnt += (uint32_t)strlen(argv[i]);
	amnt += argc + 1; /* + number of '|'. */

	int32_to_msg(amnt, rd->amt_bytes);

	/* Allocate and create buffer to be sent. */
	rd->cwd_argv = calloc(amnt + 1, sizeof(char));
	if (!rd->cwd_argv)
		return (-1);
	p = rd->cwd_argv;

	strcpy(p, cwd);
	p += strlen(p) + 1;
	for (i = 0; i < argc; i++)
	{
		strcpy(p, argv[i]);
		p += strlen(p) + 1;
	}

	return (amnt);
}

/**
 *
 */
static void usage(const char *prgname)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s <program> <program-arguments>\n"
		"or\n"
		"  %s <program-arguments>\n", prgname, prgname
	);
	exit(EXIT_FAILURE);
}

/**
 *
 */
int main(int argc, char **argv)
{
	struct sockaddr_in sock_addr;
	struct run_data rd;
	ssize_t amnt;
	int sock;

	/* Require at least <program> and 1 program-argument. */
	if (strstr(argv[0], PRG_NAME))
	{
		if (argc < 3) /* client <program> <arg1>. */
			usage(argv[0]);
	}
	else if (argc < 2) /* <program> <arg1>. */
		usage(argv[0]);

	/* Prepare data to be sent. */
	if ((amnt = prepare_data(&rd, argc, argv)) < 0)
		die("Unable to prepare data to be sent!\n");

	/* Create socket. */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		die("Unable to create a socket!\n");

	memset((void*)&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sock_addr.sin_port = htons(SV_PORT);

	/* Connect. */
	if (connect(sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0)
		die("Unable to connect to localhost on port %d!\n", SV_PORT);


	/* Send argc, amt_bytes, cwd and path. */
	if (send_all(sock, rd.argc, sizeof rd.argc, 0) < 0)
		die("Cant send argc, aborting!...\n");
	if (send_all(sock, rd.amt_bytes, sizeof rd.amt_bytes, 0) < 0)
		die("Cant send amt_bytes, aborting!...\n");
	if (send_all(sock, rd.cwd_argv, amnt, 0) < 0)
		die("Cant send cwd_argv, aborting!...\n");


	sleep(10);


	free(rd.cwd_argv);
	return (0);
}
