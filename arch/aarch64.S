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

.section .bss
.type new_argc, %object
.size new_argc, 8
new_argc: .zero 8

/*
 * This is the preloader's main entrypoint
 *
 * Register usage:
 * x19: atexit_ptr/x0 backup
 * x20: cwd_argv
 * x21: tmp backup of new_argc value
 * x22: new_argc address
 * everything else: tmp values/args
 */
.align 8
.section .text
.globl arch_pre_daemon_main
.type  arch_pre_daemon_main, %function
arch_pre_daemon_main:
	stp fp, lr, [sp, -16]!
	mov fp, sp

	/* Backup atexit pointer. */
	mov x19, x0

	/* Call our daemon main
	 * Return: returns cwd_argv in x0. */
	adrp x0,  :got:new_argc
	ldr  x0,  [x0, #:got_lo12:new_argc]
	mov  x22, x0  /* Backup new_argc addr. */
	bl  daemon_main
	mov x20, x0      /* Backup our cwd_argv. */

	/* Grab our old argc and compare to the new one
	 * Our stack:
	 * 24(sp) -> argv
	 * 16(sp) -> argc
	 *  8(sp) -> ret_addr/lr
	 *  0(sp) -> fp */
	ldr x0, [sp, #16]
	ldr x1, [x22]
	mov x21, x1    /* tmp backup of our new_argc. */
	bl  arch_validate_argc

	/* Change argc. */
	str x21, [sp, #16]

	/* Change argv. */
	mov x0, x21     /* new_argc     */
	mov x1, x20     /* cwd_argv     */
	add x2, sp, #24 /* sp[IDX_ARGV] */
	bl  arch_change_argv

	/* Restore original entry point content
	 * Return: returns the size of the patch in x0. */
	bl arch_restore_start

	/* Fix return address. */
	mov sp, fp
	ldp fp, lr, [sp], 16
	sub lr, lr, x0 /* decrease patch size. */

	/* Restore proper x0/atexit value and return. */
	mov x0, x19
	ret
