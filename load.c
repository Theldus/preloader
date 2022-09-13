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

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "log.h"

/**
 * @brief For a given file @p file, read a dynamic
 * library for each line specified on it.
 *
 * @param file File to be read, containing shared lib
 * file paths, one per line.
 *
 * @return Always 0.
 */
int load_file(const char *file)
{
	ssize_t lbytes;
	size_t  rbytes;
	char   *line;
	FILE   *f;

	f = fopen(file, "r");
	if (!f)
		die("Unable to read file: %s\n", file);

	line   = NULL;
	lbytes = 0;
	rbytes = 0;
	while ((lbytes = getline(&line, &rbytes, f)) != -1)
	{
		line[lbytes - 1] = '\0';

		/*
		 * Yes... I am purposely ignoring the return of 'dlopen':
		 * if it failed, there is nothing that can be done, if it
		 * succeeded, there is no need to save the handler either,
		 * since it is not known how long the library should stay
		 * available in memory.
		 *
		 * Therefore, everything is expected to die along with the
		 * process.
		 */
		if (!dlopen(line, RTLD_NOW))
			log_info("Unable to dlopen lib: %s\nr: %s\n\n", line,
				dlerror());
	}
	free(line);
	fclose(f);
	return (0);
}
