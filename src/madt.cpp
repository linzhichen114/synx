#include "apic/madt.h"
#include "kprint.h"
#include <stddef.h>
#include <limine.h>

extern volatile struct limine_rsdp_request rsdp_request;
extern volatile struct limine_hhdm_request hhdm_request;

namespace madt {

static const MadtHeader* find_madt() {
    auto* resp = rsdp_request.response;
    if (!resp || !resp->address) return nullptr;

    auto* rsdp = (const RsdpDescriptor*)resp->address;

    uint64_t sdt_phys = 0;
    bool is_xsdt = false;

    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        sdt_phys = rsdp->xsdt_address;
        is_xsdt = true;
        kout << "acpi_madt: Using XSDT at phys " << hex << sdt_phys << dec << endl;
    } else if (rsdp->rsdt_address != 0) {
        sdt_phys = rsdp->rsdt_address;
        kout << "acpi_madt: Using RSDT at phys " << hex << sdt_phys << dec << endl;
    } else {
        kernel_panic("acpi_madt: No XSDT or RSDT found in RSDP.");
        return nullptr;
    }

    auto* sdt_header = (const SdtHeader*)(sdt_phys + hhdm_request.response->offset);

    if (sdt_header->length < sizeof(SdtHeader)) {
        kernel_panic("acpi_madt: SDT length invalid.");
        return nullptr;
    }

    uint32_t entry_size = is_xsdt ? sizeof(uint64_t) : sizeof(uint32_t);
    uint32_t entry_count = (sdt_header->length - sizeof(SdtHeader)) / entry_size;

    for (uint32_t i = 0; i < entry_count; i++) {
        uint64_t table_phys;
        if (is_xsdt) {
            auto* entries = (const uint64_t*)((uint8_t*)sdt_header + sizeof(SdtHeader));
            table_phys = entries[i];
        } else {
            auto* entries = (const uint32_t*)((uint8_t*)sdt_header + sizeof(SdtHeader));
            table_phys = entries[i];
        }

        auto* tbl = (const SdtHeader*)(table_phys + hhdm_request.response->offset);

        if (tbl->signature[0] == 'A' && tbl->signature[1] == 'P' &&
            tbl->signature[2] == 'I' && tbl->signature[3] == 'C') {
            kout << "acpi_madt: Found MADT at phys " << hex << table_phys << dec << endl;
            return (const MadtHeader*)tbl;
        }
    }

    kernel_panic("acpi_madt: MADT not found.");
    return nullptr;
}

static ApicInfo g_apic_info = {};

void parse() {
    auto* madt = find_madt();

    g_apic_info.lapic_phys_base = madt->local_apic_address;
    kout << "acpi_madt: MADT found, LAPIC base=0x" << hex 
         << g_apic_info.lapic_phys_base << dec << endl;

    uint8_t* ptr = (uint8_t*)madt + sizeof(MadtHeader);
    uint8_t* end = (uint8_t*)madt + madt->sdt.length;

    while (ptr < end) {
        auto* entry = (MadtEntry*)ptr;

        switch (entry->type) {
            case 0: {
                auto* la = (MadtLocalApic*)entry;
                if (la->flags & 1) {
                    kout << "acpi_madt:   CPU APIC ID=" << la->apic_id << endl;
                }
                break;
            }
            case 1: {
                auto* io = (MadtIoApic*)entry;
                if (g_apic_info.ioapic_count < ApicInfo::MAX_IOAPICS) {
                    auto& desc = g_apic_info.ioapics[g_apic_info.ioapic_count++];
                    desc.phys_base = io->ioapic_address;
                    desc.gsi_base  = io->gsi_base;
                    desc.id        = io->ioapic_id;
                    kout << "acpi_madt:   IOAPIC ID=" << io->ioapic_id
                         << ", Base=" << hex << io->ioapic_address << dec
                         << ", GSI base=" << io->gsi_base << endl;
                }
                break;
            }
            case 2: {
                auto* iso = (MadtIso*)entry;
                if (g_apic_info.iso_count < ApicInfo::MAX_ISOS) {
                    auto& e = g_apic_info.isos[g_apic_info.iso_count++];
                    e.isa_irq = iso->irq_source;
                    e.gsi     = iso->gsi;
                    e.flags   = iso->flags;
                    kout << "acpi_madt:   ISO: IRQ" << iso->irq_source 
                         << " -> GSI" << iso->gsi << endl;
                }
                break;
            }
            // TODO: Handle Type 3 NMI, 4 LINT and 5 LAPIC Address Override
        }

        ptr += entry->length;
    }

}

const ApicInfo& get_apic_info() { return g_apic_info; }

} // namespace madt