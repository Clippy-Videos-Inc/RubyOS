# frozen_string_literal: true

# tools/common.rb - utilidades compartilhadas pelos scripts de build e teste.

require 'rbconfig'

module RubyOSTools
  ROOT = File.expand_path('..', __dir__)
  BUILD_DIR = File.join(ROOT, 'build')
  KERNEL_ELF = File.join(BUILD_DIR, 'kernel.elf')
  ISO_PATH = File.join(BUILD_DIR, 'RubyOS.iso')
  DISK_IMAGE = File.join(BUILD_DIR, 'disk.img')
  DISK_IMAGE_BYTES = 8 * 1024 * 1024   # 8 MiB: folga sobre o limite de 5 MiB da imagem serializada (fs_persist.c)

  module_function

  # Cria build/disk.img (zerado) se ainda nao existir, para o QEMU anexar como disco IDE
  # (-drive ...,if=ide). E so um arquivo de bytes zerados: fs_save/fs_load (kernel/
  # fs_persist.c) e que dao significado a ele. Nao recria se ja existir, para persistir
  # entre execucoes.
  def ensure_disk_image!(path = DISK_IMAGE, bytes = DISK_IMAGE_BYTES)
    return path if File.exist?(path) && File.size(path) == bytes

    require 'fileutils'
    FileUtils.mkdir_p(File.dirname(path))
    File.open(path, 'wb') { |f| f.truncate(bytes) }
    path
  end

  # Procura um executavel no PATH (considera .exe/.bat etc. no Windows).
  def which(name)
    exts = ENV.fetch('PATHEXT', '').split(';')
    exts = [''] if exts.empty?
    ENV.fetch('PATH', '').split(File::PATH_SEPARATOR).each do |dir|
      exts.each do |ext|
        candidate = File.join(dir, "#{name}#{ext}")
        return candidate if File.file?(candidate) && File.executable?(candidate)
      end
    end
    nil
  end

  # Executa um comando mostrando-o; aborta o script se falhar.
  def run!(*cmd)
    puts "  $ #{cmd.join(' ')}"
    system(*cmd) || abort("ERRO: comando falhou: #{cmd.join(' ')}")
  end
end
