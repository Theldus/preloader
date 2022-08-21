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
.lcomm new_argc, 8

/*
 *                This is the preloader's main entrypoint
 *
 * After the entrypoint/_start is modified, this function is executed in place
 * of the original code.
 *
 * This function is responsible for backing up/restore the pointer to be
 * registered in atexit(BA_OS), as well as invoking the 'daemon_main'.
 *
 * After the child process is forked and returns from daemon_main, this
 * function changes the arguments to the new argc/argv (via arch_change_argv),
 * retrieves the old content of _start/entrypoint and restarts execution as
 * if nothing had happened.
 *
 * Note:
 * This function was chosen to be written in assembly because of the expected
 * layout of the stack and registers at the beginning of _start: it was 'too
 * dangerous' to trust the C compiler to write code that was not optimized in
 * unexpected ways, plus small snippets of ASM would still be needed, even in
 * C. So choosing to write in ASM was the best and safest option, and it
 * allows us to optimize the rest of the code knowing that the
 * architecture-dependent part works as expected every time.
 *
 * Despite this, the general behavior should be similar on other architectures
 * as well. You can use this code as a starting point to write for other
 * architectures if you want to port the preloader to them.
 *
 * Register usage:
 * r12: atexit_ptr/%rdx backup
 * r13: cwd_argv
 * rax, rdi, rsi, rdx: tmp values/args
 */
.section .text
.globl arch_pre_daemon_main
.type  arch_pre_daemon_main, @function
arch_pre_daemon_main:
	push %rbp
	mov  %rsp, %rbp

	# Backup atexit pointer
	mov %rdx, %r12

	# Call our daemon main
	# Return: returns cwd_argv in %rax
	lea  new_argc(%rip), %rdi
	call daemon_main
	mov  %rax, %r13 # Backup our cwd_argv

	# Grab our old argc and compare to the new one
	# Our stack:
	# 24(%rsp) -> argv
	# 16(%rsp) -> argc
	#  8(%rsp) -> ret_addr
	#  0(%rsp) -> rbp
	movl 16(%rsp), %edi
	movl new_argc(%rip), %esi
	call arch_validate_argc

	# Change argc
	mov new_argc(%rip), %rdi # First arg of change_argv too
	mov %rdi, 16(%rsp)

	# Change argv
	mov %r13, %rsi      # cwd_argv
	lea 24(%rsp), %rdx  # sp[IDX_ARGV]
	call arch_change_argv

	# Restore original entry point content
	# Return: returns the size of the patch in %rax
	call arch_restore_start

	# Fix return address
	sub %rax, 8(%rsp)

	# Restore %rdx and return
	mov %r12, %rdx
	leaveq
	retq
