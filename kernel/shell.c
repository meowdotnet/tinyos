#include "shell.h"
#include "lib.h"
#include "graphics/vga.h"
#include "keyboard/keyboard.h"

#define SHELL_BUF 128

static void prompt(void)
{
    vga_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("tinyos$ ");
    vga_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
}

static void do_help(void)
{
    vga_puts("commands: help uname echo <text> clear whoami\n");
}

static void do_uname(void)
{
    vga_puts("tinyos 0.1 pure-UNIX i386\n");
}

static void do_echo(const char *args)
{
    while (*args == ' ')
        args++;
    vga_puts(args);
    vga_putc('\n');
}

static void run_line(char *line)
{
    char *args;

    while (*line == ' ')
        line++;
    if (*line == '\0')
        return;

    args = line;
    while (*args && *args != ' ')
        args++;
    if (*args == ' ') {
        *args = '\0';
        args++;
    }

    if (kstrcmp(line, "help") == 0)
        do_help();
    else if (kstrcmp(line, "uname") == 0)
        do_uname();
    else if (kstrcmp(line, "echo") == 0)
        do_echo(args);
    else if (kstrcmp(line, "clear") == 0)
        vga_clear();
    else if (kstrcmp(line, "whoami") == 0)
        vga_puts("root\n");
    else {
        vga_puts("sh: unknown command: ");
        vga_puts(line);
        vga_putc('\n');
    }
}

void shell_run(void)
{
    char buf[SHELL_BUF];
    unsigned int len = 0;

    vga_puts("sh: type 'help'\n");
    prompt();

    for (;;) {
        char c = keyboard_getc();
        if (c == '\n') {
            vga_putc('\n');
            buf[len] = '\0';
            run_line(buf);
            len = 0;
            prompt();
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                vga_backspace();
            }
        } else if (len < SHELL_BUF - 1) {
            buf[len++] = c;
            vga_putc(c);
        }
    }
}
