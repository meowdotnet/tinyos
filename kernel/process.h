#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"

enum process_state {
    PROCESS_UNUSED,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
};

struct process {
    uint32_t pid;
    uint32_t page_directory;
    uint32_t kernel_stack;
    uint32_t user_stack;
    uint32_t kernel_stack_page;
    uint32_t user_stack_page;
    uint32_t user_code_page;
    enum process_state state;
};

/* Establishes PID 1 as the current bootstrap process. */
void process_init(void);
/*
 * Creates an address space with a supervisor-only kernel mapping and one
 * writable user stack page.  It does not yet manufacture a ring-3 context.
 */
struct process *process_create(void);
void process_destroy(struct process *process);
int process_map_user_page(struct process *process, uint32_t virtual_address,
                          uint32_t physical_address, int writable);
void process_exit_current(uint32_t status);
int process_load_builtin(struct process *process, const void *image,
                         uint32_t image_size, uint32_t entry);
void process_start(struct process *process, uint32_t entry);
void process_return_to_kernel(void);
struct process *process_current(void);
struct process *process_schedule(void);
uint32_t process_count(void);

#endif
