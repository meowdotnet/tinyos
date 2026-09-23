#ifndef KLIB_H
#define KLIB_H

/* Freestanding helpers: no libc, no libgcc-32 dependency. */

unsigned int kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, unsigned int n);
void *kmemcpy(void *dst, const void *src, unsigned int n);
void *kmemset(void *s, int c, unsigned int n);
int kmemcmp(const void *a, const void *b, unsigned int n);

void kprintf(const char *fmt, ...);

#endif
