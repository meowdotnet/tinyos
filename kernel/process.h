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
    enum process_state state;
};

/* Establishes PID 1 as the current bootstrap process. */
void process_init(void);
struct process *process_current(void);
struct process *process_schedule(void);
uint32_t process_count(void);

#endif
