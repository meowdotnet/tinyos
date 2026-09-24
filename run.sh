#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$ROOT"

mode=qemu
case ${1-} in
    ""|--qemu) ;;
    --terminal) mode=terminal ;;
    --help|-h)
        echo "Usage: ./run.sh [--qemu|--terminal]"
        echo "  (default, --qemu) Run QEMU with its normal graphical display."
        echo "  --terminal          Run in the host terminal using QEMU's curses display."
        exit 0
        ;;
    *)
        echo "Unknown option: $1" >&2
        echo "Usage: ./run.sh [--qemu|--terminal]" >&2
        exit 2
        ;;
esac

if [ "$#" -gt 1 ]; then
    echo "Usage: ./run.sh [--qemu|--terminal]" >&2
    exit 2
fi

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "Error: qemu-system-i386 is required to run tinyos." >&2
    exit 127
fi

make -C "$ROOT"

if [ "$mode" = terminal ]; then
    exec qemu-system-i386 -kernel "$ROOT/build/tinyos.elf" \
        -display curses -monitor none -serial none -no-reboot -no-shutdown
else
    exec qemu-system-i386 -kernel "$ROOT/build/tinyos.elf" \
        -no-reboot -no-shutdown
fi
