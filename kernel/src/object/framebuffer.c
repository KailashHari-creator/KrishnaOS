#include "object/framebuffer.h"

#include <stddef.h>
#include <stdint.h>

#include <krishna/abi.h>

#include "memory/vmm.h"
#include "object/object.h"
#include "sync/spinlock.h"
#include "task/process.h"

#define FRAMEBUFFER_USER_MAPPING_BASE \
    UINT64_C(0x0000000100000000)

struct framebuffer_object_context {
    struct limine_framebuffer *framebuffer;

    uint64_t byte_size;

    struct kernel_process *mapped_process;

    uint64_t mapping_base;
    uint64_t mapped_address;

    size_t mapped_page_count;
};

static struct kernel_object framebuffer_object;
static struct framebuffer_object_context framebuffer_context;

static spinlock_t framebuffer_lock =
    SPINLOCK_INITIALIZER;

static bool framebuffer_initialized;

_Static_assert(
    sizeof(struct krishna_framebuffer_info) == 48,
    "Framebuffer ABI information size changed"
);

static int64_t framebuffer_ioctl(
    void *context_pointer,
    uint64_t request,
    void *buffer,
    size_t size
)
{
    struct framebuffer_object_context *context =
        (struct framebuffer_object_context *)
            context_pointer;

    if (context == NULL ||
        context->framebuffer == NULL) {
        return -KRISHNA_ERROR_IO;
    }

    if (request !=
        KRISHNA_FRAMEBUFFER_IOCTL_GET_INFO) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    if (buffer == NULL ||
        size <
            sizeof(
                struct krishna_framebuffer_info
            )) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    struct limine_framebuffer *framebuffer =
        context->framebuffer;

    struct krishna_framebuffer_info *info =
        (struct krishna_framebuffer_info *)
            buffer;

    *info =
        (struct krishna_framebuffer_info){
            .width = framebuffer->width,
            .height = framebuffer->height,
            .pitch = framebuffer->pitch,
            .byte_size =
                context->byte_size,

            .bits_per_pixel =
                framebuffer->bpp,

            .red_mask_shift =
                framebuffer->red_mask_shift,

            .red_mask_size =
                framebuffer->red_mask_size,

            .green_mask_shift =
                framebuffer->green_mask_shift,

            .green_mask_size =
                framebuffer->green_mask_size,

            .blue_mask_shift =
                framebuffer->blue_mask_shift,

            .blue_mask_size =
                framebuffer->blue_mask_size,

            .reserved = {0}
        };

    return (int64_t)sizeof(*info);
}

static int64_t framebuffer_map(
    void *context_pointer,
    struct kernel_process *process,
    uint64_t requested_address,
    uint64_t offset,
    uint64_t length,
    uint64_t protection,
    uint64_t flags
)
{
    struct framebuffer_object_context *context =
        (struct framebuffer_object_context *)
            context_pointer;

    if (context == NULL ||
        context->framebuffer == NULL ||
        process == NULL) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    /*
     * Version 0 supports only a complete framebuffer mapping chosen
     * by the kernel.
     */
    if (requested_address != 0 ||
        offset != 0 ||
        length != context->byte_size ||
        flags != 0) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    uint64_t required_protection =
        KRISHNA_MEMORY_PROTECTION_READ |
        KRISHNA_MEMORY_PROTECTION_WRITE;

    if (protection != required_protection) {
        return -KRISHNA_ERROR_PERMISSION_DENIED;
    }

    uintptr_t framebuffer_address =
        (uintptr_t)
            context->framebuffer->address;

    uint64_t framebuffer_offset =
        (uint64_t)framebuffer_address &
        (VMM_PAGE_SIZE - UINT64_C(1));

    uint64_t kernel_page =
        (uint64_t)framebuffer_address -
        framebuffer_offset;

    if (context->byte_size >
        UINT64_MAX - framebuffer_offset) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    uint64_t mapped_bytes =
        context->byte_size +
        framebuffer_offset;

    if (mapped_bytes >
        UINT64_MAX -
            (VMM_PAGE_SIZE - UINT64_C(1))) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    uint64_t page_count_u64 =
        (
            mapped_bytes +
            VMM_PAGE_SIZE -
            UINT64_C(1)
        ) / VMM_PAGE_SIZE;

    if (page_count_u64 == 0 ||
        page_count_u64 > SIZE_MAX) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    struct vmm_address_space *process_space =
        kernel_process_address_space(
            process
        );

    struct vmm_address_space *kernel_space =
        vmm_kernel_address_space();

    if (process_space == NULL ||
        kernel_space == NULL) {
        return -KRISHNA_ERROR_NO_SUCH_PROCESS;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &framebuffer_lock
        );

    if (context->mapped_process != NULL) {
        spinlock_unlock_irqrestore(
            &framebuffer_lock,
            interrupt_state
        );

        return -KRISHNA_ERROR_BUSY;
    }

    size_t mapped_pages = 0;

    uint64_t mapping_flags =
        VMM_PAGE_USER |
        VMM_PAGE_WRITABLE |
        VMM_PAGE_WRITE_THROUGH |
        VMM_PAGE_CACHE_DISABLE;

    if (vmm_nx_supported()) {
        mapping_flags |=
            VMM_PAGE_NO_EXECUTE;
    }

    for (size_t index = 0;
         index < (size_t)page_count_u64;
         index++) {
        uint64_t kernel_virtual_address =
            kernel_page +
            (uint64_t)index *
                VMM_PAGE_SIZE;

        uint64_t physical_address;

        if (!vmm_translate(
                kernel_space,
                kernel_virtual_address,
                &physical_address
            )) {
            break;
        }

        physical_address &=
            ~(VMM_PAGE_SIZE - UINT64_C(1));

        uint64_t user_virtual_address =
            FRAMEBUFFER_USER_MAPPING_BASE +
            (uint64_t)index *
                VMM_PAGE_SIZE;

        if (!vmm_map_page(
                process_space,
                user_virtual_address,
                physical_address,
                mapping_flags
            )) {
            break;
        }

        mapped_pages++;
    }

    if (mapped_pages !=
        (size_t)page_count_u64) {
        while (mapped_pages > 0) {
            mapped_pages--;

            (void)vmm_unmap_page(
                process_space,
                FRAMEBUFFER_USER_MAPPING_BASE +
                    (uint64_t)mapped_pages *
                        VMM_PAGE_SIZE,
                NULL
            );
        }

        spinlock_unlock_irqrestore(
            &framebuffer_lock,
            interrupt_state
        );

        return -KRISHNA_ERROR_OUT_OF_MEMORY;
    }

    context->mapped_process = process;
    context->mapping_base =
        FRAMEBUFFER_USER_MAPPING_BASE;

    context->mapped_address =
        FRAMEBUFFER_USER_MAPPING_BASE +
        framebuffer_offset;

    context->mapped_page_count =
        (size_t)page_count_u64;

    int64_t result =
        (int64_t)context->mapped_address;

    spinlock_unlock_irqrestore(
        &framebuffer_lock,
        interrupt_state
    );

    return result;
}

