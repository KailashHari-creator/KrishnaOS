#include "interrupts.h"

#include <stddef.h>
#include <stdint.h>

#include <memory.h>

#include "drivers/serial.h"

/*
 * One x86-64 Interrupt Descriptor Table entry.
 */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

/*
 * Structure consumed by LIDT.
 */
struct idt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static uint16_t kernel_code_selector;
static bool idt_initialized;

/*
 * Permanently stop the current processor.
 */
__attribute__((noreturn))
static void exception_halt(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}


/*
 * Print a fixed-width 64-bit hexadecimal number.
 *
 * Serial output is deliberately used because framebuffer rendering
 * itself may have caused the exception.
 */
static void serial_write_hex_u64(uint64_t value)
{
    static const char digits[] =
        "0123456789ABCDEF";

    serial_write("0x");

    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit =
            (uint8_t)((value >> shift) & 0x0F);

        serial_write_character(digits[digit]);
    }
}


/*
 * Read CR2.
 *
 * For a page fault, CR2 contains the virtual address that caused
 * the fault.
 */
static uint64_t read_cr2(void)
{
    uint64_t value;

    __asm__ volatile (
        "mov %%cr2, %0"
        : "=r"(value)
    );

    return value;
}


/*
 * Install one handler in the IDT.
 */
static void idt_set_entry(
    uint8_t vector,
    uint64_t handler_address,
    uint16_t code_selector
)
{
    struct idt_entry *entry =
        &idt[vector];

    entry->offset_low =
        (uint16_t)(handler_address & 0xFFFF);

    entry->selector = code_selector;

    /*
     * IST zero means use the current stack.
     *
     * A dedicated emergency IST stack will be added when we
     * introduce the Task State Segment.
     */
    entry->ist = 0;

    /*
     * 0x8E:
     *
     * Present = 1
     * DPL     = 0
     * Type    = 64-bit interrupt gate
     */
    entry->attributes = 0x8E;

    entry->offset_middle =
        (uint16_t)((handler_address >> 16) & 0xFFFF);

    entry->offset_high =
        (uint32_t)((handler_address >> 32) & 0xFFFFFFFF);

    entry->reserved = 0;
}


/*
 * Print the common portion of a fatal exception report.
 */
static void report_exception(
    const char *name,
    struct interrupt_frame *frame
)
{
    serial_write(
        "\n\n========================================\n"
        "KRISHNA OS KERNEL EXCEPTION\n"
        "========================================\n"
        "Exception: "
    );

    serial_write(name);

    serial_write("\nInstruction pointer: ");
    serial_write_hex_u64(frame->instruction_pointer);

    serial_write("\nCode segment:        ");
    serial_write_hex_u64(frame->code_segment);

    serial_write("\nCPU flags:           ");
    serial_write_hex_u64(frame->flags);

    serial_write("\n");
}


/*
 * Vector 0: division by zero or quotient overflow.
 *
 * This exception does not push an error code.
 */
__attribute__((interrupt))
static void divide_error_handler(
    struct interrupt_frame *frame
)
{
    report_exception("Divide error", frame);
    exception_halt();
}


/*
 * Vector 6: invalid or unsupported instruction.
 *
 * This exception does not push an error code.
 */
__attribute__((interrupt))
static void invalid_opcode_handler(
    struct interrupt_frame *frame
)
{
    report_exception("Invalid opcode", frame);
    exception_halt();
}


/*
 * Vector 8: an exception occurred while delivering another
 * exception.
 *
 * The CPU pushes an error code for this exception.
 */
__attribute__((interrupt))
static void double_fault_handler(
    struct interrupt_frame *frame,
    unsigned long error_code
)
{
    report_exception("Double fault", frame);

    serial_write("Error code:          ");
    serial_write_hex_u64((uint64_t)error_code);
    serial_write("\n");

    exception_halt();
}


/*
 * Vector 13: general-protection fault.
 *
 * Examples include non-canonical addresses, invalid segment
 * operations, and privileged-instruction violations.
 */
