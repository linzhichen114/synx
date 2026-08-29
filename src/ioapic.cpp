#include "apic/ioapic.h"
#include "kprint.h"
#include "mem/paging.h"
#include "string.h"

namespace ioapic {

static volatile uint32_t* g_ioapic_base = nullptr;

static uint32_t read_reg(uint8_t reg) {
    g_ioapic_base[0] = reg;
    return g_ioapic_base[4];
}

static void write_reg(uint8_t reg, uint32_t val) {
    g_ioapic_base[0] = reg;
    g_ioapic_base[4] = val;
}

constexpr uint32_t RDT_LOW_BASE  = 0x10;
constexpr uint32_t MASK_BIT      = (1U << 16);
constexpr uint32_t TRIGGER_LEVEL = (1U << 15);
constexpr uint32_t POLARITY_LOW  = (1U << 13);


void init(uint64_t phys_base) {
    paging::map_ioapic(phys_base);

    g_ioapic_base = reinterpret_cast<volatile uint32_t*>(paging::IOAPIC_VIRT_BASE);

    uint32_t ver = read_reg(0x01);
    uint8_t max_entries = ((ver >> 16) & 0xFF) + 1;
    uint8_t apic_id     = (read_reg(0x00) >> 24) & 0x0F;

    kout << "ioapic: Initalized:" << endl;
    kout << "ioapic:   Virt base=" << paging::IOAPIC_VIRT_BASE << ", Phys base=" << phys_base << endl;
    kout << "ioapic:   ID=" << apic_id << ", Version=" << (ver & 0xFF) << ", Max Entries=" << max_entries << endl;

    for (uint8_t i = 0; i < max_entries; i++) {
        write_reg(RDT_LOW_BASE + 2 * i, MASK_BIT | (32 + i));
        write_reg(RDT_LOW_BASE + 2 * i + 1, 0);
    }

    kout << "ioapic: Verifying IRQ1: Vector `";
    write_reg(0x10 + 2 * 1, 0x00000021);
    write_reg(0x10 + 2 * 1 + 1, 0);
    uint32_t verify_low = read_reg(0x10 + 2 * 1);
    kout << verify_low << "'";
    if (verify_low == 33) 
        kout << " - passed." << endl;
    else {
        kout << " - ERR" << endl;
        
        char buf[256]; buf[0] = '\0';
        strcat(buf, "ioapic: Assertion Failed: (IRQ1) vector `");
        char vec_buf[5]; vec_buf[0] = '\0';
        itoa(vec_buf, verify_low, 10);
        strcat(buf, vec_buf);
        strcat(buf, "', needed `33'.");
        kernel_panic(buf);
    }
}

void route_irq(uint8_t irq, uint8_t dest_apic_id, uint8_t vector) {
    if (!g_ioapic_base) {
        kernel_panic("ioapic: Not Initialized.\n");
        return;
    }

    uint32_t low = static_cast<uint32_t>(vector);
    uint32_t high = static_cast<uint32_t>(dest_apic_id) << 24;

    write_reg(RDT_LOW_BASE + 2 * irq, low);
    write_reg(RDT_LOW_BASE + 2 * irq + 1, high);

    kout << "ioapic: Routed IRQ " << irq 
         << " => Vector " << vector 
         << ", CPU " << dest_apic_id << endl;
}

void mask_irq(uint8_t irq, bool masked) {
    if (!g_ioapic_base) return;

    uint32_t low = read_reg(RDT_LOW_BASE + 2 * irq);
    if (masked)
        low |= MASK_BIT;
    else
        low &= ~MASK_BIT;
    write_reg(RDT_LOW_BASE + 2 * irq, low);
}

} // namespace ioapic