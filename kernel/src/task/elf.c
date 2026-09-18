#include "task/elf.h"

#include <stddef.h>
#include <stdint.h>

#include "memory/heap.h"
#include "memory/layout.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "task/process.h"

#define ELF_IDENT_SIZE ((size_t)16)

#define ELF_CLASS_64 UINT8_C(2)
#define ELF_DATA_LITTLE_ENDIAN UINT8_C(1)
#define ELF_IDENT_VERSION UINT8_C(1)

#define ELF_TYPE_EXECUTABLE UINT16_C(2)
#define ELF_MACHINE_X86_64 UINT16_C(62)
#define ELF_VERSION_CURRENT UINT32_C(1)

#define ELF_PROGRAM_LOAD UINT32_C(1)

#define ELF_FLAG_EXECUTE UINT32_C(1)
#define ELF_FLAG_WRITE UINT32_C(2)

#define ELF64_MAX_PROGRAM_HEADERS UINT16_C(128)

/*
 * Initially limit one program image to 64 MiB.
 *
 * 16384 pages × 4096 bytes = 64 MiB.
 */
#define ELF64_MAX_IMAGE_PAGES ((size_t)16384)


struct elf64_header {
    uint8_t ident[ELF_IDENT_SIZE];

    uint16_t type;
    uint16_t machine;
    uint32_t version;

    uint64_t entry;

    uint64_t program_header_offset;
    uint64_t section_header_offset;

    uint32_t flags;

    uint16_t header_size;

    uint16_t program_header_size;
    uint16_t program_header_count;

    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} __attribute__((packed));


struct elf64_program_header {
    uint32_t type;
    uint32_t flags;

    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;

    uint64_t file_size;
    uint64_t memory_size;

    uint64_t alignment;
} __attribute__((packed));


struct elf64_loaded_page {
    uint64_t virtual_address;
    uint64_t physical_address;

    /*
     * Combined ELF permissions for every segment touching this page.
     */
    uint32_t elf_flags;

    bool mapped;
};


struct elf64_analysis {
    const struct elf64_header *header;
    const uint8_t *bytes;

    /*
     * Maximum number of page records required before overlapping pages
     * are deduplicated.
     */
    size_t page_capacity;
};


static bool add_u64(
    uint64_t left,
    uint64_t right,
    uint64_t *sum
)
{
    if (sum == NULL ||
        UINT64_MAX - left < right) {
        return false;
    }

    *sum = left + right;

    return true;
}


static bool power_of_two(uint64_t value)
{
    return value != 0 &&
        (value & (value - UINT64_C(1))) == 0;
}


static uint64_t page_floor(uint64_t address)
{
    return address &
        ~(VMM_PAGE_SIZE - UINT64_C(1));
}


static bool page_ceiling(
    uint64_t address,
    uint64_t *result
)
{
    uint64_t rounded;

    if (result == NULL ||
        !add_u64(
            address,
            VMM_PAGE_SIZE - UINT64_C(1),
            &rounded
        )) {
        return false;
    }

    *result = page_floor(rounded);

    return true;
}


static const struct elf64_program_header *program_header(
    const struct elf64_analysis *analysis,
    uint16_t index
)
{
    uint64_t offset =
        analysis->header->program_header_offset +
        (uint64_t)index *
            sizeof(struct elf64_program_header);

    return
        (const struct elf64_program_header *)
        (const void *)
        (analysis->bytes + (size_t)offset);
}


