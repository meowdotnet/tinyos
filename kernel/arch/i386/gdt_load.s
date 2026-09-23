.code32
.global gdt_load
.type gdt_load, @function
gdt_load:
    mov 4(%esp), %eax
    lgdt (%eax)
    ljmp $0x08, $gdt_loaded
gdt_loaded:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $0x28, %ax
    ltr %ax
    ret
