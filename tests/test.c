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

int main(int argc, char **argv)
{
	int i;
	char **p;
	char buff[128];

	printf("hello stdout\n");
	fprintf(stderr, "hello stderr\n");

	/* Read from stdin until EOF. */
	while (fgets(buff, sizeof buff, stdin))
		puts(buff);

	/* Confirm EOF and test stdout after stdin is EOF'ed. */
	printf("feof(stdin): %d\n", feof(stdin));
	fprintf(stderr, "testing stderr again!\n");

	/* Arguments. */
	printf("argc: %d\n", argc);
	for (i = 0; i < argc; i++)
		printf("argv[%d] = %s\n", i, argv[i]);

	/* 'Non-typical' way to print arguments. */
	printf("\"non-typical\":\n");
	for (p = argv; *p; p++)
		printf("argv: %s\n", *p);

	/* Get some env var to make sure they're no 'corrupted'. */
	printf("PWD: (%s)\n", getenv("PWD"));

	/* Return some number. */
	return (42);
}
