#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>
#include <limine.h>

struct boot_memory_summary {
    uint64_t region_count;
    uint64_t usable_region_count;
    uint64_t usable_bytes;
    uint64_t bootloader_reclaimable_bytes;
};

void boot_analyse_memory(
    struct limine_memmap_response *memory_map,
    struct boot_memory_summary *summary
);

#endif