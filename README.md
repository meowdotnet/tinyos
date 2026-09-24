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

The convenience launcher builds the kernel and starts it in QEMU's terminal
display by default. Pass `--qemu` to use QEMU's normal graphical display:

```sh
./run.sh
./run.sh --qemu
```

## Layout

- `boot/` — Multiboot entry point and initial stack
- `kernel/` — kernel entry, freestanding helpers, physical-page allocator,
  identity paging, one-slot process manager, x86 descriptor/interrupt setup,
  shell, and panic handler
- `drivers/graphics/` — VGA text-mode console
- `drivers/keyboard/` — PS/2 keyboard (US scancode set 1)
- `linker.ld`, `grub.cfg`, `Makefile` — ELF layout and build/boot setup

After boot, the kernel reports memory and interrupt setup, then type `help` at
the `tinyos$` prompt. Keyboard input now arrives through IRQ1. The kernel uses
flat ring 0/ring 3 GDT entries and a TSS, while its first 64 MiB are identity
mapped with 4 MiB pages; these are the base for a later userspace transition.
PID 1 represents the bootstrap kernel context and is selected by the initial
one-slot scheduler.
The `panic` command deliberately halts the kernel and is useful for checking the
fatal-error display.