static bool analyze_elf(
    const void *file,
    size_t file_size,
    struct elf64_analysis *analysis
)
{
    if (file == NULL ||
        analysis == NULL ||
        file_size < sizeof(struct elf64_header)) {
        return false;
    }

    const uint8_t *bytes =
        (const uint8_t *)file;

    const struct elf64_header *header =
        (const struct elf64_header *)
        (const void *)bytes;

    /*
     * Validate the ELF identity and the executable architecture.
     */
    if (header->ident[0] != UINT8_C(0x7F) ||
        header->ident[1] != (uint8_t)'E' ||
        header->ident[2] != (uint8_t)'L' ||
        header->ident[3] != (uint8_t)'F' ||
        header->ident[4] != ELF_CLASS_64 ||
        header->ident[5] !=
            ELF_DATA_LITTLE_ENDIAN ||
        header->ident[6] != ELF_IDENT_VERSION ||
        header->type != ELF_TYPE_EXECUTABLE ||
        header->machine != ELF_MACHINE_X86_64 ||
        header->version != ELF_VERSION_CURRENT ||
        header->header_size !=
            sizeof(struct elf64_header) ||
        header->program_header_size !=
            sizeof(struct elf64_program_header) ||
        header->program_header_count == 0 ||
        header->program_header_count >
            ELF64_MAX_PROGRAM_HEADERS) {
        return false;
    }

    uint64_t program_table_size =
        (uint64_t)header->program_header_count *
        sizeof(struct elf64_program_header);

    uint64_t program_table_end;

    if (!add_u64(
            header->program_header_offset,
            program_table_size,
            &program_table_end
        ) ||
        header->program_header_offset >
            file_size ||
        program_table_end > file_size) {
        return false;
    }

    struct elf64_analysis candidate = {
        .header = header,
        .bytes = bytes,
        .page_capacity = 0
    };

    bool found_loadable_segment = false;
    bool entry_is_executable = false;

    for (uint16_t index = 0;
         index < header->program_header_count;
         index++) {
        const struct elf64_program_header *segment =
            program_header(
                &candidate,
                index
            );

        if (segment->type != ELF_PROGRAM_LOAD ||
            segment->memory_size == 0) {
            continue;
        }

        found_loadable_segment = true;

        uint64_t file_end;
        uint64_t memory_end;

        if (segment->file_size >
                segment->memory_size ||
            !add_u64(
                segment->offset,
                segment->file_size,
                &file_end
            ) ||
            file_end > file_size ||
            !add_u64(
                segment->virtual_address,
                segment->memory_size,
                &memory_end
            ) ||
            segment->virtual_address <
                USER_SPACE_BASE ||
            memory_end > USER_SPACE_TOP ||
            memory_end <=
                segment->virtual_address) {
            return false;
        }

        /*
         * Enforce W^X:
         *
         * No segment may be both writable and executable.
         */
        if ((segment->flags &
                (ELF_FLAG_WRITE |
                 ELF_FLAG_EXECUTE)) ==
            (ELF_FLAG_WRITE |
             ELF_FLAG_EXECUTE)) {
            return false;
        }

        /*
         * ELF requires p_vaddr and p_offset to have the same remainder
         * modulo p_align.
         */
        if (segment->alignment >
                UINT64_C(1) &&
            (!power_of_two(segment->alignment) ||
             (segment->virtual_address &
                (segment->alignment -
                 UINT64_C(1))) !=
             (segment->offset &
                (segment->alignment -
                 UINT64_C(1))))) {
            return false;
        }

        uint64_t page_end;

        if (!page_ceiling(
                memory_end,
                &page_end
            )) {
            return false;
        }

        uint64_t page_start =
            page_floor(
                segment->virtual_address
            );

        uint64_t segment_page_count =
            (page_end - page_start) /
            VMM_PAGE_SIZE;

        if (segment_page_count >
                ELF64_MAX_IMAGE_PAGES ||
            candidate.page_capacity >
                ELF64_MAX_IMAGE_PAGES -
                    (size_t)segment_page_count) {
            return false;
        }

        candidate.page_capacity +=
            (size_t)segment_page_count;

        /*
         * The entry point must refer to actual file-backed executable
         * code, not merely an executable BSS region.
         */
        uint64_t file_virtual_end =
            segment->virtual_address +
            segment->file_size;

        if ((segment->flags &
                ELF_FLAG_EXECUTE) != 0 &&
            header->entry >=
                segment->virtual_address &&
            header->entry <
                file_virtual_end) {
            entry_is_executable = true;
        }
    }

    if (!found_loadable_segment ||
        !entry_is_executable ||
        candidate.page_capacity == 0) {
        return false;
    }

    *analysis = candidate;

    return true;
}


