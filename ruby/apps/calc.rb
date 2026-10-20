# /apps/calc.rb - Ruby Calculator
#
# Calculadora de inteiros com precedencia, parenteses, potencia, hexadecimal e variaveis.
# Nao usa eval: tem um analisador proprio (descida recursiva), entao so aceita expressoes.
#
#   > 10 + 20            30
#   > (2 + 3) * 4        20
#   > 2 ** 10            1024
#   > x = 7              7
#   > x * x + ans        56      (ans = ultimo resultado)
#
# O Ruby da RubyOS 0.3 nao tem Float: a divisao e inteira (7 / 2 = 3).

class Calculator
  class CalcError < StandardError; end

  attr_reader :vars

  def initialize
    @vars = { "ans" => 0 }
  end

  # Avalia uma linha. Retorna o valor inteiro. "nome = expr" atribui.
  def run(src)
    @tokens = tokenize(src)
    @pos = 0
    if @tokens.size >= 2 && @tokens[0][0] == :id && @tokens[1] == [:op, "="]
      name = @tokens[0][1]
      raise CalcError, "'ans' e reservado" if name == "ans"
      @pos = 2
      value = expression
      finish
      @vars[name] = value
    else
      value = expression
      finish
    end
    @vars["ans"] = value
    value
  end

  private

  def tokenize(src)
    tokens = []
    i = 0
    n = src.size
    while i < n
      c = src[i]
      if c == " " || c == "\t"
        i += 1
      elsif c >= "0" && c <= "9"
        j = i
        if c == "0" && (src[i + 1] == "x" || src[i + 1] == "X")
          j = i + 2
          j += 1 while j < n && hex_digit?(src[j])
          raise CalcError, "hexadecimal vazio" if j == i + 2
          tokens << [:num, src[i + 2, j - i - 2].to_i(16)]
        else
          j += 1 while j < n && src[j] >= "0" && src[j] <= "9"
          tokens << [:num, src[i, j - i].to_i]
        end
        i = j
      elsif letter?(c)
        j = i
        j += 1 while j < n && (letter?(src[j]) || (src[j] >= "0" && src[j] <= "9"))
        tokens << [:id, src[i, j - i]]
        i = j
      elsif c == "*" && src[i + 1] == "*"
        tokens << [:op, "**"]
        i += 2
      elsif "+-*/%()=".include?(c)
        tokens << [:op, c]
        i += 1
      else
        raise CalcError, "caractere invalido: '#{c}'"
      end
    end
    tokens
  end

  def hex_digit?(c)
    (c >= "0" && c <= "9") || (c >= "a" && c <= "f") || (c >= "A" && c <= "F")
  end

  def letter?(c)
    (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c == "_"
  end

  def peek
    @tokens[@pos]
  end

  def op?(s)
    t = peek
    !t.nil? && t[0] == :op && t[1] == s
  end

  def finish
    raise CalcError, "sobrou texto depois da expressao" unless peek.nil?
  end

  # expressao := termo (("+" | "-") termo)*
  def expression
    v = term
    while op?("+") || op?("-")
      plus = op?("+")
      @pos += 1
      r = term
      v = plus ? v + r : v - r
    end
    v
  end

  # termo := potencia (("*" | "/" | "%") potencia)*
  def term
    v = power
    while op?("*") || op?("/") || op?("%")
      o = peek[1]
      @pos += 1
      r = power
      if o == "*"
        v *= r
      else
        raise CalcError, "divisao por zero" if r == 0
        v = o == "/" ? v / r : v % r
      end
    end
    v
  end

  # potencia := unario ["**" potencia]   (associa a direita)
  def power
    base = unary
    if op?("**")
      @pos += 1
      exp = power
      raise CalcError, "expoente negativo nao e suportado (so inteiros)" if exp < 0
      result = 1
      exp.times { result *= base }
      return result
    end
    base
  end

  def unary
    if op?("-")
      @pos += 1
      return -unary
    end
    if op?("+")
      @pos += 1
      return unary
    end
    primary
  end

  def primary
    t = peek
    raise CalcError, "expressao incompleta" if t.nil?
    @pos += 1
    if t[0] == :num
      t[1]
    elsif t[0] == :id
      raise CalcError, "variavel desconhecida: #{t[1]}" unless @vars.key?(t[1])
      @vars[t[1]]
    elsif t[1] == "("
      v = expression
      raise CalcError, "faltou ')'" unless op?(")")
      @pos += 1
      v
    else
      raise CalcError, "inesperado: '#{t[1]}'"
    end
  end
end

def calc_help
  puts "Operadores: + - * / % ** ( )   hexadecimal: 0xFF   variaveis: x = 5"
  puts "ans = ultimo resultado. Comandos: vars, help, exit (ou Ctrl+D). Divisao e inteira."
end

# Modo nao interativo: calc 10 + 20   (ARGV vira a expressao)
if ARGV.size > 0
  begin
    puts Calculator.new.run(ARGV.join(" "))
  rescue Calculator::CalcError => e
    puts "erro: #{e.message}"
  rescue StandardError => e
    puts "erro: #{e.message}"
  end
else
  puts "Ruby Calculator"
  puts "Digite uma expressao (help para ajuda, exit para sair)."
  calc = Calculator.new
  while true
    line = RubyOS::Console.readline("> ")
    break if line.nil?
    line = line.strip
    next if line.empty?
    break if line == "exit" || line == "quit" || line == "q"
    if line == "help"
      calc_help
      next
    end
    if line == "vars"
      calc.vars.each { |k, v| puts "#{k} = #{v}" }
      next
    end
    begin
      puts calc.run(line)
    rescue Calculator::CalcError => e
      puts "erro: #{e.message}"
    rescue StandardError => e
      puts "erro: #{e.message}"
    end
  end
end
