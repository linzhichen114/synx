.global _start
.section .text

_start:
	cmpq $2, (%rsp)
	jne fail
	mov 8(%rsp), %r12
	cmpb $'e', (%r12)
	jne fail
	cmpb $'x', 1(%r12)
	jne fail
	cmpb $'e', 2(%r12)
	jne fail
	cmpb $'c', 3(%r12)
	jne fail
	cmpb $'-', 4(%r12)
	jne fail
	cmpb $'t', 5(%r12)
	jne fail
	cmpb $'e', 6(%r12)
	jne fail
	cmpb $'s', 7(%r12)
	jne fail
	cmpb $'t', 8(%r12)
	jne fail
	cmpb $0, 9(%r12)
	jne fail

	mov 16(%rsp), %r12
	cmpb $'a', (%r12)
	jne fail
	cmpb $'r', 1(%r12)
	jne fail
	cmpb $'g', 2(%r12)
	jne fail
	cmpb $0, 3(%r12)
	jne fail
	cmpq $0, 24(%rsp)
	jne fail

	mov 32(%rsp), %r12
	cmpb $'S', (%r12)
	jne fail
	cmpb $'Y', 1(%r12)
	jne fail
	cmpb $'N', 2(%r12)
	jne fail
	cmpb $'X', 3(%r12)
	jne fail
	cmpb $'_', 4(%r12)
	jne fail
	cmpb $'E', 5(%r12)
	jne fail
	cmpb $'X', 6(%r12)
	jne fail
	cmpb $'E', 7(%r12)
	jne fail
	cmpb $'C', 8(%r12)
	jne fail
	cmpb $'=', 9(%r12)
	jne fail
	cmpb $'w', 10(%r12)
	jne fail
	cmpb $'o', 11(%r12)
	jne fail
	cmpb $'r', 12(%r12)
	jne fail
	cmpb $'k', 13(%r12)
	jne fail
	cmpb $'s', 14(%r12)
	jne fail
	cmpb $0, 15(%r12)
	jne fail
	cmpq $0, 40(%rsp)
	jne fail

	mov $1, %eax
	mov $1, %edi
	lea success_message(%rip), %rsi
	mov $(success_end - success_message), %edx
	syscall
	test %rax, %rax
	js fail
	mov $60, %eax
	xor %edi, %edi
	syscall

fail:
	mov $60, %eax
	mov $1, %edi
	syscall
1:
	jmp 1b

.section .rodata
success_message:
	.ascii "execve argv/envp smoke test: ok\n"
success_end:

.section .note.GNU-stack,"",@progbits
