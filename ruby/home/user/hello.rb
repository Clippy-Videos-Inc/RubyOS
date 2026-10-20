# Seu primeiro programa Ruby na RubyOS. Execute com:  run hello.rb   (ou: hello.rb)
nome = ARGV[0] || "mundo"
puts "Ola, #{nome}! Aqui e o Ruby rodando dentro do kernel (pid #{RubyOS::Proc.pid})."
puts "Os argumentos recebidos: #{ARGV.inspect}"
