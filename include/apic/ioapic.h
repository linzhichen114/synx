#pragma once
#include <stdint.h>

namespace ioapic {

void init(uint64_t phys_base, uint32_t gsi_base);

void route_irq(uint8_t irq, uint8_t dest_apic_id, uint8_t vector);

void mask_irq(uint8_t irq, bool masked);

} // namespace ioapic