static int64_t framebuffer_unmap(
    void *context_pointer,
    struct kernel_process *process,
    uint64_t address,
    uint64_t length
)
{
    struct framebuffer_object_context *context =
        (struct framebuffer_object_context *)
            context_pointer;

    if (context == NULL ||
        process == NULL) {
        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    interrupt_state_t interrupt_state =
        spinlock_lock_irqsave(
            &framebuffer_lock
        );

    if (context->mapped_process !=
            process ||
        context->mapped_address !=
            address ||
        length != context->byte_size) {
        spinlock_unlock_irqrestore(
            &framebuffer_lock,
            interrupt_state
        );

        return -KRISHNA_ERROR_INVALID_ARGUMENT;
    }

    struct vmm_address_space *space =
        kernel_process_address_space(
            process
        );

    if (space == NULL) {
        spinlock_unlock_irqrestore(
            &framebuffer_lock,
            interrupt_state
        );

        return -KRISHNA_ERROR_NO_SUCH_PROCESS;
    }

    bool success = true;

    for (size_t index = 0;
         index <
            context->mapped_page_count;
         index++) {
        if (!vmm_unmap_page(
                space,
                context->mapping_base +
                    (uint64_t)index *
                        VMM_PAGE_SIZE,
                NULL
            )) {
            success = false;
        }
    }

    if (success) {
        context->mapped_process = NULL;
        context->mapping_base = 0;
        context->mapped_address = 0;
        context->mapped_page_count = 0;
    }

    spinlock_unlock_irqrestore(
        &framebuffer_lock,
        interrupt_state
    );

    return success
        ? 0
        : -KRISHNA_ERROR_IO;
}

static const struct kernel_object_operations
framebuffer_operations = {
    .read = NULL,
    .write = NULL,
    .ioctl = framebuffer_ioctl,
    .map = framebuffer_map,
    .unmap = framebuffer_unmap,
    .destroy = NULL
};

bool framebuffer_object_init(
    struct limine_framebuffer *framebuffer
)
{
    if (framebuffer_initialized) {
        return false;
    }

    if (framebuffer == NULL ||
        framebuffer->address == NULL ||
        framebuffer->width == 0 ||
        framebuffer->height == 0 ||
        framebuffer->pitch == 0 ||
        framebuffer->bpp != 32 ||
        framebuffer->pitch >
            UINT64_MAX /
                framebuffer->height) {
        return false;
    }

    framebuffer_context =
        (struct framebuffer_object_context){
            .framebuffer = framebuffer,
            .byte_size =
                framebuffer->pitch *
                framebuffer->height,

            .mapped_process = NULL,
            .mapping_base = 0,
            .mapped_address = 0,
            .mapped_page_count = 0
        };

    if (!kernel_object_initialize(
            &framebuffer_object,
            &framebuffer_operations,
            &framebuffer_context
        )) {
        return false;
    }

    framebuffer_initialized = true;
    return true;
}

bool framebuffer_object_attach(
    struct kernel_process *process
)
{
    if (!framebuffer_initialized ||
        process == NULL) {
        return false;
    }

    return kernel_process_handle_install(
        process,
        KRISHNA_HANDLE_FRAMEBUFFER,
        &framebuffer_object,
        KERNEL_HANDLE_RIGHT_IOCTL |
            KERNEL_HANDLE_RIGHT_MAP
    );
}