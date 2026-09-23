/* tinyos kernel entry: init VGA console, report multiboot status. */

#include "graphics/vga.h"
#include "lib.h"

#define MULTIBOOT_MAGIC 0x2BADB002

void kmain(unsigned int magic, void *info)
{
    (void)info;

    vga_init();
    vga_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("tinyos -- tiny pure-UNIX kernel\n");
    vga_setcolor(VGA_LIGHT_GREY, VGA_BLACK);

    if (magic == MULTIBOOT_MAGIC)
        vga_puts("boot: multiboot magic OK\n");
    else
        vga_puts("boot: BAD multiboot magic!\n");

    vga_puts("console: vga 80x25 ready\n");
    kprintf("klib: printf test: %d %u %x %s %c %p %%\n",
            -42, 42u, 0xC0FFEE, "ok", '!', (unsigned int)info);

    for (;;)
        __asm__ volatile("hlt");
}
