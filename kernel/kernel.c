/* tinyos kernel entry: init console + keyboard, drop into shell. */

#include "graphics/vga.h"
#include "keyboard/keyboard.h"
#include "arch/i386/gdt.h"
#include "arch/i386/interrupts.h"
#include "fs.h"
#include "lib.h"
#include "memory.h"
#include "process.h"
#include "panic.h"

#define MULTIBOOT_MAGIC 0x2BADB002

extern char user_init_image[];
extern char user_init_image_end[];

void kmain(unsigned int magic, void *info)
{
    (void)info;

    vga_init();
    vga_setcolor(VGA_LIGHT_GREY, VGA_BLACK);

    if (magic != MULTIBOOT_MAGIC)
        vga_puts("boot: BAD multiboot magic!\n");

    gdt_init();
    memory_init(info, magic == MULTIBOOT_MAGIC);
    paging_init();
    process_init();

    {
        int tfs_rc = tfs_mount();
        if (tfs_rc != 0) {
            kprintf("fs: mount failed (%d)\n", tfs_rc);
        } else {
            tfs_rc = tfs_selftest();
            if (tfs_rc != 0)
                kprintf("fs: self-test failed (%d)\n", tfs_rc);
        }
    }

    keyboard_init();
    interrupts_init();
    interrupts_enable();

    {
        struct process *init = process_create();
        unsigned int image_size = (unsigned int)(user_init_image_end - user_init_image);

        if (!init || process_load_builtin(init, user_init_image, image_size,
                                          USER_VIRTUAL_BASE) != 0)
            panic("unable to start init");
        process_start(init, USER_VIRTUAL_BASE);
    }

    for (;;)
        __asm__ volatile("hlt");
}
