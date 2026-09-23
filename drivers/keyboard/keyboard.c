#include "keyboard/keyboard.h"

#define KB_DATA 0x60
#define KB_STATUS 0x64

static int shift_held;
static int caps_on;

#define KEYBOARD_QUEUE_SIZE 128
static char queue[KEYBOARD_QUEUE_SIZE];
static unsigned int queue_head;
static unsigned int queue_tail;

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
    queue_head = 0;
    queue_tail = 0;
    /* drain stale controller output */
    while (inb(KB_STATUS) & 0x01)
        (void)inb(KB_DATA);
}

int keyboard_hasdata(void)
{
    return queue_head != queue_tail;
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static void queue_put(char c)
{
    unsigned int next = (queue_head + 1) % KEYBOARD_QUEUE_SIZE;
    if (next != queue_tail) {
        queue[queue_head] = c;
        queue_head = next;
    }
}

static void process_scancode(unsigned char code)
{
    int released = (code & 0x80) != 0;
    char c;

    code &= 0x7F;

    if (code == 0x2A || code == 0x36) {
        shift_held = !released;
        return;
    }
    if (code == 0x3A && !released) {
        caps_on = !caps_on;
        return;
    }
    if (released || code >= 128)
        return;

    c = shift_held ? kmap_shift[code] : kmap[code];
    if (c == 0)
        return;

    if (caps_on && is_alpha(c))
        c = shift_held ? (char)(c + 32) : (char)(c - 32);

    queue_put(c);
}

void keyboard_irq(void)
{
    while (inb(KB_STATUS) & 0x01)
        process_scancode(inb(KB_DATA));
}

char keyboard_getc(void)
{
    for (;;) {
        if (keyboard_hasdata()) {
            char c = queue[queue_tail];
            queue_tail = (queue_tail + 1) % KEYBOARD_QUEUE_SIZE;
            return c;
        }

        /* IRQ1 wakes us when the next key reaches the controller. */
        __asm__ volatile("hlt");
    }
}
