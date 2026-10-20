#!/usr/bin/env ruby
# frozen_string_literal: true

# tools/embed.rb - embute os arquivos Ruby (e de dados) na imagem do kernel.
#
# O ramfs da RubyOS nasce vazio a cada boot. Para que existam /apps/calc.rb, a RubyShell etc.,
# este script le os arquivos do repositorio e gera build/generated/rb_embedded.c: uma tabela
# {caminho no ramfs, bytes, tamanho} que o kernel copia para o ramfs ao iniciar
# (kernel/ruby/rb_runtime.c: ruby_seed_filesystem).
#
# Mapeamento (repositorio -> ramfs):
#   ruby/apps/x.rb      -> /apps/x.rb          aplicativos
#   ruby/system/x.rb    -> /system/x.rb        shell e servicos do sistema
#   ruby/lib/x.rb       -> /lib/x.rb           bibliotecas (require "x")
#   ruby/home/...       -> /home/...           exemplos para o usuario
#   filesystem/...      -> /...                demais arquivos (README.md e ignorado)
#
# Uso: ruby tools/embed.rb [saida.c]

require 'fileutils'
require_relative 'common'

module RubyOSEmbed
  RUBY_ROOTS = %w[apps system lib home].freeze
  OUT = File.join(RubyOSTools::BUILD_DIR, 'generated', 'rb_embedded.c')

  module_function

  # Retorna [[caminho_no_ramfs, caminho_no_host], ...] em ordem estavel.
  def collect
    files = []
    RUBY_ROOTS.each do |dir|
      base = File.join(RubyOSTools::ROOT, 'ruby', dir)
      Dir.glob(File.join(base, '**', '*')).sort.each do |path|
        next unless File.file?(path)

        files << ["/#{dir}/#{path.sub("#{base}/", '')}", path]
      end
    end
    fs_base = File.join(RubyOSTools::ROOT, 'filesystem')
    Dir.glob(File.join(fs_base, '**', '*')).sort.each do |path|
      next unless File.file?(path)

      rel = path.sub("#{fs_base}/", '')
      next if rel == 'README.md'

      files << ["/#{rel}", path]
    end
    paths = files.map(&:first)
    dup = paths.detect { |p| paths.count(p) > 1 }
    abort "ERRO: caminho duplicado na imagem: #{dup}" if dup
    files
  end

  def c_string(str)
    str.bytes.map { |b| format('0x%02x', b) }.each_slice(16).map { |row| '    ' + row.join(', ') }.join(",\n")
  end

  def generate(out = OUT)
    files = collect
    FileUtils.mkdir_p(File.dirname(out))
    src = +"/* GERADO por tools/embed.rb - nao edite. */\n"
    src << "#include \"../../kernel/ruby/rb_embedded.h\"\n\n"
    files.each_with_index do |(_, host), i|
      data = File.binread(host)
      body = data.empty? ? '    0' : c_string(data)
      src << "static const unsigned char file_#{i}[] = {\n#{body}\n};\n\n"
    end
    src << "const struct rb_embedded_file rb_embedded_files[] = {\n"
    files.each_with_index do |(path, host), i|
      src << "    { \"#{path}\", file_#{i}, #{File.size(host)}u },\n"
    end
    src << "    { 0, 0, 0 }\n};\n\n"
    src << "const uint32_t rb_embedded_count = #{files.size}u;\n"
    File.write(out, src)
    puts "Embutidos #{files.size} arquivos (#{files.sum { |_, h| File.size(h) }} bytes) -> #{out}"
  end
end

RubyOSEmbed.generate(*ARGV) if $PROGRAM_NAME == __FILE__
