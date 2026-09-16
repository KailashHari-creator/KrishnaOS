#ifndef KRISHNA_ARCH_X86_64_APIC_H
#define KRISHNA_ARCH_X86_64_APIC_H

#include <stdbool.h>
#include <stdint.h>

enum local_apic_mode {
    LOCAL_APIC_MODE_DISABLED = 0,
    LOCAL_APIC_MODE_XAPIC,
    LOCAL_APIC_MODE_X2APIC
};

struct local_apic_information {
    bool supported;
    bool enabled;
    bool bootstrap_processor;

    enum local_apic_mode mode;

    uint64_t physical_address;
};

/*
 * Read the CPU's Local APIC configuration.
 *
 * This function is completely read-only. It does not enable,
 * disable, map, or modify the APIC.
 */
bool local_apic_probe(
    struct local_apic_information *information
);

struct local_apic_identity {
    uint8_t apic_id;
    uint8_t version;

    /*
     * Number of Local Vector Table entries implemented.
     */
    uint8_t lvt_entry_count;

    bool software_enabled;
};

bool local_apic_map(
    const struct local_apic_information *information
);

bool local_apic_read_identity(
    struct local_apic_identity *identity
);

uint64_t local_apic_virtual_address(void);

/*
 * Calibrate and start the Local APIC timer in periodic mode.
 *
 * The TSC frequency is used only during calibration.
 * This function leaves maskable interrupts disabled.
 */
bool local_apic_timer_init(
    uint32_t requested_frequency,
    uint64_t tsc_frequency
);

uint64_t local_apic_timer_ticks(void);
uint32_t local_apic_timer_frequency(void);

#endif