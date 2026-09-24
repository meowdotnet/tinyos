/* Physical load address: 1 MiB, where GRUB puts Multiboot kernels. */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

PHDRS
{
    text PT_LOAD FLAGS(5); /* read + execute */
    data PT_LOAD FLAGS(6); /* read + write */
}

SECTIONS
{
    . = 1M;

    .boot : {
        KEEP(*(.multiboot))
    } :text

    .text : {
        *(.text)
    } :text

    .rodata : {
        *(.rodata*)
        *(.user_init)
        *(.user_sh)
    } :text

    .data : {
        *(.data)
    } :data

    .bss : {
        *(COMMON)
        *(.bss)
    } :data

    kernel_end = .;
}
