#ifndef SYSCALL_H
#define SYSCALL_H

#define SYS_WRITE 1u
#define SYS_READ  2u
#define SYS_EXIT  3u
#define SYS_SPAWN 4u
#define SYS_WAIT  5u
#define SYS_CLEAR 6u
#define SYS_SHUTDOWN 7u

/*
 * This struct must exactly match the stack frame built by
 * interrupt_common (interrupt_stubs.s).  Offsets from the frame base:
 *   0 edi, 4 esi, 8 ebp, 12 esp, 16 ebx, 20 edx, 24 ecx, 28 eax,
 *   32 vector, 36 error, 40 eip, 44 cs, 48 eflags, 52 user_esp, 56 user_ss
 * The iret half begins at offset 40.
 */
struct syscall_frame {
    unsigned int edi, esi, ebp, ignored_esp, ebx, edx, ecx, eax;
    unsigned int vector, error, eip, cs, eflags, user_esp, user_ss;
};

void syscall_dispatch(struct syscall_frame *frame);

#endif
