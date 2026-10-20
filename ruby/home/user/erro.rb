# Mostra como erros de Ruby aparecem: arquivo:linha, mensagem e classe.
def divide(a, b)
  a / b
end
puts "vou dividir por zero..."
puts divide(1, 0)
