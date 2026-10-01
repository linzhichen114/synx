.global syscall_entry
.type syscall_entry, @function

syscall_entry:
	swapgs
	movq %rsp, %gs:200
	movq %gs:192, %rsp
	subq $192, %rsp

	movq %rax,   0(%rsp)
	movq %rbx,   8(%rsp)
	movq %rcx,  16(%rsp)
	movq %rdx,  24(%rsp)
	movq %rsi,  32(%rsp)
	movq %rdi,  40(%rsp)
	movq %rbp,  48(%rsp)
	movq %r8,   56(%rsp)
	movq %r9,   64(%rsp)
	movq %r10,  72(%rsp)
	movq %r11,  80(%rsp)
	movq %r12,  88(%rsp)
	movq %r13,  96(%rsp)
	movq %r14, 104(%rsp)
	movq %r15, 112(%rsp)
	movq %rcx, 120(%rsp)
	movq %r11, 128(%rsp)
	movq %gs:200, %rax
	movq %rax, 136(%rsp)

	movq %rsp, %rdi
	call syscall_dispatch
	movq %rax, 0(%rsp)

	movq   8(%rsp), %rbx
	movq  24(%rsp), %rdx
	movq  32(%rsp), %rsi
	movq  40(%rsp), %rdi
	movq  48(%rsp), %rbp
	movq  56(%rsp), %r8
	movq  64(%rsp), %r9
	movq  72(%rsp), %r10
	movq  88(%rsp), %r12
	movq  96(%rsp), %r13
	movq 104(%rsp), %r14
	movq 112(%rsp), %r15
	movq 120(%rsp), %rcx
	movq 128(%rsp), %r11
	movq 0(%rsp), %rax
	movq 136(%rsp), %rsp
	/*
	 * Restore the user GS base before returning with sysretq. The CPU uses
	 * RCX for the user RIP, R11 for user RFLAGS, and the current RSP as the
	 * user stack pointer.
	 */
	swapgs
	sysretq

.size syscall_entry, .-syscall_entry