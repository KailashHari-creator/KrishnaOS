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

.align 16

.global krishna_terminal_icon_start
.global krishna_terminal_icon_end

krishna_terminal_icon_start:
    .incbin "../assets/terminal_icon.rgba"
krishna_terminal_icon_end:

.align 16

.global krishna_font_start
.global krishna_font_end

krishna_font_start:
    .incbin "../assets/krishna_font.kfont"
krishna_font_end:

.balign 16
.global krishna_terminal_art_start
.global krishna_terminal_art_end

krishna_terminal_art_start:
    .incbin "../assets/terminal_startup_art.rgba"
krishna_terminal_art_end:

.section .note.GNU-stack,"",@progbits