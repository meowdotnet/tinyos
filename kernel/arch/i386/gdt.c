#include "arch/i386/gdt.h"

struct gdt_entry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed));

struct gdt_pointer {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

struct tss_entry {
    unsigned int prev_tss;
    unsigned int esp0;
    unsigned int ss0;
    unsigned int unused[22];
    unsigned short trap;
    unsigned short iomap_base;
} __attribute__((packed));

extern void gdt_load(struct gdt_pointer *pointer);
extern char stack_top;

static struct gdt_entry gdt[6];
static struct gdt_pointer gdt_pointer;
static struct tss_entry tss;

static void set_descriptor(unsigned int index, unsigned int base,
                           unsigned int limit, unsigned char access,
                           unsigned char granularity)
{
    gdt[index].limit_low = (unsigned short)(limit & 0xffffu);
    gdt[index].base_low = (unsigned short)(base & 0xffffu);
    gdt[index].base_middle = (unsigned char)((base >> 16) & 0xffu);
    gdt[index].access = access;
    gdt[index].granularity = (unsigned char)(((limit >> 16) & 0x0fu) |
                                              (granularity & 0xf0u));
    gdt[index].base_high = (unsigned char)((base >> 24) & 0xffu);
}

void gdt_init(void)
{
    unsigned int i;
    for (i = 0; i < sizeof(gdt); i++)
        ((unsigned char *)gdt)[i] = 0;
    for (i = 0; i < sizeof(tss); i++)
        ((unsigned char *)&tss)[i] = 0;

    set_descriptor(1, 0, 0xfffffu, 0x9au, 0xc0u);
    set_descriptor(2, 0, 0xfffffu, 0x92u, 0xc0u);
    set_descriptor(3, 0, 0xfffffu, 0xfau, 0xc0u);
    set_descriptor(4, 0, 0xfffffu, 0xf2u, 0xc0u);
    tss.esp0 = (unsigned int)&stack_top;
    tss.ss0 = 0x10u;
    tss.iomap_base = sizeof(tss);
    set_descriptor(5, (unsigned int)&tss, sizeof(tss) - 1u, 0x89u, 0x00u);

    gdt_pointer.limit = sizeof(gdt) - 1u;
    gdt_pointer.base = (unsigned int)&gdt;
    gdt_load(&gdt_pointer);
}
