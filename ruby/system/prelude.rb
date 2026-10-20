# /system/prelude.rb - carregado no inicio de TODO programa Ruby da RubyOS.
#
# O mruby sai "pelado": sem print/puts/p/gets (dependem de stdio, que nao existe aqui).
# Este arquivo os implementa sobre RubyOS::Console e acrescenta o que falta ao dia a dia
# (sleep, rand, require, exit, Time minimo). Tudo em Ruby puro, sobre inteiros: nao ha Float.

module RubyOS
  module Console
    # Le uma linha com eco e edicao simples (Backspace). Retorna a linha sem o "\n",
    # "" se o usuario digitou Ctrl+C, ou nil em Ctrl+D com a linha vazia (fim da entrada).
    def self.readline(prompt = nil)
      write(prompt) if prompt
      buf = ""
      while true
        c = getc
        if c == 10 || c == 13
          write("\n")
          return buf
        elsif c == 8 || c == 127
          unless buf.empty?
            buf = buf[0, buf.size - 1]
            write("\b")
          end
        elsif c == 3
          write("^C\n")
          return ""
        elsif c == 4
          if buf.empty?
            write("\n")
            return nil
          end
        elsif c >= 32 && c < 127 && buf.size < 250
          ch = c.chr
          buf << ch
          write(ch)
        end
      end
    end

    # Executa o bloco capturando tudo o que o processo pid imprimir. Retorna [texto, truncado?].
    def self.capture(pid = Proc.pid)
      capture_begin(pid)
      begin
        yield
      ensure
        result = capture_end
      end
      result
    end
  end

  # Saida/entrada padrao no estilo $stdout/$stdin.
  class Stream
    def initialize(kind)
      @kind = kind
    end

    def write(*args)
      n = 0
      args.each do |a|
        s = a.to_s
        Console.write(s)
        n += s.size
      end
      n
    end

    def print(*args)
      write(*args)
      nil
    end

    def <<(obj)
      write(obj)
      self
    end

    def puts(*args)
      if args.empty?
        Console.write("\n")
        return nil
      end
      args.each do |a|
        if a.is_a?(Array)
          a.each { |x| puts(x) }
        else
          s = a.to_s
          Console.write(s)
          Console.write("\n") unless s.end_with?("\n")
        end
      end
      nil
    end

    def printf(fmt, *args)
      write(sprintf(fmt, *args))
      nil
    end

    def gets
      Console.readline
    end

    def getc
      c = Console.getc
      c.chr
    end

    def flush
      self
    end

    def sync
      true
    end

    def sync=(v)
      v
    end

    def tty?
      true
    end
    alias isatty tty?
  end

  PRNG = [12345]
end

$stdout = RubyOS::Stream.new(:out)
$stderr = $stdout
$stdin = $stdout
STDOUT = $stdout
STDERR = $stdout
STDIN = $stdout

module Kernel
  def print(*args)
    $stdout.write(*args)
    nil
  end

  def puts(*args)
    $stdout.puts(*args)
  end

  def p(*args)
    args.each { |a| $stdout.puts(a.inspect) }
    args.size <= 1 ? args[0] : args
  end

  def pp(*args)
    p(*args)
  end

  def printf(fmt, *args)
    $stdout.printf(fmt, *args)
  end

  def putc(c)
    $stdout.write(c.is_a?(Integer) ? c.chr : c.to_s[0])
    c
  end

  def gets
    RubyOS::Console.readline
  end

  # sleep(segundos) -- so inteiros (nao ha Float). Sem argumento dorme "para sempre".
  def sleep(seconds = nil)
    if seconds.nil?
      while true
        RubyOS::Proc.sleep_ms(1000)
      end
    end
    raise ArgumentError, "tempo negativo" if seconds < 0
    RubyOS::Proc.sleep_ms(seconds * 1000)
    seconds
  end

  def exit(status = 0)
    RubyOS::Proc.exit
  end

  def exit!(status = 0)
    RubyOS::Proc.exit
  end

  def abort(msg = nil)
    puts msg if msg
    RubyOS::Proc.exit
  end

  # Gerador pseudoaleatorio simples (LCG). Aceita rand, rand(n) e rand(a..b).
  def srand(seed = 0)
    old = RubyOS::PRNG[0]
    x = seed.to_i & 0x3FFFFFFF
    RubyOS::PRNG[0] = x == 0 ? 1 : x
    old
  end

  # xorshift de 30 bits (so inteiros: sem Float e sem estouro de 32 bits).
  def rand(max = 0)
    x = RubyOS::PRNG[0]
    x ^= (x & 0x1FFFF) << 13
    x ^= x >> 17
    x ^= (x & 0x1FFFFFF) << 5
    x &= 0x3FFFFFFF
    x = 1 if x == 0
    RubyOS::PRNG[0] = x
    if max.is_a?(Range)
      lo = max.first
      hi = max.last
      hi -= 1 if max.exclude_end?
      return lo + (x >> 3) % (hi - lo + 1)
    end
    return x if max == 0
    (x >> 3) % max
  end

  # require "nome" carrega /lib/nome.rb (ou um caminho) uma unica vez.
  def require(name)
    name = name.to_s
    path = name.end_with?(".rb") ? name : name + ".rb"
    candidates = path.include?("/") ? [path] : ["/lib/" + path, path]
    $LOADED_FEATURES ||= []
    candidates.each do |file|
      next unless RubyOS::FS.stat(file)
      return false if $LOADED_FEATURES.include?(file)
      $LOADED_FEATURES << file
      RubyOS::Sys.load_file(file)
      return true
    end
    raise LoadError, "cannot load such file -- #{name}"
  end

  def require_relative(name)
    require(name)
  end
end

# Metodos de Integer que o mruby nao traz.
class Integer
  def even?
    self % 2 == 0
  end

  def odd?
    self % 2 != 0
  end

  def positive?
    self > 0
  end

  def negative?
    self < 0
  end
end

# Time minimo, so inteiros, lido do relogio (RTC) da maquina.
class Time
  attr_reader :year, :month, :day, :hour, :min, :sec

  def self.now
    new(*RubyOS::Sys.rtc)
  end

  def initialize(year, month, day, hour, min, sec)
    @year = year
    @month = month
    @day = day
    @hour = hour
    @min = min
    @sec = sec
  end

  def to_a
    [@year, @month, @day, @hour, @min, @sec]
  end

  def date_s
    sprintf("%04d-%02d-%02d", @year, @month, @day)
  end

  def time_s
    sprintf("%02d:%02d:%02d", @hour, @min, @sec)
  end

  def to_s
    date_s + " " + time_s
  end
end
