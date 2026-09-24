#ifndef MEMORY_H
#define MEMORY_H

/* Early physical memory and paging foundations for processes. */
#define PAGE_SIZE 4096u
#define USER_VIRTUAL_BASE 0x40000000u
#define USER_VIRTUAL_LIMIT 0xc0000000u

void memory_init(void *multiboot_info, int multiboot_valid);
unsigned int memory_alloc_page(void); /* physical address, 0 when exhausted */
/* A frame returned here is eligible to be mapped with the user permission. */
unsigned int memory_alloc_user_page(void);
void memory_free_page(unsigned int physical_address);
unsigned int memory_free_pages(void);
void paging_init(void);
unsigned int paging_kernel_directory(void);
/*
 * A user directory shares the kernel's supervisor-only low-memory mapping.
 * The returned physical address is also identity-mapped for kernel use.
 */
unsigned int paging_create_user_directory(void);
void paging_destroy_user_directory(unsigned int page_directory);
void paging_switch_directory(unsigned int page_directory);
int paging_map_user_page(unsigned int page_directory,
                         unsigned int virtual_address,
                         unsigned int physical_address, int writable);
unsigned int paging_unmap_user_page(unsigned int page_directory,
                                    unsigned int virtual_address);
int paging_user_range_readable(unsigned int page_directory,
                               unsigned int virtual_address, unsigned int length);
int paging_user_range_writable(unsigned int page_directory,
                               unsigned int virtual_address, unsigned int length);

#endif
