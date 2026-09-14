#include "boot/boot_info.h"

void boot_analyse_memory(
    struct limine_memmap_response *memory_map,
    struct boot_memory_summary *summary
)
{
    summary->region_count = memory_map->entry_count;
    summary->usable_region_count = 0;
    summary->usable_bytes = 0;
    summary->bootloader_reclaimable_bytes = 0;

    for (uint64_t i = 0; i < memory_map->entry_count; i++) {
        struct limine_memmap_entry *entry =
            memory_map->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            summary->usable_region_count++;
            summary->usable_bytes += entry->length;
        }

        if (entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            summary->bootloader_reclaimable_bytes += entry->length;
        }
    }
}