__attribute__((interrupt))
static void general_protection_handler(
    struct interrupt_frame *frame,
    unsigned long error_code
)
{
    report_exception(
        "General-protection fault",
        frame
    );

    serial_write("Error code:          ");
    serial_write_hex_u64((uint64_t)error_code);
    serial_write("\n");

    exception_halt();
}


/*
 * Vector 14: page fault.
 *
 * CR2 identifies the faulting virtual address. The error code
 * explains why the translation failed.
 */
__attribute__((interrupt))
static void page_fault_handler(
    struct interrupt_frame *frame,
    unsigned long error_code
)
{
    uint64_t fault_address = read_cr2();
    uint64_t error = (uint64_t)error_code;

    report_exception("Page fault", frame);

    serial_write("Faulting address:    ");
    serial_write_hex_u64(fault_address);

    serial_write("\nError code:          ");
    serial_write_hex_u64(error);

    serial_write("\nCause:               ");

    if ((error & (UINT64_C(1) << 0)) != 0) {
        serial_write("protection violation");
    } else {
        serial_write("non-present page");
    }

    serial_write("\nOperation:           ");

    if ((error & (UINT64_C(1) << 1)) != 0) {
        serial_write("write");
    } else {
        serial_write("read");
    }

    serial_write("\nPrivilege:           ");

    if ((error & (UINT64_C(1) << 2)) != 0) {
        serial_write("user");
    } else {
        serial_write("kernel");
    }

    serial_write("\nReserved-bit fault:  ");

    if ((error & (UINT64_C(1) << 3)) != 0) {
        serial_write("yes");
    } else {
        serial_write("no");
    }

    serial_write("\nInstruction fetch:   ");

    if ((error & (UINT64_C(1) << 4)) != 0) {
        serial_write("yes");
    } else {
        serial_write("no");
    }

    serial_write(
        "\n\nThe kernel has been halted to prevent "
        "memory corruption.\n"
    );

    exception_halt();
}


void interrupts_init(void)
{
    struct idt_descriptor descriptor;

    /*
     * External hardware interrupts must remain disabled until
     * KRISHNA OS has a complete IRQ controller and IRQ handlers.
     *
     * CPU exceptions such as page faults are still delivered.
     */
    __asm__ volatile ("cli" ::: "memory");

    memset(idt, 0, sizeof(idt));

    /*
     * Reuse the kernel code selector installed by Limine.
     */
    __asm__ volatile (
        "mov %%cs, %0"
        : "=r"(kernel_code_selector)
    );

    idt_set_entry(
        0,
        (uint64_t)(uintptr_t)divide_error_handler,
        kernel_code_selector
    );

    idt_set_entry(
        6,
        (uint64_t)(uintptr_t)invalid_opcode_handler,
        kernel_code_selector
    );

    idt_set_entry(
        8,
        (uint64_t)(uintptr_t)double_fault_handler,
        kernel_code_selector
    );

    idt_set_entry(
        13,
        (uint64_t)(uintptr_t)general_protection_handler,
        kernel_code_selector
    );

    idt_set_entry(
        14,
        (uint64_t)(uintptr_t)page_fault_handler,
        kernel_code_selector
    );

    descriptor.limit =
        (uint16_t)(sizeof(idt) - 1);

    descriptor.base =
        (uint64_t)(uintptr_t)idt;

    __asm__ volatile (
        "lidt %0"
        :
        : "m"(descriptor)
        : "memory"
    );
    idt_initialized = true;

    /*
     * Do not execute STI here.
     *
     * Mouse and keyboard remain polling-based, while cursor timing
     * uses the timestamp counter.
     */
}
bool interrupts_install_gate(
    uint8_t vector,
    uintptr_t handler_address
)
{
    if (!idt_initialized ||
        handler_address == 0) {
        return false;
    }

    idt_set_entry(
        vector,
        (uint64_t)handler_address,
        kernel_code_selector
    );

    return true;
}


void interrupts_enable(void)
{
    __asm__ volatile (
        "sti"
        :
        :
        : "memory"
    );
}


void interrupts_disable(void)
{
    __asm__ volatile (
        "cli"
        :
        :
        : "memory"
    );
}