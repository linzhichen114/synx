.section .text
.global switch_task
.type switch_task, @function

switch_task:
    # ================= 1. 保存当前（旧）任务的现场 =================
    # %rdi 是第一个参数 (old_context)
    # 注意：偏移量必须与 TaskContext 结构体完全一致
    mov %rax, 0x30(%rdi)
    mov %rbx, 0x28(%rdi)
    mov %rcx, 0x20(%rdi)
    mov %rdx, 0x18(%rdi)
    mov %rsi, 0x10(%rdi)
    # rdi 最后保存，因为后面还要用
    mov %rbp, 0x00(%rdi)
    mov %r8,  0x38(%rdi)
    mov %r9,  0x40(%rdi)
    mov %r10, 0x48(%rdi)
    mov %r11, 0x50(%rdi)
    mov %r12, 0x58(%rdi)
    mov %r13, 0x60(%rdi)
    mov %r14, 0x68(%rdi)
    mov %r15, 0x70(%rdi)
    mov %rdi, 0x08(%rdi)  # 保存 rdi

    # 保存当前的栈指针 (RSP)
    mov %rsp, 0x88(%rdi)

    # 保存返回地址 (RIP)。函数调用时，返回地址在栈顶
    mov (%rsp), %rax
    mov %rax, 0x78(%rdi)

    # ================= 2. 恢复新任务的现场 =================
    # %rsi 是第二个参数 (new_context)
    mov 0x30(%rsi), %rax
    mov 0x28(%rsi), %rbx
    mov 0x20(%rsi), %rcx
    mov 0x18(%rsi), %rdx
    mov 0x10(%rsi), %rsi  # 恢复 rsi
    mov 0x00(%rsi), %rbp
    mov 0x38(%rsi), %r8
    mov 0x40(%rsi), %r9
    mov 0x48(%rsi), %r10
    mov 0x50(%rsi), %r11
    mov 0x58(%rsi), %r12
    mov 0x60(%rsi), %r13
    mov 0x68(%rsi), %r14
    mov 0x70(%rsi), %r15
    mov 0x08(%rsi), %rdi  # 恢复 rdi

    # 恢复新任务的栈指针
    mov 0x88(%rsi), %rsp

    # 使用 iretq 恢复新任务的 RIP、CS 和 RFLAGS
    # 在栈上伪造中断帧：SS, RSP, RFLAGS, CS, RIP
    push 0x98(%rsi)   # SS
    push 0x88(%rsi)   # RSP
    push 0x90(%rsi)   # RFLAGS
    push 0x80(%rsi)   # CS
    push 0x78(%rsi)   # RIP
    iretq