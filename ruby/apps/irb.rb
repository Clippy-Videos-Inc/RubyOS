# /apps/irb.rb - REPL de Ruby (mruby). As variaveis locais persistem entre as linhas.
#
#   irb> x = 6
#   => 6
#   irb> x * 7
#   => 42
#
# Uma linha por vez (sem blocos de varias linhas). "exit" ou Ctrl+D sai.

puts "Ruby (mruby) - REPL da RubyOS. 'exit' para sair."
b = binding
while true
  line = RubyOS::Console.readline("irb> ")
  break if line.nil?
  line = line.strip
  next if line.empty?
  break if line == "exit" || line == "quit"
  begin
    result = eval(line, b)
    puts "=> #{result.inspect}"
  rescue Exception => e
    puts "#{e.class}: #{e.message}"
  end
end
