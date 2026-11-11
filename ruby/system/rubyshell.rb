# /system/rubyshell.rb - a RubyShell, escrita em Ruby (roda como o processo "shell").
#
# Comandos internos (ls, cd, cat, ps, kill...) sao metodos Ruby sobre o modulo RubyOS;
# qualquer outro nome e procurado como programa Ruby em /apps e /bin (calc, files, edit...).
# O formato das mensagens e o mesmo da shell C de emergencia (kernel/shell*.c).

module Shell
  PROMPT = "RubyOS> "
  GREY = 7
  CYAN = 11
  RED = 12

  GROUPS = ["Arquivos:", "Processos:", "Sistema:", "Ruby:"]
  COMMANDS = {}

  # Registra um comando interno. O bloco recebe os argumentos (sem o nome) e devolve 0 se ok.
  def self.command(group, name, usage, help, &block)
    COMMANDS[name] = { group: group, usage: usage, help: help, fn: block }
  end

  def self.out(s)
    RubyOS::Console.write(s)
  end

  def self.err(cmd, arg, e)
    out("#{cmd}: #{arg}: #{e.message}\n")
  end

  # Divide a linha como a shell C: separa por espacos; aspas abrem uma palavra se estiverem
  # no inicio dela.
  def self.tokenize(line)
    tokens = []
    i = 0
    n = line.size
    while i < n
      i += 1 while i < n && (line[i] == " " || line[i] == "\t")
      break if i >= n
      if line[i] == "\""
        i += 1
        j = i
        j += 1 while j < n && line[j] != "\""
        tokens << line[i, j - i]
        i = j + 1
      else
        j = i
        j += 1 while j < n && line[j] != " " && line[j] != "\t"
        tokens << line[i, j - i]
        i = j
      end
    end
    tokens
  end

  # Procura ">" ou ">>". Retorna [args, destino, anexar?] ou :erro.
  def self.extract_redirect(tokens)
    tokens.each_with_index do |t, i|
      next unless t == ">" || t == ">>"
      return :erro if i == 0 || i != tokens.size - 2
      return [tokens[0, i], tokens[i + 1], t == ">>"]
    end
    [tokens, nil, false]
  end

  def self.digits?(s)
    !s.nil? && !s.empty? && s.each_char.all? { |c| c >= "0" && c <= "9" }
  end

  def self.pad(s, width)
    s.to_s.ljust(width)
  end

  # ------------------------------------------------------------ arquivos

  command("Arquivos:", "pwd", "pwd", "mostra o diretorio atual") do |_a|
    out(RubyOS::FS.cwd + "\n")
    0
  end

  command("Arquivos:", "cd", "cd [diretorio]", "muda o diretorio atual (sem argumento: /home/user)") do |a|
    if a.size > 1
      out("Uso: cd [diretorio]\n")
      next 1
    end
    target = a[0] || "/home/user"
    begin
      RubyOS::FS.chdir(target)
      0
    rescue RubyOS::Error => e
      err("cd", target, e)
      1
    end
  end

  def self.ls_name(name, type)
    if type == "dir"
      RubyOS::Console.color(CYAN)
      out(name + "/")
      RubyOS::Console.color(GREY)
    else
      out(name)
    end
  end

  MODE_BITS = "rwxrwxrwx".freeze

  # "rwxr-xr-x" a partir de um modo octal de 9 bits (so dono/outros existem de verdade;
  # o "grupo" do meio e sempre igual a "outros", so por familiaridade com um Unix de
  # verdade -- ver ramfs.h).
  def self.mode_str(mode)
    expanded = ((mode & 0700) << 0) | ((mode & 07) << 3) | (mode & 07)
    (0..8).map { |i| (expanded & (0400 >> i)) != 0 ? MODE_BITS[i] : "-" }.join
  end

  def self.ls_long(name, type, size, owner = nil, mode = nil)
    out(type == "dir" ? "d" : "-")
    out(mode ? mode_str(mode) : "?????????")
    out("  ")
    out((owner || 0).to_s.rjust(4))
    out("  ")
    out(type == "dir" ? "       -" : size.to_s.rjust(8))
    out("  ")
    ls_name(name, type)
    out("\n")
  end

  command("Arquivos:", "ls", "ls [-l] [-1] [caminho...]",
          "lista arquivos e diretorios (-l detalhado, -1 um por linha)") do |a|
    longfmt = false
    one = false
    bad = nil
    paths = []
    a.each do |arg|
      if arg.size > 1 && arg[0] == "-"
        arg[1, arg.size - 1].each_char do |f|
          if f == "l"
            longfmt = true
          elsif f == "1"
            one = true
          else
            bad ||= f
          end
        end
      else
        paths << arg
      end
    end
    if bad
      out("ls: opcao desconhecida: -#{bad} (use -l ou -1)\n")
      next 1
    end
    paths << "." if paths.empty?
    rc = 0
    paths.each_with_index do |path, idx|
      begin
        st = RubyOS::FS.stat(path)
        raise RubyOS::Error, "arquivo ou diretorio inexistente" unless st
        if st[0] == "file"
          name = path.split("/").last || path
          if longfmt
            owner, mode = RubyOS::FS.perm(path)
            ls_long(name, "file", st[1], owner, mode)
          else
            ls_name(name, "file")
            out("\n")
          end
          next
        end
        out("#{path}:\n") if paths.size > 1
        col = 0
        RubyOS::FS.entries(path).each do |name, type, size|
          w = name.size + (type == "dir" ? 1 : 0)
          if longfmt
            owner, mode = RubyOS::FS.perm("#{path}/#{name}")
            ls_long(name, type, size, owner, mode)
          elsif one
            ls_name(name, type)
            out("\n")
          else
            if col > 0 && col + 2 + w > 78
              out("\n")
              col = 0
            end
            if col > 0
              out("  ")
              col += 2
            end
            ls_name(name, type)
            col += w
          end
        end
        out("\n") if !longfmt && !one && col > 0
        out("\n") if paths.size > 1 && idx + 1 < paths.size
      rescue RubyOS::Error => e
        err("ls", path, e)
        rc = 1
      end
    end
    rc
  end

  command("Arquivos:", "mkdir", "mkdir <diretorio>...", "cria diretorios (o pai precisa existir)") do |a|
    if a.empty?
      out("Uso: mkdir <diretorio>...\n")
      next 1
    end
    rc = 0
    a.each do |p|
      begin
        RubyOS::FS.mkdir(p)
      rescue RubyOS::Error => e
        err("mkdir", p, e)
        rc = 1
      end
    end
    rc
  end

  command("Arquivos:", "rmdir", "rmdir <diretorio>...", "remove diretorios vazios") do |a|
    if a.empty?
      out("Uso: rmdir <diretorio>...\n")
      next 1
    end
    rc = 0
    a.each do |p|
      begin
        RubyOS::FS.rmdir(p)
      rescue RubyOS::Error => e
        err("rmdir", p, e)
        rc = 1
      end
    end
    rc
  end

  command("Arquivos:", "touch", "touch <arquivo>...", "cria arquivos vazios (se nao existirem)") do |a|
    if a.empty?
      out("Uso: touch <arquivo>...\n")
      next 1
    end
    rc = 0
    a.each do |p|
      begin
        RubyOS::FS.touch(p)
      rescue RubyOS::Error => e
        err("touch", p, e)
        rc = 1
      end
    end
    rc
  end

  command("Arquivos:", "cat", "cat <arquivo>...", "mostra o conteudo de arquivos") do |a|
    if a.empty?
      out("Uso: cat <arquivo>...\n")
      next 1
    end
    rc = 0
    a.each do |p|
      begin
        data = RubyOS::FS.read(p)
        out(data)
        out("\n") if data.size > 0 && !data.end_with?("\n")
      rescue RubyOS::Error => e
        err("cat", p, e)
        rc = 1
      end
    end
    rc
  end

  command("Arquivos:", "rm", "rm [-r] <caminho>...", "remove arquivos (-r: diretorios e tudo dentro)") do |a|
    recursive = false
    if a[0] == "-r" || a[0] == "-R"
      recursive = true
      a = a[1, a.size - 1]
    end
    if a.empty?
      out("Uso: rm [-r] <caminho>...\n")
      next 1
    end
    rc = 0
    a.each do |p|
      begin
        RubyOS::FS.rm(p, recursive)
      rescue RubyOS::Error => e
        err("rm", p, e)
        rc = 1
      end
    end
    rc
  end

  command("Arquivos:", "cp", "cp <origem> <destino>", "copia um arquivo (destino pode ser um diretorio)") do |a|
    if a.size == 3 && (a[0] == "-r" || a[0] == "-R")
      out("cp: -r nao esta implementado (so copia arquivos)\n")
      next 1
    end
    if a.size != 2
      out("Uso: cp <origem> <destino>\n")
      next 1
    end
    begin
      RubyOS::FS.cp(a[0], a[1])
      0
    rescue RubyOS::Error => e
      err("cp", a[0], e)
      1
    end
  end

  command("Arquivos:", "mv", "mv <origem> <destino>", "move ou renomeia arquivos e diretorios") do |a|
    if a.size != 2
      out("Uso: mv <origem> <destino>\n")
      next 1
    end
    begin
      RubyOS::FS.mv(a[0], a[1])
      0
    rescue RubyOS::Error => e
      err("mv", a[0], e)
      1
    end
  end

  command("Arquivos:", "chmod", "chmod <modo> <caminho>...", "troca as permissoes (octal, ex.: 644)") do |a|
    if a.size < 2
      out("Uso: chmod <modo-octal> <caminho>...  (ex.: chmod 600 segredo.txt)\n")
      next 1
    end
    mode = begin
      Integer(a[0], 8)
    rescue ArgumentError, TypeError
      nil
    end
    if mode.nil? || mode < 0 || mode > 0777
      out("chmod: #{a[0]}: modo invalido (use octal, ex.: 644, 755)\n")
      next 1
    end
    rc = 0
    a[1, a.size - 1].each do |p|
      begin
        RubyOS::FS.chmod(p, mode)
      rescue RubyOS::Error => e
        err("chmod", p, e)
        rc = 1
      end
    end
    rc
  end

  command("Arquivos:", "chown", "chown <uid> <caminho>...", "troca o dono (so root)") do |a|
    if a.size < 2 || !digits?(a[0])
      out("Uso: chown <uid> <caminho>...  (so root pode trocar o dono)\n")
      next 1
    end
    new_owner = a[0].to_i
    rc = 0
    a[1, a.size - 1].each do |p|
      begin
        RubyOS::FS.chown(p, new_owner)
      rescue RubyOS::Error => e
        err("chown", p, e)
        rc = 1
      end
    end
    rc
  end

  # ------------------------------------------------------- usuario / uid

  def self.uid_name(u)
    return "root" if u == 0
    return "user" if u == 1000
    "?"
  end

  command("Sistema:", "whoami", "whoami", "mostra o usuario atual") do |_a|
    out("#{uid_name(RubyOS::Proc.uid)}\n")
    0
  end

  command("Sistema:", "id", "id", "mostra o uid do usuario atual") do |_a|
    u = RubyOS::Proc.uid
    out("uid=#{u}(#{uid_name(u)})\n")
    0
  end

  command("Sistema:", "su", "su <uid>", "troca de usuario (sem senha nesta versao)") do |a|
    if a.size != 1 || !digits?(a[0])
      out("Uso: su <uid>  (0 = root, 1000 = user)\n")
      next 1
    end
    new_uid = a[0].to_i
    # Sem autenticacao nesta versao (planejada para o futuro): ver kernel/shell_sys.c e
    # README ("Permissoes: o que e e o que nao e").
    RubyOS::Proc.setuid(new_uid)
    out("agora voce e #{uid_name(new_uid)} (uid #{new_uid})\n")
    0
  end

  # ------------------------------------------------------- disco / persistencia

  command("Sistema:", "save", "save", "grava o ramfs inteiro no disco (so root)") do |_a|
    begin
      RubyOS::FS.save
      out("ramfs salvo no disco.\n")
      0
    rescue RubyOS::Error => e
      out("save: #{e.message}\n")
      1
    end
  end

  command("Sistema:", "load", "load", "substitui o ramfs pela imagem do disco (so root)") do |_a|
    begin
      RubyOS::FS.load
      out("ramfs substituido pela imagem do disco. 'cd /home/user' se o diretorio atual sumiu.\n")
      0
    rescue RubyOS::Error => e
      out("load: #{e.message}\n")
      1
    end
  end

  # ----------------------------------------------------------- processos

  command("Processos:", "ps", "ps", "lista os processos (PID, estado, memoria)") do |_a|
    out(pad("PID", 6) + pad("PPID", 6) + pad("NAME", 16) + pad("STATE", 10) + pad("MEM", 10) + "UP\n")
    RubyOS::Proc.list.each do |pid, ppid, name, state, bytes, up|
      kb = (bytes + 1023) / 1024
      out(pad(pid, 6) + pad(ppid, 6) + pad(name, 16) + pad(state, 10) + pad("#{kb} KB", 10) + "#{up}s\n")
    end
    0
  end

  command("Processos:", "processes", "processes", "o mesmo que ps") do |a|
    COMMANDS["ps"][:fn].call(a)
  end

  command("Processos:", "spawn", "spawn <hello|worker|hog|spin|prog.rb> [seg]",
          "cria uma tarefa (demo ou programa Ruby)") do |a|
    if a.empty? || a.size > 2
      out("Uso: spawn <hello|worker|hog|spin|programa.rb> [segundos]\n")
      next 1
    end
    name = a[0]
    secs = 30
    if name == "worker" && a.size == 2
      secs = digits?(a[1]) ? a[1].to_i : 0
      if secs < 1 || secs > 3600
        out("spawn: segundos deve ser de 1 a 3600\n")
        next 1
      end
    end
    begin
      pid = RubyOS::Proc.spawn_demo(name, secs)
      unless pid
        path = RubyOS::Proc.find(name)
        unless path
          out("spawn: programa desconhecido: #{name} (use hello, worker, hog, spin ou um programa Ruby)\n")
          next 1
        end
        pid = RubyOS::Proc.spawn(path, a[1, a.size - 1])
      end
      out("processo criado: pid #{pid} (#{name})\n")
      RubyOS::Proc.yield
      0
    rescue RubyOS::Error => e
      out("spawn: #{e.message}\n")
      1
    end
  end

  command("Processos:", "kill", "kill <pid>", "encerra um processo e libera a memoria dele") do |a|
    unless a.size == 1 && digits?(a[0])
      out("Uso: kill <pid>\n")
      next 1
    end
    pid = a[0].to_i
    begin
      RubyOS::Proc.kill(pid)
      out("processo #{pid} encerrado\n")
      0
    rescue RubyOS::Error => e
      out("kill: #{pid}: #{e.message}\n")
      1
    end
  end

  command("Processos:", "run", "run <programa.rb> [args]", "executa um programa Ruby (Ctrl+C interrompe)") do |a|
    if a.empty?
      out("Uso: run <programa.rb> [argumentos...]\n")
      next 1
    end
    path = RubyOS::Proc.find(a[0])
    unless path
      out("run: #{a[0]}: programa Ruby nao encontrado\n")
      next 1
    end
    run_program(path, a[1, a.size - 1])
  end

  # ------------------------------------------------------------- sistema

  command("Sistema:", "help", "help [comando]", "lista os comandos ou detalha um deles") do |a|
    if a.size >= 1
      c = COMMANDS[a[0]]
      if c
        out("Uso: #{c[:usage]}\n  #{c[:help]}\n")
        next 0
      end
      path = RubyOS::Proc.find(a[0])
      if path
        out("Uso: #{a[0]} [argumentos]\n  programa Ruby: #{path}\n")
        next 0
      end
      out("help: comando desconhecido: #{a[0]}\n")
      next 1
    end
    out("Comandos (use 'help <comando>' para detalhes):\n")
    GROUPS.each do |g|
      names = COMMANDS.keys.select { |n| COMMANDS[n][:group] == g }
      out("  " + pad(g, 10) + names.map { |n| " " + n }.join + "\n")
    end
    apps = RubyOS::FS.entries("/apps").map { |n, t, _s| t == "file" && n.end_with?(".rb") ? n[0, n.size - 3] : nil }.compact
    out("  " + pad("Programas:", 10) + apps.map { |n| " " + n }.join + "\n") unless apps.empty?
    out("Redirecionamento: comando > arquivo   ou   comando >> arquivo\n")
    0
  end

  command("Sistema:", "clear", "clear", "limpa a tela") do |_a|
    RubyOS::Console.clear
    0
  end

  command("Sistema:", "echo", "echo [texto...]", "imprime os argumentos") do |a|
    out(a.join(" ") + "\n")
    0
  end

  command("Sistema:", "memory", "memory", "memoria fisica, heap do kernel e ramfs") do |_a|
    m = RubyOS::Sys.memory
    hex = lambda { |v| "0x" + v.to_s(16).rjust(8, "0") }
    out("Memoria fisica\n")
    out("  RAM utilizavel: #{m[:ram_kb]} KB (#{m[:ram_kb] / 1024} MB)\n")
    out("  Livre fora do heap: #{m[:free_kb]} KB\n")
    out("  Kernel: #{hex.call(m[:kernel_start])} - #{hex.call(m[:kernel_end])} (#{(m[:kernel_end] - m[:kernel_start]) / 1024} KB)\n")
    out("Heap do kernel (#{hex.call(m[:heap_base])}, #{m[:heap_size] / 1024} KB)\n")
    out("  Em uso: #{m[:heap_used]} bytes em #{m[:heap_blocks_used]} blocos\n")
    out("  Livre: #{m[:heap_free]} bytes em #{m[:heap_blocks_free]} blocos (maior: #{m[:heap_largest]})\n")
    out("  Cabecalhos: #{m[:heap_overhead]} bytes\n")
    out("  Pertence ao kernel: #{m[:heap_kernel]} bytes (ramfs e estruturas)\n")
    out("  Pertence a processos: #{m[:heap_used] - m[:heap_kernel]} bytes\n")
    out("RamFS (somente RAM)\n")
    out("  Nos: #{m[:fs_nodes]} de #{m[:fs_max_nodes]} (#{m[:fs_dirs]} diretorios, #{m[:fs_files]} arquivos)\n")
    out("  Dados: #{m[:fs_bytes]} de #{m[:fs_max_bytes]} bytes\n")
    0
  end

  command("Sistema:", "date", "date", "data atual (RTC)") do |_a|
    out(Time.now.date_s + "\n")
    0
  end

  command("Sistema:", "time", "time", "hora atual (RTC)") do |_a|
    out(Time.now.time_s + "\n")
    0
  end

  command("Sistema:", "uptime", "uptime", "tempo desde o boot") do |_a|
    s = RubyOS::Sys.uptime
    out("up #{s / 3600} h #{(s / 60) % 60} min #{s % 60} s\n")
    0
  end

  command("Sistema:", "reboot", "reboot", "reinicia a maquina") do |_a|
    RubyOS::Sys.reboot
  end

  command("Sistema:", "shutdown", "shutdown", "desliga (so em VMs com ACPI)") do |_a|
    RubyOS::Sys.shutdown
  end

  command("Ruby:", "rb", "rb <expressao Ruby>", "avalia Ruby na propria shell (ex.: rb 6 * 7)") do |a|
    if a.empty?
      out("Uso: rb <expressao Ruby>   (para um REPL completo use: irb)\n")
      next 1
    end
    begin
      out("=> " + eval(a.join(" ")).inspect + "\n")
      0
    rescue Exception => e
      out("#{e.class}: #{e.message}\n")
      1
    end
  end

  # -------------------------------------------------------------- execucao

  # Executa um programa Ruby em primeiro plano. Ctrl+C o encerra.
  def self.run_program(path, args, target = nil, append = false)
    pid = RubyOS::Proc.spawn(path, args)
    ok = true
    if target
      text, truncated = RubyOS::Console.capture(pid) { ok = RubyOS::Proc.wait(pid) }
      write_redirect(target, text, append, truncated)
    else
      ok = RubyOS::Proc.wait(pid)
    end
    ok ? 0 : 1
  rescue RubyOS::Error => e
    out("#{path}: #{e.message}\n")
    1
  end

  def self.write_redirect(target, text, append, truncated)
    RubyOS::FS.write(target, text, append)
    out("shell: aviso: saida truncada em 65536 bytes\n") if truncated
  rescue RubyOS::Error => e
    out("shell: #{target}: #{e.message}\n")
  end

  def self.run_builtin(cmd, args, target, append)
    status = 0
    if target
      text, truncated = RubyOS::Console.capture { status = guarded(cmd, args) }
      write_redirect(target, text, append, truncated)
    else
      status = guarded(cmd, args)
    end
    status
  end

  def self.guarded(cmd, args)
    cmd[:fn].call(args)
  rescue Exception => e
    out("rubyshell: erro interno: #{e.class}: #{e.message}\n")
    1
  end

  def self.execute(line)
    tokens = tokenize(line)
    return if tokens.empty?
    red = extract_redirect(tokens)
    if red == :erro
      out("shell: sintaxe invalida (use: comando > arquivo)\n")
      return
    end
    args, target, append = red
    name = args[0]
    rest = args[1, args.size - 1]

    cmd = COMMANDS[name]
    if cmd
      run_builtin(cmd, rest, target, append)
      return
    end
    path = RubyOS::Proc.find(name)
    if path
      run_program(path, rest, target, append)
    else
      out("Comando desconhecido: #{name} (digite 'help')\n")
    end
  end

  def self.main
    while true
      RubyOS::Console.color(RED)
      out(PROMPT)
      RubyOS::Console.color(GREY)
      line = RubyOS::Console.readline
      next if line.nil?
      begin
        execute(line)
      rescue Exception => e
        out("rubyshell: erro interno: #{e.class}: #{e.message}\n")
      end
    end
  end
end

Shell.main
