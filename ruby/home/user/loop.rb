# Laco infinito de proposito: serve para testar Ctrl+C e kill.
#   run loop.rb          (Ctrl+C interrompe)
#   spawn loop.rb  e depois  kill <pid>
i = 0
while true
  i += 1
  puts "contando: #{i}" if i % 200000 == 0
end
