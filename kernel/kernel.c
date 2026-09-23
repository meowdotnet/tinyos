/* tinyos kernel entry: init console + keyboard, drop into shell. */

#include "graphics/vga.h"
#include "keyboard/keyboard.h"
#include "lib.h"
#include "shell.h"

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

    kprintf("boot: info=%p\n", (unsigned int)info);

    keyboard_init();
    vga_puts("input: ps/2 keyboard ready\n");

    shell_run();

    for (;;)
        __asm__ volatile("hlt");
}
