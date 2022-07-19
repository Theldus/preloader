#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include "util.h"

/**
 *
 */
int64_t time_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000 + tv.tv_usec/1000;
}

/**
 *
 *
 * @return Returns 0 if and only if there is a process
 * already running for the saved pid_file. In this case,
 * the library *should not* proceed with the execution.
 *
 * Otherwise, returns -1, meaning that the pid_file
 * do not exist and/or is invalid and should be ignored,
 * the execution can proceed as usual.
 */
int read_and_check_pid(const char *pid_file)
{
	int i;
	int fd;
	int ret;
	ssize_t r;
	size_t pid;
	char buff[16] = {0};

	ret = -1;

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
 *
 */
int create_pid(const char *pid_file)
{
	int fd;

	fd = creat(pid_file, 0644);
	if (fd < 0)
		return (-1);

	dprintf(fd, "%d", (int)getpid());
	close(fd);
	return (0);
}
