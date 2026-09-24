# tinyos

tinyos is a small, freestanding 32-bit x86 kernel with a UNIX-inspired layout
and interactive shell. It boots through the Multiboot v1 protocol and currently
provides VGA text output, an interrupt-driven PS/2 keyboard driver, basic kernel
helpers, and shell builtins. It is an educational starting point, not a POSIX
system: there is no userspace, filesystem, process model, or system-call
interface yet.

## Build

The only required build tools are GCC with 32-bit freestanding code generation
and GNU binutils (`ld`). No 32-bit libc or NASM is needed.

```sh
make
```

The kernel ELF is written to `build/tinyos.elf`. To create a bootable ISO, install
GRUB's `grub-mkrescue` and `xorriso`, then run `make iso`. To run it directly in
QEMU, install `qemu-system-i386` and run `make qemu`.

The convenience launcher builds the kernel and starts it in QEMU's graphical
display by default. This preserves the standard black VGA background instead
of inheriting a terminal theme's ANSI black. Use `--terminal` for QEMU's curses
display:

```sh
./run.sh
./run.sh --terminal
```

## Layout

- `boot/` — Multiboot entry point and initial stack
- `kernel/` — kernel entry, freestanding helpers, physical-page allocator,
  protected paging, process manager (context switching, spawn/wait/exit),
  x86 descriptor/interrupt setup, syscall dispatch, and panic handler
- `user/` — userspace images: `init.s` (PID 1 supervisor) and `sh.s` (the
  ring-3 interactive shell), both linked to run at `0x40000000`
- `drivers/graphics/` — VGA text-mode console
- `drivers/keyboard/` — PS/2 keyboard (US scancode set 1)
- `linker.ld`, `grub.cfg`, `Makefile` — ELF layout and build/boot setup

After boot the kernel reports memory and interrupt setup, then a genuine
userspace process tree takes over:

```
kernel
  └─ PID 1  /init        (ring 3, user image, at 0x40000000)
       └─ PID 2  /bin/sh (ring 3, user image)
```

`/init` is a real userspace process, not a kernel hook. It calls `SYS_SPAWN` to
create `/bin/sh`, then `SYS_WAIT`. When the shell calls `SYS_EXIT`, the kernel
switches back to `/init`, `SYS_WAIT` returns the child's status, and `/init`
respawns a fresh shell. There is no special "and now somehow we get a shell"
path in the kernel; the interactive shell (`help`, `uname`, `whoami`, `echo`,
`clear`, `shutdown`, `exit`) is `/bin/sh` running entirely in ring 3 and talking
to the kernel only through `int $0x80` (`SYS_WRITE`/`SYS_READ`/`SYS_EXIT`/
`SYS_SPAWN`/`SYS_WAIT`/`SYS_CLEAR`/`SYS_SHUTDOWN`). Backspace is handled by the
shell's line editor, which emits a `"\b \b"` erase sequence through `SYS_WRITE`;
the VGA text driver interprets `\b` by blanking the previous cell and stepping
the cursor back. `shutdown` requests an ACPI soft-off (S5) by writing the
sleep-enable value to the QEMU/Bochs PM1 control port (`0x604`), which powers
off the emulator and closes its window.

Keyboard input arrives through IRQ1. The kernel uses flat ring 0/ring 3 GDT
entries and a TSS. Its first 64 MiB are identity-mapped through supervisor-only
4 KiB entries, so every process can transition to mapped kernel code without
exposing kernel memory to ring 3. Each process receives a distinct page
directory, a private kernel stack, and a writable user stack at `0x80000000`;
user pages live in `0x40000000` through `0xbfffffff` and must use frames from
`memory_alloc_user_page()`. The scheduler context-switches kernel stacks
(`process_iret_to`) and resumes blocked processes through their saved interrupt
frame. The `panic` command was moved out of the kernel; fatal exceptions halt
with a diagnostic vector line.
