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
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>

#include "log.h"
#include "util.h"

/**
 * @brief Given a @p pid_path and @p port, return the string
 * containing the full path for the pid file.
 *
 * @param pid_path PID path folder
 * @param port Main/control port.
 *
 * @return If success, return the path for the pid file,
 * otherwise, returns NULL.
 */
static char* get_pid_file_path(const char *pid_path, int port)
{
	static char path[4096];
	if (strlen(pid_path) + sizeof "/preloader_65535.pid" > sizeof path)
		return (NULL);

	snprintf(path, sizeof path - 1, "%s/preloader_%d.pid", pid_path, port);
	return (path);
}

/**
 * @brief Read the current PID file (if any) and check
 * if there is a process running for the read pid.
 *
 * @param pid_path PID path folder.
 * @param port Main/control port.
 *
 * @return Returns 0 if and only if there is a process
 * already running for the saved pid_file. In this case,
 * the library *should not* proceed with the execution.
 *
 * Otherwise, returns -1, meaning that the pid file
 * do not exist and/or is invalid and should be ignored,
 * the execution can proceed as usual.
 */
int read_and_check_pid(const char *pid_path, int port)
{
	int i;
	int fd;
	int ret;
	ssize_t r;
	size_t pid;
	char *pid_file;
	char buff[16] = {0};

	ret = -1;

	pid_file = get_pid_file_path(pid_path, port);
	if (!pid_file)
		die("pid_path is too big!\n");

	fd = open(pid_file, O_RDONLY);
	if (fd < 0)
		return (ret);

	r = read(fd, buff, sizeof buff);
	if (r < 0)
		goto err0;

	pid = 0;
	for (i = 0; i < r; i++)
	{
		if (buff[i] < '0' || buff[i] > '9')
			goto err0;
		else
		{
			pid *= 10;
			pid += (buff[i] - '0');
		}
	}

	/* Now we have a (possibly valid) pid, check if
	 * the process is still running. */
	if (kill(pid, 0) < 0)
		goto err0; /* Process not running. */

	close(fd);
	return (0);

	/* In case of failure, erase the file. */
err0:
	close(fd);
	unlink(pid_file);
	return (ret);
}

/**
 * @brief Create a pid file for a given @p pid_path and @p port.
 *
 * @param pid_path PID path folder.
 * @param port Main/control port.
 *
 * @return Returns 0 if success, -1 otherwise.
 */
int create_pid(const char *pid_path, int port)
{
	int fd;
	char *pid_file;

	pid_file = get_pid_file_path(pid_path, port);
	if (!pid_file)
		die("pid_path is too big!\n");

	fd = creat(pid_file, 0644);
	if (fd < 0)
		return (-1);

	dprintf(fd, "%d", (int)getpid());
	close(fd);
	return (0);
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
