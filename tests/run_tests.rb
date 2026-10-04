#!/usr/bin/env ruby
# frozen_string_literal: true

# tests/run_tests.rb - testes automatizados do RubyOS 0.1.
#
# Uso:  ruby tests/run_tests.rb
#
# Etapas:
#   1. Testes unitarios em C no PC hospedeiro (kernel/util.c).
#   2. Compilacao do kernel e validacao do cabecalho Multiboot.
#   3. Boot no QEMU (-kernel) com a shell controlada pela porta serial.
#   4. Teclado PS/2 real, com teclas injetadas pelo monitor do QEMU (sendkey).
#   5. Boot pela imagem ISO (GRUB) -- se grub-mkrescue estiver instalado.
#
# O QEMU roda sem janela (-display none); nada no seu computador e modificado.

require 'fileutils'
require 'open3'
require 'socket'
require 'tmpdir'
require_relative '../tools/common'

ROOT = RubyOSTools::ROOT
RESULTS = []

def check(name)
  ok = begin
    yield
  rescue StandardError => e
    puts "      erro: #{e.class}: #{e.message}"
    false
  end
  puts "[#{ok ? 'PASS' : 'FAIL'}] #{name}"
  RESULTS << ok
end

def skip(name, why)
  puts "[SKIP] #{name} (#{why})"
end

# Uma sessao do QEMU: acumula a saida serial e permite enviar teclas pela serial.
class QemuSession
  def initialize(args)
    @stdin, @stdout, @wait = Open3.popen2e(*args)
    @buffer = +''
    @mutex = Mutex.new
    @reader = Thread.new do
      loop do
        chunk = @stdout.readpartial(4096)
        @mutex.synchronize { @buffer << chunk }
      end
    rescue EOFError, IOError
      nil
    end
  end

  def output
    @mutex.synchronize { @buffer.dup }.delete("\r")
  end

  def wait_for(pattern, timeout: 20)
    deadline = Time.now + timeout
    until output.match?(pattern)
      return false if Time.now > deadline

      sleep 0.05
    end
    true
  end

  def send_text(text)
    @stdin.write(text)
    @stdin.flush
  end

  # Digita um comando pela serial e espera o proximo prompt.
  def command(line, expect: nil, timeout: 10)
    mark = output.length
    send_text("#{line}\r")
    deadline = Time.now + timeout
    loop do
      tail = output[mark..] || ''
      return tail if tail.match?(/RubyOS> \z/) && (expect.nil? || tail.match?(expect))
      return tail if Time.now > deadline

      sleep 0.05
    end
  end

  def exited?(timeout: 10)
    !@wait.join(timeout).nil?
  end

  def kill
    Process.kill('KILL', @wait.pid)
  rescue Errno::ESRCH
    nil
  ensure
    @stdin.close unless @stdin.closed?
    @reader.join(1)
  end
end

def qemu_binary
  RubyOSTools.which('qemu-system-i386') || RubyOSTools.which('qemu-system-x86_64')
end

def qemu_args(boot_args, monitor)
  [qemu_binary, '-display', 'none', '-serial', 'stdio', '-monitor', monitor,
   '-no-reboot', '-m', '64M', *boot_args]
end

def with_qemu(boot_args, monitor: 'none')
  session = QemuSession.new(qemu_args(boot_args, monitor))
  yield session
ensure
  session&.kill
end

# ---------------------------------------------------------------- 1. host
puts '== 1. Testes unitarios no PC hospedeiro =='
FileUtils.mkdir_p(RubyOSTools::BUILD_DIR)
host_cc = RubyOSTools.which('gcc') || RubyOSTools.which('cc') || RubyOSTools.which('clang')
if host_cc
  test_bin = File.join(RubyOSTools::BUILD_DIR, 'test_util')
  check('util.c compila no hospedeiro') do
    system(host_cc, '-Wall', '-Wextra', '-I', File.join(ROOT, 'kernel'),
           File.join(ROOT, 'tests/host/test_util.c'), File.join(ROOT, 'kernel/util.c'), '-o', test_bin)
  end
  check('util.c: conversoes, strings e tokenizador') do
    out, status = Open3.capture2e(test_bin)
    puts "      #{out.strip}"
    status.success?
  end
else
  skip('testes unitarios', 'nenhum compilador C no hospedeiro')
end

# ---------------------------------------------------------------- 2. build
puts '== 2. Compilacao =='
check('ruby tools/build.rb kernel gera build/kernel.elf') do
  system(RbConfig.ruby, File.join(ROOT, 'tools/build.rb'), 'kernel', out: File::NULL) &&
    File.file?(RubyOSTools::KERNEL_ELF)
end

if (grub_file = RubyOSTools.which('grub-file'))
  check('kernel tem cabecalho Multiboot valido (grub-file)') do
    system(grub_file, '--is-x86-multiboot', RubyOSTools::KERNEL_ELF)
  end
else
  skip('cabecalho Multiboot', 'grub-file ausente')
end

unless qemu_binary
  skip('testes no QEMU', 'qemu-system-i386/x86_64 nao encontrado')
  puts "\n#{RESULTS.count(true)} passaram, #{RESULTS.count(false)} falharam"
  exit(RESULTS.all? ? 0 : 1)
end

