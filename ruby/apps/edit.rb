# /apps/edit.rb - editor de texto por linhas (estilo "ed"), em Ruby.
#
#   edit arquivo.txt
#
# Sem setas nem tela cheia: o teclado da RubyOS 0.3 so entrega caracteres simples. Em troca,
# funciona igual no teclado e na porta serial. Comandos (digite h para ver):
#   l [N [M]]   lista (com numeros)       a      acrescenta linhas no fim (termine com ".")
#   i N         insere antes da linha N   c N    troca a linha N
#   d N [M]     apaga linha(s)            w      grava
#   x           grava e sai               q      sai (avisa se houver alteracoes)
#   q!          sai sem gravar            h      ajuda

class Editor
  def initialize(path)
    @path = path
    @lines = []
    @dirty = false
    st = RubyOS::FS.stat(path)
    if st.nil?
      puts "#{path}: arquivo novo"
    elsif st[0] == "dir"
      raise RubyOS::Error, "e um diretorio"
    else
      data = RubyOS::FS.read(path)
      @lines = data.split("\n")
      puts "#{path}: #{@lines.size} linhas, #{data.size} bytes"
    end
  end

  def help
    puts "l [N [M]] lista | a acrescenta (fim com .) | i N insere antes de N | c N troca N"
    puts "d N [M] apaga   | w grava | x grava e sai | q sai | q! sai sem gravar | h ajuda"
  end

  def list(from = 1, to = @lines.size)
    from = 1 if from < 1
    to = @lines.size if to > @lines.size
    if @lines.empty?
      puts "(vazio)"
      return
    end
    i = from
    while i <= to
      puts "#{i.to_s.rjust(4)}  #{@lines[i - 1]}"
      i += 1
    end
  end

  # Le linhas ate uma linha contendo so ".". Retorna o array digitado (nil se Ctrl+D).
  def read_block
    puts "(digite o texto; uma linha com apenas . termina)"
    block = []
    while true
      l = RubyOS::Console.readline("  | ")
      return block if l.nil? || l == "."
      block << l
    end
  end

  def save
    text = @lines.empty? ? "" : @lines.join("\n") + "\n"
    RubyOS::FS.write(@path, text)
    @dirty = false
    puts "gravado: #{@path} (#{@lines.size} linhas, #{text.size} bytes)"
    true
  rescue RubyOS::Error => e
    puts "erro ao gravar: #{e.message}"
    false
  end

  def num(s)
    return nil if s.nil? || s.empty?
    return nil unless s.each_char.all? { |c| c >= "0" && c <= "9" }
    s.to_i
  end

  def valid_line?(n)
    if n.nil? || n < 1 || n > @lines.size
      puts "linha invalida (1..#{@lines.size})"
      return false
    end
    true
  end

  # Retorna false para sair do editor.
  def command(line)
    parts = line.strip.split(" ")
    return true if parts.empty?
    cmd = parts[0]
    n = num(parts[1])
    m = num(parts[2])
    case cmd
    when "h", "?"
      help
    when "l"
      list(n || 1, m || n || @lines.size)
    when "a"
      block = read_block
      unless block.empty?
        @lines.concat(block)
        @dirty = true
      end
    when "i"
      if n && n >= 1 && n <= @lines.size + 1
        block = read_block
        unless block.empty?
          @lines[n - 1, 0] = block
          @dirty = true
        end
      else
        puts "uso: i N   (1..#{@lines.size + 1})"
      end
    when "c"
      if valid_line?(n)
        puts "antes: #{@lines[n - 1]}"
        l = RubyOS::Console.readline("novo : ")
        unless l.nil?
          @lines[n - 1] = l
          @dirty = true
        end
      end
    when "d"
      m ||= n
      if valid_line?(n) && valid_line?(m) && m >= n
        @lines.slice!(n - 1, m - n + 1)
        @dirty = true
        puts "#{m - n + 1} linha(s) apagada(s)"
      else
        puts "uso: d N [M]"
      end
    when "w"
      save
    when "x"
      return !save
    when "q"
      if @dirty
        puts "ha alteracoes nao gravadas (w grava, q! sai mesmo assim)"
      else
        return false
      end
    when "q!"
      return false
    else
      puts "comando desconhecido: #{cmd} (h = ajuda)"
    end
    true
  end

  def run
    puts "Editor RubyOS. h = ajuda."
    while true
      line = RubyOS::Console.readline(": ")
      break if line.nil?
      break unless command(line)
    end
  end
end

if ARGV.size != 1
  puts "Uso: edit <arquivo>"
else
  begin
    Editor.new(ARGV[0]).run
  rescue RubyOS::Error => e
    puts "edit: #{ARGV[0]}: #{e.message}"
  end
end
