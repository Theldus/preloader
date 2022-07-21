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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
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

#define SV_DEFAULT_PORT 3636

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
 * Safe string-to-int routine that takes into account:
 * - Overflow and Underflow
 * - No undefined behaviour
 *
 * Taken from https://stackoverflow.com/a/12923949/3594716
 * and slightly adapted: no error classification, because
 * I dont need to know, error is error.
 *
 * @param out Pointer to integer.
 * @param s String to be converted.
 *
 * @return Returns 0 if success and a negative number otherwise.
 */
int str2int(int *out, const char *s)
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
 *
 */
static void usage(const char *prgname)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s [-p <port>] <program> <program-arguments>\n"
		"or\n"
		"  %s <program-arguments>\n", prgname, prgname
	);
	exit(EXIT_FAILURE);
}

/**
 *
 */
static char **parse_args(int *new_argc, char **argv, int *port)
{
	char **new_argv = argv;
	int argc = *new_argc;

	/* at least <program> <arg1>. */
	if (argc < 2)
		usage(argv[0]);

	*port = SV_DEFAULT_PORT;

	/*
	 * If calling client 'normally':
	 *    ./client           <program> <arg1> ... <argN>, min 3
	 *      client           <program> <arg1> ... <argN>, min 3
	 *    ./client -p <port> <program> <arg1> ... <argN>, min 4
	 *      client -p <port  <program> <arg1> ... <argN>, min 4
	 */
	if (!strcmp(argv[0], PRG_NAME) || !strcmp(argv[0], "./"PRG_NAME))
	{
		if (!strcmp(argv[1], "-p"))
		{
			if (argc < 4)
				usage(argv[0]);

			/* Validate port number. */
			if (str2int(port, argv[2]) < 0 || (*port < 0 || *port > 65535))
			{
				fprintf(stderr, "Invalid port number: (%s), "
					"should be in: 0-65535\n", argv[2]);
				usage(argv[0]);
			}

			argc -= 3;
			new_argv += 3;
		}

		/* Ok, no port specified, check the arg count. */
		else if (argc < 2)
			usage(argv[0]);
		else
		{
			argc -= 1;
			new_argv += 1;
		}
	}

	/*
	 * If called by a symlink or if this client is renamed,
	 * like:
	 *    <program> <arg1> ... <argN>
	 * do not touch argv and argv. */

	*new_argc = argc;
	return (new_argv);
}

/**
 *
 */
int main(int argc, char **argv)
{
	struct sockaddr_in sock_addr;
	struct run_data rd;
	char buff[1024];
	char **new_argv;
	int new_argc;
	ssize_t amnt;
	int sock;
	int port;

	/* Parse and validate arguments. */
	new_argc = argc;
	new_argv = parse_args(&new_argc, argv, &port);

	/* Prepare data to be sent. */
	if ((amnt = prepare_data(&rd, new_argc, new_argv)) < 0)
		die("Unable to prepare data to be sent!\n");

	/* Create socket. */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		die("Unable to create a socket!\n");

	memset((void*)&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sock_addr.sin_port = htons(port);

	/* Connect. */
	if (connect(sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0)
		die("Unable to connect to localhost on port %d!\n", port);

	/* Send argc, amt_bytes, cwd and argv. */
	if (send_all(sock, rd.argc, sizeof rd.argc, 0) < 0)
		die("Cant send argc, aborting!...\n");
	if (send_all(sock, rd.amt_bytes, sizeof rd.amt_bytes, 0) < 0)
		die("Cant send amt_bytes, aborting!...\n");
	if (send_all(sock, rd.cwd_argv, amnt, 0) < 0)
		die("Cant send cwd_argv, aborting!...\n");

	/* While connected and there is something to print. */
	memset(buff, 0, sizeof buff);
	while ((amnt = recv(sock, buff, sizeof buff, 0)) > 0)
		write(STDOUT_FILENO, buff, amnt);

	close(sock);
	free(rd.cwd_argv);
	return (0);
}
