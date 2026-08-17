#include "gdt.h"
#include "ostreamk.h"
#include <memory.h>

static uint64_t _kernel_stack_top;
static GDTEntry gdt[7];
static GDTPtr   gp;
static TSSEntry tss;

void loadGdt() {
    __asm__ volatile (
        "lgdt %[GDTPtr]\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "push $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        :
        : [GDTPtr] "m"(gp)
        : "rax", "memory"
    );
}

void loadTss() {
    __asm__ volatile (
        "mov $0x28, %%rax\n"
        "ltr %%ax\n"
        : 
        : 
        : "rax", "memory"
    );
}

void tssInit(uint64_t kStackTop) {
    ostreamk kout;
    kout << "gdt: Creating TSS Descriptor (Kernel Stack Top: " << (uint64_t*)kStackTop << ") :" << endl;

    // 1. 将 TSS 清零
    memset(&tss, 0, sizeof(struct TSSEntry));

    // 2. 设置 Ring 0 栈指针
    kout << "gdt:   Setting RSP0..." << endl;
    tss.rsp0 = kStackTop;

    // 3. 设置 I/O 权限位图基址
    kout << "gdt:   Setting I/O Permission Bitmap Base Address..." << endl;
    tss.iopbBase = sizeof(struct TSSEntry);

    uint64_t tss_base = (uint64_t) &tss;
    uint32_t limit    = sizeof(struct TSSEntry) - 1; // 103

    // 低 8 字节描述符（索引 5）
    kout << "gdt:   Writing Low 8 Bytes Descriptor (Index 5)..." << endl;
    gdt[5].limit_low = limit & 0xFFFF;
    gdt[5].base_low = tss_base & 0xFFFF;
    gdt[5].base_middle = (tss_base >> 16) & 0xFF;
    gdt[5].access = 0x89;      // Present=1, DPL=0, S=0, Type=1001 (Available 64-bit TSS)
    gdt[5].granularity = ((limit >> 16) & 0x0F) | ((tss_base >> 24) & 0xFF);

    // 高 8 字节描述符（索引 6）
    kout << "gdt:   Writing High 8 Bytes Descriptor (Index 6)..." << endl;
    gdt[6].limit_low = (tss_base >> 32) & 0xFFFF;
    gdt[6].base_low = (tss_base >> 48) & 0xFFFF;
    gdt[6].base_middle = 0;
    gdt[6].access = 0;
    gdt[6].granularity = 0;

    kout << "gdt: - All Done." << endl;
}

void gdtInit() {
    ostreamk kout;

    kout << "gdt: Initlizing GDT & TSS..." << endl;

    // null descriptor
    kout << "gdt: Creating null descriptor... ";
    gdt[0].limit_low   = 0;
    gdt[0].base_low    = 0;
    gdt[0].base_middle = 0;
    gdt[0].access      = 0;
    gdt[0].granularity = 0;
    gdt[0].base_high   = 0;
    kout << "done" << endl;

    // ring 0 code
    kout << "gdt: Creating Ring 0 Code descriptor... ";
    gdt[1].limit_low   = 0xFFFF;
    gdt[1].base_low    = 0;
    gdt[1].base_middle = 0;
    gdt[1].access      = 0x9A; // Present=1, DPL=0, Type=Code, Readable
    gdt[1].granularity = 0xAF; // Granularity=1(4KB), 32-bit/64-bit(L=1), Limit High
    gdt[1].base_high   = 0;
    kout << "done" << endl;

    // ring 0 data
    kout << "gdt: Creating Ring 0 Data descriptor... ";
    gdt[2].limit_low   = 0xFFFF;
    gdt[2].base_low    = 0;
    gdt[2].base_middle = 0;
    gdt[2].access      = 0x92; // Present=1, DPL=0, Type=Data, Writable
    gdt[2].granularity = 0xCF; // Granularity=1(4KB), 32-bit, Limit High
    gdt[2].base_high   = 0;
    kout << "done" << endl;

    // ring 3 code 
    kout << "gdt: Creating Ring 3 Code descriptor... ";
    gdt[3].limit_low   = 0xFFFF;
    gdt[3].base_low    = 0;
    gdt[3].base_middle = 0;
    gdt[3].access      = 0xFA; // Present=1, DPL=0, Type=Data, Writable
    gdt[3].granularity = 0xAF; // Granularity=1(4KB), 32-bit, Limit High
    gdt[3].base_high   = 0;
    kout << "done" << endl;

    // ring 3 data
    kout << "gdt: Creating Ring 3 Data descriptor... ";
    gdt[4].limit_low   = 0xFFFF;
    gdt[4].base_low    = 0;
    gdt[4].base_middle = 0;
    gdt[4].access      = 0xF2; // Present=1, DPL=0, Type=Data, Writable
    gdt[4].granularity = 0xCF; // Granularity=1(4KB), 32-bit, Limit High
    gdt[4].base_high   = 0;
    kout << "done" << endl;

    // TSS descriptor
    tssInit((uint64_t)&_kernel_stack_top);

    // load GDT & TSS
    kout << "gdt: Loading GDT...";
    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint64_t)&gdt;
    loadGdt();
    kout << "done" << endl;
    kout << "gdt: Loading TSS...";
    loadTss();
    kout << "done" << endl;

    kout << "gdt: Sussessfully initlized GDT & TSS." << endl;

}