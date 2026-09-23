#include "lib.h"
#include "graphics/vga.h"
#include <stdarg.h>

unsigned int kstrlen(const char *s)
{
    unsigned int n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

int kstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)((unsigned char)*a - (unsigned char)*b);
}

int kstrncmp(const char *a, const char *b, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb || ca == '\0')
            return (int)ca - (int)cb;
    }
    return 0;
}

void *kmemcpy(void *dst, const void *src, unsigned int n)
{
    unsigned int i;
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

void *kmemset(void *s, int c, unsigned int n)
{
    unsigned int i;
    char *p = (char *)s;
    for (i = 0; i < n; i++)
        p[i] = (char)c;
    return s;
}

int kmemcmp(const void *a, const void *b, unsigned int n)
{
    unsigned int i;
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (i = 0; i < n; i++)
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    return 0;
}

/* --- minimal printf: %c %s %d %i %u %x %X %p %% --- */

static void print_unsigned(unsigned int v, unsigned int base,
                           const char *digits)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        vga_putc('0');
        return;
    }
    while (v > 0 && i < 32) {
        buf[i++] = digits[v % base];
        v /= base;
    }
    while (i-- > 0)
        vga_putc(buf[i]);
}

static void print_signed(int v)
{
    if (v < 0) {
        vga_putc('-');
        /* avoid overflow on INT_MIN: negate in unsigned domain */
        print_unsigned((unsigned int)(-(v + 1)) + 1u, 10, "0123456789");
    } else {
        print_unsigned((unsigned int)v, 10, "0123456789");
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    unsigned int i;
    va_start(ap, fmt);
    for (i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            vga_putc(fmt[i]);
            continue;
        }
        i++;
        switch (fmt[i]) {
        case '\0':
            goto out;
        case '%':
            vga_putc('%');
            break;
        case 'c':
            vga_putc((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            vga_puts(s);
            break;
        }
        case 'd':
        case 'i':
            print_signed(va_arg(ap, int));
            break;
        case 'u':
            print_unsigned(va_arg(ap, unsigned int), 10, "0123456789");
            break;
        case 'x':
            print_unsigned(va_arg(ap, unsigned int), 16, "0123456789abcdef");
            break;
        case 'X':
            print_unsigned(va_arg(ap, unsigned int), 16, "0123456789ABCDEF");
            break;
        case 'p': {
            unsigned int v = va_arg(ap, unsigned int);
            vga_puts("0x");
            print_unsigned(v, 16, "0123456789abcdef");
            break;
        }
        default:
            vga_putc('%');
            vga_putc(fmt[i]);
            break;
        }
    }
out:
    va_end(ap);
}
