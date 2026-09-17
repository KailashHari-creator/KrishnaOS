#include "arch/x86_64/apic.h"
#include "memory/layout.h"
#include "memory/vmm.h"
#include "arch/x86_64/io.h"
#include "interrupts.h"
#include "task/thread.h"
#include "arch/x86_64/context_switch.h"

#include <stddef.h>
#include <stdint.h>

#define CPUID_BASIC_FEATURES UINT32_C(1)

#define CPUID_EDX_MSR  (UINT32_C(1) << 5)
#define CPUID_EDX_APIC (UINT32_C(1) << 9)

#define IA32_APIC_BASE_MSR UINT32_C(0x1B)

#define IA32_APIC_BASE_BSP \
    (UINT64_C(1) << 8)

#define IA32_APIC_BASE_X2APIC_ENABLE \
    (UINT64_C(1) << 10)

#define IA32_APIC_BASE_GLOBAL_ENABLE \
    (UINT64_C(1) << 11)

#define IA32_APIC_BASE_ADDRESS_MASK \
    UINT64_C(0x000FFFFFFFFFF000)

/*
 * KRISHNA reserves the first page of its MMIO virtual region for the
 * bootstrap processor's Local APIC.
 */
#define LOCAL_APIC_VIRTUAL_ADDRESS \
    KERNEL_MMIO_BASE

#define LOCAL_APIC_ID_REGISTER      UINT64_C(0x020)
#define LOCAL_APIC_VERSION_REGISTER UINT64_C(0x030)
#define LOCAL_APIC_SPURIOUS_REGISTER UINT64_C(0x0F0)

#define LOCAL_APIC_SOFTWARE_ENABLE \
    (UINT32_C(1) << 8)

#define LOCAL_APIC_TASK_PRIORITY_REGISTER \
    UINT64_C(0x080)

#define LOCAL_APIC_EOI_REGISTER \
    UINT64_C(0x0B0)

#define LOCAL_APIC_LVT_TIMER_REGISTER \
    UINT64_C(0x320)

#define LOCAL_APIC_TIMER_INITIAL_COUNT_REGISTER \
    UINT64_C(0x380)

#define LOCAL_APIC_TIMER_CURRENT_COUNT_REGISTER \
    UINT64_C(0x390)

#define LOCAL_APIC_TIMER_DIVIDE_REGISTER \
    UINT64_C(0x3E0)

#define LOCAL_APIC_TIMER_VECTOR \
    UINT8_C(0xF0)

#define LOCAL_APIC_SPURIOUS_VECTOR \
    UINT8_C(0xFF)

#define LOCAL_APIC_LVT_MASKED \
    (UINT32_C(1) << 16)

#define LOCAL_APIC_TIMER_PERIODIC \
    (UINT32_C(1) << 17)

/*
 * Divide the APIC timer input clock by 16.
 */
#define LOCAL_APIC_TIMER_DIVIDE_BY_16 \
    UINT32_C(0x03)

#define LOCAL_APIC_CALIBRATION_DIVISOR \
    UINT64_C(100)

#define LEGACY_PIC_MASTER_DATA \
    UINT16_C(0x21)

#define LEGACY_PIC_SLAVE_DATA \
    UINT16_C(0xA1)

static volatile uint32_t *local_apic_registers;
static volatile uint64_t timer_tick_count;
static uint32_t local_apic_configured_frequency;
static bool local_apic_timer_initialized;

static uint64_t read_tsc(void);

static uint32_t local_apic_read_register(
    uint64_t register_offset
)
{
    /*
     * Every xAPIC register occupies one 16-byte slot.
     */
    size_t index =
        (size_t)(register_offset /
                 sizeof(uint32_t));

    return local_apic_registers[index];
}

static void local_apic_write_register(
    uint32_t register_offset,
    uint32_t value
)
{
    size_t index =
        (size_t)register_offset /
        sizeof(uint32_t);

    local_apic_registers[index] = value;

    /*
     * Prevent compiler reordering around the MMIO write.
     * local_apic_registers must be volatile.
     */
    __asm__ volatile ("" ::: "memory");
}

