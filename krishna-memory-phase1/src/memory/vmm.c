#include "memory/vmm.h"

#include <memory.h>

#include "memory/pmm.h"

#define PAGE_ENTRY_COUNT UINT64_C(512)
#define PAGE_ADDRESS_MASK UINT64_C(0x000FFFFFFFFFF000)

#define PAGE_PRESENT UINT64_C(1)
#define PAGE_WRITABLE (UINT64_C(1) << 1)
#define PAGE_USER (UINT64_C(1) << 2)
#define PAGE_LARGE (UINT64_C(1) << 7)
#define PAGE_OWNED (UINT64_C(1) << 9)
#define PAGE_NO_EXECUTE (UINT64_C(1) << 63)

#define IA32_EFER UINT32_C(0xC0000080)
#define IA32_EFER_NXE (UINT64_C(1) << 11)
#define CR0_WRITE_PROTECT (UINT64_C(1) << 16)

static struct vmm_address_space kernel_space;
static uint64_t vmm_hhdm_offset;
static bool nx_available;
static bool vmm_ready;

struct walk_result {
    uint64_t *tables[4];
    uint64_t table_physical[4];
    uint16_t indexes[4];
};

static uint64_t read_cr0(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(value));
    return value;
}

static void write_cr0(uint64_t value)
{
    __asm__ volatile ("mov %0, %%cr0" :: "r"(value) : "memory");
}

static uint64_t read_cr3(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void write_cr3(uint64_t value)
{
    __asm__ volatile ("mov %0, %%cr3" :: "r"(value) : "memory");
}

static uint64_t read_msr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile (
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );

    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value)
{
    __asm__ volatile (
        "wrmsr"
        :: "c"(msr),
           "a"((uint32_t)value),
           "d"((uint32_t)(value >> 32))
        : "memory"
    );
}

static bool processor_supports_nx(void)
{
    uint32_t maximum_extended_leaf;
    uint32_t unused_b;
    uint32_t unused_c;
    uint32_t unused_d;

    __asm__ volatile (
        "cpuid"
        : "=a"(maximum_extended_leaf),
          "=b"(unused_b),
          "=c"(unused_c),
          "=d"(unused_d)
        : "a"(UINT32_C(0x80000000)), "c"(0)
    );

    if (maximum_extended_leaf < UINT32_C(0x80000001)) {
        return false;
    }

    uint32_t features_d;

    __asm__ volatile (
        "cpuid"
        : "=a"(unused_b),
          "=b"(unused_c),
          "=c"(unused_d),
          "=d"(features_d)
        : "a"(UINT32_C(0x80000001)), "c"(0)
    );

    return (features_d & (UINT32_C(1) << 20)) != 0;
}

static bool canonical_address(uint64_t address)
{
    uint64_t upper = address >> 48;
    uint64_t sign = (address >> 47) & 1;
    return sign == 0 ? upper == 0 : upper == UINT64_C(0xFFFF);
}

static uint64_t *table_virtual(uint64_t physical_address)
{
    if (physical_address > UINT64_MAX - vmm_hhdm_offset) {
        return NULL;
    }

    return (uint64_t *)(physical_address + vmm_hhdm_offset);
}

static void invalidate_page(uint64_t virtual_address)
{
    __asm__ volatile (
        "invlpg (%0)"
        :: "r"(virtual_address)
        : "memory"
    );
}

static bool table_empty(const uint64_t *table)
{
    for (uint64_t i = 0; i < PAGE_ENTRY_COUNT; i++) {
        if ((table[i] & PAGE_PRESENT) != 0) {
            return false;
        }
    }

    return true;
}

static void fill_indexes(uint64_t address, uint16_t indexes[4])
{
    indexes[0] = (uint16_t)((address >> 39) & UINT64_C(0x1FF));
    indexes[1] = (uint16_t)((address >> 30) & UINT64_C(0x1FF));
    indexes[2] = (uint16_t)((address >> 21) & UINT64_C(0x1FF));
    indexes[3] = (uint16_t)((address >> 12) & UINT64_C(0x1FF));
}

static void rollback_new_tables(
    uint64_t **parent_entries,
    uint64_t *new_table_physical,
    size_t created
)
{
    while (created > 0) {
        created--;
        *parent_entries[created] = 0;
        pmm_free_page(new_table_physical[created]);
    }
}

