.code32
/* Multiboot v1 header + 32-bit entry. GRUB loads us, we validate magic. */

.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot, "a"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss, "aw", @nobits
.align 16
stack_bottom:
.skip 16384 /* 16 KiB kernel stack */
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    mov $stack_top, %esp

    /* Multiboot calling convention: EAX = magic, EBX = info struct. */
    push %ebx
    push %eax
    call kmain

    cli
.hang:
    hlt
    jmp .hang
