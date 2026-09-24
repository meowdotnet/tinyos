/* tinyos kernel entry: init console + keyboard, drop into shell. */

#include "graphics/vga.h"
#include "keyboard/keyboard.h"
#include "arch/i386/gdt.h"
#include "arch/i386/interrupts.h"
#include "lib.h"
#include "memory.h"
#include "process.h"
#include "panic.h"
#include "shell.h"

#define MULTIBOOT_MAGIC 0x2BADB002

extern char user_init_image[];
extern char user_init_image_end[];

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

    gdt_init();
    memory_init(info, magic == MULTIBOOT_MAGIC);
    paging_init();
    kprintf("memory: %u KiB free, supervisor kernel map active\n",
            memory_free_pages() * 4u);
    process_init();
    kprintf("process: bootstrap pid %u, one-slot scheduler ready\n",
            process_current()->pid);

    keyboard_init();
    interrupts_init();
    interrupts_enable();
    vga_puts("input: ps/2 keyboard ready\n");
    vga_puts("interrupts: IDT and PIC ready\n");

    {
        struct process *init = process_create();
        unsigned int image_size = (unsigned int)(user_init_image_end - user_init_image);

        if (!init || process_load_builtin(init, user_init_image, image_size,
                                          USER_VIRTUAL_BASE) != 0)
            panic("unable to start init");
        kprintf("process: starting init pid %u in ring 3\n", init->pid);
        process_start(init, USER_VIRTUAL_BASE);
    }

    for (;;)
        __asm__ volatile("hlt");
}
