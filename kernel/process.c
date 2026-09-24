#include "process.h"
#include "arch/i386/gdt.h"
#include "lib.h"
#include "memory.h"
#include "syscall.h"

#define PROCESS_MAX 8u
#define USER_STACK_TOP 0x80000000u
#define BUILTIN_SHELL 1u

extern char stack_top;
extern void process_iret_to(struct process *next);

extern char user_sh_image[];
extern char user_sh_image_end[];

static struct process processes[PROCESS_MAX];
static struct process *current;
static uint32_t active_processes;
static uint32_t next_pid;

static struct process *find_by_pid(uint32_t pid)
{
    unsigned int i;
    for (i = 1; i < PROCESS_MAX; i++)
        if (processes[i].state != PROCESS_UNUSED && processes[i].pid == pid)
            return &processes[i];
    return 0;
}

static struct process *pick_ready(void)
{
    unsigned int i;
    unsigned int start = current ? (unsigned int)(current - processes) + 1u : 1u;

    for (i = 0; i < PROCESS_MAX; i++) {
        struct process *candidate = &processes[(start + i) % PROCESS_MAX];
        if (candidate == &processes[0])
            continue;
        if (candidate->state == PROCESS_READY)
            return candidate;
    }
    return 0;
}

/* Build a minimal ring-3 return frame at the top of a fresh kernel stack. */
static void prepare_user_frame(struct process *p, uint32_t entry)
{
    struct syscall_frame *f =
        (struct syscall_frame *)(p->kernel_stack_page + PAGE_SIZE - sizeof(*f));

    kmemset(f, 0, sizeof(*f));
    f->eip = entry;
    f->cs = 0x1bu;
    f->eflags = 0x202u;
    f->user_esp = p->user_stack;
    f->user_ss = 0x23u;
    p->saved_esp = (uint32_t)f;
}

void process_init(void)
{
    struct process *bootstrap = &processes[0];

    kmemset(processes, 0, sizeof(processes));
    bootstrap->pid = 0;
    bootstrap->page_directory = paging_kernel_directory();
    bootstrap->kernel_stack = (uint32_t)&stack_top;
    bootstrap->state = PROCESS_RUNNING;
    current = bootstrap;
    active_processes = 1;
    next_pid = 1;
}

void process_reap_children(void)
{
    unsigned int i;
    if (!current)
        return;
    for (i = 1; i < PROCESS_MAX; i++) {
        struct process *z = &processes[i];
        if (z->state == PROCESS_TERMINATED && z->parent_pid == current->pid &&
            !(current->pending_wait_valid &&
              z->pid == current->pending_wait_pid))
            process_destroy(z);
    }
}

struct process *process_create(void)
{
    struct process *p = 0;
    unsigned int i;
    unsigned int dir, ksp, usp;
    int stack_mapped = 0;

    /* Reap our own terminated children so respawns reuse their slots. */
    process_reap_children();
    for (i = 1; i < PROCESS_MAX; i++) {
        if (processes[i].state == PROCESS_UNUSED) {
            p = &processes[i];
            break;
        }
    }
    if (!p)
        return 0;

    dir = paging_create_user_directory();
    if (!dir)
        return 0;
    ksp = memory_alloc_page();
    usp = memory_alloc_user_page();
    if (!ksp || !usp)
        goto fail;
    if (paging_map_user_page(dir, USER_STACK_TOP - PAGE_SIZE, usp, 1) != 0)
        goto fail;
    stack_mapped = 1;

    kmemset((void *)ksp, 0, PAGE_SIZE);
    kmemset((void *)usp, 0, PAGE_SIZE);
    kmemset(p, 0, sizeof(*p));
    p->pid = next_pid++;
    p->parent_pid = current ? current->pid : 0u;
    p->page_directory = dir;
    p->kernel_stack_page = ksp;
    p->kernel_stack = ksp + PAGE_SIZE;
    p->user_stack_page = usp;
    p->user_stack = USER_STACK_TOP;
    p->state = PROCESS_BLOCKED;
    p->uid = 0;
    p->gid = 0;
    tfs_init_process_fds(p->open_files);
    prepare_user_frame(p, USER_VIRTUAL_BASE);
    active_processes++;
    return p;

fail:
    if (!stack_mapped && usp)
        memory_free_page(usp);
    if (ksp)
        memory_free_page(ksp);
    paging_destroy_user_directory(dir);
    return 0;
}

void process_destroy(struct process *p)
{
    if (!p || p == &processes[0] || p == current || p->state == PROCESS_UNUSED)
        return;
    if (p->user_stack_page) {
        paging_unmap_user_page(p->page_directory, p->user_stack - PAGE_SIZE);
        memory_free_page(p->user_stack_page);
    }
    if (p->kernel_stack_page)
        memory_free_page(p->kernel_stack_page);
    if (p->user_code_page) {
        paging_unmap_user_page(p->page_directory, USER_VIRTUAL_BASE);
        memory_free_page(p->user_code_page);
    }
    paging_destroy_user_directory(p->page_directory);
    kmemset(p, 0, sizeof(*p));
    active_processes--;
}

