#include "keyboard/keyboard.h"

#define KB_DATA 0x60
#define KB_STATUS 0x64

static int shift_held;
static int caps_on;

static unsigned char inb(unsigned short port)
{
    unsigned char v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* Scancode set 1, US. Index = press code 0x00-0x7F. */
static const char kmap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0,
};

static const char kmap_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0,
};

void keyboard_init(void)
{
    shift_held = 0;
    caps_on = 0;
    /* drain stale controller output */
    while (inb(KB_STATUS) & 0x01)
        (void)inb(KB_DATA);
}

int keyboard_hasdata(void)
{
    return (inb(KB_STATUS) & 0x01) != 0;
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char keyboard_getc(void)
{
    for (;;) {
        unsigned char code;
        int released;
        char c;

        /* No IRQ handler/IDT is installed yet, so HLT would never wake. */
        while (!keyboard_hasdata())
            __asm__ volatile("pause");

        code = inb(KB_DATA);
        released = (code & 0x80) != 0;
        code &= 0x7F;

        /* modifier tracking */
        if (code == 0x2A || code == 0x36) {
            shift_held = !released;
            continue;
        }
        if (code == 0x3A && !released) {
            caps_on = !caps_on;
            continue;
        }
        if (released)
            continue;
        if (code >= 128)
            continue;

        c = shift_held ? kmap_shift[code] : kmap[code];
        if (c == 0)
            continue; /* ctrl/alt/F-keys: swallow for now */

        /* caps lock flips alpha only */
        if (caps_on && is_alpha(c))
            c = shift_held ? (char)(c + 32) : (char)(c - 32);

        return c;
    }
}
