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

/* Stack layout. */
.equ argv_pos,     28
.equ argc_pos,     24
.equ ret_addr_pos, 20
.equ ebp_pos,      16

/* Arguments. */
.equ new_argc_pos, 12
.equ third_arg,     8
.equ second_arg,    4
.equ first_arg,     0

/* Quick macro to load from a stack and store in the stack. */
.macro stack2stack src dest
	movl \src(%esp), %eax
	movl %eax, \dest(%esp)
.endm

/* Quick macro to load an address from stack and store in the stack. */
.macro lea2stack src dest
	lea  \src(%esp), %eax
	mov  %eax, \dest(%esp)
.endm

/*
 * This is the preloader's main entrypoint
 *
 * Register usage:
 * esi: atexit_ptr/%rdx backup
 * edi: cwd_argv
 * everything else: tmp values/args
 */
.section .text
.globl arch_pre_daemon_main
.type  arch_pre_daemon_main, @function
arch_pre_daemon_main:
	push %ebp
	mov  %esp, %ebp
	sub  $16,  %esp

	# Backup atexit pointer
	mov %edx, %esi

	# Call our daemon main
	# Return: returns cwd_argv in %eax
	lea2stack new_argc_pos first_arg
	call daemon_main
	mov  %eax, %edi # Backup our cwd_argv

	# Grab our old argc and compare to the new one
	# Our stack:
	# 28(%esp) -> argv
	# 24(%esp) -> argc
	# 20(%esp) -> ret_addr
	# 16(%esp) -> rbp
	# 12(%esp) -> new_argc
	#  8(%esp) -> third  func arg
	#  4(%esp) -> second func arg
	#  0(%esp) -> first  func arg

	stack2stack argc_pos     first_arg
	stack2stack new_argc_pos second_arg
	call arch_validate_argc

	# Change argc
	stack2stack new_argc_pos argc_pos # eax = new_argc_pos

	# Change argv
	mov  %eax, first_arg(%esp)   # new_argc_pos
	mov  %edi, second_arg(%esp)  # cwd_argv
	lea2stack argv_pos third_arg # sp[IDX_ARGV]
	call arch_change_argv

	# Restore original entry point content
	# Return: returns the size of the patch in %rax
	call arch_restore_start

	# Fix return address
	sub %eax, ret_addr_pos(%esp)

	# Restore %edx and return
	mov %esi, %edx
	leave
	ret
