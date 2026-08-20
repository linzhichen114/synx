.global switch_context
.type switch_context, @function

# void switch_context(scheduler::CpuContext* old_ctx, const scheduler::CpuContext* new_ctx);
#   %rdi = old_ctx, %rsi = new_ctx
switch_context:
    # 保存旧任务的 callee-saved 寄存器到 old_ctx
    movq %rbx, 0x08(%rdi)
    movq %rbp, 0x28(%rdi)
    movq %r12, 0x30(%rdi)
    movq %r13, 0x38(%rdi)
    movq %r14, 0x40(%rdi)
    movq %r15, 0x48(%rdi)
    movq %rsp, 0x50(%rdi)

    # 从 new_ctx 恢复新任务的 callee-saved 寄存器
    movq 0x08(%rsi), %rbx
    movq 0x28(%rsi), %rbp
    movq 0x30(%rsi), %r12
    movq 0x38(%rsi), %r13
    movq 0x40(%rsi), %r14
    movq 0x48(%rsi), %r15
    movq 0x50(%rsi), %rsp

    ret

.size switch_context, .-switch_context