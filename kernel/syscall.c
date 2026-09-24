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

    if (!process || process->pid == 0) {
        frame->eax = (unsigned int)-1;
        return;
    }

    /*
     * Save the interrupt frame base for this process before any deeper
     * kernel call pushes onto the same kernel stack.  process_iret_to()
     * resumes via iret at saved_esp+40, so a blocked process returns to
     * user space with the frame's eax delivered to it.
     */
    process->saved_esp = (unsigned int)frame;

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
        /* Blocking terminal read: allow IRQ1 to wake the hlt loop. */
        __asm__ volatile("sti");
        ((char *)frame->ebx)[0] = keyboard_getc();
        __asm__ volatile("cli");
        frame->eax = 1;
        return;
    case SYS_EXIT:
        /* Never returns: switches to another ready process (or halts). */
        process_exit_current(frame->ebx);
        return;
    case SYS_SPAWN: {
        struct process *child = process_spawn_builtin(frame->ebx);
        frame->eax = child ? child->pid : (unsigned int)-1;
        return;
    }
    case SYS_WAIT:
        /* Never returns to this stack; parent is resumed after the child
         * exits, with the child's status in eax. */
        process_block_and_switch();
        return;
    case SYS_CLEAR:
        vga_clear();
        frame->eax = 0;
        return;
    case SYS_SHUTDOWN:
        /*
         * ACPI soft-off. On QEMU/Bochs the PIIX4/PM registers live at I/O
         * 0x600; PM1_CNT is at 0x604. Writing SLP_EN(0x2000) with the S5
         * sleep-type requests a poweroff, which terminates the VM.
         */
        vga_puts("tinyos: powering off...\n");
        __asm__ volatile("outw %0, %1"
                         : : "a"((unsigned short)0x2000u),
                             "Nd"((unsigned short)0x0604u));
        /* Some emulators use 0xB004 for the same purpose. */
        __asm__ volatile("outw %0, %1"
                         : : "a"((unsigned short)0x2000u),
                             "Nd"((unsigned short)0xB004u));
        __asm__ volatile("cli; hlt");
        return;
    default:
        frame->eax = (unsigned int)-1;
        return;
    }
}