static bool walk_to_leaf(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    bool create,
    bool user_mapping,
    struct walk_result *result
)
{
    fill_indexes(virtual_address, result->indexes);
    result->table_physical[0] = space->pml4_physical;
    result->tables[0] = table_virtual(space->pml4_physical);

    if (result->tables[0] == NULL) {
        return false;
    }

    uint64_t *created_parent_entries[3];
    uint64_t created_table_physical[3];
    size_t created = 0;

    for (size_t level = 0; level < 3; level++) {
        uint64_t *entry =
            &result->tables[level][result->indexes[level]];

        if ((*entry & PAGE_PRESENT) == 0) {
            if (!create) {
                return false;
            }

            uint64_t new_table = pmm_allocate_page();

            if (new_table == PMM_INVALID_ADDRESS) {
                rollback_new_tables(
                    created_parent_entries,
                    created_table_physical,
                    created
                );
                return false;
            }

            uint64_t *new_table_virtual = table_virtual(new_table);

            if (new_table_virtual == NULL) {
                pmm_free_page(new_table);
                rollback_new_tables(
                    created_parent_entries,
                    created_table_physical,
                    created
                );
                return false;
            }

            memset(new_table_virtual, 0, VMM_PAGE_SIZE);

            uint64_t table_flags =
                PAGE_PRESENT | PAGE_WRITABLE | PAGE_OWNED;

            if (user_mapping) {
                table_flags |= PAGE_USER;
            }

            *entry = new_table | table_flags;
            created_parent_entries[created] = entry;
            created_table_physical[created] = new_table;
            created++;
        } else {
            if ((*entry & PAGE_LARGE) != 0) {
                rollback_new_tables(
                    created_parent_entries,
                    created_table_physical,
                    created
                );
                return false;
            }

            if (user_mapping) {
                *entry |= PAGE_USER | PAGE_WRITABLE;
            }
        }

        result->table_physical[level + 1] =
            *entry & PAGE_ADDRESS_MASK;
        result->tables[level + 1] =
            table_virtual(result->table_physical[level + 1]);

        if (result->tables[level + 1] == NULL) {
            rollback_new_tables(
                created_parent_entries,
                created_table_physical,
                created
            );
            return false;
        }
    }

    return true;
}

static uint64_t sanitized_leaf_flags(uint64_t flags)
{
    const uint64_t allowed =
        VMM_PAGE_WRITABLE |
        VMM_PAGE_USER |
        VMM_PAGE_WRITE_THROUGH |
        VMM_PAGE_CACHE_DISABLE |
        VMM_PAGE_GLOBAL |
        VMM_PAGE_NO_EXECUTE;

    return flags & allowed;
}

bool vmm_init(uint64_t hhdm_offset)
{
    if (vmm_ready) {
        return false;
    }

    vmm_hhdm_offset = hhdm_offset;
    kernel_space.pml4_physical = read_cr3() & PAGE_ADDRESS_MASK;

    if (kernel_space.pml4_physical == 0 ||
        table_virtual(kernel_space.pml4_physical) == NULL) {
        return false;
    }

    nx_available = processor_supports_nx();

    if (nx_available) {
        write_msr(IA32_EFER, read_msr(IA32_EFER) | IA32_EFER_NXE);
    }

    /* Make read-only mappings read-only even while executing in ring 0. */
    write_cr0(read_cr0() | CR0_WRITE_PROTECT);

    vmm_ready = true;
    return true;
}

struct vmm_address_space *vmm_kernel_address_space(void)
{
    return vmm_ready ? &kernel_space : NULL;
}

bool vmm_map_page(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
)
{
    if (!vmm_ready || space == NULL ||
        !canonical_address(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE - 1)) != 0 ||
        (physical_address & (VMM_PAGE_SIZE - 1)) != 0 ||
        (physical_address & ~PAGE_ADDRESS_MASK) != 0 ||
        ((flags & VMM_PAGE_NO_EXECUTE) != 0 && !nx_available)) {
        return false;
    }

    struct walk_result walk;

    if (!walk_to_leaf(
            space,
            virtual_address,
            true,
            (flags & VMM_PAGE_USER) != 0,
            &walk
        )) {
        return false;
    }

    uint64_t *leaf = &walk.tables[3][walk.indexes[3]];

    if ((*leaf & PAGE_PRESENT) != 0) {
        return false;
    }

    *leaf = physical_address |
        sanitized_leaf_flags(flags) |
        PAGE_OWNED |
        PAGE_PRESENT;

    invalidate_page(virtual_address);
    return true;
}

