#include "gdt.h"
#include "ostreamk.h"
#include <memory.h>


static GDTEntry gdt[3];
static GDTPtr   gp;
// static TSSEntry tss alignas(4096);

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
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
    );
}

void gdtInit() {
    ostreamk kout;

    kout << "gdt: Initlizing..." << endl;

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
    // kout << "gdt: Creating TSS Descriptor:" << endl;

    // load GDT
    kout << "gdt: Loading GDT..." << endl;
    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint64_t)&gdt;
    loadGdt();

}