#include "process.h"
#include "arch/i386/gdt.h"
#include "lib.h"
#include "memory.h"

#define PROCESS_MAX 1u

extern char stack_top;

static struct process processes[PROCESS_MAX];
static struct process *current;
static uint32_t active_processes;

static void switch_to(struct process *next)
{
    if (current == next)
        return;
    if (current && current->state == PROCESS_RUNNING)
        current->state = PROCESS_READY;

    current = next;
    current->state = PROCESS_RUNNING;
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
    current = 0;
    switch_to(bootstrap);
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
