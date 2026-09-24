#include "arch/i386/interrupts.h"
#include "graphics/vga.h"
#include "keyboard/keyboard.h"
#include "lib.h"
#include "syscall.h"

#define IDT_ENTRIES 256u
#define PIC1_COMMAND 0x20u
#define PIC1_DATA 0x21u
#define PIC2_COMMAND 0xa0u
#define PIC2_DATA 0xa1u
#define PIC_EOI 0x20u

struct idt_entry {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char type_attr;
    unsigned short offset_high;
} __attribute__((packed));

struct idt_pointer {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

extern void *interrupt_stub_table[];
extern void syscall_stub(void);
extern void idt_load(struct idt_pointer *pointer);

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_pointer idt_pointer;

static void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void set_gate(unsigned int vector, unsigned int handler,
                     unsigned char type_attr)
{
    idt[vector].offset_low = (unsigned short)(handler & 0xffffu);
    idt[vector].selector = 0x08u;
    idt[vector].zero = 0;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_high = (unsigned short)(handler >> 16);
}

static void pic_init(void)
{
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    outb(PIC1_DATA, 0xfdu); /* unmask IRQ1 only */
    outb(PIC2_DATA, 0xffu);
}

void interrupts_init(void)
{
    unsigned int i;
    for (i = 0; i < IDT_ENTRIES; i++)
        set_gate(i, (unsigned int)interrupt_stub_table[0], 0x8eu);
    for (i = 0; i < 48u; i++)
        set_gate(i, (unsigned int)interrupt_stub_table[i], 0x8eu);
    set_gate(0x80u, (unsigned int)syscall_stub, 0xeeu);
    idt_pointer.limit = sizeof(idt) - 1u;
    idt_pointer.base = (unsigned int)&idt;
    idt_load(&idt_pointer);
    pic_init();
}

void interrupts_enable(void)
{
    __asm__ volatile("sti");
}

void interrupts_disable(void)
{
    __asm__ volatile("cli");
}

void interrupt_dispatch(unsigned int vector, struct syscall_frame *frame)
{
    if (vector == 0x80u) {
        syscall_dispatch(frame);
        return;
    }
    if (vector == 0x21u) {
        keyboard_irq();
        outb(PIC1_COMMAND, PIC_EOI);
        return;
    }
    if (vector >= 0x20u && vector < 0x30u) {
        if (vector >= 0x28u)
            outb(PIC2_COMMAND, PIC_EOI);
        outb(PIC1_COMMAND, PIC_EOI);
        return;
    }

    interrupts_disable();
    vga_setcolor(VGA_LIGHT_RED, VGA_BLACK);
    kprintf("fatal exception: vector %u\n", vector);
    for (;;)
        __asm__ volatile("hlt");
}
