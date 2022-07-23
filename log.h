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

#ifndef LOG_H
#define LOG_H

	#include <stdio.h>
	#include <unistd.h>
	#include <inttypes.h>

	#include "daem.h"

	#define LOG_LVL_INFO 0
	#define LOG_LVL_ERR  1
	#define LOG_LVL_CRIT 2
	#define LOG_LVL_ALL  4

	extern int log_init(struct args *a);
	extern void log_close(void);
	extern void log_info(const char *fmt, ...);
	extern void log_err(const char *fmt, ...);
	extern void log_crit(const char *fmt, ...);

	#define die(...) \
		do { \
			log_crit(__VA_ARGS__); \
			_exit(1); \
		} while (0)

#endif /* LOG_H */
