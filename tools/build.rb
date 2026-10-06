#!/usr/bin/env ruby
# frozen_string_literal: true

# tools/build.rb - compila o RubyOS e gera a imagem inicializavel.
#
# Uso:
#   ruby tools/build.rb          # compila o kernel e gera build/RubyOS.iso
#   ruby tools/build.rb kernel   # so compila build/kernel.elf
#   ruby tools/build.rb iso      # igual ao padrao
#   ruby tools/build.rb clean    # apaga build/
#
# Ferramentas: nasm, gcc (com -m32) ou i686-elf-gcc, ld ou i686-elf-ld,
# e grub-mkrescue para a ISO.

require 'fileutils'
require_relative 'common'
require_relative 'create_image'

module RubyOSBuild
  include RubyOSTools
  extend self

  OBJ_DIR = File.join(RubyOSTools::BUILD_DIR, 'obj')

  CFLAGS = %w[
    -std=gnu11 -O2 -Wall -Wextra
    -ffreestanding -nostdlib
    -fno-pic -fno-pie -fno-stack-protector
    -fno-asynchronous-unwind-tables -fcf-protection=none
    -fno-tree-loop-distribute-patterns
    -mno-sse -mno-mmx -march=i686
  ].freeze

  def detect_compiler
    cross = RubyOSTools.which('i686-elf-gcc')
    return [cross] if cross

    gcc = RubyOSTools.which('gcc') || abort('ERRO: gcc nao encontrado.')
    [gcc, '-m32']
  end

  def detect_linker
    cross = RubyOSTools.which('i686-elf-ld')
    return [cross] if cross

    ld = RubyOSTools.which('ld') || abort('ERRO: ld nao encontrado.')
    [ld, '-m', 'elf_i386']
  end

  def object_path(source)
    rel = source.sub("#{RubyOSTools::ROOT}/", '').tr('/\\', '__')
    File.join(OBJ_DIR, rel.sub(/\.(c|asm)\z/, '.o'))
  end

  def build_kernel
    nasm = RubyOSTools.which('nasm') || abort('ERRO: nasm nao encontrado.')
    cc = detect_compiler
    ld = detect_linker

    FileUtils.rm_rf(RubyOSTools::BUILD_DIR)
    FileUtils.mkdir_p(OBJ_DIR)

    objects = []

    Dir.glob(File.join(RubyOSTools::ROOT, 'boot', '*.asm')).sort.each do |src|
      obj = object_path(src)
      RubyOSTools.run!(nasm, '-f', 'elf32', src, '-o', obj)
      objects << obj
    end

    Dir.glob(File.join(RubyOSTools::ROOT, 'kernel', '**', '*.c')).sort.each do |src|
      obj = object_path(src)
      RubyOSTools.run!(*cc, *CFLAGS, '-I', File.join(RubyOSTools::ROOT, 'kernel'), '-c', src, '-o', obj)
      objects << obj
    end

    ld_flags = ['-T', File.join(RubyOSTools::ROOT, 'boot', 'linker.ld'), '-nostdlib', '-z', 'noexecstack']
    help = `#{ld.first} --help 2>&1`
    ld_flags << '--no-warn-rwx-segments' if help.include?('--no-warn-rwx-segments')

    RubyOSTools.run!(*ld, *ld_flags, '-o', RubyOSTools::KERNEL_ELF, *objects)
    puts "Kernel: #{RubyOSTools::KERNEL_ELF} (#{File.size(RubyOSTools::KERNEL_ELF) / 1024} KiB)"

    verify_multiboot
  end

  # Confirma que o kernel tem um cabecalho Multiboot valido (se grub-file existir).
  def verify_multiboot
    grub_file = RubyOSTools.which('grub-file')
    return puts('(grub-file ausente: verificacao Multiboot ignorada)') unless grub_file

    if system(grub_file, '--is-x86-multiboot', RubyOSTools::KERNEL_ELF)
      puts 'Cabecalho Multiboot: valido'
    else
      abort 'ERRO: o kernel nao tem um cabecalho Multiboot valido.'
    end
  end

  def clean
    FileUtils.rm_rf(RubyOSTools::BUILD_DIR)
    puts 'build/ removido.'
  end
end

case ARGV.first
when 'clean'
  RubyOSBuild.clean
when 'kernel'
  RubyOSBuild.build_kernel
when nil, 'iso'
  RubyOSBuild.build_kernel
  RubyOSImage.create
else
  abort "Uso: ruby tools/build.rb [kernel|iso|clean]"
end
