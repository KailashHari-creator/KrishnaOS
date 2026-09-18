#include "arch/x86_64/gdt.h"

#include <stddef.h>
#include <stdint.h>

#include <memory.h>

#define GDT_ACCESS_KERNEL_CODE UINT8_C(0x9A)
#define GDT_ACCESS_KERNEL_DATA UINT8_C(0x92)
#define GDT_ACCESS_USER_CODE   UINT8_C(0xFA)
#define GDT_ACCESS_USER_DATA   UINT8_C(0xF2)
#define GDT_ACCESS_TSS         UINT8_C(0x89)

/*
 * G = 1, L = 1, D/B = 0.
 */
#define GDT_FLAGS_64_BIT_CODE UINT8_C(0x0A)

/*
 * G = 1, D/B = 1.
 *
 * Long mode mostly ignores the base and limit of data segments, but
 * this remains a conventional valid data descriptor.
 */
#define GDT_FLAGS_DATA UINT8_C(0x0C)

#define BOOTSTRAP_KERNEL_STACK_SIZE \
    (UINT64_C(16) * UINT64_C(1024))

struct task_state_segment {
    uint32_t reserved_zero;

    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint64_t reserved_one;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved_two;
    uint16_t reserved_three;
    uint16_t io_map_base;
} __attribute__((packed));

struct tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t limit_high_and_flags;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_table {
    uint64_t null_descriptor;
    uint64_t kernel_code_descriptor;
    uint64_t kernel_data_descriptor;
    uint64_t user_data_descriptor;
    uint64_t user_code_descriptor;
    struct tss_descriptor tss_descriptor;
} __attribute__((packed));

struct gdt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(
    sizeof(struct task_state_segment) == 104,
    "Unexpected x86-64 TSS size"
);

_Static_assert(
    offsetof(struct task_state_segment, rsp0) == 4,
    "Unexpected TSS RSP0 offset"
);

_Static_assert(
    offsetof(struct task_state_segment, ist1) == 36,
    "Unexpected TSS IST1 offset"
);

_Static_assert(
    offsetof(struct task_state_segment, io_map_base) == 102,
    "Unexpected TSS I/O-map offset"
);

_Static_assert(
    sizeof(struct tss_descriptor) == 16,
    "Unexpected TSS descriptor size"
);

_Static_assert(
    sizeof(struct gdt_table) == 56,
    "Unexpected GDT size"
);

extern void arch_gdt_load(
    const struct gdt_descriptor *descriptor,
    uint16_t kernel_code_selector,
    uint16_t kernel_data_selector,
    uint16_t tss_selector
);

static struct task_state_segment bootstrap_tss
    __attribute__((aligned(16)));

static struct gdt_table bootstrap_gdt
    __attribute__((aligned(16)));

/*
 * Temporary privilege-transition stack.
 *
 * Once userspace threads exist, RSP0 must be changed to the selected
 * thread's guarded kernel-stack top during scheduling.
 */
static uint8_t bootstrap_kernel_stack[
    BOOTSTRAP_KERNEL_STACK_SIZE
] __attribute__((aligned(16)));

static bool gdt_initialized;

static uint64_t make_segment_descriptor(
    uint8_t access,
    uint8_t flags
)
{
    const uint32_t limit = UINT32_C(0xFFFFF);
    const uint32_t base = 0;

    uint64_t descriptor = 0;

    descriptor |=
        (uint64_t)(limit & UINT32_C(0xFFFF));

    descriptor |=
        (uint64_t)(base & UINT32_C(0xFFFF)) << 16;

    descriptor |=
        (uint64_t)((base >> 16) &
                   UINT32_C(0xFF)) << 32;

    descriptor |=
        (uint64_t)access << 40;

    descriptor |=
        (uint64_t)((limit >> 16) &
                   UINT32_C(0x0F)) << 48;

    descriptor |=
        (uint64_t)(flags & UINT8_C(0x0F)) << 52;

    descriptor |=
        (uint64_t)((base >> 24) &
                   UINT32_C(0xFF)) << 56;

    return descriptor;
}

static void install_tss_descriptor(void)
{
    uint64_t base =
        (uint64_t)(uintptr_t)&bootstrap_tss;

    uint32_t limit =
        (uint32_t)(sizeof(bootstrap_tss) - 1);

    bootstrap_gdt.tss_descriptor =
        (struct tss_descriptor){
            .limit_low =
                (uint16_t)(limit &
                           UINT32_C(0xFFFF)),

            .base_low =
                (uint16_t)(base &
                           UINT64_C(0xFFFF)),

            .base_middle =
                (uint8_t)((base >> 16) &
                          UINT64_C(0xFF)),

            .access = GDT_ACCESS_TSS,

            .limit_high_and_flags =
                (uint8_t)((limit >> 16) &
                          UINT32_C(0x0F)),

            .base_high =
                (uint8_t)((base >> 24) &
                          UINT64_C(0xFF)),

            .base_upper =
                (uint32_t)(base >> 32),

            .reserved = 0
        };
}

