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

#include "log.h"
#include "preloader.h"

static const char *log_string[] = {"info", "err", "crit"};
static const char dev_null[] = "/dev/null";
static struct args *args;

/**
 * @brief Initialize the log system for a given log-level log file
 * and log file provided in the programa arguments @p a.
 *
 * @param a Program arguments, containing the log info required
 *          to initialize the log-level.
 *
 * @return Returns 0 if success, -1 otherwise.
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
 * @brief Close all resources used during the logging.
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
 * @brief Logs a formatted message provided in @p fmt, for a
 * given log level @p l, into the already specified output.
 *
 * @param l Log level.
 * @param fmt Formatted string to be logged into the screen,
 *            or file.
 * @param ap va_list containing the arguments.
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
 * @brief Logs a formatted message @p fmt as an info-message
 * into the already defined output.
 *
 * @param fmt Formatted message.
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
 * @brief Logs a formatted message @p fmt as an error-message
 * into the already defined output.
 *
 * @param fmt Formatted message.
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
 * @brief Logs a formatted message @p fmt as a critical-message
 * into the already defined output.
 *
 * @param fmt Formatted message.
 *
 * @note Critical messages cannot be omitted.
 */
void log_crit(const char *fmt, ...)
{
	/* Critical log-level messages are always showed. */
	va_list ap;
	va_start(ap, fmt);
	log_msg(LOG_LVL_CRIT, fmt, ap);
	va_end(ap);
}
