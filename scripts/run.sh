#!/bin/sh
# scripts/run.sh - executa o RubyOS no QEMU.
#
# Uso:
#   scripts/run.sh            # boot pela ISO, em janela (sem disco: sem persistencia)
#   scripts/run.sh kernel     # boot direto do kernel (sem GRUB), com disco em build/disk.img
#   scripts/run.sh serial     # sem janela: o console e o proprio terminal (Ctrl+A X sai)
#
# "kernel" e "serial" anexam build/disk.img (criado na primeira vez, 8 MiB) como disco IDE,
# para os comandos "save"/"load" (RubyOS 0.4). O arquivo fica no host entre execucoes.

set -e
cd "$(dirname "$0")/.."

[ -f build/disk.img ] || ruby -r./tools/common -e 'RubyOSTools.ensure_disk_image!'

case "${1:-iso}" in
  iso)
    [ -f build/RubyOS.iso ] || ruby tools/build.rb
    exec qemu-system-x86_64 -cdrom build/RubyOS.iso -m 64M
    ;;
  kernel)
    [ -f build/kernel.elf ] || ruby tools/build.rb kernel
    exec qemu-system-i386 -kernel build/kernel.elf -m 64M -drive file=build/disk.img,format=raw,if=ide
    ;;
  serial)
    [ -f build/kernel.elf ] || ruby tools/build.rb kernel
    exec qemu-system-i386 -kernel build/kernel.elf -m 64M -nographic -drive file=build/disk.img,format=raw,if=ide
    ;;
  *)
    echo "Uso: $0 [iso|kernel|serial]" >&2
    exit 1
    ;;
esac
