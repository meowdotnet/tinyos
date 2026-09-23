#include "panic.h"
#include "graphics/vga.h"

void panic(const char *reason)
{
    __asm__ volatile("cli");
    vga_setcolor(VGA_WHITE, VGA_RED);
    vga_puts("\n*** KERNEL PANIC ***\n");
    vga_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(reason ? reason : "unknown error");
    vga_putc('\n');

    for (;;)
        __asm__ volatile("hlt");
}
