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
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>

#include "ipc.h"
#include "log.h"

/*
 * What is the reaper?
 * When a child process dies, it sends a SIGCHLD to the parent
 * process, signaling that it has died.
 *
 * If the parent does nothing, the child becomes a zombie waiting
 * for an ack from the parent. By accumulating multiple children
 * with no response from the parent, a zombie horde forms and eats
 * all your system resources, it's not cool.
 *
 * The parent process basically has 3 alternatives:
 * - Ignore children's SIGCHLD, so the child process can rest
 *   in peace without waiting for the parent.
 *
 * - Define a signal handler to asynchronously handle when a
 *   child dies.
 *
 * - Stop execution and wait for the children to die (via wait()).
 *
 * The first option is not viable: the preloader *needs* to know
 * when a child is terminated. The second option is too much work:
 * the child can interrupt the parent at any time, which would fail
 * to execute certain syscalls and etc. The third option is what
 * the reaper uses: a dedicated thread (the 'reaper') waits for
 * the children to die, and when it does, some action is taken.
 *
 * This way, the main thread can continue running peacefully without
 * worrying about the children dying.
 */

/* Children list mutex. */
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Children list. */
static struct child_list
{
	size_t size;
	size_t last_empty;
	struct children
	{
		int fd;
		pid_t pid;
	} *c;
} cl;

/**
 * @brief Given a pid @p pid, gets the position
 * the child process occupies in the list.
 *
 * @param pid Child pid.
 *
 * @return If success, returns a number greater
 * than or equal 0. Otherwise, returns -1.
 */
static off_t get_child_pos(pid_t pid)
{
	off_t i;

pthread_mutex_lock(&list_mutex);
	for (i = 0; i < (off_t)cl.size; i++)
		if (cl.c[i].pid == pid)
			break;

	if (i == (off_t)cl.size)
		i = -1;
pthread_mutex_unlock(&list_mutex);

	return (i);
}

/**
 * @brief Indefinitely waits for child processes to exit
 * and/or die.
 *
 * When a child process die, the reaper should get its
 * exit code and then send to the appropriate client
 * process. After that, the socket from the client
 * can safely be closed, as well the resources allocated
 * for the child.
 *
 * @param p Unused.
 * @return Always NULL (unused too).
 */
static void* wait_children(void *p)
{
	((void)p);

	#define MAX_ATTEMPTS   3
	#define PAUSE_MS      20

	int attempts;
	int wstatus;
	off_t cpos;
	pid_t pid;
	int ret;

	attempts = 0;

	while (1)
	{
		pid = wait(&wstatus);

		/*
		 * There may be a slight race condition where the child
		 * process dies before the parent process even adds it
		 * to the list. To work around this scenario, the code
		 * below tries MAX_ATTEMPTS times to get the child of
		 * the list, with a pause of PAUSE_MS milliseconds
		 * between each attempt.
		 *
		 * If it still can't get it, the daemon is aborted.
		 */
	again:
		cpos = get_child_pos(pid);
		if (cpos < 0)
		{
			attempts++;

			log_crit("Unable to find child (pid: %d), attempt: %d/%d\n",
				pid, attempts, MAX_ATTEMPTS);

			if (attempts < MAX_ATTEMPTS)
			{
				usleep(PAUSE_MS * 1000);
				goto again;
			}
			else
				die("Attempts exceeded for pid: %d, aborting!\n", pid);
		}
		else
			attempts = 0;

		/* Get return code. */
		if (WIFEXITED(wstatus))
			ret = WEXITSTATUS(wstatus);
		else if (WIFSIGNALED(wstatus))
			ret = WTERMSIG(wstatus) + 128; /* I'm just mimicking bash here. */
		else
			ret = 1;

		/* Send return code. */
		if (!ipc_send_int32(ret, cl.c[cpos].fd))
			log_crit("Unable to send return value to (pid: %d / fd: %d), "
				"maybe disconnected?\n", pid, cl.c[cpos].fd);

		ipc_close(1, cl.c[cpos].fd);

	/* Set empty position. */
	pthread_mutex_lock(&list_mutex);
		cl.c[cpos].fd = -1;
		cl.last_empty = cpos;
	pthread_mutex_unlock(&list_mutex);
	}

	return (NULL);
}

/**
 * @brief As the children list is dynamically allocated,
 * this function increases the list capacity.
 *
 * @note This routine assumes that the list is safely
 * protected against race conditions before it is called,
 * as it does not use any kind of locks here.
 */
static void increase_buffer(void)
{
	size_t i;
	struct children *c;

	c = realloc(cl.c, sizeof(*cl.c) * (cl.size * 2));
	if (!c)
		die("Unable to increase buffer! (from %zu bytes to %zu bytes)\n",
			cl.size, cl.size * 2);

	/* Clear all new positions. */
	for (i = 0; i < cl.size; i++)
		c->fd = -1;

	cl.last_empty = cl.size;
	cl.c     = c;
	cl.size *= 2;
}

/**
 * @brief Adds a new pid/fd pair into the children list.
 *
 * @param pid PID pair to be added in the list.
 * @param fd FD pair to be added.
 */
void reaper_add_child(pid_t pid, int fd)
{
	size_t i;
	size_t pos;

pthread_mutex_lock(&list_mutex);
	pos = cl.last_empty;

	/* If last_empty is an invalid position or if already occupied. */
	if (pos >= cl.size || cl.c[pos].fd != -1)
	{
		/* Look for empty position. */
		for (i = 0; i < cl.size; i++)
			if (cl.c[i].fd == -1)
				break;

		/* If not found, increase buffer. */
		if (i == cl.size)
		{
			increase_buffer();
			pos = cl.last_empty;
		}
		else
			pos = i;
	}

	/* Add child. */
	cl.c[pos].pid = pid;
	cl.c[pos].fd  = fd;
	cl.last_empty = pos + 1; /* just an educated guess. */
pthread_mutex_unlock(&list_mutex);
}

/**
 * @brief Initialize all resources related to the reaper:
 * - Allocate children list.
 * - Start reaper thread.
 */
void reaper_init(void)
{
	size_t i;
	pthread_t t;

	cl.size = 16;
	cl.last_empty = 0;
	cl.c = calloc(16, sizeof(*cl.c));
	if (!cl.c)
		die("Cant allocate children list!\n");

	/* Clear all new positions. */
	for (i = 0; i < cl.size; i++)
		cl.c[i].fd = -1;

	/* Start our reaper. */
	if (pthread_create(&t, NULL, wait_children, NULL))
		die("Unable to create reaper thread...");

	pthread_detach(t);
}

/**
 * @brief Deallocate all resources related to the reaper:
 * - Children list.
 */
void reaper_finish(void)
{
	free(cl.c);
}
