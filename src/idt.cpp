#include "idt.h"
#include "gdt.h"
#include "kprint.h"
#include <string.h>
#include "sysdef.h"
#include "kallsyms.h"
#include "proc/sched.h"
#include "apic/apic.h"
#include "ps2_keyboard.h"

idt::IDTPtr idtPtr;

namespace idt {

#define IDT_ENTRIES 256
static IDTEntry idt[IDT_ENTRIES];

extern "C" void isr_entry();
extern "C" void irq_entry();

extern "C" void exceptionHandler(InterruptFrame* frame, uint64_t error_code) {
    asm volatile("cli");

    const char* exceptions[] = {
        "Division By Zero (#DE)", "Debug (#DB)", "Non-maskable Interrupt", "Breakpoint (#BP)",
        "Overflow (#OF)", "Bound Range Exceeded (#BR)", "Invalid Opcode (#UD)", "Device Not Available (#NM)",
        "Double Fault (#DF)", "Coprocessor Segment Overrun", "Invalid TSS (#TS)", "Segment Not Present (#NP)",
        "Stack-Segment Fault (#SS)", "General Protection Fault (#GP)", "Page Fault (#PF)",
        "Reserved", 
        "x87 Floating-Point Exception (#MF)", "Alignment Check (#AC)", "Machine Check (#MC)", "SIMD Floating-Point Exception (#XF)",
        "Virtualization Exception (#VE)", "Control Protection Exception (#CP)",
        "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
        "Hypervisor Injection Exception (#HV)", "VMM Communication Exception (#VC)", "Security Exception (#SX)",
        "Reserved", "Triple Fault"
    };

    char msg_buf[128]; msg_buf[0] = '\0';

    strcat(msg_buf, "ISR: ");
    if (frame->int_no < 16) {
        strcat(msg_buf, exceptions[frame->int_no]);
    } else {
        strcat(msg_buf, "Hardware Interrupt #");
        char num_buf[8];
        itoa(num_buf, frame->int_no);
        strcat(msg_buf, num_buf);
    }

    char err_buf[16]; err_buf[0] = '\0';
    if (frame->int_no == 8 || frame->int_no == 10
     || frame->int_no == 11 || frame->int_no == 12
     || frame->int_no == 13 || frame->int_no == 14
     || frame->int_no == 17 || frame->int_no == 21
     || frame->int_no == 29 || frame->int_no == 30) {
        strcat(msg_buf, " - Err: 0x");
        itoa(err_buf, error_code);
    }
    strcat(msg_buf, err_buf);

    if (frame->int_no == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        strcat(msg_buf, ", CR2: 0x");
        char cr2_buf[20];
        itoa(cr2_buf, cr2);
        strcat(msg_buf, cr2_buf);
    }

    kernel_panic(msg_buf);
}

extern "C" void irqHandler(InterruptFrame* frame) {
    uint8_t vector = frame->int_no;

    switch (vector) {
        case apic::APIC_TIMER_VECTOR: {
            apic::timer_tick();
            apic::send_eoi();
            scheduler::schedule();
            break;
        }
        case ps2::PS2_KEYBOARD_VECTOR: {
            uint8_t status = inb(ps2::STATUS_PORT);
            if (!(status & ps2::STATUS_OUTPUT_FULL)) {
                kout << "idt: (IRQ 1) Spurious interruption signal detected, ignored.";
                apic::send_eoi();
                break;
            }

            uint8_t data = inb(ps2::DATA_PORT);
            ps2::decode_and_push(data);
            apic::send_eoi();
            break;
        }
        default: {
            kout << "idt: WARNING: Unhandled IRQ, vector "
                 << vector << "." << endl;
            apic::send_eoi();
            break;
        }
    }
}

static void idtSetGate(uint8_t num, uint64_t handler) {
    idt[num].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[num].selector    = 0x08;
    idt[num].ist         = 0;
    idt[num].type_attr   = 0x8E;
    idt[num].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[num].zero        = 0;
}


__attribute__((naked)) void isr_stub_generic() {
    __asm__ volatile(
        "push $0\n"
        "push $0xFF\n"
        "jmp isr_entry\n"
    );
}

// #DE
__attribute__((naked)) void isr_stub_0() {
    __asm__ volatile(
        "push $0\n"
        "push $0\n"
        "jmp isr_entry\n"
    );
}

// #UD
__attribute__((naked)) void isr_stub_6() {
    __asm__ volatile(
        "push $0\n"
        "push $6\n"
        "jmp isr_entry\n"
    );
}

// #DF
__attribute__((naked)) void isr_stub_8() {
    __asm__ volatile(
        "cli\n"
        "mov $0xDF, %%al\n"
        "out %%al, $0x80\n"
        "hlt\n"
        "jmp .-2\n"
        ::: "al"
    );__builtin_unreachable();

    __asm__ volatile(
        "cli\n"
        "push $8\n"
        "jmp isr_entry\n"
    );
}

// Coprocessor Segment Overrun
__attribute__((naked)) void isr_stub_9() {
    __asm__ volatile(
        "push $0\n"
        "push $9\n"
        "jmp isr_entry\n"
    );
}

// #GP
__attribute__((naked)) void isr_stub_13() {
    __asm__ volatile(
        "push $13\n"
        "jmp isr_entry\n"
    );
}

// #PF
__attribute__((naked)) void isr_stub_14() {
    __asm__ volatile(
        "push $14\n"
        "jmp isr_entry\n"
    );
}

__attribute__((naked)) void isr_entry() {
    __asm__ volatile(
        "pushq %rax\n"
        "pushq %rbx\n"
        "pushq %rcx\n"
        "pushq %rdx\n"
        "pushq %rsi\n"
        "pushq %rdi\n"
        "pushq %rbp\n"
        "pushq %r8\n"
        "pushq %r9\n"
        "pushq %r10\n"
        "pushq %r11\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "movq %rsp, %rdi\n"
        "movq 128(%rsp), %rsi\n"
        "call exceptionHandler\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %r11\n"
        "popq %r10\n"
        "popq %r9\n"
        "popq %r8\n"
        "popq %rbp\n"
        "popq %rdi\n"
        "popq %rsi\n"
        "popq %rdx\n"
        "popq %rcx\n"
        "popq %rbx\n"
        "popq %rax\n"
        "addq $16, %rsp\n"
        "iretq\n"
    );
}

__attribute__((naked)) void irq_entry() {
    __asm__ volatile(
        "pushq %rax\n"
        "pushq %rbx\n"
        "pushq %rcx\n"
        "pushq %rdx\n"
        "pushq %rsi\n"
        "pushq %rdi\n"
        "pushq %rbp\n"
        "pushq %r8\n"
        "pushq %r9\n"
        "pushq %r10\n"
        "pushq %r11\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "movq %rsp, %rdi\n"
        "call irqHandler\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %r11\n"
        "popq %r10\n"
        "popq %r9\n"
        "popq %r8\n"
        "popq %rbp\n"
        "popq %rdi\n"
        "popq %rsi\n"
        "popq %rdx\n"
        "popq %rcx\n"
        "popq %rbx\n"
        "popq %rax\n"
        "addq $16, %rsp\n"
        "iretq\n"
    );
}

__attribute__((naked)) void irq_stub_0() {
    __asm__ volatile(
        "push $0\n"
        "push $32\n"
        "jmp irq_entry\n"
    );
}

__attribute__((naked)) void irq_stub_1() {
    __asm__ volatile(
        "push $0\n"
        "push $33\n"
        "jmp irq_entry\n"
    );
}

void set_irqHandler(uint8_t vector, uint64_t handler_addr) {
    if (vector < 32) return;
    idtSetGate(vector, handler_addr);
}

void init_bsp() {
    memset(idt, 0, sizeof(idt));

    for (int i = 0; i <= 32; i++)
        idtSetGate(i, (uint64_t)isr_stub_generic);

    idtSetGate(0,  (uint64_t)isr_stub_0);
    idtSetGate(6,  (uint64_t)isr_stub_6);
    idtSetGate(8,  (uint64_t)isr_stub_8);
    idtSetGate(9,  (uint64_t)isr_stub_9);
    idtSetGate(13, (uint64_t)isr_stub_13);
    idtSetGate(14, (uint64_t)isr_stub_14);

    set_irqHandler(apic::APIC_TIMER_VECTOR,  (uint64_t)irq_stub_0);
    set_irqHandler(ps2::PS2_KEYBOARD_VECTOR, (uint64_t)irq_stub_1);

    idtPtr.limit = sizeof(idt) - 1;
    idtPtr.base = (uint64_t)&idt;

    __asm__ volatile("lidt %0" : : "m"(idtPtr));

    kout << "idt: IDT Loaded." << endl;
}

}