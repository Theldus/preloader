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
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#ifndef PRG_NAME
#define PRG_NAME "preloader_cli"
#endif

#ifndef PID_PATH
#define PID_PATH "/tmp"
#endif

#define die(...) \
	do { \
		fprintf(stderr, __VA_ARGS__); \
		exit(EXIT_FAILURE); \
	} while (0)

#define SV_DEFAULT_PORT 3636

/* Process PID. */
static pid_t process_pid;

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
 * @brief Prepare the initial data that should be sent to the
 * server and returns them as a char buffer.
 *
 * @param size Amount of data to be sent.
 * @param argc Argument count.
 * @param argv Argument list.
 *
 * @return If success, returns the data to be sent (as an char
 * array), otherwise, NULL.
 */
static char* prepare_data(size_t *size, int argc, char **argv)
{
	int i;
	uint32_t amnt;
	char *p, *buff;
	char cwd[4096] = {0};

	/* Get current working directory. */
	if (!getcwd(cwd, sizeof(cwd)))
		return (NULL);

	/* Get the amounr of data to be sent. */
	amnt = (uint32_t)strlen(cwd);
	for (i = 0; i < argc; i++)
		amnt += (uint32_t)strlen(argv[i]);
	amnt += argc + 1; /* + number of 'NUL'. */
	amnt += 8; /* argc + amt_bytes. */

	/* Allocate and create buffer to be sent. */
	buff = calloc(amnt + 1, sizeof(char));
	if (!buff)
		return (NULL);

	int32_to_msg(argc, (uint8_t*)buff);
	int32_to_msg(amnt, (uint8_t*)buff + 4);

	p = buff + 8; /* skip argc + amnt. */

	strcpy(p, cwd);
	p += strlen(p) + 1;
	for (i = 0; i < argc; i++)
	{
		strcpy(p, argv[i]);
		p += strlen(p) + 1;
	}

	*size = amnt;
	return (buff);
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
 * @brief Program usage.
 *
 * @param prgname Program name.
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
 * @brief Parse command-line arguments.
 *
 * @param new_argc New argument counter.
 * @param argv Old argument list.
 * @param port Port to be connected.
 *
 * @return Returns the new argument list.
 */
static char **parse_args(int *new_argc, char **argv, int *port)
{
	char **new_argv = argv;
	char  *prog_base, *tmp;
	int argc = *new_argc;

	/* at least <program> <arg1>. */
	if (argc < 2)
		usage(argv[0]);

	*port = SV_DEFAULT_PORT;

	/* Get true program name. */
	prog_base = strdup(argv[0]);
	if (!prog_base)
		die("Unable to allocate memory!\n");
	tmp = basename(argv[0]);

	/*
	 * If calling client 'normally':
	 *     ./client           <program> <arg1> ... <argN>, min 3
	 *       client           <program> <arg1> ... <argN>, min 3
	 * /path/client           <program> <arg1> ... <argN>, min 3
	 *    ./client  -p <port> <program> <arg1> ... <argN>, min 4
	 *      client  -p <port  <program> <arg1> ... <argN>, min 4
	 * /path/client -p <port  <program> <arg1> ... <argN>, min 4
	 */
	if (!strcmp(tmp, PRG_NAME))
	{
		free(prog_base);
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
	else
		free(prog_base);

	/*
	 * If called by a symlink or if this client is renamed,
	 * like:
	 *    <program> <arg1> ... <argN>
	 * do not touch argv and argv. */

	*new_argc = argc;
	return (new_argv);
}

/**
 * @brief Connect to a given Unix Domain Socket ID and
 * saves the socket into @p sock.
 *
 * @param port Socket ID to be connect.
 * @param sock Returned socket pointer.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
static int do_connect(uint16_t port, int *sock)
{
	struct sockaddr_un sock_addr;

	/* Validate path. */
	if (sizeof PID_PATH + sizeof "/preloader_65535.sock" >
		sizeof(sock_addr.sun_path))
	{
		die("Socket path exceeds maximum allowed! (%zu)\n",
			sizeof(sock_addr.sun_path));
	}

	/* Create socket. */
	*sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (*sock < 0)
		die("Unable to create a socket!\n");

	memset((void*)&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sun_family = AF_UNIX;
	snprintf(sock_addr.sun_path, sizeof sock_addr.sun_path - 1,
		"%s/preloader_%d.sock", PID_PATH, port);

	return connect(*sock, (struct sockaddr *)&sock_addr,
		sizeof(sock_addr));
}

/**
 * @brief Send to @p sock all the data in the buffer @p buffer_data
 * and also the file descriptors the client process have too.
 *
 * @param sock Connection to send the data + fds.
 * @param buff_data Data to be sent.
 * @param buff_data_len Buffer length.
 *
 * @return Returns a positive number if success, otherwise, returns
 * a number lesser than or equal 0.
 */
static ssize_t send_fds(int sock, char *buff_data, size_t buff_data_len)
{
	struct cmsghdr *cmsghdr;
	struct msghdr msghdr;
	struct iovec iov;
	int fds[3] = {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO};

	char buff[CMSG_SPACE(3 * sizeof(int))];

	/* Fill message header and our I/O vec. */
	memset(&msghdr, 0, sizeof(msghdr));
	msghdr.msg_iov = &iov;
	msghdr.msg_iovlen = 1;
	iov.iov_base = buff_data;
	iov.iov_len = buff_data_len;

	/* Set 'msghdr' fields that describe ancillary data */
	msghdr.msg_control = buff;
	msghdr.msg_controllen = sizeof(buff);

	/* Set up ancillary data describing file descriptor to send */
	cmsghdr = CMSG_FIRSTHDR(&msghdr);
	memset(cmsghdr, 0, sizeof(*cmsghdr));
	cmsghdr->cmsg_level = SOL_SOCKET;
	cmsghdr->cmsg_type = SCM_RIGHTS;
	cmsghdr->cmsg_len = CMSG_LEN(sizeof(int) * 3);

	/* Copy fds. */
	memcpy(CMSG_DATA(cmsghdr), &fds, sizeof(int) * 3);

	/* Send real data plus ancillary data */
	return sendmsg(sock, &msghdr, 0);
}

/**
 * @brief Client signal handler.
 *
 * Since the client needs to behave transparently, as if
 * it were the original process, all signals that the
 * client receives are forwarded to the original process.
 *
 * @param sig Signal received.
 */
static void sig_handler(int sig)
{
	if (process_pid)
		kill(process_pid, sig);
}

/* Main routine. */
int main(int argc, char **argv)
{
	uint8_t ret_buff[4]; /* Generic buffer to I/O.         */
	char *send_buff;     /* Data to be sent to the server. */
	char **new_argv;     /* New argument list.          */
	int new_argc;        /* New argument count.         */
	size_t amnt;         /* Amount of bytes to be sent. */
	int32_t ret;         /* Generic int32_t to I/O.     */
	int port;            /* Main server/control port.   */

	int sock;            /* Control socket fd.    */
	ret = 42;

	signal(SIGINT,  sig_handler);
	signal(SIGTERM, sig_handler);

	/* Parse and validate arguments. */
	new_argc = argc;
	new_argv = parse_args(&new_argc, argv, &port);

	/* Prepare data to be sent. */
	if (!(send_buff = prepare_data(&amnt, new_argc, new_argv)))
		die("Unable to prepare data to be sent!\n");

	/* Connect to server port. */
	if (do_connect(port, &sock) < 0)
		die("Unable to connect on sv port %d!\n", port);

	/*
	 * Send fds (stdout, stderr and stdin) +
	 * data (argc, amt_bytes, cwd and argv)
	 */
	if (send_fds(sock, send_buff, amnt) != (ssize_t)amnt)
		die("Unable to send the file descriptors!...\n");

	/* Wait for process PID. */
	if ((amnt = recv(sock, ret_buff, 4, 0)) != 4)
		goto out;

	ret = msg_to_int32(ret_buff);
	process_pid = ret;

	/*
	 * Our fds were already sent to the preloader and they're
	 * used directly by the original process. No need to poll
	 * or anything else here =).
	 */

	/* Wait for return value. */
	if ((amnt = recv(sock, ret_buff, 4, 0)) == 4)
		ret = msg_to_int32(ret_buff);

out:
	close(sock);
	free(send_buff);
	return ((int)ret);
}