# ---------------------------------------------------------------- 3. QEMU
puts '== 3. Boot e shell no QEMU (-kernel) =='
with_qemu(['-kernel', RubyOSTools::KERNEL_ELF]) do |vm|
  check('boot: banner, etapas de init e prompt') do
    vm.wait_for(/RubyOS> \z/) &&
      vm.output.include?('RubyOS 0.1') &&
      vm.output.include?('Ruby Powered Operating System') &&
      vm.output.include?('Initializing kernel...') &&
      vm.output.include?('Initializing memory...') &&
      vm.output.include?('Initializing filesystem...') &&
      vm.output.include?('Initializing Ruby runtime...') &&
      vm.output.include?('RubyOS iniciado.')
  end

  check('help lista os comandos') do
    out = vm.command('help')
    %w[help clear echo about date time uptime reboot shutdown].all? { |c| out.match?(/^\s+#{c}\s/) }
  end

  check('echo imprime argumentos') do
    vm.command('echo ola mundo').match?(/^ola mundo$/)
  end

  check('echo com aspas preserva espacos') do
    vm.command('echo "a   b" c').match?(/^a   b c$/)
  end

  check('backspace edita a linha') do
    vm.command("echx\bo editado").match?(/^editado$/)
  end

  check('about mostra versao, kernel, runtime, CPU, memoria e arquitetura') do
    out = vm.command('about')
    out.match?(/^RubyOS 0\.1$/) && out.match?(/^Kernel: 0\.1$/) && out.match?(/^Ruby Runtime: /) &&
      out.match?(/^CPU: .+/) && out.match?(/^Memory: ~\d+ MB/) && out.match?(/^Architecture: i686/)
  end

  check('date no formato AAAA-MM-DD') do
    vm.command('date').match?(/^\d{4}-\d{2}-\d{2}$/)
  end

  check('time no formato HH:MM:SS') do
    vm.command('time').match?(/^\d{2}:\d{2}:\d{2}$/)
  end

  check('uptime (timer PIT funcionando)') do
    sleep 2.2
    out = vm.command('uptime')
    m = out.match(/^up (\d+) h (\d+) min (\d+) s$/)
    m && (m[3].to_i >= 2 || m[2].to_i.positive?)
  end

  check('comando desconhecido gera erro e a shell continua') do
    vm.command('naoexiste').match?(/Comando desconhecido: naoexiste/)
  end

  check('linha vazia nao quebra a shell') do
    vm.command('').match?(/RubyOS> \z/)
  end

  check('shutdown encerra a VM') do
    vm.send_text("shutdown\r")
    vm.exited?(timeout: 15)
  end
end

with_qemu(['-kernel', RubyOSTools::KERNEL_ELF]) do |vm|
  check('reboot reinicia a CPU (QEMU sai com -no-reboot)') do
    vm.wait_for(/RubyOS> \z/) && (vm.send_text("reboot\r") || true) && vm.exited?(timeout: 15)
  end
end

# ---------------------------------------------------------------- 4. PS/2
puts '== 4. Teclado PS/2 (teclas injetadas pelo monitor do QEMU) =='
Dir.mktmpdir('rubyos') do |dir|
  sock_path = File.join(dir, 'monitor.sock')
  with_qemu(['-kernel', RubyOSTools::KERNEL_ELF], monitor: "unix:#{sock_path},server,nowait") do |vm|
    sleep 0.1 until File.exist?(sock_path)
    monitor = UNIXSocket.new(sock_path)
    send_keys = lambda do |keys|
      keys.each do |k|
        monitor.write("sendkey #{k}\n")
        sleep 0.08
      end
    end

    check('boot ate o prompt') { vm.wait_for(/RubyOS> \z/) }

    check('teclas minusculas, espaco e Enter chegam a shell') do
      send_keys.call(%w[e c h o spc k b d spc o k ret])
      vm.wait_for(/^kbd ok$/)
    end

    check('Shift gera maiusculas e simbolos') do
      send_keys.call(%w[e c h o spc shift-a shift-b shift-1 ret])
      vm.wait_for(/^AB!$/)
    end

    check('Backspace pelo teclado PS/2') do
      send_keys.call(%w[e c h o spc x backspace o k 2 ret])
      vm.wait_for(/^ok2$/)
    end
    monitor.close
  end
end

# ---------------------------------------------------------------- 5. ISO
puts '== 5. Boot pela imagem ISO (GRUB) =='
if RubyOSTools.which('grub-mkrescue') || RubyOSTools.which('grub2-mkrescue')
  check('ruby tools/build.rb gera build/RubyOS.iso') do
    system(RbConfig.ruby, File.join(ROOT, 'tools/build.rb'), out: File::NULL, err: File::NULL) &&
      File.file?(RubyOSTools::ISO_PATH)
  end

  with_qemu(['-cdrom', RubyOSTools::ISO_PATH, '-boot', 'd']) do |vm|
    check('ISO: GRUB carrega o kernel e a shell responde') do
      vm.wait_for(/RubyOS> \z/, timeout: 40) && vm.command('echo iso ok').match?(/^iso ok$/)
    end
  end
else
  skip('boot pela ISO', 'grub-mkrescue ausente')
end

puts "\n#{RESULTS.count(true)} passaram, #{RESULTS.count(false)} falharam"
exit(RESULTS.all? ? 0 : 1)
