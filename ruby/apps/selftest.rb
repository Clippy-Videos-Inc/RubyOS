# /apps/selftest.rb - autoteste do runtime Ruby da RubyOS.
#
# Roda dentro do sistema e confere o interpretador (mruby) e a ponte com o kernel (RubyOS::*).
# Saida final: "selftest: N verificacoes, F falhas". Uso: selftest   (ou: run selftest)

$checks = 0
$fails = 0

def check(name)
  $checks += 1
  ok = false
  begin
    ok = yield
  rescue Exception => e
    puts "  erro em '#{name}': #{e.class}: #{e.message}"
  end
  unless ok
    $fails += 1
    puts "  FALHOU: #{name}"
  end
end

# ---------------------------------------------------------------- linguagem
check("inteiros: aritmetica, divisao inteira, modulo, potencia") do
  7 / 2 == 3 && -7 / 2 == -4 && 7 % 3 == 1 && -7 % 3 == 2 && 2**10 == 1024 && 10.divmod(3) == [3, 1]
end
check("inteiros: maior valor de 32 bits") do
  2147483647 + 0 == 2147483647
end
check("inteiros: shift e conversao de base") do
  (1 << 20) == 1048576
end
check("inteiros: to_s(2) e to_i(16)") do
  255.to_s(2) == "11111111" && "ff".to_i(16) == 255
end
check("inteiros: estouro vira excecao, nao lixo") do
  begin
    2147483647 + 1
    false
  rescue RangeError
    true
  end
end
check("Integer#times, upto, downto, step") do
  a = []
  3.times { |i| a << i }
  1.upto(3) { |i| a << i }
  3.downto(1) { |i| a << i }
  1.step(7, 3) { |i| a << i }
  a == [0, 1, 2, 1, 2, 3, 3, 2, 1, 1, 4, 7]
end
check("Strings: metodos comuns") do
  s = "RubyOS"
  s.size == 6 && s.upcase == "RUBYOS" && s.downcase == "rubyos" && s.reverse == "SOybuR" &&
    s[0, 4] == "Ruby" && s.include?("OS") && s * 2 == "RubyOSRubyOS" && "a,b,c".split(",") == %w[a b c] &&
    "  x ".strip == "x" && "abc".start_with?("ab") && "abc".end_with?("bc") && "ab".center(6, "*") == "**ab**"
end
check("Strings: interpolacao, format, ljust/rjust") do
  n = 42
  "n=#{n}" == "n=42" && format("%05d|%-4s|%x", 42, "ab", 255) == "00042|ab  |ff" &&
    "7".rjust(3, "0") == "007" && sprintf("%s e %s", "a", "b") == "a e b"
end
check("Symbols e comparacoes") do
  :a == :a && :abc.to_s == "abc" && "abc".to_sym == :abc && (1 <=> 2) == -1 && "a" < "b"
end
check("Array: map, select, reduce, sort, uniq, flatten, zip") do
  a = [3, 1, 2, 3]
  a.map { |x| x * 2 } == [6, 2, 4, 6] && a.select(&:odd?) == [3, 1, 3] && a.inject(:+) == 9 &&
    a.sort == [1, 2, 3, 3] && a.uniq == [3, 1, 2] && [[1, [2]], 3].flatten == [1, 2, 3] &&
    [1, 2].zip([3, 4]) == [[1, 3], [2, 4]] && a.min == 1 && a.max == 3 && a.sum == 9 &&
    a.each_slice(2).to_a == [[3, 1], [2, 3]] && a.sort_by { |x| -x } == [3, 3, 2, 1]
end
check("Hash: acesso, iteracao, merge, default") do
  h = { a: 1, "b" => 2 }
  h[:c] = 3
  d = Hash.new(0)
  "hello".each_char { |c| d[c] += 1 }
  h[:a] == 1 && h["b"] == 2 && h.size == 3 && h.keys.size == 3 && h.merge({ z: 9 })[:z] == 9 &&
    d["l"] == 2 && h.map { |k, v| v }.sort == [1, 2, 3] && h.select { |k, v| v > 1 }.size == 2
end
check("Range e Enumerable") do
  (1..5).to_a == [1, 2, 3, 4, 5] && (1...5).to_a.size == 4 && (1..10).select(&:even?) == [2, 4, 6, 8, 10] &&
    (1..4).inject(:*) == 24 && (1..3).each_with_index.to_a == [[1, 0], [2, 1], [3, 2]] && (1..6).each_cons(3).to_a.size == 4
