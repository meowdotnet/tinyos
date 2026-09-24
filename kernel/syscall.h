#ifndef SYSCALL_H
#define SYSCALL_H

#define SYS_WRITE 1u
#define SYS_READ  2u
#define SYS_EXIT  3u

struct syscall_frame {
    unsigned int edi, esi, ebp, ignored_esp, ebx, edx, ecx, eax;
    unsigned int vector, error, eip, cs, eflags, user_esp, user_ss;
};

void syscall_dispatch(struct syscall_frame *frame);

#endif
