#ifndef PANIC_H
#define PANIC_H

/* Print a fatal kernel error and stop the CPU. Does not return. */
void panic(const char *reason);

#endif
