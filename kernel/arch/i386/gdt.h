#ifndef I386_GDT_H
#define I386_GDT_H

/* Installs ring 0/ring 3 flat segments and a TSS for later ring transitions. */
void gdt_init(void);
void gdt_set_kernel_stack(unsigned int stack_top);

#endif
