#ifndef KRISHNA_MEMORY_VMM_H
#define KRISHNA_MEMORY_VMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VMM_PAGE_SIZE UINT64_C(4096)

enum vmm_page_flags {
    VMM_PAGE_WRITABLE       = UINT64_C(1) << 1,
    VMM_PAGE_USER           = UINT64_C(1) << 2,
    VMM_PAGE_WRITE_THROUGH  = UINT64_C(1) << 3,
    VMM_PAGE_CACHE_DISABLE  = UINT64_C(1) << 4,
    VMM_PAGE_GLOBAL         = UINT64_C(1) << 8,
    VMM_PAGE_NO_EXECUTE     = UINT64_C(1) << 63
};

struct vmm_address_space {
    uint64_t pml4_physical;
};

/* Adopt Limine's currently active page-table hierarchy. */
bool vmm_init(uint64_t hhdm_offset);

/*
 * Deep-clone the bootstrap page tables supplied by Limine and
 * activate a page-table hierarchy owned by KRISHNA OS.
 *
 * vmm_init() must be called first.
 */
bool vmm_take_ownership(void);

struct vmm_address_space *vmm_kernel_address_space(void);

bool vmm_map_page(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);

bool vmm_map_pages(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t physical_address,
    size_t page_count,
    uint64_t flags
);

bool vmm_unmap_page(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t *old_physical_address
);

bool vmm_translate(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t *physical_address
);

bool vmm_protect_page(
    struct vmm_address_space *space,
    uint64_t virtual_address,
    uint64_t flags
);

/*
 * Create an empty userspace address space.
 *
 * The lower canonical half starts empty and belongs to the process.
 * The upper canonical half shares the kernel mappings but remains
 * inaccessible from ring 3.
 */
bool vmm_address_space_create(
    struct vmm_address_space *space
);

/*
 * Destroy an inactive, empty userspace address space.
 *
 * All userspace mappings must be removed before calling this
 * function. The shared kernel mappings are never released.
 */
bool vmm_address_space_destroy(
    struct vmm_address_space *space
);

bool vmm_address_space_is_active(
    const struct vmm_address_space *space
);

/*
 * Translate a userspace address while verifying that every relevant
 * page-table level permits Ring-3 access.
 *
 * When write_access is true, the effective mapping must also be writable.
 */
bool vmm_translate_user(
    const struct vmm_address_space *space,
    uint64_t virtual_address,
    bool write_access,
    uint64_t *physical_address
);

void vmm_activate(struct vmm_address_space *space);
bool vmm_nx_supported(void);

#endif
