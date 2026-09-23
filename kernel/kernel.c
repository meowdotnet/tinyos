/* tinyos kernel entry: validate Multiboot magic, poke VGA directly.
 * A real console driver lands in drivers/graphics/ next commit. */

#define MULTIBOOT_MAGIC 0x2BADB002
#define VGA_BASE ((volatile unsigned short *)0xB8000)
#define VGA_CELLS (80 * 25)

void kmain(unsigned int magic, void *info)
{
    const char *msg;
    unsigned int i;
    (void)info;

    if (magic == MULTIBOOT_MAGIC)
        msg = "tinyos: booted via multiboot [OK]";
    else
        msg = "tinyos: BAD multiboot magic [!]";

    for (i = 0; i < VGA_CELLS; i++)
        VGA_BASE[i] = (unsigned short)(0x0F00 | ' ');

    for (i = 0; msg[i] != '\0'; i++)
        VGA_BASE[i] = (unsigned short)(0x0F00 | msg[i]);

    for (;;)
        __asm__ volatile("hlt");
}
