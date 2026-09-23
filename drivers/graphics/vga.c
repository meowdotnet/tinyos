#include "graphics/vga.h"

#define VGA_BASE ((volatile unsigned short *)0xB8000)

static unsigned int vga_row;
static unsigned int vga_col;
static unsigned char vga_color;

static unsigned char vga_entry_color(unsigned char fg, unsigned char bg)
{
    return (unsigned char)((bg << 4) | (fg & 0x0F));
}

static unsigned short vga_entry(char c, unsigned char color)
{
    return (unsigned short)(((unsigned short)color << 8) | (unsigned char)c);
}

static void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void vga_update_cursor(void)
{
    unsigned short pos = (unsigned short)(vga_row * VGA_WIDTH + vga_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

static void vga_scroll(void)
{
    unsigned int x, y;
    for (y = 0; y < VGA_HEIGHT - 1; y++)
        for (x = 0; x < VGA_WIDTH; x++)
            VGA_BASE[y * VGA_WIDTH + x] = VGA_BASE[(y + 1) * VGA_WIDTH + x];
    for (x = 0; x < VGA_WIDTH; x++)
        VGA_BASE[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            vga_entry(' ', vga_color);
}

void vga_init(void)
{
    vga_row = 0;
    vga_col = 0;
    vga_color = vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_clear(void)
{
    unsigned int x, y;
    for (y = 0; y < VGA_HEIGHT; y++)
        for (x = 0; x < VGA_WIDTH; x++)
            VGA_BASE[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_setcolor(unsigned char fg, unsigned char bg)
{
    vga_color = vga_entry_color(fg, bg);
}

void vga_putc(char c)
{
    if (c == '\n') {
        vga_col = 0;
        if (++vga_row == VGA_HEIGHT) {
            vga_scroll();
            vga_row = VGA_HEIGHT - 1;
        }
        vga_update_cursor();
        return;
    }
    VGA_BASE[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color);
    if (++vga_col == VGA_WIDTH) {
        vga_col = 0;
        if (++vga_row == VGA_HEIGHT) {
            vga_scroll();
            vga_row = VGA_HEIGHT - 1;
        }
    }
    vga_update_cursor();
}

void vga_puts(const char *s)
{
    unsigned int i = 0;
    while (s[i] != '\0')
        vga_putc(s[i++]);
}

void vga_write(const char *s, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++)
        vga_putc(s[i]);
}
