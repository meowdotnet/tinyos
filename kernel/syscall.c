#include "syscall.h"
#include "graphics/vga.h"
#include "keyboard/keyboard.h"
#include "memory.h"
#include "process.h"

#define SYSCALL_IO_MAX 512u

void syscall_dispatch(struct syscall_frame *frame)
{
    struct process *process = process_current();
    unsigned int i;

    if (!process) {
        frame->eax = (unsigned int)-1;
        return;
    }
    switch (frame->eax) {
    case SYS_WRITE:
        if (frame->ecx > SYSCALL_IO_MAX ||
            !paging_user_range_readable(process->page_directory, frame->ebx,
                                        frame->ecx)) {
            frame->eax = (unsigned int)-1;
            return;
        }
        for (i = 0; i < frame->ecx; i++)
            vga_putc(((const char *)frame->ebx)[i]);
        frame->eax = frame->ecx;
        return;
    case SYS_READ:
        if (frame->ecx == 0 || frame->ecx > SYSCALL_IO_MAX ||
            !paging_user_range_readable(process->page_directory, frame->ebx,
                                        frame->ecx)) {
            frame->eax = (unsigned int)-1;
            return;
        }
        ((char *)frame->ebx)[0] = keyboard_getc();
        frame->eax = 1;
        return;
    case SYS_EXIT:
        process_exit_current(frame->ebx);
        frame->eax = 0;
        return;
    default:
        frame->eax = (unsigned int)-1;
        return;
    }
}
