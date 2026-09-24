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

/*
 * Field order is ABI-sensitive: process_iret_to() in
 * kernel/arch/i386/user_mode.s reads `saved_esp` at byte offset 36.
 */
struct process {
    uint32_t pid;              /*  0 */
    uint32_t parent_pid;       /*  4 */
    uint32_t page_directory;   /*  8 */
    uint32_t kernel_stack;     /* 12  top of supervisor stack */
    uint32_t user_stack;       /* 16  top of ring-3 stack */
    uint32_t kernel_stack_page;/* 20 */
    uint32_t user_stack_page;  /* 24 */
    uint32_t user_code_page;   /* 28 */
    uint32_t exit_status;      /* 32 */
    uint32_t saved_esp;        /* 36  kernel ESP while switched out */
    enum process_state state;  /* 40 */
};

typedef char process_saved_esp_offset_check[
    __builtin_offsetof(struct process, saved_esp) == 36 ? 1 : -1];

/* PID 0: kernel bootstrap context, never resumed by the scheduler. */
void process_init(void);
struct process *process_create(void);
void process_destroy(struct process *process);
int process_map_user_page(struct process *process, uint32_t virtual_address,
                          uint32_t physical_address, int writable);
int process_load_builtin(struct process *process, const void *image,
                         uint32_t image_size, uint32_t entry);
void process_start(struct process *process, uint32_t entry);

/* Create a fresh userspace process from a builtin image id. */
struct process *process_spawn_builtin(uint32_t id);

/* Cooperative context switch; never returns on the caller's stack. */
void process_exit_current(uint32_t status);
void process_block_and_switch(void);

/* Queries. */
struct process *process_current(void);
uint32_t process_count(void);

#endif
