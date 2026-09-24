#include "process.h"
#include "arch/i386/gdt.h"
#include "lib.h"
#include "memory.h"

#define PROCESS_MAX 8u
#define USER_STACK_TOP 0x80000000u

extern char stack_top;

static struct process processes[PROCESS_MAX];
static struct process *current;
static uint32_t active_processes;
static uint32_t next_pid;

static void switch_to(struct process *next)
{
    if (current == next)
        return;
    if (current && current->state == PROCESS_RUNNING)
        current->state = PROCESS_READY;

    current = next;
    current->state = PROCESS_RUNNING;
    paging_switch_directory(current->page_directory);
    gdt_set_kernel_stack(current->kernel_stack);
}

void process_init(void)
{
    struct process *bootstrap = &processes[0];

    kmemset(processes, 0, sizeof(processes));
    bootstrap->pid = 1;
    bootstrap->page_directory = paging_kernel_directory();
    bootstrap->kernel_stack = (uint32_t)&stack_top;
    bootstrap->user_stack = 0;
    bootstrap->state = PROCESS_READY;
    active_processes = 1;
    next_pid = 2;
    current = 0;
    switch_to(bootstrap);
}

struct process *process_create(void)
{
    struct process *process = 0;
    unsigned int i;
    unsigned int directory;
    unsigned int kernel_stack_page;
    unsigned int user_stack_page;

    for (i = 0; i < PROCESS_MAX; i++) {
        if (processes[i].state == PROCESS_UNUSED) {
            process = &processes[i];
            break;
        }
    }
    if (!process)
        return 0;

    directory = paging_create_user_directory();
    if (!directory)
        return 0;
    kernel_stack_page = memory_alloc_page();
    user_stack_page = memory_alloc_user_page();
    if (!kernel_stack_page || !user_stack_page)
        goto fail;
    if (paging_map_user_page(directory, USER_STACK_TOP - PAGE_SIZE,
                             user_stack_page, 1) != 0)
        goto fail;

    kmemset((void *)kernel_stack_page, 0, PAGE_SIZE);
    kmemset((void *)user_stack_page, 0, PAGE_SIZE);
    kmemset(process, 0, sizeof(*process));
    process->pid = next_pid++;
    process->page_directory = directory;
    process->kernel_stack_page = kernel_stack_page;
    process->kernel_stack = kernel_stack_page + PAGE_SIZE;
    process->user_stack_page = user_stack_page;
    process->user_stack = USER_STACK_TOP;
    /* Context setup comes with the first user-mode entry implementation. */
    process->state = PROCESS_BLOCKED;
    active_processes++;
    return process;

fail:
    if (user_stack_page)
        memory_free_page(user_stack_page);
    if (kernel_stack_page)
        memory_free_page(kernel_stack_page);
    paging_destroy_user_directory(directory);
    return 0;
}

void process_destroy(struct process *process)
{
    if (!process || process == &processes[0] || process == current ||
        process->state == PROCESS_UNUSED)
        return;
    if (process->user_stack_page) {
        paging_unmap_user_page(process->page_directory,
                               process->user_stack - PAGE_SIZE);
        memory_free_page(process->user_stack_page);
    }
    if (process->kernel_stack_page)
        memory_free_page(process->kernel_stack_page);
    paging_destroy_user_directory(process->page_directory);
    kmemset(process, 0, sizeof(*process));
    active_processes--;
}

int process_map_user_page(struct process *process, uint32_t virtual_address,
                          uint32_t physical_address, int writable)
{
    if (!process || process->state == PROCESS_UNUSED)
        return -1;
    return paging_map_user_page(process->page_directory, virtual_address,
                                physical_address, writable);
}

struct process *process_current(void)
{
    return current;
}

struct process *process_schedule(void)
{
    unsigned int i;

    if (current && current->state == PROCESS_RUNNING)
        return current;
    for (i = 0; i < PROCESS_MAX; i++) {
        if (processes[i].state == PROCESS_READY) {
            switch_to(&processes[i]);
            return current;
        }
    }
    return 0;
}

uint32_t process_count(void)
{
    return active_processes;
}
