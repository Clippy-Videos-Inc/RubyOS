# Alteracoes locais no mruby 3.3.0 (vendorizado)

O restante desta pasta e o mruby 3.3.0 sem modificacoes (licenca MIT, ver `LICENSE`).
O repositorio foi *podado* (sem testes, exemplos, ferramentas e gems nao usadas) para que o
RubyOS compile offline. As unicas alteracoes de codigo sao:

1. `src/vm.c`, macro `OP_CMP` da variante `MRB_NO_FLOAT`: usava `mrb_fixnum` para os dois
   operandos. Com `MRB_WORD_BOXING` + `MRB_INT32`, inteiros fora de -2^30..2^30-1 sao objetos
   no heap (tipo `MRB_TT_INTEGER`, mas nao fixnum) e `mrb_fixnum()` lia lixo: `2147483647 == 2147483647`
   dava `false` quando os dois lados eram objetos distintos (e `<`, `>` etc. tambem erravam).
   Trocado por `mrb_integer`, como ja faz a variante com Float. Coberto pelo `selftest`
   ("inteiros: maior valor de 32 bits").
