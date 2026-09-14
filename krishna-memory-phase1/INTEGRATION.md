# KRISHNA OS memory phase 1 integration

This checkpoint adds both a physical-memory manager (PMM) and real x86-64
four-level virtual-memory operations (VMM). It adopts Limine's active CR3
instead of prematurely replacing Limine's complete bootstrap address space.

Copy the supplied `src/memory` directory into `kernel/src/memory`. The existing
GNUmakefile already discovers C files recursively, so no source list changes
are required.

The archive also contains `modified/main.c` and `modified/shell.c`, produced
from the exact files supplied for this checkpoint. They already contain the
changes described below. Back up your current files, compare them, and then
replace the corresponding files under `kernel/src` if they have not changed.

## 1. Add headers to `main.c`

```c
#include "memory/pmm.h"
#include "memory/vmm.h"
```

## 2. Request Limine's higher-half direct map

Place this after `memory_map_request` and before the Limine request markers:

```c
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};
```

The HHDM allows the kernel to access a physical frame at virtual address
`physical_address + hhdm_offset`. Physical addresses must never be cast
directly to pointers.

## 3. Initialise PMM and VMM

In `kmain`, place this after `memory.usable_bytes` has been validated and before
the splash reaches 100 percent:

```c
if (hhdm_request.response == NULL) {
    serial_write("[FAIL] No higher-half direct map received\n");
    kernel_halt();
}

uint64_t hhdm_offset = hhdm_request.response->offset;

if (!pmm_init(memory_map_request.response, hhdm_offset)) {
    serial_write("[FAIL] Physical-memory manager initialization failed\n");
    kernel_halt();
}

serial_write("[OK] Physical-memory manager initialized\n");

if (!vmm_init(hhdm_offset)) {
    serial_write("[FAIL] Virtual-memory manager initialization failed\n");
    kernel_halt();
}

serial_write("[OK] Four-level x86-64 paging initialized\n");

struct pmm_statistics pmm_stats;
pmm_get_statistics(&pmm_stats);

serial_write("Managed physical pages: ");
serial_write_u64(pmm_stats.managed_pages);
serial_write("\nFree physical pages: ");
serial_write_u64(pmm_stats.free_pages);
serial_write("\nPaging metadata pages: ");
serial_write_u64(pmm_stats.bitmap_pages);
serial_write("\nNX protection: ");
serial_write(vmm_nx_supported() ? "supported\n" : "unavailable\n");
```

The order is important: memory map -> HHDM -> PMM -> VMM. The VMM needs PMM
frames whenever a new page-table level must be created.

## 4. Add the diagnostic shell command

At the top of `shell.c`:

```c
#include "memory/memory_test.h"
#include "memory/pmm.h"
```

Add this line to the `help` output:

```c
"  memtest          Test physical and virtual memory\n"
```

Add this command block in `shell_execute`, before the unknown-command output:

```c
if (strings_equal(command, "memtest")) {
    struct pmm_statistics statistics;
    struct memory_test_result test;

    pmm_get_statistics(&statistics);

    console_write(shell->console, "KRISHNA memory diagnostics\n");
    console_write(shell->console, "Managed pages: ");
    console_write_u64(shell->console, statistics.managed_pages);
    console_write(shell->console, "\nFree pages: ");
    console_write_u64(shell->console, statistics.free_pages);
    console_write(shell->console, "\n");

    bool passed = memory_run_self_test(&test);

    console_write(shell->console, "Allocation:  ");
    console_write(shell->console,
        test.allocation_passed ? "PASS\n" : "FAIL\n");
    console_write(shell->console, "Distinct:    ");
    console_write(shell->console,
        test.distinct_pages_passed ? "PASS\n" : "FAIL\n");
    console_write(shell->console, "Mapping:     ");
    console_write(shell->console,
        test.mapping_passed ? "PASS\n" : "FAIL\n");
    console_write(shell->console, "Translation: ");
    console_write(shell->console,
        test.translation_passed ? "PASS\n" : "FAIL\n");
    console_write(shell->console, "Write/read:  ");
    console_write(shell->console,
        test.write_read_passed ? "PASS\n" : "FAIL\n");
    console_write(shell->console, "Cleanup:     ");
    console_write(shell->console,
        test.cleanup_passed ? "PASS\n" : "FAIL\n");
    console_write(shell->console,
        passed ? "Memory subsystem: HEALTHY\n" :
                 "Memory subsystem: FAILED\n");
    return;
}
```

## 5. Build and test

```bash
cd ~/kailash-os
make clean
make run-uefi
```

Open Terminal and run:

```text
memtest
```

Do not continue to the heap until all six checks report `PASS` repeatedly.

## Page-table entry format used here

Each page-table entry is 64 bits. For an ordinary 4 KiB mapping:

| Bits | Meaning |
|---|---|
| 0 | Present |
| 1 | Writable |
| 2 | User-accessible |
| 3 | Write-through cache policy |
| 4 | Cache disabled |
| 5 | Accessed, updated by CPU |
| 6 | Dirty on a leaf entry, updated by CPU |
| 7 | Large page at PDPT/PD level |
| 8 | Global translation |
| 9-11 | Available to the OS; KRISHNA uses bit 9 to mark VMM-owned tables and mappings |
| 12-51 | Physical frame address |
| 63 | No-execute when EFER.NXE is enabled |

For a virtual address, bits 47-39 select PML4, 38-30 select PDPT, 29-21
select PD, 20-12 select PT, and 11-0 are the byte offset inside the 4 KiB page.

The CPU performs this walk. There is no LRU search. The TLB caches recent
translations, and `invlpg` invalidates a changed translation. A Clock/LRU-style
replacement policy belongs to a future swap subsystem after storage, page
faults, and process working sets exist.

## Deliberately deferred, without redesigning this foundation

- A KRISHNA-owned replacement PML4 and safe Limine boot-memory reclamation.
- Per-process address-space creation and destruction.
- Page-fault exception handling and demand-zero pages.
- Copy-on-write and `fork` semantics.
- Swap and Clock-style victim selection.
- SMP TLB shootdowns.
- 2 MiB/1 GiB huge-page creation (translation already recognizes them).

Those are later users/extensions of PMM and VMM, not replacements for them.
