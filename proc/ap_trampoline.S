.extern PER_CPU_DATA_SIZE
.extern PER_CPU_GP_OFFSET
.global ap_entry
.type ap_entry, @function

# void ap_entry(struct limine_mp_info* info);
#    %rdi = info pointer (Limine guarantees this)
ap_entry:
    mov %rdi, %r12


    # processor_id = info->processor_id (offset 0, uint32_t)
    mov (%rdi), %ecx
    imul PER_CPU_DATA_SIZE(%rip), %rcx
    leaq per_cpu_data(%rip), %rax
    add %rcx, %rax
    add PER_CPU_GP_OFFSET(%rip), %rax
    lgdt (%rax)


    pushq $0x08
    leaq .Lcs_ok(%rip), %rcx
    pushq %rcx
    lretq

.Lcs_ok:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    call init_idt_ap

    mov 24(%r12), %rsp
    xor %rbp, %rbp

    mov %r12, %rdi
    call ap_main

    cli
    hlt
    jmp .-2

.size ap_entry, .-ap_entry