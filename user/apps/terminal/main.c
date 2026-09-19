#include <stddef.h>
#include <stdint.h>

#include <krishna/syscall.h>

static void terminal_write(
    const char *text,
    size_t length
)
{
    (void)krishna_write(
        KRISHNA_STDOUT,
        text,
        length
    );
}

int main(void)
{
    static const char started_message[] =
        "[TERMINAL] Ring-3 terminal process started\n";

    terminal_write(
        started_message,
        sizeof(started_message) - 1
    );

    /*
     * The terminal will eventually block on its IPC input channel.
     * Until channels are connected, keep this process alive without
     * touching the framebuffer or directly consuming keyboard events.
     */
    for (;;) {
        krishna_yield();
    }
}