bool gdt_init(void)
{
    if (gdt_initialized) {
        return false;
    }

    /*
     * The IDT is not ready yet. No maskable interrupt may occur while
     * replacing the firmware/bootloader GDT.
     */
    __asm__ volatile ("cli" ::: "memory");

    memset(
        &bootstrap_tss,
        0,
        sizeof(bootstrap_tss)
    );

    memset(
        &bootstrap_gdt,
        0,
        sizeof(bootstrap_gdt)
    );

    /*
     * An I/O-map offset beyond the TSS limit means that this TSS does
     * not contain an I/O-permission bitmap.
     */
    bootstrap_tss.io_map_base =
        (uint16_t)sizeof(bootstrap_tss);

    bootstrap_tss.rsp0 =
        (uint64_t)(uintptr_t)(
            bootstrap_kernel_stack +
            sizeof(bootstrap_kernel_stack)
        );

    bootstrap_gdt.kernel_code_descriptor =
        make_segment_descriptor(
            GDT_ACCESS_KERNEL_CODE,
            GDT_FLAGS_64_BIT_CODE
        );

    bootstrap_gdt.kernel_data_descriptor =
        make_segment_descriptor(
            GDT_ACCESS_KERNEL_DATA,
            GDT_FLAGS_DATA
        );

    /*
     * User data precedes user code. This ordering will also work with
     * the selector layout expected by SYSRET if we add fast syscalls
     * later.
     */
    bootstrap_gdt.user_data_descriptor =
        make_segment_descriptor(
            GDT_ACCESS_USER_DATA,
            GDT_FLAGS_DATA
        );

    bootstrap_gdt.user_code_descriptor =
        make_segment_descriptor(
            GDT_ACCESS_USER_CODE,
            GDT_FLAGS_64_BIT_CODE
        );

    install_tss_descriptor();

    struct gdt_descriptor descriptor = {
        .limit =
            (uint16_t)(sizeof(bootstrap_gdt) - 1),

        .base =
            (uint64_t)(uintptr_t)&bootstrap_gdt
    };

    arch_gdt_load(
        &descriptor,
        GDT_KERNEL_CODE_SELECTOR,
        GDT_KERNEL_DATA_SELECTOR,
        GDT_TSS_SELECTOR
    );

    gdt_initialized = true;

    return gdt_self_test();
}

bool gdt_self_test(void)
{
    struct gdt_descriptor loaded_descriptor;

    uint16_t code_selector;
    uint16_t data_selector;
    uint16_t stack_selector;
    uint16_t task_selector;

    if (!gdt_initialized) {
        return false;
    }

    __asm__ volatile (
        "sgdt %0"
        : "=m"(loaded_descriptor)
        :
        : "memory"
    );

    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(code_selector)
    );

    __asm__ volatile (
        "mov %%ds, %0"
        : "=r"(data_selector)
    );

    __asm__ volatile (
        "mov %%ss, %0"
        : "=r"(stack_selector)
    );

    __asm__ volatile (
        "str %0"
        : "=r"(task_selector)
    );

    return
        loaded_descriptor.base ==
            (uint64_t)(uintptr_t)&bootstrap_gdt &&

        loaded_descriptor.limit ==
            (uint16_t)(sizeof(bootstrap_gdt) - 1) &&

        code_selector ==
            GDT_KERNEL_CODE_SELECTOR &&

        data_selector ==
            GDT_KERNEL_DATA_SELECTOR &&

        stack_selector ==
            GDT_KERNEL_DATA_SELECTOR &&

        task_selector ==
            GDT_TSS_SELECTOR &&

        bootstrap_tss.rsp0 != 0 &&

        (bootstrap_tss.rsp0 &
         UINT64_C(0x0F)) == 0 &&

        bootstrap_tss.io_map_base ==
            sizeof(bootstrap_tss);
}

bool gdt_set_kernel_stack(uint64_t stack_top)
{
    uint64_t upper_bits =
        stack_top >> 48;

    if (!gdt_initialized ||
        stack_top == 0 ||
        (stack_top & UINT64_C(0x0F)) != 0 ||
        (upper_bits != 0 &&
         upper_bits != UINT64_C(0xFFFF))) {
        return false;
    }

    bootstrap_tss.rsp0 = stack_top;

    __asm__ volatile ("" ::: "memory");

    return true;
}

uint64_t gdt_kernel_stack(void)
{
    if (!gdt_initialized) {
        return 0;
    }

    return bootstrap_tss.rsp0;
}