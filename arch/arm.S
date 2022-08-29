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
.lcomm new_argc, 4

/*
 * This is the preloader's main entrypoint
 *
 * Register usage:
 * r4: atexit_ptr/a1/r0 backup
 * r5: cwd_argv
 * r6: tmp backup of new_argc value
 * everything else: tmp values/args
 */
.align 8
.section .text
.globl arch_pre_daemon_main
.type  arch_pre_daemon_main, %function
arch_pre_daemon_main:
	push {fp, lr}
	add  fp, sp, #0

	@ Backup atexit pointer
	mov r4, a1

	@ Call our daemon main
	@ Return: returns cwd_argv in r0/a1
	ldr a1, =new_argc
	bl  daemon_main
	mov r5, a1 @ Backup our cwd_argv

	@ Grab our old argc and compare to the new one
	@ Our stack:
	@ 12(sp) -> argv
	@  8(sp) -> argc
	@  4(sp) -> ret_addr/lr
	@  0(sp) -> fp
	ldr a1, [sp, #8]
	ldr a2, =new_argc
	ldr a2, [a2]
	mov r6, a2    @ tmp backup of our new_argc
	bl  arch_validate_argc

	@ Change argc
	str r6, [sp, #8]

	@ Change argv
	mov a1, r6      @ new_argc
	mov a2, r5      @ cwd_argv
	add a3, sp, #12 @ sp[IDX_ARGV]
	bl  arch_change_argv

	@ Restore original entry point content
	@ Return: returns the size of the patch in r0/a1
	bl arch_restore_start

	@ Fix return address
	add sp, fp, #0
	pop {fp, lr}
	sub lr, lr, a1 @ decrease patch size

	@ Restore proper a1/r0/atexit value and return
	mov a1, r4
	bx  lr
