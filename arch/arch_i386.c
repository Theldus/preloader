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

#include <stdint.h>
#include <string.h>

#include "arch.h"
#include "log.h"
#include "util.h"

#ifndef __i386__
#error "This file should be built for i386-only targets!"
#endif

/**/
static uintptr_t arch_addr_start;

/**/
static uint8_t patch[] = {
	/* mov $imm32, %eax. */
	0xb8,
	0x00, 0x00, 0x00, 0x00,
	/* call *%eax. */
	0xff, 0xd0
};

/**/
static uint8_t bck_start[sizeof patch] = {0};

/**
 *
 */
size_t arch_restore_start(void) {
	memcpy((char*)arch_addr_start, bck_start, sizeof bck_start);
	return (sizeof bck_start);
}

/**
 *
 */
int arch_patch_start(uintptr_t start)
{
	COMPILE_TIME_ASSERT(sizeof(uintptr_t) == sizeof(uint32_t));

	int i;

	union
	{
		int32_t addr;
		uint8_t val[4];
	} u_addr;

	arch_addr_start = start;

	/* Backup the bytes that we're overwritten. */
	memcpy(bck_start, (void*)start, sizeof patch);

	/* Calculate rel-offset. */
	u_addr.addr = (uint32_t)arch_pre_daemon_main;

	/* Patch: movabs $imm64, %rax. */
	for (i = 0; i < 4; i++)
		patch[1 + i] = u_addr.val[i];

	memcpy((void*)start, patch, sizeof patch);
	return (0);
}
