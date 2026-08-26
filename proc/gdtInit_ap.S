.global gdt_load_ap
.type gdt_load_ap, @function

# void gdt_load_ap(uint32_t cpu_id);
#   %edi = cpu_id
gdt_load_ap:
    call __gdt_get_per_cpu_data   # rax = &per_cpu_data[0]

    mov %edi, %ecx                # ecx = cpu_id
    imul $192, %rcx, %rcx         # rcx = cpu_id * sizeof(PerCpuData)
    add %rcx, %rax                # rax = &per_cpu_data[cpu_id]

    add $56, %rax                 # rax = &per_cpu_data[cpu_id].gp

    lgdt (%rax)

    pushq $0x08
    lea cs_reloaded(%rip), %rcx
    pushq %rcx
    lretq

cs_reloaded:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ret

.size gdt_load_ap, .-gdt_load_ap