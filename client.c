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
#include <signal.h>
#include <unistd.h>
#include <sys/epoll.h>
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

/* Process PID. */
static pid_t process_pid;

/* epoll fd. */
static int epfd;

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
static int do_connect(uint16_t port, int *sock)
{
	struct sockaddr_in sock_addr;

	/* Create socket. */
	*sock = socket(AF_INET, SOCK_STREAM, 0);
	if (*sock < 0)
		die("Unable to create a socket!\n");

	memset((void*)&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sock_addr.sin_port = htons(port);

	return connect(*sock, (struct sockaddr *)&sock_addr,
		sizeof(sock_addr));
}

/**
 *
 */
static inline int epoll_event_error(uint32_t evt)
{
	if ((evt & EPOLLHUP) ||
		(evt & EPOLLERR) ||
		(evt & EPOLLPRI))
	{
		return (1);
	}
	return (0);
}

/**
 *
 *
 * @return Returns 0 if success, -1 if error (including EOF).
 */
static int handle_epoll_event(struct epoll_event *ev, int out_fd,
	int is_sock)
{
	static char buff[1024];
	ssize_t amnt;
	int cfd; /* close fd. */

	amnt = read(ev->data.fd, buff, sizeof buff);

	/* Check if EOF or error... if so, close the file descriptor and
	 * the connection, if fd belongs to a socket. */
	if (amnt <= 0)
	{
		/* If 'is_sock' is true, our input fd is a socket, otherwise,
		 * output fd is a socket. */
		cfd = (is_sock ? ev->data.fd : out_fd);
		close(cfd);

		/* Remove from epoll, if not already removed. */
		epoll_ctl(epfd, EPOLL_CTL_DEL, ev->data.fd, NULL);
		return (-1);
	}

	if (write(out_fd, buff, amnt) != amnt)
		return (-1);

	return (0);
}

/**
 *
 */
static void sig_handler(int sig)
{
	if (process_pid)
		kill(process_pid, sig);
}

/**
 *
 */
int main(int argc, char **argv)
{
	struct epoll_event ev, events[3];
	struct run_data rd;
	char ret_buff[4];
	char **new_argv;
	int new_argc;
	ssize_t amnt;
	int32_t ret;
	int nfds;
	int port;
	int i;

	int sock;
	int sock_stdout;
	int sock_stderr;
	int sock_stdin;
	int closed_fds;

	closed_fds = 0;
	ret = 42;

	signal(SIGINT,  sig_handler);
	signal(SIGTERM, sig_handler);

	/* Parse and validate arguments. */
	new_argc = argc;
	new_argv = parse_args(&new_argc, argv, &port);

	/* Prepare data to be sent. */
	if ((amnt = prepare_data(&rd, new_argc, new_argv)) < 0)
		die("Unable to prepare data to be sent!\n");

	/* Connect to server port. */
	if (do_connect(port, &sock) < 0)
		die("Unable to connect on sv port %d!\n", port);

	/* Send argc, amt_bytes, cwd and argv. */
	if (send_all(sock, rd.argc, sizeof rd.argc, 0) < 0)
		die("Cant send argc, aborting!...\n");
	if (send_all(sock, rd.amt_bytes, sizeof rd.amt_bytes, 0) < 0)
		die("Cant send amt_bytes, aborting!...\n");
	if (send_all(sock, rd.cwd_argv, amnt, 0) < 0)
		die("Cant send cwd_argv, aborting!...\n");

	/* Now connect to "I/O" ports. */
	if (do_connect(port + 1, &sock_stdout) < 0)
		die("Unable to connect on stdout port %d!\n", port + 1);
	if (do_connect(port + 2, &sock_stderr) < 0)
		die("Unable to connect on stderr port %d!\n", port + 2);
	if (do_connect(port + 3, &sock_stdin)  < 0)
		die("Unable to connect on stdin port %d!\n",  port + 3);

	/* Wait for process PID. */
	if ((amnt = recv(sock, ret_buff, 4, 0)) != 4)
		goto out;

	ret = msg_to_int32(ret_buff);
	process_pid = ret;

	/*
	 * Setup epoll to listen to our fds.
	 *
	 * Note: I was quite reluctant to use epoll here, since poll()
	 * should be more than enough for just 3 fds.... right?....
	 *
	 * Er, nope.... for some mysterious reason, listening to 3 fds
	 * (mostly stdin) was adding huge overhead (twice slower) when
	 * multiple clients were running at the same time. More
	 * interestingly, poll'ing() from sock_stdout + sock_stderr
	 * did not incur overhead, but only when I used stdin and only
	 * with it.
	 *
	 * Fortunately, using epoll() has brought back the same
	 * performance the code had several commits ago.
	 */
	epfd = epoll_create1(0);
	if (epfd < 0)
		die("Unable to create an epoll file descriptor!\n");

	ev.events  = EPOLLIN;
	ev.data.fd = sock_stdout;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0)
		die("Unable to add stdout event!\n");

	ev.data.fd = sock_stderr;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0)
		die("Unable to add stderr event!\n");

	ev.data.fd = STDIN_FILENO;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0)
		die("Unable to add stdin event!\n");

	/* Listen to our fds. */
	while (1)
	{
		/* Check if we should close. */
		if (closed_fds >= 2)
			break;

		/* If not, wait for new stuff. */
		nfds = epoll_wait(epfd, events, 3, -1);
		if (nfds < 0)
			break;

		for (i = 0; i < nfds; i++)
		{
			if (epoll_event_error(events[i].events))
				goto out_epoll;

			/* handle stdout. */
			if (events[i].data.fd == sock_stdout)
			{
				if (handle_epoll_event(&events[i], STDOUT_FILENO, 1) < 0)
					closed_fds++;
			}

			/* handle stderr. */
			else if (events[i].data.fd == sock_stderr)
			{
				if (handle_epoll_event(&events[i], STDERR_FILENO, 1) < 0)
					closed_fds++;
			}

			/* handle stdin. */
			else if (events[i].data.fd == STDIN_FILENO)
				handle_epoll_event(&events[i], sock_stdin, 0);
		}
	}

out_epoll:

	/* Wait for return value. */
	if ((amnt = recv(sock, ret_buff, 4, 0)) == 4)
		ret = msg_to_int32(ret_buff);

out:
	close(sock);
	close(sock_stdout);
	close(sock_stderr);
	close(sock_stdin);
	close(epfd);

	free(rd.cwd_argv);
	return ((int)ret);
}
