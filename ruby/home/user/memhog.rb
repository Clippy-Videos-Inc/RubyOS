# Consome memoria ate bater na cota do processo (6 MiB). Mostra que o limite e aplicado
# e que o kernel recupera tudo quando o programa termina.
blocks = []
begin
  while true
    blocks << "x" * 4096
  end
rescue NoMemoryError => e
  puts "cota atingida com #{blocks.size * 4} KB alocados (#{e.class})"
end
