#ifndef KRISHNA_TASK_ELF_H
#define KRISHNA_TASK_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct kernel_process;

/*
 * Resources belonging to one loaded ELF image.
 *
 * The pages field is intentionally opaque outside elf.c.
 */
struct elf64_loaded_image {
    uint64_t entry;

    void *pages;
    size_t page_count;
};

/*
 * Validate a static, little-endian x86-64 executable.
 *
 * This currently accepts ET_EXEC images only—not PIE, shared libraries,
 * dynamic linking or interpreters.
 */
bool elf64_validate(
    const void *file,
    size_t file_size
);

/*
 * Load all PT_LOAD segments into the supplied process.
 *
 * The loader:
 *
 * - allocates fresh physical pages;
 * - zeroes every page;
 * - copies file-backed bytes;
 * - leaves the remaining BSS bytes zero;
 * - applies user/read-write/execute permissions;
 * - rejects writable-and-executable pages.
 */
bool elf64_load(
    struct kernel_process *process,
    const void *file,
    size_t file_size,
    struct elf64_loaded_image *image
);

/*
 * Remove and free every page installed by elf64_load().
 *
 * The process must not have a thread executing the image.
 */
bool elf64_unload(
    struct kernel_process *process,
    struct elf64_loaded_image *image
);

#endif