end
check("Blocos, procs, lambdas, closures") do
  counter = 0
  inc = lambda { counter += 1 }
  3.times { inc.call }
  add = proc { |a, b| (a || 0) + (b || 0) }
  compose = ->(f, g) { ->(x) { f.(g.(x)) } }
  counter == 3 && add.call(1) == 1 && compose.(->(x) { x + 1 }, ->(x) { x * 2 }).(5) == 11 &&
    [1, 2].each_with_object([]) { |x, acc| acc << x * 3 } == [3, 6]
end
check("Classes, heranca, modulos, Comparable, super") do
  mod = Module.new do
    def hi
      "hi " + name
    end
  end
  base = Class.new do
    attr_reader :name
    def initialize(name)
      @name = name
    end
    def to_s
      "base:" + @name
    end
  end
  child = Class.new(base) do
    include Comparable
    include mod
    def to_s
      "child/" + super
    end
    def <=>(o)
      name <=> o.name
    end
  end
  a = child.new("a")
  b = child.new("b")
  a.to_s == "child/base:a" && a.hi == "hi a" && a < b && [b, a].min.name == "a" && a.is_a?(base)
end
check("Struct, metodos dinamicos, respond_to?, send") do
  pt = Struct.new(:x, :y)
  p1 = pt.new(1, 2)
  p1.x == 1 && p1.to_a == [1, 2] && p1.respond_to?(:y) && p1.send(:y) == 2 && 5.send(:+, 3) == 8
end
check("Excecoes: rescue, ensure, retry, raise, classes proprias") do
  log = []
  tries = 0
  klass = Class.new(StandardError)
  begin
    tries += 1
    raise klass, "boom" if tries < 3
    log << "ok#{tries}"
  rescue klass => e
    log << e.message
    retry
  ensure
    log << "fim"
  end
  def mm
    yield
  ensure
    $ens = true
  end
  mm { 1 }
  log == ["boom", "boom", "ok3", "fim"] && $ens == true &&
    (begin; Integer("x"); rescue ArgumentError; true; end) && (begin; nil.foo; rescue NoMethodError; true; end)
end
check("catch/throw, loop/break, next, redo-free iteracao") do
  r = catch(:done) do
    10.times { |i| throw :done, i if i == 4 }
    :never
  end
  s = 0
  loop do
    s += 1
    break if s >= 5
  end
  r == 4 && s == 5 && [1, 2, 3].each { |x| next if x == 2 } == [1, 2, 3]
end
check("Fiber e Enumerator externo") do
  f = Fiber.new do
    Fiber.yield 1
    Fiber.yield 2
    3
  end
  e = [10, 20].each
  f.resume == 1 && f.resume == 2 && f.resume == 3 && e.next == 10 && e.next == 20
end
check("Enumerator::Lazy") do
  (1..Float::INFINITY rescue (1..1000)).lazy.select(&:even?).map { |x| x * x }.first(3) == [4, 16, 36]
end
check("Set") do
  s = Set.new([1, 2, 2, 3])
  s.size == 3 && s.include?(2) && (s | Set.new([9])).size == 4 && (s & Set.new([1, 9])).to_a == [1]
end
check("eval, binding, instance_variable_get, define_method, method_missing") do
  b = binding
  eval("zz = 5", b)
  klass = Class.new do
    define_method(:dobro) { |x| x * 2 }
    def method_missing(n, *a)
      n.to_s.start_with?("get_") ? n.to_s[4..-1] : super
    end
    def respond_to_missing?(n, p = false)
      n.to_s.start_with?("get_") || super
    end
  end
  o = klass.new
  o.instance_variable_set(:@v, 7)
  eval("zz + 1", b) == 6 && eval("1 + 2") == 3 && o.dobro(4) == 8 && o.get_ola == "ola" && o.instance_variable_get(:@v) == 7
end
check("ObjectSpace/GC: alocar e liberar muito nao quebra") do
  200.times { Array.new(100) { |i| "s" * (i % 20 + 1) } }
  RubyOS::Sys.gc
  true
end
check("Recursao: 150 niveis simples e 40 com blocos") do
  def depth(n)
    n == 0 ? 0 : [1].map { |x| x + depth(n - 1) }.first
  end
  def plain(n)
    n == 0 ? 0 : 1 + plain(n - 1)
  end
  depth(40) == 40 && plain(150) == 150
