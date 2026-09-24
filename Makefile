CC      = gcc
LD      = ld
CFLAGS  = -m32 -std=c99 -ffreestanding -nostdlib -nostartfiles \
          -fno-builtin -fno-stack-protector -fno-pic \
          -mno-mmx -mno-sse -mno-sse2 \
          -Wall -Wextra -O2 -Iinclude -Ikernel -Idrivers
ASFLAGS = -m32
LDFLAGS = -m elf_i386 -T linker.ld

OBJS = boot/boot.o \
       kernel/kernel.o \
       kernel/lib.o \
       kernel/memory.o \
       kernel/process.o \
       kernel/syscall.o \
       kernel/arch/i386/gdt.o \
       kernel/arch/i386/gdt_load.o \
       kernel/arch/i386/interrupts.o \
       kernel/arch/i386/interrupt_stubs.o \
       kernel/shell.o \
       kernel/panic.o \
       drivers/graphics/vga.o \
       drivers/keyboard/keyboard.o

KERNEL = build/tinyos.elf

all: $(KERNEL)

$(KERNEL): $(OBJS) linker.ld
	mkdir -p build
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(CC) $(ASFLAGS) -c $< -o $@

# ISO needs grub-mkrescue + xorriso (not installed on this box yet).
iso: $(KERNEL)
	mkdir -p build/isodir/boot/grub
	cp $(KERNEL) build/isodir/boot/tinyos.elf
	cp grub.cfg build/isodir/boot/grub/grub.cfg
	grub-mkrescue -o build/tinyos.iso build/isodir

# Needs qemu-system-i386 (not installed on this box yet).
qemu: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL)

clean:
	rm -f $(OBJS) $(KERNEL)

.PHONY: all iso qemu clean
