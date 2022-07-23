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
#include <fcntl.h>
#include <stdarg.h>

#include "daem.h"
#include "log.h"

static const char *log_string[] = {"info", "err", "crit"};
static const char dev_null[] = "/dev/null";
static struct args *args;

/**
 *
 */
int log_init(struct args *a)
{
	if (!a || a->log_lvl < LOG_LVL_INFO || a->log_lvl > LOG_LVL_ALL)
		return (-1);

	args = a;

	if (args->log_fd >= 0)
		return (0);

	if (!args->log_file)
		args->log_file = (char*)dev_null;

	args->log_fd = open(args->log_file, O_WRONLY|O_APPEND|O_CREAT);
	if (args->log_fd < 0)
	{
		args->log_fd = STDERR_FILENO;
		return (-1);
	}
	return (0);
}

/**
 *
 */
void log_close(void)
{
	if (args && args->log_file)
	{
		close(args->log_fd);
		if (args->log_file != dev_null)
			free(args->log_file);
	}
}

/**
 *
 */
static void log_msg(int l, const char *fmt, va_list ap)
{
	int log_fd = STDERR_FILENO;
	if (l < LOG_LVL_INFO || l > LOG_LVL_ALL)
		return;
	if (args)
		log_fd = args->log_fd;
	dprintf(log_fd, "[%s] ", log_string[l]);
	vdprintf(log_fd, fmt, ap);
}

/**
 *
 */
void log_info(const char *fmt, ...)
{
	va_list ap;
	if (args && args->log_lvl != LOG_LVL_INFO && args->log_lvl != LOG_LVL_ALL)
		return;
	va_start(ap, fmt);
	log_msg(LOG_LVL_INFO, fmt, ap);
	va_end(ap);
}

/**
 *
 */
void log_err(const char *fmt, ...)
{
	va_list ap;
	if (args && args->log_lvl != LOG_LVL_ERR && args->log_lvl != LOG_LVL_ALL)
		return;
	va_start(ap, fmt);
	log_msg(LOG_LVL_ERR, fmt, ap);
	va_end(ap);
}

/**
 *
 */
void log_crit(const char *fmt, ...)
{
	/* Critical log-level messages are always showed. */
	va_list ap;
	va_start(ap, fmt);
	log_msg(LOG_LVL_CRIT, fmt, ap);
	va_end(ap);
}