end
check("freeze, dup, clone, object_id, ==, equal?") do
  s = "x".freeze
  d = s.dup
  s.frozen? && !d.frozen? && s == d && !s.equal?(d)
end
check("Time (RTC) tem ano plausivel") do
  Time.now.year >= 2000
end
check("rand/srand determinista") do
  srand(7)
  a = [rand(100), rand(100)]
  srand(7)
  b = [rand(100), rand(100)]
  a == b && rand(1..6) >= 1
end
check("require carrega /lib uma unica vez") do
  RubyOS::FS.write("/tmp/_lib_t.rb", "$_req = ($_req || 0) + 1\ndef lib_t; :ok; end\n")
  require "/tmp/_lib_t.rb"
  require "/tmp/_lib_t.rb"
  lib_t == :ok && $_req == 1
end

check("require \"textutil\" (biblioteca de /lib)") do
  require "textutil"
  TextUtil.wrap("um dois tres quatro", 9) == ["um dois", "tres", "quatro"] && TextUtil.title("aBC dEf") == "Abc Def"
end

# ------------------------------------------------------------ ponte com o kernel
check("FS: gravar, ler, anexar, stat, entries, cp, mv, rm") do
  d = "/tmp/_st"
  RubyOS::FS.mkdir(d)
  RubyOS::FS.write("#{d}/a.txt", "um")
  RubyOS::FS.write("#{d}/a.txt", "+dois", true)
  RubyOS::FS.cp("#{d}/a.txt", "#{d}/b.txt")
  RubyOS::FS.mv("#{d}/b.txt", "#{d}/c.txt")
  names = RubyOS::FS.entries(d).map { |n, t, s| n }
  ok = RubyOS::FS.read("#{d}/a.txt") == "um+dois" && RubyOS::FS.stat("#{d}/c.txt") == ["file", 7] &&
       names == ["a.txt", "c.txt"] && RubyOS::FS.stat("/nao/existe").nil? && RubyOS::FS.stat(d)[0] == "dir"
  RubyOS::FS.rm(d, true)
  ok && RubyOS::FS.stat(d).nil?
end
check("FS: erros viram RubyOS::Error com a mensagem do kernel") do
  begin
    RubyOS::FS.read("/nao/existe")
    false
  rescue RubyOS::Error => e
    e.message == "arquivo ou diretorio inexistente"
  end
end
check("FS: cwd e chdir sao por processo") do
  old = RubyOS::FS.cwd
  RubyOS::FS.chdir("/tmp")
  r = RubyOS::FS.cwd == "/tmp"
  RubyOS::FS.chdir(old)
  r && RubyOS::FS.cwd == old
end
check("FS: arquivo binario com bytes nulos e acentos") do
  data = "a\0b\xC3\xA9"
  RubyOS::FS.write("/tmp/_bin", data)
  RubyOS::FS.read("/tmp/_bin") == data && RubyOS::FS.read("/tmp/_bin").size == 5
end
check("Proc: pid, lista, spawn + wait, kill") do
  me = RubyOS::Proc.pid
  list = RubyOS::Proc.list
  pid = RubyOS::Proc.spawn("/home/user/hello.rb", ["x"])
  alive = RubyOS::Proc.alive?(pid)
  RubyOS::Proc.wait(pid)
  gone = !RubyOS::Proc.alive?(pid)
  list.any? { |r| r[0] == me && r[2] == "selftest" } && alive && gone
end
check("Proc: sleep_ms espera de verdade") do
  t0 = RubyOS::Sys.ticks
  sleep(1)
  RubyOS::Sys.ticks - t0 >= 95
end
check("Sys: memoria, cpu, rtc, uptime") do
  m = RubyOS::Sys.memory
  m[:ram_kb] > 1024 && m[:heap_size] > 0 && RubyOS::Sys.cpu.size == 2 && RubyOS::Sys.rtc.size == 6 &&
    RubyOS::Sys.uptime >= 0 && RubyOS::Sys.process_memory > 0
end
check("Captura de saida: Console.capture") do
  text, trunc = RubyOS::Console.capture { puts "capturado" }
  text == "capturado\n" && trunc == false
end

puts "selftest: #{$checks} verificacoes, #{$fails} falhas"
