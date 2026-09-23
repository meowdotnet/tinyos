#ifndef I386_GDT_H
#define I386_GDT_H

/* Installs ring 0/ring 3 flat segments and a TSS for later ring transitions. */
void gdt_init(void);

#endif
