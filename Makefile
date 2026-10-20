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

# Boot direto do kernel (sem GRUB/ISO), com a serial no terminal.
run-kernel: kernel
	qemu-system-i386 -kernel build/kernel.elf -serial stdio

# Suite completa contra a RubyShell (Ruby), a shell padrao.
test:
	$(RUBY) tests/run_tests.rb

# A mesma suite contra a shell C de emergencia (kernel iniciado com "shell=c").
test-c:
	RUBYOS_TEST_SHELL=c $(RUBY) tests/run_tests.rb

test-all: test test-c

clean:
	$(RUBY) tools/build.rb clean
