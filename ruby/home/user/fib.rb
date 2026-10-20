# Numeros de Fibonacci: run fib.rb [quantidade]
n = (ARGV[0] || 15).to_i
a, b = 0, 1
out = []
n.times do
  out << a
  a, b = b, a + b
end
puts out.join(" ")
