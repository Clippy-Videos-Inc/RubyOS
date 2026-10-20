# ruby/runtime/mruby_build_config.rb - configuracao do build do mruby para o RubyOS.
#
# Usado por tools/build.rb (nao rode a mao): MRUBY_CONFIG aponta para este arquivo e as
# variaveis RUBYOS_ROOT e RUBYOS_BUILD dizem onde esta o projeto e onde gravar a saida.
#
# Dois builds:
#   host    - so o mrbc (compilador de bytecode) rodando no PC de desenvolvimento, usado para
#             compilar o codigo Ruby das gems durante o build;
#   rubyos  - libmruby.a para o kernel: freestanding, 32 bits, sem stdio e sem Float.

ROOT = ENV.fetch('RUBYOS_ROOT')
OUT  = ENV.fetch('RUBYOS_BUILD')
LIBC_INCLUDE = File.join(ROOT, 'kernel', 'libc', 'include')
M32 = ENV['RUBYOS_CC_M32'] == '0' ? '' : '-m32'   # com i686-elf-gcc nao se usa -m32
GCC_INCLUDE  = `#{ENV.fetch('RUBYOS_CC', 'gcc')} #{M32} -print-file-name=include`.strip

# Opcoes que precisam ser iguais no host e no alvo (afetam o formato do bytecode).
COMMON_DEFINES = %w[MRB_NO_FLOAT MRB_INT32].freeze

# Gems escolhidas (todas funcionam sem stdio e sem Float).
GEMS = %w[
  mruby-compar-ext mruby-enum-ext mruby-string-ext mruby-numeric-ext mruby-array-ext
  mruby-hash-ext mruby-range-ext mruby-proc-ext mruby-symbol-ext mruby-object-ext
  mruby-objectspace mruby-set mruby-fiber mruby-enumerator mruby-enum-lazy mruby-enum-chain
  mruby-toplevel-ext mruby-kernel-ext mruby-class-ext
  mruby-sprintf mruby-struct mruby-metaprog mruby-error mruby-eval mruby-binding mruby-catch mruby-method
].freeze

MRuby::Build.new do |conf|
  conf.toolchain :gcc
  conf.build_dir = File.join(OUT, 'mruby', 'host')
  conf.cc.defines.concat(COMMON_DEFINES)
  conf.gem core: 'mruby-bin-mrbc'
end

MRuby::CrossBuild.new('rubyos') do |conf|
  conf.toolchain :gcc
  conf.build_dir = File.join(OUT, 'mruby', 'rubyos')

  conf.cc.command = ENV.fetch('RUBYOS_CC', 'gcc')
  conf.cc.flags << M32 unless M32.empty?
  conf.cc.flags.concat(%w[
    -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector
    -fno-asynchronous-unwind-tables -fcf-protection=none
    -fno-tree-loop-distribute-patterns -mno-sse -mno-mmx -march=i686
    -nostdinc -Wno-unused-parameter
  ])
  conf.cc.include_paths << LIBC_INCLUDE
  conf.cc.flags << "-isystem" << GCC_INCLUDE
  conf.cc.defines.concat(COMMON_DEFINES + %w[MRB_NO_STDIO MRB_USE_DEBUG_HOOK MRB_NO_DEFAULT_RO_DATA_P MRB_CALL_LEVEL_MAX=300])

  conf.gembox 'stdlib'
  GEMS.each { |g| conf.gem core: g unless conf.gems.any? { |x| x.name == g } }
end