int process_map_user_page(struct process *p, uint32_t virtual,
                          uint32_t physical, int writable)
{
    if (!p || p->state == PROCESS_UNUSED)
        return -1;
    return paging_map_user_page(p->page_directory, virtual, physical, writable);
}

int process_load_builtin(struct process *p, const void *image,
                         uint32_t image_size, uint32_t entry)
{
    unsigned int page;

    if (!p || !image || !image_size || image_size > PAGE_SIZE ||
        entry != USER_VIRTUAL_BASE || p->user_code_page)
        return -1;
    page = memory_alloc_user_page();
    if (!page)
        return -1;
    kmemset((void *)page, 0, PAGE_SIZE);
    kmemcpy((void *)page, image, image_size);
    if (process_map_user_page(p, USER_VIRTUAL_BASE, page, 0) != 0) {
        memory_free_page(page);
        return -1;
    }
    p->user_code_page = page;
    return 0;
}

static void switch_to(struct process *next, int deliver, uint32_t status)
{
    if (next->pending_wait_valid && next->saved_esp) {
        ((struct syscall_frame *)next->saved_esp)->eax =
            next->pending_wait_status;
        next->pending_wait_pid = 0;
        next->pending_wait_valid = 0;
    } else if (deliver && next->saved_esp) {
        ((struct syscall_frame *)next->saved_esp)->eax = status;
    }
    current = next;
    next->state = PROCESS_RUNNING;
    paging_switch_directory(next->page_directory);
    gdt_set_kernel_stack(next->kernel_stack);
    process_iret_to(next);
    __builtin_unreachable();
}

/* Enter the very first userspace process; the bootstrap context is retired. */
void process_start(struct process *p, uint32_t entry)
{
    if (!p)
        return;
    prepare_user_frame(p, entry);
    p->state = PROCESS_READY;
    if (processes[0].state != PROCESS_UNUSED) {
        processes[0].state = PROCESS_UNUSED;
        if (active_processes)
            active_processes--;
    }
    switch_to(p, 0, 0);
    __builtin_unreachable();
}

struct process *process_spawn_builtin(uint32_t id)
{
    struct process *p;
    const void *img;
    uint32_t size;

    if (id != BUILTIN_SHELL || !current || current == &processes[0])
        return 0;
    img = user_sh_image;
    size = (uint32_t)(user_sh_image_end - user_sh_image);
    p = process_create();
    if (!p)
        return 0;
    if (process_load_builtin(p, img, size, USER_VIRTUAL_BASE) != 0) {
        process_destroy(p);
        return 0;
    }
    p->state = PROCESS_READY;
    return p;
}

void process_exit_current(uint32_t status)
{
    struct process *parent;
    struct process *next;

    if (!current || current == &processes[0])
        goto halt;

    current->exit_status = status;
    current->state = PROCESS_TERMINATED;
    parent = find_by_pid(current->parent_pid);
    if (parent) {
        parent->pending_wait_status = status;
        parent->pending_wait_pid = current->pid;
        parent->pending_wait_valid = 1;
        if (parent->state == PROCESS_BLOCKED)
            parent->state = PROCESS_READY;
    }
    next = pick_ready();
    if (!next)
        goto halt;
    switch_to(next, next == parent, status);
    __builtin_unreachable();

halt:
    for (;;)
        __asm__ volatile("hlt");
}

static int has_child(const struct process *parent)
{
    unsigned int i;
    for (i = 1; i < PROCESS_MAX; i++) {
        if (processes[i].state != PROCESS_UNUSED &&
            processes[i].parent_pid == parent->pid)
            return 1;
    }
    return 0;
}

int process_block_and_switch(void)
{
    struct process *next;

    if (!current || current == &processes[0])
        return -1;
    if (current->pending_wait_valid) {
        struct process *done = find_by_pid(current->pending_wait_pid);
        uint32_t status = current->pending_wait_status;
        current->pending_wait_pid = 0;
        current->pending_wait_valid = 0;
        if (done && done->state == PROCESS_TERMINATED)
            process_destroy(done);
        if (current->saved_esp)
            ((struct syscall_frame *)current->saved_esp)->eax = status;
        return 0;
    }
    if (!has_child(current))
        return -1;
    current->state = PROCESS_BLOCKED;
    next = pick_ready();
    if (!next)
        goto halt;
    switch_to(next, 0, 0);
    __builtin_unreachable();

halt:
    for (;;)
        __asm__ volatile("hlt");
}

struct process *process_current(void)
{
    return current;
}

uint32_t process_count(void)
{
    return active_processes;
}
