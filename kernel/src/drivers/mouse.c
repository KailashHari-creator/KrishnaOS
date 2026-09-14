#include "drivers/mouse.h"
#include "arch/x86_64/io.h"

#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_COMMAND_PORT    0x64

#define PS2_ENABLE_MOUSE    0xA8
#define PS2_READ_CONFIG     0x20
#define PS2_WRITE_CONFIG    0x60
#define PS2_SEND_TO_MOUSE   0xD4

#define MOUSE_SET_DEFAULTS  0xF6
#define MOUSE_ENABLE        0xF4
#define MOUSE_ACK           0xFA

#define PS2_TIMEOUT         1000000

static uint8_t packet[3];
static uint8_t packet_index;


static inline void port_write8(
    uint16_t port,
    uint8_t value
) {
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}


static bool wait_for_input_buffer(void)
{
    for (uint32_t i = 0; i < PS2_TIMEOUT; i++) {
        /*
         * Bit 1 being clear means the controller is ready
         * to receive another command.
         */
        if ((io_read8(PS2_STATUS_PORT) & 0x02) == 0) {
            return true;
        }

        __asm__ volatile ("pause");
    }

    return false;
}


static bool wait_for_output_buffer(void)
{
    for (uint32_t i = 0; i < PS2_TIMEOUT; i++) {
        if ((io_read8(PS2_STATUS_PORT) & 0x01) != 0) {
            return true;
        }

        __asm__ volatile ("pause");
    }

    return false;
}


static bool controller_command(uint8_t command)
{
    if (!wait_for_input_buffer()) {
        return false;
    }

    port_write8(PS2_COMMAND_PORT, command);
    return true;
}


static bool controller_write_data(uint8_t data)
{
    if (!wait_for_input_buffer()) {
        return false;
    }

    port_write8(PS2_DATA_PORT, data);
    return true;
}


static bool mouse_command(uint8_t command)
{
    if (!controller_command(PS2_SEND_TO_MOUSE)) {
        return false;
    }

    if (!controller_write_data(command)) {
        return false;
    }

    if (!wait_for_output_buffer()) {
        return false;
    }

    return io_read8(PS2_DATA_PORT) == MOUSE_ACK;
}


bool mouse_init(void)
{
    /*
     * Discard stale controller data before initialization.
     */
    for (uint32_t i = 0; i < 32; i++) {
        if ((io_read8(PS2_STATUS_PORT) & 0x01) == 0) {
            break;
        }

        (void)io_read8(PS2_DATA_PORT);
    }

    /*
     * Enable the PS/2 controller's second port.
     */
    if (!controller_command(PS2_ENABLE_MOUSE)) {
        return false;
    }

    /*
     * Read the PS/2 controller configuration byte.
     */
    if (!controller_command(PS2_READ_CONFIG) ||
        !wait_for_output_buffer()) {
        return false;
    }

    uint8_t configuration =
        io_read8(PS2_DATA_PORT);

    /*
     * Bit 5 clear: enable the mouse clock.
     * Bit 1 clear: disable mouse interrupts because we're polling.
     */
    configuration &= ~(1u << 5);
    configuration &= ~(1u << 1);

    if (!controller_command(PS2_WRITE_CONFIG) ||
        !controller_write_data(configuration)) {
        return false;
    }

    if (!mouse_command(MOUSE_SET_DEFAULTS)) {
        return false;
    }

    if (!mouse_command(MOUSE_ENABLE)) {
        return false;
    }

    packet_index = 0;
    return true;
}


bool mouse_poll(struct mouse_event *event)
{
    uint8_t status = io_read8(PS2_STATUS_PORT);

    /*
     * No PS/2 data is waiting.
     */
    if ((status & 0x01) == 0) {
        return false;
    }

    /*
     * Bit 5 must be set, otherwise this byte belongs
     * to the keyboard.
     */
    if ((status & 0x20) == 0) {
        return false;
    }

    uint8_t value = io_read8(PS2_DATA_PORT);

    /*
     * Bit 3 is always set in the first byte of a standard
     * three-byte PS/2 mouse packet. Use it for synchronization.
     */
    if (packet_index == 0 &&
        (value & 0x08) == 0) {
        return false;
    }

    packet[packet_index++] = value;

    if (packet_index < 3) {
        return false;
    }

    packet_index = 0;

    event->left_button =
        (packet[0] & 0x01) != 0;

    event->right_button =
        (packet[0] & 0x02) != 0;

    event->middle_button =
        (packet[0] & 0x04) != 0;

    /*
     * Ignore movement when the mouse reports overflow.
     */
    if ((packet[0] & 0xC0) != 0) {
        event->delta_x = 0;
        event->delta_y = 0;
        return true;
    }

    int16_t x = packet[1];
    int16_t y = packet[2];

    if ((packet[0] & 0x10) != 0) {
        x -= 256;
    }

    if ((packet[0] & 0x20) != 0) {
        y -= 256;
    }

    event->delta_x = x;

    /*
     * PS/2 positive Y means upward, while framebuffer
     * positive Y means downward.
     */
    event->delta_y = -y;

    return true;
}