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

#ifndef __UCLIBC__
#include <sys/auxv.h>
#endif

static char expected_char = 'a';

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

	printf("argv[0] = %s\n", argv[0]);

	for (i = 1; i < argc; i++)
	{
		/* Validate arguments: shoud always be: 'a', 'b', 'c'... */
		if (argv[i][0] == expected_char)
		{
			printf("argv[%d] = %s\n", i, argv[i]);
			expected_char++;
			if (expected_char > 'z')
				expected_char = 'a';
		}
		else
			return (21);
	}

	/* 'Non-typical' way to print arguments. */
	printf("\"non-typical\":\n");
	p = argv;
	expected_char = 'a';

	printf("argv: %s\n", *p++);
	for (; *p; p++)
	{
		if (p[0][0] == expected_char)
		{
			printf("argv: %s\n", *p);
			expected_char++;
			if (expected_char > 'z')
				expected_char = 'a';
		}
		else
			return (22);
	}

	/* Get some env var to make sure they're no 'corrupted'. */
	printf("PWD: (%s)\n", getenv("PWD"));

	/* Get some aux val to make sure they're accessible too. */
#ifndef __UCLIBC__
	printf("AT_PAGESZ: %lu\n", getauxval(AT_PAGESZ));
#endif

	/* Return some number. */
	return (42);
}
