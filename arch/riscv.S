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
 * s3: atexit_ptr/x0 backup
 * s4: cwd_argv
 * s5: tmp backup of new_argc value
 * s6: new_argc address
 * everything else: tmp values/args
 */
.align 8
.section .text
.globl hellomiaou
.globl arch_pre_daemon_main
.type  arch_pre_daemon_main, %function
arch_pre_daemon_main:
	addi sp, sp, -16
	sd ra, 8(sp)
	sd fp, 0(sp)
	mv fp, sp

	/* Backup atexit pointer. */
	mv s3, a0

	/* Call our daemon main
	 * Return: returns cwd_argv in x0. */
	la a0, new_argc
	mv s6, a0  /* Backup new_argc addr. */
	call daemon_main
	mv s4, a0  /* Backup our cwd_argv. */

	/* Grab our old argc and compare to the new one
	 * Our stack:
	 * 24(sp) -> argv
	 * 16(sp) -> argc
	 *  8(sp) -> ret_addr/lr
	 *  0(sp) -> fp */
	ld a0, 16(sp)
	ld a1, 0(s6)
	mv s5, a1    /* tmp backup of our new_argc. */
	call arch_validate_argc

	/* Change argc. */
	sd s5, 16(sp)

	/* Change argv. */
	mv a0, s5     /* new_argc     */
	mv a1, s4     /* cwd_argv     */
	addi a2, sp, 24 /* sp[IDX_ARGV] */
	call arch_change_argv

	/* Restore original entry point content
	 * Return: returns the size of the patch in x0. */
	call arch_restore_start

	/* Fix return address. */
	mv sp, fp
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	sub ra, ra, a0 /* decrease patch size. */

	/* Restore proper x0/atexit value and return. */
	mv a0, s3
	ret
