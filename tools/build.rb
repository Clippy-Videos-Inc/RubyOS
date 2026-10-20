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
require_relative 'embed'

module RubyOSBuild
  include RubyOSTools
  extend self

  OBJ_DIR = File.join(RubyOSTools::BUILD_DIR, 'obj')
  MRUBY_DIR = File.join(RubyOSTools::ROOT, 'third_party', 'mruby')
  MRUBY_CONFIG = File.join(RubyOSTools::ROOT, 'ruby', 'runtime', 'mruby_build_config.rb')
  LIBMRUBY = File.join(RubyOSTools::BUILD_DIR, 'mruby', 'rubyos', 'lib', 'libmruby.a')

  # Opcoes que mudam o layout das estruturas do mruby: PRECISAM ser iguais na biblioteca
  # (ruby/runtime/mruby_build_config.rb) e no codigo do kernel que inclui <mruby.h>.
  MRUBY_DEFINES = %w[
    MRB_NO_FLOAT MRB_INT32 MRB_NO_STDIO MRB_USE_DEBUG_HOOK MRB_NO_DEFAULT_RO_DATA_P
    MRB_CALL_LEVEL_MAX=300
  ].freeze

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

  # Codigo que usa a libc minima e/ou o mruby (kernel/libc e kernel/ruby).
  def runtime_source?(source)
    rel = source.sub("#{RubyOSTools::ROOT}/", '')
    rel.start_with?('kernel/libc/') || rel.start_with?('kernel/ruby/') || rel.start_with?('build/generated/')
  end

  def gcc_include_dir(cc)
    dir = `#{cc.join(' ')} -print-file-name=include`.strip
    abort 'ERRO: nao foi possivel localizar os headers freestanding do GCC.' if dir.empty? || dir == 'include'
    dir
  end

  # Compila o mruby (biblioteca estatica i386 freestanding) com o rake. Incremental.
  def build_mruby(cc)
    abort 'ERRO: third_party/mruby ausente.' unless File.directory?(MRUBY_DIR)
    rake = RubyOSTools.which('rake') || abort('ERRO: rake nao encontrado (apt install rake).')
    env = {
      'RUBYOS_ROOT' => RubyOSTools::ROOT,
      'RUBYOS_BUILD' => RubyOSTools::BUILD_DIR,
      'RUBYOS_CC' => cc.first,
      'RUBYOS_CC_M32' => cc.include?('-m32') ? '1' : '0',
      'MRUBY_CONFIG' => MRUBY_CONFIG
    }
    puts 'Compilando o mruby (runtime Ruby)...'
    ok = Dir.chdir(MRUBY_DIR) { system(env, rake, '-j4', out: File::NULL) }
    # Em caso de falha, repete mostrando a saida completa.
    Dir.chdir(MRUBY_DIR) { system(env, rake) } unless ok
    abort 'ERRO: a compilacao do mruby falhou.' unless ok && File.file?(LIBMRUBY)
  end

  def object_path(source)
    rel = source.sub("#{RubyOSTools::ROOT}/", '').tr('/\\', '__')
    File.join(OBJ_DIR, rel.sub(/\.(c|asm)\z/, '.o'))
  end

  def build_kernel
    nasm = RubyOSTools.which('nasm') || abort('ERRO: nasm nao encontrado.')
    cc = detect_compiler
    ld = detect_linker

    FileUtils.rm_rf(OBJ_DIR)                    # o mruby (build/mruby) e incremental: fica
    FileUtils.rm_f([RubyOSTools::KERNEL_ELF, RubyOSTools::ISO_PATH])
    FileUtils.mkdir_p(OBJ_DIR)

    build_mruby(cc)
    RubyOSEmbed.generate
    gcc_inc = gcc_include_dir(cc)
    libc_inc = File.join(RubyOSTools::ROOT, 'kernel', 'libc', 'include')
    runtime_flags = ['-nostdinc', '-I', libc_inc, '-I', File.join(MRUBY_DIR, 'include'),
                     '-isystem', gcc_inc, *MRUBY_DEFINES.map { |d| "-D#{d}" }]

    objects = []

    Dir.glob(File.join(RubyOSTools::ROOT, 'boot', '*.asm')).sort.each do |src|
      obj = object_path(src)
      RubyOSTools.run!(nasm, '-f', 'elf32', src, '-o', obj)
      objects << obj
    end

    asm_sources = Dir.glob(File.join(RubyOSTools::ROOT, 'kernel', '**', '*.asm')).sort
    asm_sources.each do |src|
      obj = object_path(src)
      RubyOSTools.run!(nasm, '-f', 'elf32', src, '-o', obj)
      objects << obj
    end

    c_sources = Dir.glob(File.join(RubyOSTools::ROOT, 'kernel', '**', '*.c')).sort
    c_sources << File.join(RubyOSTools::BUILD_DIR, 'generated', 'rb_embedded.c')
    c_sources.each do |src|
      obj = object_path(src)
      extra = runtime_source?(src) ? runtime_flags : []
      RubyOSTools.run!(*cc, *CFLAGS, *extra, '-I', File.join(RubyOSTools::ROOT, 'kernel'), '-c', src, '-o', obj)
      objects << obj
    end

    ld_flags = ['-T', File.join(RubyOSTools::ROOT, 'boot', 'linker.ld'), '-nostdlib', '-z', 'noexecstack']
    help = `#{ld.first} --help 2>&1`
    ld_flags << '--no-warn-rwx-segments' if help.include?('--no-warn-rwx-segments')

    RubyOSTools.run!(*ld, *ld_flags, '-o', RubyOSTools::KERNEL_ELF, *objects, LIBMRUBY)
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
    FileUtils.rm_f(Dir.glob(File.join(RubyOSTools::ROOT, 'ruby', 'runtime', '*.lock')))   # criado pelo mruby
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
