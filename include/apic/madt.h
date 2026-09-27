#pragma once
#include <stdint.h>
#include <stddef.h>

namespace madt {

struct SdtHeader {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
}__attribute__((packed));

struct MadtHeader {
    SdtHeader sdt;
    uint32_t  local_apic_address;
    uint32_t  flags;
} __attribute__((packed));

struct MadtEntry {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct MadtLocalApic {
    MadtEntry header;
    uint8_t   acpi_proc_id;
    uint8_t   apic_id;
    uint32_t  flags;
} __attribute__((packed));

struct MadtIoApic {
    MadtEntry header;
    uint8_t   ioapic_id;
    uint8_t   reserved;
    uint32_t  ioapic_address;
    uint32_t  gsi_base;
} __attribute__((packed));

struct MadtIso {
    MadtEntry header;
    uint8_t   bus_source;
    uint8_t   irq_source;
    uint32_t  gsi;
    uint16_t  flags;
} __attribute__((packed));

struct RsdpDescriptor {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;

    /* --- ACPI 2.0+ fields --- */
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

struct ApicInfo {
    uint64_t lapic_phys_base;
    uint8_t  bsp_apic_id;

    struct IoApicDesc {
        uint32_t phys_base;
        uint32_t gsi_base;
        uint8_t  id;
    };
    static constexpr size_t MAX_IOAPICS = 8;
    IoApicDesc ioapics[MAX_IOAPICS];
    size_t     ioapic_count = 0;

    static constexpr size_t MAX_ISOS = 32;
    struct IsoEntry {
        uint8_t  isa_irq;
        uint32_t gsi;
        uint16_t flags;
    };
    IsoEntry isos[MAX_ISOS];
    size_t   iso_count = 0;
};

void parse();
const ApicInfo& get_apic_info();

} // namespace madt