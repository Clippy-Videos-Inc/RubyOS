# /apps/files.rb - gerenciador de arquivos em modo texto.
#
# Mostra o diretorio atual numerado; os comandos usam esses numeros:
#   N        entra no diretorio N ou le o arquivo N      u         sobe um nivel
#   v N      le o arquivo N (paginado)                   e N       edita o arquivo N (edit)
#   d N      apaga N (pede confirmacao)                  r N nome  renomeia N
#   c N dst  copia o arquivo N                           n nome    cria um arquivo vazio
#   m nome   cria um diretorio                           g caminho vai para um caminho
#   h        ajuda                                       q         sai

class FileManager
  PAGE = 20

  def initialize
    @entries = []
  end

  def refresh
    @entries = RubyOS::FS.entries(".")
  end

  def show
    refresh
    puts ""
    puts RubyOS::FS.cwd
    if @entries.empty?
      puts "  (diretorio vazio)"
      return
    end
    @entries.each_with_index do |(name, type, size), i|
      if type == "dir"
        puts "#{(i + 1).to_s.rjust(3)}  d  #{name}/"
      else
        puts "#{(i + 1).to_s.rjust(3)}  -  #{name}  (#{size} bytes)"
      end
    end
  end

  def help
    puts "N entra/le | u sobe | v N le | e N edita | d N apaga | r N nome renomeia"
    puts "c N dst copia | n nome arquivo novo | m nome diretorio | g caminho | q sai"
  end

  def pick(arg)
    return nil if arg.nil? || arg.empty? || !arg.each_char.all? { |c| c >= "0" && c <= "9" }
    i = arg.to_i
    return nil if i < 1 || i > @entries.size
    @entries[i - 1]
  end

  def view(path)
    data = RubyOS::FS.read(path)
    lines = data.split("\n")
    puts "---- #{path} (#{lines.size} linhas) ----"
    lines.each_with_index do |l, i|
      puts l
      if (i + 1) % PAGE == 0 && i + 1 < lines.size
        c = RubyOS::Console.readline("-- Enter: mais, q: parar -- ")
        return if c.nil? || c == "q"
      end
    end
    puts "---- fim ----"
  end

  def edit(path)
    prog = RubyOS::Proc.find("edit")
    pid = RubyOS::Proc.spawn(prog, [path])
    RubyOS::Proc.wait(pid)
  end

  def open_entry(entry)
    name, type, _size = entry
    if type == "dir"
      RubyOS::FS.chdir(name)
    else
      view(name)
    end
  end

  def confirm(question)
    a = RubyOS::Console.readline("#{question} (s/n) ")
    a == "s" || a == "S" || a == "y"
  end

  def command(line)
    parts = line.strip.split(" ")
    return true if parts.empty?
    cmd = parts[0]
    case cmd
    when "q", "quit", "exit"
      return false
    when "h", "?"
      help
    when "u"
      RubyOS::FS.chdir("..")
    when "g"
      if parts[1]
        RubyOS::FS.chdir(parts[1])
      else
        puts "uso: g caminho"
      end
    when "v", "e", "d", "r", "c"
      entry = pick(parts[1])
      if entry.nil?
        puts "numero invalido (veja a lista)"
      else
        name, type, _s = entry
        case cmd
        when "v"
          type == "dir" ? puts("#{name}: e um diretorio") : view(name)
        when "e"
          type == "dir" ? puts("#{name}: e um diretorio") : edit(name)
        when "d"
          if confirm("apagar #{name}#{type == 'dir' ? ' e tudo dentro' : ''}?")
            RubyOS::FS.rm(name, true)
            puts "apagado: #{name}"
          end
        when "r"
          if parts[2]
            RubyOS::FS.mv(name, parts[2])
          else
            puts "uso: r N novo_nome"
          end
        when "c"
          if parts[2]
            RubyOS::FS.cp(name, parts[2])
          else
            puts "uso: c N destino"
          end
        end
      end
    when "n"
      parts[1] ? RubyOS::FS.touch(parts[1]) : puts("uso: n nome")
    when "m"
      parts[1] ? RubyOS::FS.mkdir(parts[1]) : puts("uso: m nome")
    else
      entry = pick(cmd)
      if entry
        open_entry(entry)
      else
        puts "comando desconhecido: #{cmd} (h = ajuda)"
      end
    end
    true
  rescue RubyOS::Error => e
    puts "erro: #{e.message}"
    true
  end

  def run
    puts "Gerenciador de arquivos RubyOS. h = ajuda, q = sair."
    while true
      show
      line = RubyOS::Console.readline("files> ")
      break if line.nil?
      break unless command(line)
    end
  end
end

FileManager.new.run