uint64_t *local_apic_timer_interrupt_dispatch(
    uint64_t *interrupted_rsp
)
{
    timer_tick_count++;

    uint64_t *resume_rsp =
        kernel_thread_timer_interrupt(
            interrupted_rsp
        );

    /*
     * Acknowledge the timer before returning into whichever thread
     * the scheduler selected.
     */
    local_apic_write_register(
        LOCAL_APIC_EOI_REGISTER,
        UINT32_C(0)
    );

    return resume_rsp;
}

__attribute__((interrupt))
static void local_apic_spurious_interrupt_handler(
    struct interrupt_frame *frame
)
{
    (void)frame;

    /*
     * A genuine Local APIC spurious interrupt must not receive EOI.
     */
}

static void mask_legacy_pic(void)
{
    /*
     * Keyboard and mouse are still polling-based. No legacy IRQ is
     * currently allowed to reach the processor.
     */
    io_write8(
        LEGACY_PIC_MASTER_DATA,
        UINT8_C(0xFF)
    );

    io_write8(
        LEGACY_PIC_SLAVE_DATA,
        UINT8_C(0xFF)
    );
}

static void cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
)
{
    uint32_t result_a;
    uint32_t result_b;
    uint32_t result_c;
    uint32_t result_d;

    __asm__ volatile (
        "cpuid"
        : "=a"(result_a),
          "=b"(result_b),
          "=c"(result_c),
          "=d"(result_d)
        : "a"(leaf),
          "c"(subleaf)
    );

    if (eax != NULL) {
        *eax = result_a;
    }

    if (ebx != NULL) {
        *ebx = result_b;
    }

    if (ecx != NULL) {
        *ecx = result_c;
    }

    if (edx != NULL) {
        *edx = result_d;
    }
}


static uint64_t read_msr(uint32_t msr)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile (
        "rdmsr"
        : "=a"(low),
          "=d"(high)
        : "c"(msr)
    );

    return ((uint64_t)high << 32) | low;
}