bool elf64_validate(
    const void *file,
    size_t file_size
)
{
    struct elf64_analysis analysis;

    return analyze_elf(
        file,
        file_size,
        &analysis
    );
}


static struct elf64_loaded_page *find_page(
    struct elf64_loaded_page *pages,
    size_t page_count,
    uint64_t virtual_address
)
{
    for (size_t index = 0;
         index < page_count;
         index++) {
        if (pages[index].virtual_address ==
            virtual_address) {
            return &pages[index];
        }
    }

    return NULL;
}


static uint64_t vmm_flags_from_elf(
    uint32_t elf_flags
)
{
    uint64_t flags =
        VMM_PAGE_USER;

    if ((elf_flags & ELF_FLAG_WRITE) != 0) {
        flags |= VMM_PAGE_WRITABLE;
    }

    if ((elf_flags & ELF_FLAG_EXECUTE) == 0 &&
        vmm_nx_supported()) {
        flags |= VMM_PAGE_NO_EXECUTE;
    }

    return flags;
}


static bool release_pages(
    struct vmm_address_space *space,
    struct elf64_loaded_page *pages,
    size_t page_count
)
{
    bool released = true;

    /*
     * Release in reverse allocation order.
     */
    for (size_t index = page_count;
         index > 0;
         index--) {
        struct elf64_loaded_page *page =
            &pages[index - 1];

        if (page->physical_address ==
            PMM_INVALID_ADDRESS) {
            continue;
        }

        if (page->mapped) {
            uint64_t removed;

            if (!vmm_unmap_page(
                    space,
                    page->virtual_address,
                    &removed
                ) ||
                removed !=
                    page->physical_address) {
                released = false;
                continue;
            }

            page->mapped = false;
        }

        if (!pmm_free_page(
                page->physical_address
            )) {
            released = false;
            continue;
        }

        page->physical_address =
            PMM_INVALID_ADDRESS;
    }

    return released;
}


