.code32
.global process_enter_user
.type process_enter_user, @function
process_enter_user:
    mov 4(%esp), %eax
    mov 8(%esp), %edx
    mov $0x23, %cx
    mov %cx, %ds
    mov %cx, %es
    mov %cx, %fs
    mov %cx, %gs
    pushl $0x23
    pushl %edx
    pushfl
    popl %ecx
    orl $0x200, %ecx
    pushl %ecx
    pushl $0x1b
    pushl %eax
    iret
