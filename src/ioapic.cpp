#include "apic/ioapic.h"
#include "apic/madt.h"
#include "kprint.h"
#include "mem/paging.h"
#include "string.h"

namespace ioapic {

struct IoApicInstance {
    volatile uint32_t* virt_base;
    uint32_t           phys_base;
    uint32_t           gsi_base;
    uint32_t           max_rte;
};

static constexpr size_t MAX_IOAPICS = 8;
static IoApicInstance g_ioapics[MAX_IOAPICS];
static size_t g_ioapic_count = 0;

constexpr uint32_t RDT_LOW_BASE  = 0x10;
constexpr uint32_t MASK_BIT      = (1U << 16);
constexpr uint32_t TRIGGER_LEVEL = (1U << 15);
constexpr uint32_t POLARITY_LOW  = (1U << 13);

static uint32_t read_reg(const IoApicInstance* inst, uint8_t reg) {
    inst->virt_base[0] = reg;
    return inst->virt_base[4];
}

static void write_reg(const IoApicInstance* inst, uint8_t reg, uint32_t val) {
    inst->virt_base[0] = reg;
    inst->virt_base[4] = val;
}

static uint32_t isa_to_gsi(uint8_t isa_irq) {
    const auto& info = madt::get_apic_info();
    for (size_t i = 0; i < info.iso_count; i++) {
        if (info.isos[i].isa_irq == isa_irq)
            return info.isos[i].gsi;
    }
    return isa_irq;
}

static IoApicInstance* find_ioapic_for_gsi(uint32_t gsi) {
    for (size_t i = 0; i < g_ioapic_count; i++) {
        if (gsi >= g_ioapics[i].gsi_base && 
            gsi < g_ioapics[i].gsi_base + g_ioapics[i].max_rte)
            return &g_ioapics[i];
    }
    return nullptr;
}


void init(uint64_t phys_base, uint32_t gsi_base) {
    if (g_ioapic_count >= MAX_IOAPICS) {
        kernel_panic("ioapic: Too many IOAPICs!");
        return;
    }

    uint64_t virt_addr = paging::IOAPIC_VIRT_BASE + (g_ioapic_count * 0x1000);
    paging::map_ioapic(phys_base, virt_addr);

    auto& inst = g_ioapics[g_ioapic_count];
    inst.phys_base = static_cast<uint32_t>(phys_base);
    inst.virt_base = reinterpret_cast<volatile uint32_t*>(virt_addr);
    inst.gsi_base  = gsi_base;

    uint32_t ver = read_reg(&inst, 0x01);
    inst.max_rte = ((ver >> 16) & 0xFF) + 1;
    uint8_t apic_id = (read_reg(&inst, 0x00) >> 24) & 0x0F;

    for (uint32_t i = 0; i < inst.max_rte; i++) {
        write_reg(&inst, RDT_LOW_BASE + 2 * i, MASK_BIT);
        write_reg(&inst, RDT_LOW_BASE + 2 * i + 1, 0);
    }

    kout << "ioapic: IOAPIC #" << g_ioapic_count << " Initialized:" << endl;
    kout << "ioapic:   Phys=" << hex << phys_base << dec
         << ", Virt=" << hex << virt_addr << dec << endl;
    kout << "ioapic:   ID=" << apic_id
         << ", Ver=" << (ver & 0xFF)
         << ", MaxRTE=" << inst.max_rte
         << ", GSI base=" << inst.gsi_base << endl;

    g_ioapic_count++;
}

void route_irq(uint8_t isa_irq, uint8_t dest_apic_id, uint8_t vector) {
    if (g_ioapic_count == 0) {
        kernel_panic("ioapic: Not initialized");
        return;
    }

    uint32_t gsi = isa_to_gsi(isa_irq);
    auto* inst = find_ioapic_for_gsi(gsi);
    if (!inst) {
        kout << "ioapic: ERR: No IOAPIC handles GSI " << gsi
             << " (ISA IRQ " << isa_irq << ")" << endl;
        return;
    }

    uint32_t rte_idx = gsi - inst->gsi_base;

    uint32_t low = read_reg(inst, RDT_LOW_BASE + 2 * rte_idx);
    write_reg(inst, RDT_LOW_BASE + 2 * rte_idx, low | MASK_BIT);

    write_reg(inst, RDT_LOW_BASE + 2 * rte_idx + 1,
              static_cast<uint32_t>(dest_apic_id) << 24);

    low = static_cast<uint32_t>(vector);
    write_reg(inst, RDT_LOW_BASE + 2 * rte_idx, low);

    kout << "ioapic: Routed IRQ" << isa_irq
         << " -> GSI" << gsi
         << " (RTE " << rte_idx << ")"
         << " => Vec " << vector
         << ", CPU " << dest_apic_id << endl;
}

void mask_irq(uint8_t isa_irq, bool masked) {
    if (g_ioapic_count == 0) return;

    uint32_t gsi = isa_to_gsi(isa_irq);
    auto* inst = find_ioapic_for_gsi(gsi);
    if (!inst) return;

    uint32_t rte_idx = gsi - inst->gsi_base;
    uint32_t low = read_reg(inst, RDT_LOW_BASE + 2 * rte_idx);
    if (masked) low |= MASK_BIT;
    else        low &= ~MASK_BIT;
    write_reg(inst, RDT_LOW_BASE + 2 * rte_idx, low);
}

} // namespace ioapic