bool local_apic_probe(
    struct local_apic_information *information
)
{
    uint32_t maximum_basic_leaf;
    uint32_t feature_edx;
    uint64_t apic_base;

    if (information == NULL) {
        return false;
    }

    *information =
        (struct local_apic_information){0};

    cpuid(
        UINT32_C(0),
        UINT32_C(0),
        &maximum_basic_leaf,
        NULL,
        NULL,
        NULL
    );

    if (maximum_basic_leaf <
        CPUID_BASIC_FEATURES) {
        return false;
    }

    cpuid(
        CPUID_BASIC_FEATURES,
        UINT32_C(0),
        NULL,
        NULL,
        NULL,
        &feature_edx
    );

    information->supported =
        (feature_edx & CPUID_EDX_APIC) != 0;

    if (!information->supported) {
        return true;
    }

    /*
     * Reading IA32_APIC_BASE requires MSR support.
     */
    if ((feature_edx & CPUID_EDX_MSR) == 0) {
        return false;
    }

    apic_base =
        read_msr(IA32_APIC_BASE_MSR);

    information->enabled =
        (apic_base &
         IA32_APIC_BASE_GLOBAL_ENABLE) != 0;

    information->bootstrap_processor =
        (apic_base &
         IA32_APIC_BASE_BSP) != 0;

    information->physical_address =
        apic_base &
        IA32_APIC_BASE_ADDRESS_MASK;

    if (!information->enabled) {
        information->mode =
            LOCAL_APIC_MODE_DISABLED;
    } else if (
        (apic_base &
         IA32_APIC_BASE_X2APIC_ENABLE) != 0
    ) {
        information->mode =
            LOCAL_APIC_MODE_X2APIC;
    } else {
        information->mode =
            LOCAL_APIC_MODE_XAPIC;
    }

    return true;
}
bool local_apic_map(
    const struct local_apic_information *information
)
{
    uint64_t existing_physical;
    uint64_t mapped_physical;

    if (information == NULL ||
        !information->supported ||
        !information->enabled ||
        information->mode !=
            LOCAL_APIC_MODE_XAPIC ||
        information->physical_address == 0 ||
        (information->physical_address &
         (VMM_PAGE_SIZE - 1)) != 0 ||
        local_apic_registers != NULL) {
        return false;
    }

    /*
     * The dedicated MMIO virtual page must not already be occupied.
     */
    if (vmm_translate(
            vmm_kernel_address_space(),
            LOCAL_APIC_VIRTUAL_ADDRESS,
            &existing_physical
        )) {
        return false;
    }

    /*
     * PCD + PWT selects an uncached memory type with the default PAT
     * configuration. Device registers must not be treated like normal
     * write-back RAM.
     */
    uint64_t flags =
        VMM_PAGE_WRITABLE |
        VMM_PAGE_WRITE_THROUGH |
        VMM_PAGE_CACHE_DISABLE |
        VMM_PAGE_GLOBAL |
        VMM_PAGE_NO_EXECUTE;

    if (!vmm_map_page(
            vmm_kernel_address_space(),
            LOCAL_APIC_VIRTUAL_ADDRESS,
            information->physical_address,
            flags
        )) {
        return false;
    }

    /*
     * Verify that the completed page-table translation points at the
     * requested hardware page.
     */
    if (!vmm_translate(
            vmm_kernel_address_space(),
            LOCAL_APIC_VIRTUAL_ADDRESS,
            &mapped_physical
        ) ||
        mapped_physical !=
            information->physical_address) {
        uint64_t removed_physical;

        (void)vmm_unmap_page(
            vmm_kernel_address_space(),
            LOCAL_APIC_VIRTUAL_ADDRESS,
            &removed_physical
        );

        return false;
    }

    local_apic_registers =
        (volatile uint32_t *)(uintptr_t)
            LOCAL_APIC_VIRTUAL_ADDRESS;

    return true;
}
bool local_apic_read_identity(
    struct local_apic_identity *identity
)
{
    uint32_t id_register;
    uint32_t version_register;
    uint32_t spurious_register;
    uint8_t version;
    uint8_t maximum_lvt_entry;

    if (identity == NULL ||
        local_apic_registers == NULL) {
        return false;
    }

    id_register =
        local_apic_read_register(
            LOCAL_APIC_ID_REGISTER
        );

    version_register =
        local_apic_read_register(
            LOCAL_APIC_VERSION_REGISTER
        );

    spurious_register =
        local_apic_read_register(
            LOCAL_APIC_SPURIOUS_REGISTER
        );

    version =
        (uint8_t)(
            version_register &
            UINT32_C(0xFF)
        );

    maximum_lvt_entry =
        (uint8_t)(
            (version_register >> 16) &
            UINT32_C(0xFF)
        );

    if (version == 0) {
        return false;
    }

    *identity =
        (struct local_apic_identity){
            .apic_id =
                (uint8_t)(
                    (id_register >> 24) &
                    UINT32_C(0xFF)
                ),

            .version = version,

            /*
             * The register contains the highest valid LVT index,
             * rather than the number of entries.
             */
            .lvt_entry_count =
                (uint8_t)(
                    maximum_lvt_entry + 1
                ),

            .software_enabled =
                (spurious_register &
                 LOCAL_APIC_SOFTWARE_ENABLE) != 0
        };

    return true;
}
uint64_t local_apic_virtual_address(void)
{
    if (local_apic_registers == NULL) {
        return 0;
    }

    return LOCAL_APIC_VIRTUAL_ADDRESS;
}
bool local_apic_timer_init(
    uint32_t requested_frequency,
    uint64_t tsc_frequency
)
{
    uint64_t calibration_tsc_ticks;
    uint64_t calibration_start;
    uint32_t current_count;
    uint32_t elapsed_apic_counts;
    uint64_t counts_per_second;
    uint64_t initial_count;
    uint32_t spurious_register;

    if (local_apic_registers == NULL ||
        local_apic_timer_initialized ||
        requested_frequency == 0 ||
        tsc_frequency == 0) {
        return false;
    }

    /*
     * Timer and spurious gates must exist before either vector can
     * possibly be delivered.
     */
    if (!interrupts_install_gate(
            LOCAL_APIC_TIMER_VECTOR,
            (uintptr_t)arch_local_apic_timer_interrupt_entry
        ) ||
        !interrupts_install_gate(
            LOCAL_APIC_SPURIOUS_VECTOR,
            (uintptr_t)local_apic_spurious_interrupt_handler
        )) {
        return false;
    }

    /*
     * Ensure the legacy PIC cannot inject an unhandled interrupt
     * after STI. Keyboard and mouse remain polling-based.
     */
    mask_legacy_pic();

    /*
     * Accept every APIC interrupt priority class.
     */
    local_apic_write_register(
        LOCAL_APIC_TASK_PRIORITY_REGISTER,
        UINT32_C(0)
    );

    /*
     * Explicitly select the spurious vector and enable the Local APIC
     * in software. Preserve unrelated SVR control bits.
     */
    spurious_register =
        local_apic_read_register(
            LOCAL_APIC_SPURIOUS_REGISTER
        );

    spurious_register &=
        ~UINT32_C(0xFF);

    spurious_register |=
        LOCAL_APIC_SPURIOUS_VECTOR |
        LOCAL_APIC_SOFTWARE_ENABLE;

    local_apic_write_register(
        LOCAL_APIC_SPURIOUS_REGISTER,
        spurious_register
    );

    /*
     * Mask the timer while calibrating its counter.
     */
    local_apic_write_register(
        LOCAL_APIC_LVT_TIMER_REGISTER,
        LOCAL_APIC_LVT_MASKED |
        LOCAL_APIC_TIMER_VECTOR
    );

    local_apic_write_register(
        LOCAL_APIC_TIMER_DIVIDE_REGISTER,
        LOCAL_APIC_TIMER_DIVIDE_BY_16
    );

    /*
     * Run the down-counter for 1/100 second while measuring time with
     * the TSC. Interrupt delivery remains masked during calibration.
     */
    calibration_tsc_ticks =
        tsc_frequency /
        LOCAL_APIC_CALIBRATION_DIVISOR;

    if (calibration_tsc_ticks == 0) {
        return false;
    }

    local_apic_write_register(
        LOCAL_APIC_TIMER_INITIAL_COUNT_REGISTER,
        UINT32_MAX
    );

    calibration_start = read_tsc();

    while ((read_tsc() -
            calibration_start) <
           calibration_tsc_ticks) {
        __asm__ volatile ("pause");
    }

    current_count =
        local_apic_read_register(
            LOCAL_APIC_TIMER_CURRENT_COUNT_REGISTER
        );

    /*
     * Stop and remask the calibration timer.
     */
    local_apic_write_register(
        LOCAL_APIC_TIMER_INITIAL_COUNT_REGISTER,
        UINT32_C(0)
    );

    local_apic_write_register(
        LOCAL_APIC_LVT_TIMER_REGISTER,
        LOCAL_APIC_LVT_MASKED |
        LOCAL_APIC_TIMER_VECTOR
    );

    elapsed_apic_counts =
        UINT32_MAX - current_count;

    if (elapsed_apic_counts == 0) {
        return false;
    }

    counts_per_second =
        (uint64_t)elapsed_apic_counts *
        LOCAL_APIC_CALIBRATION_DIVISOR;

    initial_count =
        counts_per_second /
        requested_frequency;

    if (initial_count == 0 ||
        initial_count > UINT32_MAX) {
        return false;
    }

    local_apic_configured_frequency =
        (uint32_t)(
            counts_per_second /
            initial_count
        );


    /*
     * Configure periodic delivery and unmask the timer.
     */
    local_apic_write_register(
        LOCAL_APIC_LVT_TIMER_REGISTER,
        LOCAL_APIC_TIMER_PERIODIC |
        LOCAL_APIC_TIMER_VECTOR
    );

    local_apic_write_register(
        LOCAL_APIC_TIMER_INITIAL_COUNT_REGISTER,
        (uint32_t)initial_count
    );

    local_apic_timer_initialized = true;
    return true;
}
static uint64_t read_tsc(void)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile (
        "rdtsc"
        : "=a"(low),
          "=d"(high)
    );

    return ((uint64_t)high << 32) | low;
}
uint64_t local_apic_timer_ticks(void)
{
    return timer_tick_count;
}


uint32_t local_apic_timer_frequency(void)
{
    return local_apic_configured_frequency;
}