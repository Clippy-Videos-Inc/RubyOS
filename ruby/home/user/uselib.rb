# Usa uma biblioteca de /lib:  run uselib.rb
require "textutil"

texto = "A RubyOS roda Ruby de verdade dentro do kernel, em processos com memoria contada e limitada."
puts TextUtil.wrap(texto, 30)
puts TextUtil.box(TextUtil.title("ola mundo ruby"))
