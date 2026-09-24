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

/*
 * process_iret_to(struct process *next)
 *
 * Resume `next` by loading the kernel ESP it saved while switched out and
 * executing iret.  For a fresh process, saved_esp points at a synthetic
 * syscall_frame whose iret half (eip/cs/eflags/user_esp/user_ss) hands
 * control to ring 3 directly.  For a blocked process, saved_esp points at
 * the real interrupt frame left by int $0x80, so iret resumes it with the
 * delivered eax in place.  This function never returns.
 *
 * struct process layout (see process.h):
 *   +36 = saved_esp
 */
.global process_iret_to
.type process_iret_to, @function
process_iret_to:
    movl 4(%esp), %eax
    movl 36(%eax), %edx      /* next->saved_esp: address of the frame base */

    /* Load the ring-3 flat data selectors before returning to user mode.
     * iret restores cs/ss/eflags/esp/eip but not ds/es/fs/gs. */
    movl $0x23, %ecx
    movl %ecx, %ds
    movl %ecx, %es
    movl %ecx, %fs
    movl %ecx, %gs

    movl %edx, %esp
    /* Replicate interrupt_common's return path so GPRs (incl. eax) and the
     * CPU context are restored: popa, skip vector+error, iret. */
    popa
    addl $8, %esp
    iret
