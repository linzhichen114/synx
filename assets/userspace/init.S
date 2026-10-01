.global _start
.section .text

_start:
	mov $1, %eax
	mov $1, %edi
	lea message(%rip), %rsi
	mov $(message_end - message), %edx
	syscall
	cmp $(message_end - message), %rax
	jne fail

	mov $39, %eax
	syscall
	cmp $1, %rax
	jne fail

	mov $186, %eax
	syscall
	cmp $1, %rax
	jne fail

	mov $228, %eax
	mov $1, %edi
	lea monotonic_time(%rip), %rsi
	syscall
	test %rax, %rax
	jne fail
	cmpq $1000000000, monotonic_time+8(%rip)
	jae fail

	mov $229, %eax
	mov $1, %edi
	lea clock_resolution(%rip), %rsi
	syscall
	test %rax, %rax
	jne fail
	cmpq $0, clock_resolution+8(%rip)
	jle fail

	mov $63, %eax
	lea uts_name(%rip), %rdi
	syscall
	test %rax, %rax
	jne fail

	mov $4, %eax
	lea relative_hostname_path(%rip), %rdi
	lea stat_data(%rip), %rsi
	syscall
	test %rax, %rax
	jne fail
	cmpq $4, stat_data+48(%rip)
	jne fail

	mov $257, %eax
	mov $-100, %edi
	lea hostname_path(%rip), %rsi
	xor %edx, %edx
	xor %r10d, %r10d
	syscall
	test %rax, %rax
	js fail
	mov %rax, %r12

	mov $5, %eax
	mov %r12, %rdi
	lea stat_data(%rip), %rsi
	syscall
	test %rax, %rax
	jne fail
	cmpq $4, stat_data+48(%rip)
	jne fail

	mov $262, %eax
	mov $-100, %edi
	lea relative_hostname_path(%rip), %rsi
	lea stat_data(%rip), %rdx
	xor %r10d, %r10d
	syscall
	test %rax, %rax
	jne fail
	cmpq $4, stat_data+48(%rip)
	jne fail

	mov $8, %eax
	mov %r12, %rdi
	xor %esi, %esi
	mov $2, %edx
	syscall
	cmp $4, %rax
	jne fail
	mov $8, %eax
	mov %r12, %rdi
	xor %esi, %esi
	xor %edx, %edx
	syscall
	test %rax, %rax
	jne fail

	mov $32, %eax
	mov %r12, %rdi
	syscall
	test %rax, %rax
	js fail
	mov %rax, %r13
	mov $72, %eax
	mov %r12, %rdi
	mov $2, %esi
	mov $1, %edx
	syscall
	test %rax, %rax
	jne fail
	mov $72, %eax
	mov %r12, %rdi
	mov $1, %esi
	syscall
	cmp $1, %rax
	jne fail
	mov $3, %eax
	mov %r13, %rdi
	syscall
	test %rax, %rax
	jne fail

	mov $19, %eax
	mov %r12, %rdi
	lea hostname_iov(%rip), %rsi
	mov $1, %edx
	syscall
	test %rax, %rax
	jle fail

	mov $3, %eax
	mov %r12, %rdi
	syscall
	test %rax, %rax
	jne fail

	mov $79, %eax
	lea cwd_data(%rip), %rdi
	mov $256, %esi
	syscall
	cmp $2, %rax
	jne fail
	cmpb $'/', cwd_data(%rip)
	jne fail

	mov $80, %eax
	lea relative_etc_path(%rip), %rdi
	syscall
	test %rax, %rax
	jne fail
	mov $79, %eax
	lea cwd_data(%rip), %rdi
	mov $256, %esi
	syscall
	cmp $5, %rax
	jne fail
	cmpb $'e', cwd_data+1(%rip)
	jne fail
	mov $21, %eax
	lea relative_hostname_name(%rip), %rdi
	mov $4, %esi
	syscall
	test %rax, %rax
	jne fail
	mov $80, %eax
	lea root_path(%rip), %rdi
	syscall
	test %rax, %rax
	jne fail

	mov $257, %eax
	mov $-100, %edi
	lea etc_path(%rip), %rsi
	xor %edx, %edx
	xor %r10d, %r10d
	syscall
	test %rax, %rax
	js fail
	mov %rax, %r13
	mov $217, %eax
	mov %r13, %rdi
	lea dir_data(%rip), %rsi
	mov $256, %edx
	syscall
	test %rax, %rax
	jle fail
	mov $3, %eax
	mov %r13, %rdi
	syscall
	test %rax, %rax
	jne fail

	mov $95, %eax
	mov $18, %edi
	syscall
	cmp $18, %rax
	jne fail

	mov $20, %eax
	mov $1, %edi
	lea output_iov(%rip), %rsi
	mov $2, %edx
	syscall
	cmp $(vector_message_end - vector_message), %rax
	jne fail

	mov $59, %eax
	lea exec_path(%rip), %rdi
	lea exec_argv(%rip), %rsi
	lea exec_envp(%rip), %rdx
	syscall
	jmp fail

fail:
	mov $60, %eax
	mov $1, %edi
	syscall
1:
	jmp 1b

.section .rodata
message:
	.ascii "POSIX syscall smoke test: write/getpid/uname ok\n"
message_end:
hostname_path:
	.asciz "/etc/hostname"
relative_hostname_path:
	.asciz "etc/hostname"
relative_etc_path:
	.asciz "etc"
relative_hostname_name:
	.asciz "hostname"
root_path:
	.asciz "/"
etc_path:
	.asciz "/etc"
exec_path:
	.asciz "/sbin/exec-test"
exec_argv:
	.quad exec_argv0, exec_argv1, 0
exec_argv0:
	.asciz "exec-test"
exec_argv1:
	.asciz "arg"
exec_envp:
	.quad exec_env
	.quad 0
exec_env:
	.asciz "SYNX_EXEC=works"
hostname_iov:
	.quad hostname_data
	.quad 64
output_iov:
	.quad vector_message
	.quad vector_message_mid - vector_message
	.quad vector_message_mid
	.quad vector_message_end - vector_message_mid
vector_message:
	.ascii "readv/writev/clock syscall smoke test: "
vector_message_mid:
	.ascii "ok\n"
vector_message_end:

.section .bss
.balign 8
monotonic_time:
	.skip 16
clock_resolution:
	.skip 16
uts_name:
	.skip 390
hostname_data:
	.skip 64
stat_data:
	.skip 144
cwd_data:
	.skip 256
dir_data:
	.skip 256

.section .note.GNU-stack,"",@progbits


.section .note.GNU-stack,"",@progbits