bool vmm_map_pages(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t physical_address,
    size_t page_count,
    uint64_t flags
)
{
    if (page_count == 0 ||
        page_count > UINT64_MAX / VMM_PAGE_SIZE) {
        return false;
    }

    uint64_t count = (uint64_t)page_count;

    if (virtual_address > UINT64_MAX - (count - 1) * VMM_PAGE_SIZE ||
        physical_address > UINT64_MAX - (count - 1) * VMM_PAGE_SIZE) {
        return false;
    }

    uint64_t mapped = 0;

    for (; mapped < count; mapped++) {
        if (!vmm_map_page(
                space,
                virtual_address + mapped * VMM_PAGE_SIZE,
                physical_address + mapped * VMM_PAGE_SIZE,
                flags
            )) {
            while (mapped > 0) {
                mapped--;
                vmm_unmap_page(
                    space,
                    virtual_address + mapped * VMM_PAGE_SIZE,
                    NULL
                );
            }

            return false;
        }
    }

    return true;
}

bool vmm_unmap_page(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t *old_physical_address
)
{
    if (!vmm_ready || space == NULL ||
        !canonical_address(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE - 1)) != 0) {
        return false;
    }

    struct walk_result walk;

    if (!walk_to_leaf(
            space,
            virtual_address,
            false,
            false,
            &walk
        )) {
        return false;
    }

    uint64_t *leaf = &walk.tables[3][walk.indexes[3]];

    if ((*leaf & PAGE_PRESENT) == 0) {
        return false;
    }

    if ((*leaf & PAGE_OWNED) == 0) {
        return false;
    }

    if (old_physical_address != NULL) {
        *old_physical_address = *leaf & PAGE_ADDRESS_MASK;
    }

    *leaf = 0;
    invalidate_page(virtual_address);

    /* Reclaim only intermediate tables explicitly allocated by this VMM. */
    for (size_t level = 3; level > 0; level--) {
        if (!table_empty(walk.tables[level])) {
            break;
        }

        uint64_t *parent_entry =
            &walk.tables[level - 1][walk.indexes[level - 1]];

        if ((*parent_entry & PAGE_OWNED) == 0) {
            break;
        }

        uint64_t table_physical_address =
            *parent_entry & PAGE_ADDRESS_MASK;

        *parent_entry = 0;
        pmm_free_page(table_physical_address);
    }

    return true;
}

bool vmm_translate(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t *physical_address
)
{
    if (!vmm_ready || space == NULL || physical_address == NULL ||
        !canonical_address(virtual_address)) {
        return false;
    }

    uint16_t indexes[4];
    fill_indexes(virtual_address, indexes);

    uint64_t table_physical_address = space->pml4_physical;

    for (size_t level = 0; level < 4; level++) {
        uint64_t *table = table_virtual(table_physical_address);

        if (table == NULL) {
            return false;
        }

        uint64_t entry = table[indexes[level]];

        if ((entry & PAGE_PRESENT) == 0) {
            return false;
        }

        if (level == 1 && (entry & PAGE_LARGE) != 0) {
            uint64_t base = entry & UINT64_C(0x000FFFFFC0000000);
            *physical_address = base |
                (virtual_address & UINT64_C(0x3FFFFFFF));
            return true;
        }

        if (level == 2 && (entry & PAGE_LARGE) != 0) {
            uint64_t base = entry & UINT64_C(0x000FFFFFFFE00000);
            *physical_address = base |
                (virtual_address & UINT64_C(0x1FFFFF));
            return true;
        }

        if (level == 3) {
            *physical_address = (entry & PAGE_ADDRESS_MASK) |
                (virtual_address & (VMM_PAGE_SIZE - 1));
            return true;
        }

        table_physical_address = entry & PAGE_ADDRESS_MASK;
    }

    return false;
}

bool vmm_protect_page(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t flags
)
{
    if (!vmm_ready || space == NULL ||
        !canonical_address(virtual_address) ||
        (virtual_address & (VMM_PAGE_SIZE - 1)) != 0 ||
        ((flags & VMM_PAGE_NO_EXECUTE) != 0 && !nx_available)) {
        return false;
    }

    struct walk_result walk;

    if (!walk_to_leaf(
            space,
            virtual_address,
            false,
            (flags & VMM_PAGE_USER) != 0,
            &walk
        )) {
        return false;
    }

    uint64_t *leaf = &walk.tables[3][walk.indexes[3]];

    if ((*leaf & PAGE_PRESENT) == 0) {
        return false;
    }

    if ((*leaf & PAGE_OWNED) == 0) {
        return false;
    }

    uint64_t physical_address = *leaf & PAGE_ADDRESS_MASK;
    *leaf = physical_address |
        sanitized_leaf_flags(flags) |
        PAGE_OWNED |
        PAGE_PRESENT;

    invalidate_page(virtual_address);
    return true;
}

void vmm_activate(struct vmm_address_space *space)
{
    if (!vmm_ready || space == NULL) {
        return;
    }

    write_cr3(space->pml4_physical);
}

bool vmm_nx_supported(void)
{
    return nx_available;
}
