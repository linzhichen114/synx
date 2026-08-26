.global init_idt_ap
.type init_idt_ap, @function

# void init_idt_ap();
init_idt_ap:
    leaq idtPtr(%rip), %rax
    lidt (%rax)
    ret

.size init_idt_ap, .-init_idt_ap