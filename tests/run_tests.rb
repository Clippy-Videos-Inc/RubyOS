#!/usr/bin/env ruby
# frozen_string_literal: true

# tests/run_tests.rb - testes automatizados do RubyOS 0.2.
#
# Uso:  ruby tests/run_tests.rb
#
# Etapas:
#   1. Testes unitarios em C no PC hospedeiro: util, heap, pmm e ramfs (com AddressSanitizer
#      e UBSan quando o compilador os oferece).
#   2. Compilacao do kernel e validacao do cabecalho Multiboot.
#   3. Boot no QEMU (-kernel), shell controlada pela porta serial: comandos de sistema.
#   4. Comandos de arquivos, redirecionamento e ausencia de vazamento de memoria.
#   5. Processos: ps, spawn, kill, cota de memoria, tabela cheia, reaproveitamento de vagas.
#   6. Memoria fisica com varios tamanhos de RAM.
#   7. Teclado PS/2 real, com teclas injetadas pelo monitor do QEMU (sendkey).
#   8. Boot pela imagem ISO (GRUB) -- se grub-mkrescue estiver instalado.
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

  # Digita um comando pela serial e devolve tudo o que foi impresso ate o proximo prompt.
  def command(line, timeout: 10)
    mark = output.length
    send_text("#{line}\r")
    deadline = Time.now + timeout
    loop do
      tail = output[mark..] || ''
      return tail if tail.match?(/RubyOS> \z/)
      return tail if Time.now > deadline

      sleep 0.03
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

def with_qemu(boot_args, monitor: 'none', memory: '64M')
  args = [qemu_binary, '-display', 'none', '-serial', 'stdio', '-monitor', monitor,
          '-no-reboot', '-m', memory, *boot_args]
  session = QemuSession.new(args)
  yield session
ensure
  session&.kill
end

# Sobe o kernel e espera o prompt.
def with_kernel(**opts, &block)
  with_qemu(['-kernel', RubyOSTools::KERNEL_ELF], **opts) do |vm|
    raise 'a shell nao chegou ao prompt' unless vm.wait_for(/RubyOS> \z/)

    block.call(vm)
  end
end

# ---- leitura da saida de comandos -------------------------------------------

def ps_rows(text)
  text.lines.filter_map do |l|
    m = l.match(/^(\d+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\d+) KB\s+(\d+)s$/)
    next unless m

    { pid: m[1].to_i, ppid: m[2].to_i, name: m[3], state: m[4], kb: m[5].to_i, up: m[6].to_i }
  end
end

def heap_used(vm)
  m = vm.command('memory').match(/Em uso: (\d+) bytes em (\d+) blocos/)
  m && [m[1].to_i, m[2].to_i]
end

def memory_field(text, regex)
  m = text.match(regex)
  m && m[1].to_i
end

def lines_of(text)
  text.lines.map(&:chomp)
end

# ---------------------------------------------------------------- 1. host
puts '== 1. Testes unitarios no PC hospedeiro =='
FileUtils.mkdir_p(RubyOSTools::BUILD_DIR)
host_cc = RubyOSTools.which('gcc') || RubyOSTools.which('cc') || RubyOSTools.which('clang')
if host_cc
  probe = File.join(RubyOSTools::BUILD_DIR, 'probe.c')
  File.write(probe, "int main(void){return 0;}\n")
  sanitize = %w[-fsanitize=address,undefined]
  sanitize = [] unless system(host_cc, *sanitize, probe, '-o', File.join(RubyOSTools::BUILD_DIR, 'probe'),
                              out: File::NULL, err: File::NULL)
  puts "      (sanitizers: #{sanitize.empty? ? 'indisponiveis' : 'AddressSanitizer + UBSan'})"

  host_tests = {
    'util' => %w[kernel/util.c],
    'heap' => %w[kernel/heap.c],
    'pmm' => %w[kernel/pmm.c],
    'ramfs' => %w[kernel/ramfs.c kernel/heap.c kernel/util.c]
  }
  host_tests.each do |name, sources|
    bin = File.join(RubyOSTools::BUILD_DIR, "test_#{name}")
    check("#{name}: compila no hospedeiro") do
      system(host_cc, '-Wall', '-Wextra', '-g', *sanitize, '-I', File.join(ROOT, 'kernel'),
             File.join(ROOT, "tests/host/test_#{name}.c"), *sources.map { |s| File.join(ROOT, s) }, '-o', bin)
    end
    check("#{name}: testes unitarios") do
      out, status = Open3.capture2e(bin)
      puts "      #{out.lines.last&.strip}"
      status.success?
    end
  end
else
  skip('testes unitarios', 'nenhum compilador C no hospedeiro')
end

# ---------------------------------------------------------------- 2. build
puts '== 2. Compilacao =='
check('ruby tools/build.rb kernel gera build/kernel.elf sem avisos') do
  out, status = Open3.capture2e(RbConfig.ruby, File.join(ROOT, 'tools/build.rb'), 'kernel')
  status.success? && File.file?(RubyOSTools::KERNEL_ELF) && !out.include?('warning')
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

# ------------------------------------------------------------ 3. sistema
puts '== 3. Boot e comandos de sistema (QEMU -kernel) =='
with_kernel do |vm|
  check('boot: banner, etapas de init e prompt') do
    out = vm.output
    out.include?('RubyOS 0.2') &&
      out.include?('Ruby Powered Operating System') &&
      out.include?('Initializing kernel...') &&
      out.include?('Initializing memory...') &&
      out.match?(/RAM utilizavel: \d+ KB, heap do kernel: \d+ KB/) &&
      out.include?('Initializing filesystem...') &&
      out.match?(/ramfs: 9 diretorios, 3 arquivos/) &&
      out.include?('Initializing processes...') &&
      out.include?('Initializing Ruby runtime...') &&
      out.include?('RubyOS iniciado.')
  end

  check('help lista todos os comandos agrupados') do
    out = vm.command('help')
    names = %w[ls cd pwd mkdir rmdir touch cat rm cp mv ps processes spawn kill
               help clear echo about memory date time uptime reboot shutdown]
    names.all? { |c| out.match?(/\s#{c}(\s|$)/) } &&
      out.include?('Arquivos:') && out.include?('Processos:') && out.include?('Sistema:') &&
      lines_of(out).length <= 12                                  # cabe em uma tela de 25 linhas
  end

  check('help <comando> mostra o uso; comando inexistente da erro') do
    vm.command('help cp').include?('Uso: cp <origem> <destino>') &&
      vm.command('help naoexiste').include?('comando desconhecido')
  end

  check('echo imprime argumentos, com aspas preservando espacos') do
    vm.command('echo ola mundo').match?(/^ola mundo$/) && vm.command('echo "a   b" c').match?(/^a   b c$/)
  end

  check('backspace edita a linha') do
    vm.command("echx\bo editado").match?(/^editado$/)
  end

  check('about mostra versao, kernel, runtime, CPU, memoria e arquitetura') do
    out = vm.command('about')
    out.match?(/^RubyOS 0\.2$/) && out.match?(/^Kernel: 0\.2$/) && out.match?(/^Ruby Runtime: /) &&
      out.match?(/^CPU: .+/) && out.match?(/^Memory: ~\d+ MB/) && out.match?(/^Architecture: i686/)
  end

  check('date e time no formato esperado') do
    vm.command('date').match?(/^\d{4}-\d{2}-\d{2}$/) && vm.command('time').match?(/^\d{2}:\d{2}:\d{2}$/)
  end

  check('uptime acompanha o tempo real (timer PIT)') do
    sleep 2.2
    m = vm.command('uptime').match(/^up (\d+) h (\d+) min (\d+) s$/)
    m && (m[3].to_i >= 2 || m[2].to_i.positive?)
  end

  check('comando desconhecido gera erro e a shell continua') do
    vm.command('naoexiste').match?(/Comando desconhecido: naoexiste/) && vm.command('').match?(/RubyOS> \z/)
  end

  check('shutdown encerra a VM') do
    vm.send_text("shutdown\r")
    vm.exited?(timeout: 15)
  end
end

with_kernel do |vm|
  check('reboot reinicia a CPU (QEMU sai com -no-reboot)') do
    vm.send_text("reboot\r")
    vm.exited?(timeout: 15)
  end
end

# ------------------------------------------------------------ 4. arquivos
puts '== 4. Sistema de arquivos e comandos de arquivos =='
with_kernel do |vm|
  check('pwd inicial e /home/user') { vm.command('pwd').match?(%r{^/home/user$}) }

  check('ls / mostra a arvore inicial') do
    out = vm.command('ls /')
    %w[apps bin etc home lib system tmp].all? { |d| out.match?(%r{\b#{d}/}) }
  end

  check('ls -1 / lista um por linha, em ordem alfabetica') do
    names = lines_of(vm.command('ls -1 /')).grep(%r{\A[a-z]+/\z})   # ignora a linha do comando digitado
    names == %w[apps/ bin/ etc/ home/ lib/ system/ tmp/]
  end

  check('arquivos iniciais: cat /etc/hostname') { vm.command('cat /etc/hostname').match?(/^rubyos$/) }

  check('cd, mkdir, touch, ls (sequencia do enunciado)') do
    vm.command('cd /home/user')
    vm.command('mkdir projetos')
    vm.command('touch teste.rb')
    out = vm.command('ls')
    out.include?('projetos/') && out.include?('teste.rb') && out.include?('leia-me.txt')
  end

  check('ls -l mostra tipo e tamanho') do
    out = vm.command('ls -l')
    out.match?(%r{^d\s+-\s+projetos/$}) && out.match?(/^-\s+0\s+teste\.rb$/)
  end

  check('cat de arquivo vazio nao imprime erro') do
    out = vm.command('cat teste.rb')
    !out.include?('cat:') && out.match?(/RubyOS> \z/)
  end

  check('echo > grava e cat le') do
    vm.command('echo "puts 1+1" > teste.rb')
    vm.command('cat teste.rb').match?(/^puts 1\+1$/)
  end

  check('echo >> acrescenta') do
    vm.command('echo segunda >> teste.rb')
    lines_of(vm.command('cat teste.rb')).select { |l| %w[segunda].include?(l) || l.start_with?('puts') } ==
      ['puts 1+1', 'segunda']
  end

  check('> substitui o conteudo anterior') do
    vm.command('echo novo > teste.rb')
    out = vm.command('cat teste.rb')
    out.match?(/^novo$/) && !out.include?('segunda')
  end

  check('cp copia (e a copia e independente)') do
    vm.command('cp teste.rb projetos/copia.rb')
    vm.command('echo mudou > teste.rb')
    vm.command('cat projetos/copia.rb').match?(/^novo$/)
  end

  check('mv move para diretorio e renomeia') do
    vm.command('mv teste.rb projetos')
    vm.command('mv projetos/teste.rb projetos/renomeado.rb')
    out = vm.command('ls projetos')
    out.include?('renomeado.rb') && out.include?('copia.rb') && !vm.command('ls').include?('teste.rb')
  end

  check('cd com caminho relativo, .. e sem argumento') do
    vm.command('cd projetos')
    a = vm.command('pwd').match?(%r{^/home/user/projetos$})
    vm.command('cd ..')
    b = vm.command('pwd').match?(%r{^/home/user$})
    vm.command('cd /tmp')
    vm.command('cd')
    c = vm.command('pwd').match?(%r{^/home/user$})
    a && b && c
  end

  check('mensagens de erro claras') do
    vm.command('cat naoexiste').include?('cat: naoexiste: arquivo ou diretorio inexistente') &&
      vm.command('cd leia-me.txt').include?('nao e um diretorio') &&
      vm.command('mkdir projetos').include?('ja existe') &&
      vm.command('cd /nao/existe').include?('inexistente') &&
      vm.command('rmdir projetos').include?('nao esta vazio') &&
      vm.command('rm projetos').include?('e um diretorio') &&
      vm.command('cat projetos').include?('e um diretorio') &&
      vm.command("mkdir #{'n' * 64}").include?('longo demais') &&
      vm.command('cp projetos x').include?('e um diretorio') &&
      vm.command('mv naoexiste x').include?('inexistente')
  end

  check('rm remove arquivo; rm -r remove diretorio com conteudo') do
    vm.command('rm projetos/copia.rb')
    a = !vm.command('ls projetos').include?('copia.rb')
    vm.command('rm -r projetos')
    b = !vm.command('ls').include?('projetos/')
    a && b
  end

  check('nao remove o diretorio atual de um processo, nem a raiz') do
    vm.command('mkdir /tmp/x')
    vm.command('cd /tmp/x')
    a = vm.command('rm -r /tmp/x').include?('diretorio atual de um processo')
    b = vm.command('rmdir /tmp/x').include?('diretorio atual de um processo')
    c = vm.command('rm -r /').include?('rm: /:')
    vm.command('cd /')
    d = !vm.command('rm -r /tmp/x').include?('rm:')
    vm.command('cd /home/user')
    a && b && c && d && vm.command('ls /').include?('tmp/')
  end

  check('redirecionamento de ls, ps e cat; sintaxe invalida e detectada') do
    vm.command('ls / > /tmp/lista.txt')
    vm.command('ps > /tmp/ps.txt')
    vm.command('cat /etc/motd /etc/hostname > /tmp/dois.txt')
    vm.command('ls -1 /home/user >> /tmp/lista.txt')
    l = vm.command('cat /tmp/lista.txt')
    p = vm.command('cat /tmp/ps.txt')
    d = vm.command('cat /tmp/dois.txt')
    l.include?('home/') && l.include?('leia-me.txt') && p.match?(/^2\s+1\s+shell\s+RUNNING/) &&
      d.include?('Bem-vindo ao RubyOS 0.2') && d.match?(/^rubyos$/) &&
      vm.command('echo a >').include?('sintaxe invalida') &&
      vm.command('> arq').include?('sintaxe invalida') &&
      vm.command('echo a > b > c').include?('sintaxe invalida')
  end

  check('erro no destino do redirecionamento e reportado') do
    vm.command('echo a > /nao/existe/arq').include?('shell: /nao/existe/arq: arquivo ou diretorio inexistente')
  end

  check('memory: criar e apagar muitos arquivos nao vaza heap') do
    before = heap_used(vm)
    vm.command('mkdir /tmp/lote')
    5.times do |i|
      vm.command("touch /tmp/lote/a#{i}a /tmp/lote/a#{i}b /tmp/lote/a#{i}c /tmp/lote/a#{i}d")
      vm.command("echo conteudo #{i} > /tmp/lote/a#{i}a")
      vm.command("cp /tmp/lote/a#{i}a /tmp/lote/a#{i}b")
    end
    during = heap_used(vm)
    count = lines_of(vm.command('ls -1 /tmp/lote')).count { |l| l.match?(/^a\d[a-d]$/) }
    vm.command('rm -r /tmp/lote')
    after = heap_used(vm)
    count == 20 && during[0] > before[0] && after == before
  end

  check('memory: bloco RamFS acompanha os arquivos') do
    out = vm.command('memory')
    out.match?(/Nos: \d+ de 1024 \(\d+ diretorios, \d+ arquivos\)/) && out.match?(/Dados: \d+ de 4194304 bytes/)
  end
end

# ---------------------------------------------------------- 5. processos
puts '== 5. Processos =='
with_kernel do |vm|
  check('ps mostra init e shell com estado e memoria') do
    rows = ps_rows(vm.command('ps'))
    init = rows.find { |r| r[:pid] == 1 }
    shell = rows.find { |r| r[:pid] == 2 }
    init && init[:name] == 'init' && init[:ppid].zero? && init[:kb] >= 16 &&
      shell && shell[:name] == 'shell' && shell[:ppid] == 1 && shell[:state] == 'RUNNING' && shell[:kb] >= 16
  end

  check('processes e sinonimo de ps') do
    a = ps_rows(vm.command('ps')).map { |r| r.values_at(:pid, :name, :state) }
    b = ps_rows(vm.command('processes')).map { |r| r.values_at(:pid, :name, :state) }
    a == b && !a.empty?
  end

  check('spawn hello: executa, imprime e termina (e e liberado)') do
    base = heap_used(vm)
    out = vm.command('spawn hello')
    pid = out[/pid (\d+) \(hello\)/, 1]
    ok = pid && out.match?(/^\[hello\] ola do processo #{pid} \(hello\)$/)
    ok && ps_rows(vm.command('ps')).none? { |r| r[:name] == 'hello' } && heap_used(vm) == base
  end

  worker_pid = nil
  check('spawn worker: fica SLEEPING e a memoria cresce a cada segundo') do
    out = vm.command('spawn worker 60')
    worker_pid = out[/pid (\d+) \(worker\)/, 1]&.to_i
    first = ps_rows(vm.command('ps')).find { |r| r[:pid] == worker_pid }
    sleep 3.2
    later = ps_rows(vm.command('ps')).find { |r| r[:pid] == worker_pid }
    first && later && first[:state] == 'SLEEPING' && first[:ppid] == 2 && first[:kb] >= 17 &&
      later[:kb] > first[:kb] && later[:up] >= 3
  end

  check('shell continua responsiva com o worker rodando') do
    vm.command('echo ainda aqui').match?(/^ainda aqui$/)
  end

  check('kill encerra o worker e devolve toda a memoria dele') do
    before_kill = heap_used(vm)
    out = vm.command("kill #{worker_pid}")
    after_kill = heap_used(vm)
    out.include?("processo #{worker_pid} encerrado") &&
      ps_rows(vm.command('ps')).none? { |r| r[:pid] == worker_pid } &&
      after_kill[0] < before_kill[0] - 16_000 &&                 # pelo menos a pilha de 16 KiB
      vm.command("kill #{worker_pid}").include?('inexistente')
  end

  check('processo que termina sozinho e liberado (worker 1)') do
    base = heap_used(vm)
    vm.command('spawn worker 1')
    sleep 2.5
    ps_rows(vm.command('ps')).none? { |r| r[:name] == 'worker' } && heap_used(vm) == base
  end

  check('kill: init e shell sao protegidos; argumentos invalidos') do
    vm.command('kill 1').include?('essencial') &&
      vm.command('kill 2').include?('essencial') &&
      vm.command('kill 9999').include?('inexistente') &&
      vm.command('kill abc').include?('Uso: kill') &&
      vm.command('kill').include?('Uso: kill') &&
      ps_rows(vm.command('ps')).length == 2
  end

  check('spawn: argumentos invalidos') do
    vm.command('spawn naoexiste').include?('programa desconhecido') &&
      vm.command('spawn worker 0').include?('segundos deve ser') &&
      vm.command('spawn worker abc').include?('segundos deve ser') &&
      vm.command('spawn worker 99999').include?('segundos deve ser') &&
      vm.command('spawn').include?('Uso: spawn')
  end

  check('cota de memoria: hog para no limite de 1 MiB e e liberado') do
    base = heap_used(vm)
    out = vm.command('spawn hog')
    kb = out[/limite de memoria atingido apos (\d+) KB/, 1]&.to_i
    kb && kb >= 900 && kb < 1024 &&
      ps_rows(vm.command('ps')).none? { |r| r[:name] == 'hog' } && heap_used(vm) == base
  end

  check('tabela de processos: enche, recusa o excedente e todas as vagas voltam') do
    pids = []
    last = nil
    20.times do
      last = vm.command('spawn worker 120')
      pid = last[/pid (\d+) \(worker\)/, 1]
      break unless pid

      pids << pid.to_i
    end
    full = last.include?('tabela de processos cheia (16)')
    rows = ps_rows(vm.command('ps'))
    count_ok = pids.length == 14 && rows.length == 16
    pids.each { |p| vm.command("kill #{p}") }
    clean = ps_rows(vm.command('ps')).length == 2
    again = vm.command('spawn worker 5')
    new_pid = again[/pid (\d+) \(worker\)/, 1]&.to_i
    vm.command("kill #{new_pid}") if new_pid
    full && count_ok && clean && new_pid && new_pid > pids.max      # PIDs nao sao reutilizados
  end

  check('heap continua integro depois de tudo isso (memory)') do
    out = vm.command('memory')
    out.match?(/Em uso: \d+ bytes em \d+ blocos/) && !out.include?('PANIC')
  end
end

# ---------------------------------------------------------- 6. memoria
puts '== 6. Memoria fisica com varios tamanhos de RAM =='
{ '16M' => 16, '64M' => 64, '256M' => 256, '1024M' => 1024, '4096M' => 4096 }.each do |size, mb|
  with_qemu(['-kernel', RubyOSTools::KERNEL_ELF], memory: size) do |vm|
    check("-m #{size}: boot, mapa de memoria e heap coerentes") do
      next false unless vm.wait_for(/RubyOS> \z/, timeout: 30)

      out = vm.command('memory')
      ram = memory_field(out, /RAM utilizavel: (\d+) KB/)
      heap = memory_field(out, /Heap do kernel \(0x[0-9a-f]+, (\d+) KB\)/)
      free = memory_field(out, /Livre fora do heap: (\d+) KB/)
      # Com 4 GiB, a maquina "pc" do QEMU deixa ~3 GiB abaixo de 4 GiB (buraco de PCI) e o
      # resto acima, onde o kernel de 32 bits nao enxerga: o esperado e 2 a 4 GiB, nunca mais.
      ram_ok = if mb >= 4096
                 ram && ram >= 2 * 1024 * 1024 && ram <= 4 * 1024 * 1024
               else
                 ram && ram > mb * 1024 * 0.93 && ram <= mb * 1024 + 1024
               end
      ram && heap && free && ram_ok &&
        heap > 0 && heap <= 32 * 1024 && heap >= [(ram - 1024) / 2 * 0.9, 32 * 1024 * 0.9].min &&
        free > 0 &&
        vm.command('ps').match?(/shell\s+RUNNING/) && vm.command('echo ok').match?(/^ok$/)
    end
  end
end

# ---------------------------------------------------------------- 7. PS/2
puts '== 7. Teclado PS/2 (teclas injetadas pelo monitor do QEMU) =='
Dir.mktmpdir('rubyos') do |dir|
  sock_path = File.join(dir, 'monitor.sock')
  with_kernel(monitor: "unix:#{sock_path},server,nowait") do |vm|
    sleep 0.1 until File.exist?(sock_path)
    monitor = UNIXSocket.new(sock_path)
    send_keys = lambda do |keys|
      keys.each do |k|
        monitor.write("sendkey #{k}\n")
        sleep 0.08
      end
    end

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

    check('digitar um comando de arquivo pelo teclado (ls /)') do
      send_keys.call(%w[l s spc slash ret])
      vm.wait_for(%r{tmp/})
    end
    monitor.close
  end
end

# ---------------------------------------------------------------- 8. ISO
puts '== 8. Boot pela imagem ISO (GRUB) =='
if RubyOSTools.which('grub-mkrescue') || RubyOSTools.which('grub2-mkrescue')
  check('ruby tools/build.rb gera build/RubyOS.iso') do
    system(RbConfig.ruby, File.join(ROOT, 'tools/build.rb'), out: File::NULL, err: File::NULL) &&
      File.file?(RubyOSTools::ISO_PATH)
  end

  with_qemu(['-cdrom', RubyOSTools::ISO_PATH, '-boot', 'd']) do |vm|
    check('ISO: GRUB carrega o kernel; memoria, arquivos e processos funcionam') do
      next false unless vm.wait_for(/RubyOS> \z/, timeout: 40)

      vm.command('echo iso ok').match?(/^iso ok$/) &&
        vm.command('memory').match?(/RAM utilizavel: \d+ KB/) &&
        vm.command('ls /').include?('home/') &&
        vm.command('spawn hello').match?(/\[hello\] ola do processo/) &&
        ps_rows(vm.command('ps')).length == 2
    end
  end
else
  skip('boot pela ISO', 'grub-mkrescue ausente')
end

puts "\n#{RESULTS.count(true)} passaram, #{RESULTS.count(false)} falharam"
exit(RESULTS.all? ? 0 : 1)
