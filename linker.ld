/* Physical load address: 1 MiB, where GRUB puts Multiboot kernels. */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

SECTIONS
{
    . = 1M;

    .boot : {
        KEEP(*(.multiboot))
    }

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(COMMON)
        *(.bss)
    }

    kernel_end = .;
}
