#!/usr/bin/env ruby
# frozen_string_literal: true

# tools/create_image.rb - gera build/RubyOS.iso (GRUB + kernel Multiboot).
#
# Uso:  ruby tools/create_image.rb [kernel.elf] [saida.iso]
#
# Requer: grub-mkrescue (ou grub2-mkrescue), xorriso e os arquivos do GRUB para
# BIOS (pacote grub-pc-bin no Debian/Ubuntu).

require 'fileutils'
require_relative 'common'

module RubyOSImage
  GRUB_CFG = <<~CFG
    set timeout=1
    set default=0

    menuentry "RubyOS 0.1" {
        multiboot /boot/kernel.elf
        boot
    }
  CFG

  module_function

  def create(kernel_elf = RubyOSTools::KERNEL_ELF, iso_path = RubyOSTools::ISO_PATH)
    abort "ERRO: #{kernel_elf} nao existe. Compile antes (ruby tools/build.rb kernel)." unless File.file?(kernel_elf)

    mkrescue = RubyOSTools.which('grub-mkrescue') || RubyOSTools.which('grub2-mkrescue')
    abort 'ERRO: grub-mkrescue nao encontrado (instale grub-pc-bin, xorriso e mtools).' unless mkrescue

    iso_dir = File.join(File.dirname(iso_path), 'iso')
    FileUtils.rm_rf(iso_dir)
    FileUtils.mkdir_p(File.join(iso_dir, 'boot', 'grub'))
    FileUtils.cp(kernel_elf, File.join(iso_dir, 'boot', 'kernel.elf'))
    File.write(File.join(iso_dir, 'boot', 'grub', 'grub.cfg'), GRUB_CFG)

    FileUtils.rm_f(iso_path)
    RubyOSTools.run!(mkrescue, '-o', iso_path, iso_dir)
    puts "ISO criada: #{iso_path} (#{File.size(iso_path) / 1024} KiB)"
  end
end

RubyOSImage.create(*ARGV) if $PROGRAM_NAME == __FILE__
