.code32
.section .user_init, "a"
.global user_init_image
.global user_init_image_end
.set USER_CODE_BASE, 0x40000000
.set SYS_EXIT, 3
.set SYS_SPAWN, 4
.set SYS_WAIT, 5

/*
 * PID 1 userspace init.
 *
 *   /init (pid 1) -> spawns /bin/sh (pid 2)
 *                  -> waits for the child
 *                  -> respawns it when it exits
 *
 * Every operation is a syscall; there is no kernel-side shell hook.
 */
user_init_image:
    /* Spawn the initial shell child (builtin id 1). */
    movl $SYS_SPAWN, %eax
    movl $1, %ebx
    int $0x80
    cmpl $0, %eax
    jl fatal

wait_loop:
    /* Block until any child exits; returns its status in eax. */
    movl $SYS_WAIT, %eax
    int $0x80

    /* Respawn the shell unconditionally: init is the supervisor. */
    movl $SYS_SPAWN, %eax
    movl $1, %ebx
    int $0x80
    cmpl $0, %eax
    jl fatal
    jmp wait_loop

fatal:
    movl $SYS_EXIT, %eax
    xorl %ebx, %ebx
    int $0x80
1:  hlt
    jmp 1b

user_init_image_end:
