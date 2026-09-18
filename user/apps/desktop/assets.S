.section .rodata
.align 16

.global krishna_wallpaper_start
.global krishna_wallpaper_end

krishna_wallpaper_start:
    .incbin "../assets/krishna_wallpaper.rgba"

krishna_wallpaper_end:

.align 16

.global krishna_cursor_start
.global krishna_cursor_end

krishna_cursor_start:
    .incbin "../assets/krishna_cursor.rgba"

krishna_cursor_end:

.section .note.GNU-stack,"",@progbits