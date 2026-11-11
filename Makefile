# Makefile - atalhos para o RubyOS. A logica de build mora em tools/build.rb
# (fonte unica de verdade, tambem usada diretamente: "ruby tools/build.rb").

RUBY ?= ruby

.PHONY: all kernel iso run run-kernel test test-c test-all clean

all: iso

kernel:
	$(RUBY) tools/build.rb kernel

iso:
	$(RUBY) tools/build.rb iso

# Boot pela ISO (GRUB) em uma janela do QEMU.
run: iso
	qemu-system-x86_64 -cdrom build/RubyOS.iso

# Boot direto do kernel (sem GRUB/ISO), com a serial no terminal e um disco anexado
# (build/disk.img, criado na primeira vez) para os comandos save/load (0.4).
run-kernel: kernel
	[ -f build/disk.img ] || $(RUBY) -r./tools/common -e 'RubyOSTools.ensure_disk_image!'
	qemu-system-i386 -kernel build/kernel.elf -serial stdio -drive file=build/disk.img,format=raw,if=ide

# Suite completa contra a RubyShell (Ruby), a shell padrao.
test:
	$(RUBY) tests/run_tests.rb

# A mesma suite contra a shell C de emergencia (kernel iniciado com "shell=c").
test-c:
	RUBYOS_TEST_SHELL=c $(RUBY) tests/run_tests.rb

test-all: test test-c

clean:
	$(RUBY) tools/build.rb clean
