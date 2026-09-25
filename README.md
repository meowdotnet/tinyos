# tinyos

tinyos is a small, freestanding 32-bit x86 kernel built around a real UNIX
process model. It boots through Multiboot v1 and runs a genuine userspace
process tree: the kernel enters PID 1 `/init` in ring 3, `/init` spawns and
supervises `/bin/sh`, and both talk to the kernel only through `int $0x80`
system calls. On top of that sits **TinyFS (TFS)**, a small core-UNIX
filesystem — directories, files, inodes, and per-process file descriptors.

It is an educational project, not a POSIX system: it has no networking, disk
driver, or disk persistence (the filesystem is RAM-backed for now).

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
  TinyFS (`fs.c`/`fs.h`), x86 descriptor/interrupt setup, syscall dispatch,
  and panic handler
- `user/` — userspace images: `init.s` (PID 1 supervisor) and `sh.s` (the
  ring-3 interactive shell), both linked to run at `0x40000000`
- `drivers/graphics/` — VGA text-mode console
- `drivers/keyboard/` — PS/2 keyboard (US scancode set 1)
- `linker.ld`, `grub.cfg`, `Makefile` — ELF layout and build/boot setup

After a successful boot the kernel stays quiet and hands control to the
userspace process tree:

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
`clear`, `shutdown`, `exit`, plus the `cd`/`ls`/`cat`/`put`/`mkdir`/`stat`
file commands) is `/bin/sh` running entirely in ring 3 and talking to the kernel
only through `int $0x80`. Backspace is handled by the shell's line editor,
which emits a `"\b \b"` erase sequence through `SYS_WRITE`; the VGA text driver
interprets `\b` by blanking the previous cell and stepping the cursor back.
`shutdown` requests an ACPI soft-off (S5) by writing the sleep-enable value to
the QEMU/Bochs PM1 control port (`0x604`), which powers off the emulator and
closes its window.

## TinyFS (TFS)

A small core-UNIX filesystem — the classic model, not a modern-filesystem
lookalike. It is mounted at boot and currently RAM-backed (a "block" is one
4 KiB physical page), so it does not survive a reboot yet.

```
/
├── bin/
├── dev/
├── etc/
├── home/
├── tmp/
└── usr/
```

Core concepts, in order: **filesystem → directories → directory entries →
files → file descriptors**.

On-disk structures:
- **superblock** — magic, block size, inode page table, counters.
- **inode table** — one inode per page. An inode holds mode (file type +
  permission bits), owner (uid/gid), size, parent, and an array of direct data
  block pointers.
- **data blocks** — allocated on demand; regular-file contents and directory
  entries.
- **directories** — files whose data is a sequence of packed, variable-length
  directory entries (`inode`, `reclen`, `type`, `name`). Listings omit the
  synthetic `.` and `..` names; `..` path traversal is handled by the parent
  inode field.
- **file descriptors** — each process owns a small table of open files
  (inode + offset + flags); an fd is an index into that table.

All v1 processes run as uid/gid 0 (root), which follows the traditional root
permission bypass; the inode mode and owner fields are nevertheless checked by
the same permission path when non-root identities are added.

Operations (syscalls, all reaching the kernel via `int $0x80`): `open`,
`close`, file `read`, file `write`, `mkdir`, `readdir`, `stat`, and `lseek`.
The file calls are named `SYS_FREAD`/`SYS_FWRITE` in the current ABI because
`SYS_READ`/`SYS_WRITE` are still the small console interface; both file calls
are descriptor-based. Each process has 16 descriptor slots, and `readdir` uses
the descriptor's offset as its cursor, returning one directory entry per call.
`lseek` applies to regular files and returns the resulting byte offset.
The current syscall ABI accepts paths up to 127 bytes (the on-disk name field
supports 255-byte names for later callers).
There is no journaling, no indirect blocks, no copy-on-write — those come later
only if TinyOS actually needs them. Deletion/rename are not part of this first
cut. The shell keeps a small current-working-directory buffer for `cd`; direct
kernel-relative paths are still rooted at `/`.

A boot-time self-test (`tfs_selftest`) exercises mkdir → write → read-back →
readdir → stat, including a file crossing a 4 KiB block boundary and an
`lseek`; it stays silent on success and reports only failures before `/init`
runs. Truncation releases the file's old data pages rather than retaining
stale bytes. Its fixtures are kept under `/tmp/.tfs-selftest` and
`/tmp/.tfs-dirtest`, leaving the standard root clean.

Try it at the `sh$` prompt:

```
cd /home
mkdir z
cd z
put note hello from ring3
cat note                # -> hello from ring3
ls                      # -> note
stat note               # -> size=16  ino=N  type=file
```

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
