#ifndef MEMORY_H
#define MEMORY_H

/* Early physical memory and paging foundations for future processes. */
void memory_init(void *multiboot_info, int multiboot_valid);
unsigned int memory_alloc_page(void); /* physical address, 0 when exhausted */
void memory_free_page(unsigned int physical_address);
unsigned int memory_free_pages(void);
void paging_init(void);
unsigned int paging_kernel_directory(void);

#endif
