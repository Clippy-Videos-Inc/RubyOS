# frozen_string_literal: true

# tools/common.rb - utilidades compartilhadas pelos scripts de build e teste.

require 'rbconfig'

module RubyOSTools
  ROOT = File.expand_path('..', __dir__)
  BUILD_DIR = File.join(ROOT, 'build')
  KERNEL_ELF = File.join(BUILD_DIR, 'kernel.elf')
  ISO_PATH = File.join(BUILD_DIR, 'RubyOS.iso')

  module_function

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
