# /apps/about.rb - versao, CPU, memoria e arquitetura (em Ruby, sobre RubyOS::Sys)

cpu_name, cpu_vendor = RubyOS::Sys.cpu
mem = RubyOS::Sys.memory

puts "RubyOS #{RubyOS::VERSION}"
puts "Ruby Powered Operating System"
puts "Kernel: #{RubyOS::KERNEL}"
puts "Ruby Runtime: #{RubyOS::RUNTIME}"
puts "CPU: #{cpu_name} (#{cpu_vendor})"
puts "Memory: ~#{mem[:ram_kb] / 1024} MB (#{mem[:ram_kb]} KB utilizaveis, mapa do bootloader)"
puts "Architecture: #{RubyOS::ARCH}"
puts "Shell: RubyShell (Ruby), programa em execucao: pid #{RubyOS::Proc.pid}"
puts "Linguagem: #{RUBY_ENGINE} #{MRUBY_VERSION} (compatibilidade Ruby #{RUBY_VERSION})"
