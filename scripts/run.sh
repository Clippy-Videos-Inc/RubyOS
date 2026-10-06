#!/bin/sh
# scripts/run.sh - executa o RubyOS no QEMU.
#
# Uso:
#   scripts/run.sh            # boot pela ISO, em janela
#   scripts/run.sh kernel     # boot direto do kernel (sem GRUB)
#   scripts/run.sh serial     # sem janela: o console e o proprio terminal (Ctrl+A X sai)

set -e
cd "$(dirname "$0")/.."

case "${1:-iso}" in
  iso)
    [ -f build/RubyOS.iso ] || ruby tools/build.rb
    exec qemu-system-x86_64 -cdrom build/RubyOS.iso -m 64M
    ;;
  kernel)
    [ -f build/kernel.elf ] || ruby tools/build.rb kernel
    exec qemu-system-i386 -kernel build/kernel.elf -m 64M
    ;;
  serial)
    [ -f build/kernel.elf ] || ruby tools/build.rb kernel
    exec qemu-system-i386 -kernel build/kernel.elf -m 64M -nographic
    ;;
  *)
    echo "Uso: $0 [iso|kernel|serial]" >&2
    exit 1
    ;;
esac
