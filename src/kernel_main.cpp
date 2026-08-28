#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
extern "C" {
#include <limine.h>
}
#include "kprint.h"
#include "sysdef.h"
#include "gdt.h"
#include "idt.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include "mem/heap.h"
#include "apic/apic.h"
#include "proc/ap_entry.h"
#include "proc/sched.h"


/* Constants Definetion */
extern uint64_t _kernel_stack_top;     // BSP Stack Top
extern "C" void ap_entry(struct limine_mp_info*);
#define AP_STACK_SIZE (16 * 1024)      // AP  Stack Size (16KB)
uint64_t AP_TIMEOUT = 100000000ULL;
bool scheduler_ready = false;
extern uint64_t PER_CPU_DATA_SIZE;
extern uint64_t PER_CPU_GP_OFFSET;

/* Limine Requests */
// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .flags = 0 // 0 = xAPIC only; 1 = try x2APIC first
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .cpp file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

/* Tool Functions */
/* Constructors Calling */
typedef void (*init_func_t)();

extern "C" init_func_t __init_array_start[];
extern "C" init_func_t __init_array_end[];

static inline void __call_global_constructors() {
    for (init_func_t* func = __init_array_start; func != __init_array_end; ++func)
        if (*func)
            (*func)();
}

/* Check Page Tables */
// void validate_page_tables() {
//     for (each mapped page) {
//         uint64_t pte = read_pte(addr);
//         if (pte & 0x0000000000000E00ULL) {  // bits 9-11
//             kout << "BAD PTE at " << hex << addr << ": " << pte << endl;
//         }
//         if (pte & 0x7FF0000000000000ULL) {  // bits 52-62  
//             kout << "BAD PTE HIGH BITS at " << hex << addr << ": " << pte << endl;
//         }
//     }
// }

extern "C" void kernel_main(void) {

    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) 
        hcf();

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1)
        hcf();

    __call_global_constructors();

    kout << KERNEL_NAME << " version " << KERNEL_VERSION << " (" << COMPILER_NAME << " " << COMPILER_VERSION << ") SMP " << BUILD_DATE << " " << BUILD_TIME << endl;

    kout << "Successfully called all constructors." << endl;

    const auto fb0 = framebuffer_request.response->framebuffers[0];
    kout << "fb0: Base " << (uint64_t*)fb0->address << ", Size " << (fb0->width * fb0->height * fb0->bpp) / (uint64_t)(8 * 1024) << endl;
    kout << "fb0: Mode " << fb0->width << "x" << fb0->height << " @ " << fb0->bpp << "bpp" << endl;
    kout << "fb0: Color mode: ARGB." << endl;
    kout << "fbcon: fb0 is primary device." << endl;
    kout << "fbcon: Screen grid: " << FONT_WIDTH << "x" << FONT_HEIGHT << " characters." << endl;

    // kout << "Command Line: " << executable_cmdline_request.response->cmdline << endl;

    gdt::init_bsp((uint64_t)&_kernel_stack_top);
    idt::init_bsp();

    pmmInit();
    paging::init();
    heapInit();
    
    asm volatile("cli");
    apic::init();
    kout << "apic: Initialized, Base: " << apic::get_base_info().mmio_base << endl;

    apic::timer_init(32, true, 0);
    kout << "apic: Preemptive scheduling enabled." << endl;

    // apic::timer_stop();

    kout << "smp: BSP Initialized Successfully, Starting SMP..." << endl;

    if (mp_request.response == nullptr)
        kernel_panic("smp: limine_mp_request.response is null!");


    auto* resp = mp_request.response;
    uint64_t cpu_count = resp->cpu_count;
    uint64_t expected_aps = 0;

    kout << "smp: Limine MP Response valid at " << (uint64_t*)resp << endl;
    kout << "smp: Detected " << cpu_count << " CPUs, BSP LAPIC ID: " 
        << resp->bsp_lapic_id << endl;
    kout << "smp: Flags: " << resp->flags
        << ((resp->flags & LIMINE_MP_RESPONSE_X86_64_X2APIC) ? " (x2APIC)" : " (xAPIC)")
        << endl;

    for (uint64_t i = 0; i < cpu_count; i++) {
        auto* cpu = resp->cpus[i];
        kout << "smp:   CPU[" << i << "] proc_id=" << cpu->processor_id 
            << " lapic_id=" << cpu->lapic_id << endl;
    }

    if (cpu_count <= 1) {
        kout << "smp: WARNING: Single core system, skipping SMP." << endl;
        goto smp_skipping;
    }

    for (uint64_t i = 0; i < cpu_count; i++) {
        struct limine_mp_info* cpu = resp->cpus[i];
        
        if (cpu->lapic_id == resp->bsp_lapic_id)
            continue;
        
        void* raw_ptr = kmalloc(AP_STACK_SIZE);
        if (!raw_ptr) 
            kernel_panic("smp: Failed to allocate AP stack");
        memset(raw_ptr, 0, AP_STACK_SIZE);
        uint64_t virt_base = (uint64_t)raw_ptr;

        uint64_t ap_stack_top = (virt_base + AP_STACK_SIZE) & ~0xFULL;

        if ((ap_stack_top >> 48) != 0xFFFF || (ap_stack_top & 0xF) != 0)
            kernel_panic("smp: Invalid AP stack top");

        kout << "smp: Starting AP #" << cpu->lapic_id << ", stack=" << (uint64_t*)ap_stack_top  << " ..." << endl;

        gdt::setup_descriptors(cpu->processor_id, ap_stack_top);

        cpu->extra_argument = (uint64_t)ap_stack_top;

        cpu->goto_address = (void (*)(limine_mp_info*))&ap_entry;
        expected_aps++;
    }

    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    kout << "smp: Waiting for " << expected_aps << " APs to come online ..." << endl;
    while (__atomic_load_n(&ap_online_count, __ATOMIC_ACQUIRE) < expected_aps) {
        asm volatile("pause");
        if (--AP_TIMEOUT == 0) {
            kout << "smp: WARNING: Timeout! Now " 
                << __atomic_load_n(&ap_online_count, __ATOMIC_ACQUIRE) 
                << "/" << expected_aps << " APs online." << endl;
            break;
        }
    }


    kout << "smp: All " << expected_aps << " APs online, initializing scheduler." << endl;

smp_skipping:

    scheduler::init();

    __atomic_store_n(&scheduler_ready, true, __ATOMIC_RELEASE);

    asm volatile("sti");

    asm volatile ("sti; hlt; jmp .-2" ::: "memory");
    //kernel_panic("kernel_main: others function is not implemented yet - system halting.");
}