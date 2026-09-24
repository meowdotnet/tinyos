.code32
.section .user_init, "a"
.global user_init_image
.global user_init_image_end
.set USER_CODE_BASE, 0x40000000
.set SYS_WRITE, 1
.set SYS_EXIT, 3

user_init_image:
    mov $SYS_WRITE, %eax
    mov $(USER_CODE_BASE + message - user_init_image), %ebx
    mov $(message_end - message), %ecx
    int $0x80
    mov $SYS_EXIT, %eax
    xor %ebx, %ebx
    int $0x80
1:  jmp 1b

message:
    .ascii "hello from userspace\n"
message_end:
user_init_image_end:
