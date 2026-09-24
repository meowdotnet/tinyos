#include "syscall.h"
#include "fs.h"
#include "graphics/vga.h"
#include "keyboard/keyboard.h"
#include "memory.h"
#include "process.h"

#define SYSCALL_IO_MAX 512u

/* Resolve a NUL-terminated user path into a kernel buffer; returns length+1 or
 * 0 on failure (invalid pointer, unmapped, too long, or not NUL-terminated). */
static unsigned int copy_path_in(struct process *p, unsigned int user_ptr,
                                 char *out, unsigned int max)
{
    unsigned int n;

    if (!p || !out || max < 2u ||
        !paging_user_range_readable(p->page_directory, user_ptr, 1))
        return 0;
    for (n = 0; n + 1u < max; n++) {
        if (!paging_user_range_readable(p->page_directory, user_ptr + n, 1))
            return 0;
        out[n] = ((const char *)user_ptr)[n];
        if (out[n] == '\0')
            return n + 1u;
    }
    return 0;
}

static struct tfs_open_file *fd_lookup(struct process *p, unsigned int fd)
{
    if (fd >= TFS_MAX_OPEN)
        return 0;
    return &p->open_files[fd];
}

static int fd_alloc(struct process *p)
{
    unsigned int i;
    for (i = 0; i < TFS_MAX_OPEN; i++) {
        if (!p->open_files[i].in_use)
            return (int)i;
    }
    return TFS_EMFILE;
}

void syscall_dispatch(struct syscall_frame *frame)
{
    struct process *process = process_current();
    unsigned int i;

    if (!process || process->pid == 0) {
        frame->eax = (unsigned int)-1;
        return;
    }

    process_reap_children();

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
        if (frame->ecx > SYSCALL_IO_MAX ||
            (frame->ecx &&
             !paging_user_range_writable(process->page_directory, frame->ebx,
                                         frame->ecx))) {
            frame->eax = (unsigned int)-1;
            return;
        }
        if (frame->ecx == 0) {
            frame->eax = 0;
            return;
        }
        /* Blocking terminal read: allow IRQ1 to wake each hlt loop. */
        __asm__ volatile("sti");
        for (i = 0; i < frame->ecx; i++)
            ((char *)frame->ebx)[i] = keyboard_getc();
        __asm__ volatile("cli");
        frame->eax = frame->ecx;
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
        /* On success this never returns to this stack; the saved frame resumes
         * with the child's status. A process without children gets an error. */
        if (process_block_and_switch() < 0)
            frame->eax = (unsigned int)-1;
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
        __asm__ volatile("outw %0, %1"
                         : : "a"((unsigned short)0x2000u),
                             "Nd"((unsigned short)0xB004u));
        __asm__ volatile("cli; hlt");
        return;

    /* ---- TinyFS ---- */
    case SYS_OPEN: {
        char path[TFS_PATH_MAX + 1u];
        int fd, rc;
        if (!copy_path_in(process, frame->ebx, path, sizeof(path))) {
            frame->eax = (unsigned int)TFS_EINVAL;
            return;
        }
        fd = fd_alloc(process);
        if (fd < 0) {
            frame->eax = (unsigned int)TFS_EMFILE;
            return;
        }
        rc = tfs_open(path, frame->ecx, &process->open_files[fd]);
        if (rc != TFS_OK) {
            process->open_files[fd].in_use = 0;
            frame->eax = (unsigned int)rc;
            return;
        }
        frame->eax = (unsigned int)fd;
        return;
    }
    case SYS_CLOSE: {
        struct tfs_open_file *f = fd_lookup(process, frame->ebx);
        if (!f || !f->in_use) {
            frame->eax = (unsigned int)TFS_EBADF;
            return;
        }
        frame->eax = (unsigned int)tfs_close(f);
        return;
    }
    case SYS_FREAD: {
        struct tfs_open_file *f = fd_lookup(process, frame->ebx);
        uint32_t n = 0;
        int rc;
        if (!f || !f->in_use) {
            frame->eax = (unsigned int)TFS_EBADF;
            return;
        }
        if (frame->edx > TFS_BLOCK_SIZE * TFS_DIRECT_BLOCKS ||
            !paging_user_range_writable(process->page_directory, frame->ecx,
                                        frame->edx)) {
            frame->eax = (unsigned int)TFS_EINVAL;
            return;
        }
        rc = tfs_read(f, (void *)frame->ecx, frame->edx, &n);
        frame->eax = (rc == TFS_OK) ? n : (unsigned int)rc;
        return;
    }
    case SYS_FWRITE: {
        struct tfs_open_file *f = fd_lookup(process, frame->ebx);
        uint32_t n = 0;
        int rc;
        if (!f || !f->in_use) {
            frame->eax = (unsigned int)TFS_EBADF;
            return;
        }
        if (frame->edx > TFS_BLOCK_SIZE * TFS_DIRECT_BLOCKS ||
            !paging_user_range_readable(process->page_directory, frame->ecx,
                                        frame->edx)) {
            frame->eax = (unsigned int)TFS_EINVAL;
            return;
        }
        rc = tfs_write(f, (const void *)frame->ecx, frame->edx, &n);
        if (rc == TFS_OK || (rc == TFS_ENOSPC && n != 0))
            frame->eax = n;
        else
            frame->eax = (unsigned int)rc;
        return;
    }
    case SYS_MKDIR: {
        char path[TFS_PATH_MAX + 1u];
        if (!copy_path_in(process, frame->ebx, path, sizeof(path))) {
            frame->eax = (unsigned int)TFS_EINVAL;
            return;
        }
        frame->eax = (unsigned int)tfs_mkdir(path, frame->ecx);
        return;
    }
    case SYS_READDIR: {
        struct tfs_open_file *f = fd_lookup(process, frame->ebx);
        uint32_t n = 0;
        int rc;
        if (!f || !f->in_use) {
            frame->eax = (unsigned int)TFS_EBADF;
            return;
        }
        if (frame->edx < 8u ||
            !paging_user_range_writable(process->page_directory, frame->ecx,
                                        frame->edx)) {
            frame->eax = (unsigned int)TFS_EINVAL;
            return;
        }
        rc = tfs_readdir(f, (void *)frame->ecx, frame->edx, &n);
        frame->eax = (rc == TFS_OK) ? n : (unsigned int)rc;
        return;
    }
    case SYS_STAT: {
        char path[TFS_PATH_MAX + 1u];
        struct tfs_stat st;
        int rc;
        if (!copy_path_in(process, frame->ebx, path, sizeof(path))) {
            frame->eax = (unsigned int)TFS_EINVAL;
            return;
        }
        if (!paging_user_range_writable(process->page_directory, frame->ecx,
                                        sizeof(st))) {
            frame->eax = (unsigned int)TFS_EINVAL;
            return;
        }
        rc = tfs_stat(path, &st);
        if (rc == TFS_OK)
            *(struct tfs_stat *)frame->ecx = st;
        frame->eax = (unsigned int)rc;
        return;
    }
    case SYS_LSEEK: {
        struct tfs_open_file *f = fd_lookup(process, frame->ebx);
        if (!f || !f->in_use) {
            frame->eax = (unsigned int)TFS_EBADF;
            return;
        }
        frame->eax = (unsigned int)tfs_lseek(f, (int)frame->ecx, (int)frame->edx);
        return;
    }
    default:
        frame->eax = (unsigned int)-1;
        return;
    }
}