bool elf64_load(
    struct kernel_process *process,
    const void *file,
    size_t file_size,
    struct elf64_loaded_image *image
)
{
    if (process == NULL ||
        image == NULL ||
        image->entry != 0 ||
        image->pages != NULL ||
        image->page_count != 0) {
        return false;
    }

    struct elf64_analysis analysis;

    if (!analyze_elf(
            file,
            file_size,
            &analysis
        )) {
        return false;
    }

    struct vmm_address_space *space =
        kernel_process_address_space(
            process
        );

    if (space == NULL) {
        return false;
    }

    struct elf64_loaded_page *pages =
        (struct elf64_loaded_page *)kcalloc(
            analysis.page_capacity,
            sizeof(struct elf64_loaded_page)
        );

    if (pages == NULL) {
        return false;
    }

    for (size_t index = 0;
         index < analysis.page_capacity;
         index++) {
        pages[index].physical_address =
            PMM_INVALID_ADDRESS;
    }

    size_t page_count = 0;
    bool loaded = false;

    /*
     * First pass:
     *
     * Determine every virtual page and combine the permissions of any
     * segments sharing a page.
     */
    for (uint16_t index = 0;
         index <
            analysis.header->program_header_count;
         index++) {
        const struct elf64_program_header *segment =
            program_header(
                &analysis,
                index
            );

        if (segment->type != ELF_PROGRAM_LOAD ||
            segment->memory_size == 0) {
            continue;
        }

        uint64_t memory_end =
            segment->virtual_address +
            segment->memory_size;

        uint64_t page_end;

        if (!page_ceiling(
                memory_end,
                &page_end
            )) {
            goto cleanup;
        }

        for (uint64_t address =
                 page_floor(
                     segment->virtual_address
                 );
             address < page_end;
             address += VMM_PAGE_SIZE) {
            struct elf64_loaded_page *page =
                find_page(
                    pages,
                    page_count,
                    address
                );

            if (page == NULL) {
                page =
                    &pages[page_count++];

                page->virtual_address =
                    address;

                page->physical_address =
                    PMM_INVALID_ADDRESS;

                page->elf_flags =
                    segment->flags;

                page->mapped = false;
            } else {
                page->elf_flags |=
                    segment->flags;

                /*
                 * Two individually valid segments could overlap the same
                 * page and collectively create W+X. Reject that too.
                 */
                if ((page->elf_flags &
                        (ELF_FLAG_WRITE |
                         ELF_FLAG_EXECUTE)) ==
                    (ELF_FLAG_WRITE |
                     ELF_FLAG_EXECUTE)) {
                    goto cleanup;
                }
            }
        }
    }

    /*
     * Allocate, zero and map each page.
     */
    for (size_t index = 0;
         index < page_count;
         index++) {
        struct elf64_loaded_page *page =
            &pages[index];

        page->physical_address =
            pmm_allocate_page();

        if (page->physical_address ==
            PMM_INVALID_ADDRESS) {
            goto cleanup;
        }

        uint8_t *memory =
            (uint8_t *)
            pmm_physical_to_virtual(
                page->physical_address
            );

        if (memory == NULL) {
            goto cleanup;
        }

        /*
         * Zeroing the complete page initializes BSS and prevents data
         * from an old allocation leaking into the new process.
         */
        for (size_t byte = 0;
             byte < VMM_PAGE_SIZE;
             byte++) {
            memory[byte] = 0;
        }

        if (!vmm_map_page(
                space,
                page->virtual_address,
                page->physical_address,
                vmm_flags_from_elf(
                    page->elf_flags
                )
            )) {
            goto cleanup;
        }

        page->mapped = true;
    }

    /*
     * Copy each segment's file-backed bytes into the physical frames.
     *
     * Bytes between p_filesz and p_memsz remain zero because every frame
     * was zeroed above.
     */
    for (uint16_t index = 0;
         index <
            analysis.header->program_header_count;
         index++) {
        const struct elf64_program_header *segment =
            program_header(
                &analysis,
                index
            );

        if (segment->type != ELF_PROGRAM_LOAD ||
            segment->file_size == 0) {
            continue;
        }

        uint64_t copied = 0;

        while (copied <
               segment->file_size) {
            uint64_t destination_address =
                segment->virtual_address +
                copied;

            uint64_t virtual_page =
                page_floor(
                    destination_address
                );

            struct elf64_loaded_page *page =
                find_page(
                    pages,
                    page_count,
                    virtual_page
                );

            if (page == NULL) {
                goto cleanup;
            }

            uint8_t *destination =
                (uint8_t *)
                pmm_physical_to_virtual(
                    page->physical_address
                );

            if (destination == NULL) {
                goto cleanup;
            }

            size_t page_offset =
                (size_t)(
                    destination_address -
                    virtual_page
                );

            uint64_t remaining =
                segment->file_size -
                copied;

            size_t chunk =
                VMM_PAGE_SIZE -
                page_offset;

            if ((uint64_t)chunk >
                remaining) {
                chunk =
                    (size_t)remaining;
            }

            const uint8_t *source =
                analysis.bytes +
                (size_t)(
                    segment->offset +
                    copied
                );

            for (size_t byte = 0;
                 byte < chunk;
                 byte++) {
                destination[
                    page_offset + byte
                ] = source[byte];
            }

            copied += chunk;
        }
    }

    image->entry =
        analysis.header->entry;

    image->pages =
        pages;

    image->page_count =
        page_count;

    loaded = true;

cleanup:
    if (!loaded) {
        (void)release_pages(
            space,
            pages,
            page_count
        );

        (void)kfree(pages);
    }

    return loaded;
}


bool elf64_unload(
    struct kernel_process *process,
    struct elf64_loaded_image *image
)
{
    if (process == NULL ||
        image == NULL ||
        image->pages == NULL ||
        image->page_count == 0) {
        return false;
    }

    struct vmm_address_space *space =
        kernel_process_address_space(
            process
        );

    if (space == NULL) {
        return false;
    }

    struct elf64_loaded_page *pages =
        (struct elf64_loaded_page *)
            image->pages;

    if (!release_pages(
            space,
            pages,
            image->page_count
        )) {
        return false;
    }

    if (!kfree(pages)) {
        return false;
    }

    *image =
        (struct elf64_loaded_image){0};

    return true;
}