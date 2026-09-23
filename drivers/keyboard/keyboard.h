#ifndef KEYBOARD_H
#define KEYBOARD_H

/* PS/2 keyboard, polling, US layout, scancode set 1. */

void keyboard_init(void);
int keyboard_hasdata(void); /* 1 if a scancode is waiting */
char keyboard_getc(void);   /* blocking: next ASCII char, 0 = non-printable */

#endif
