# ruby/ - camada de usuario em Ruby

Tudo aqui e Ruby (mruby 3.3) executado **dentro** do RubyOS. `tools/embed.rb` embute estes arquivos na imagem
e o kernel os copia para o ramfs a cada boot:

| Pasta | Vira | Conteudo |
|---|---|---|
| `system/` | `/system` | `prelude.rb` (carregado em todo programa: `puts`, `gets`, `require`, `rand`...), `rubyshell.rb` (a shell) |
| `apps/` | `/apps` | programas chamados pelo nome: `calc`, `about`, `files`, `edit`, `irb`, `selftest` |
| `lib/` | `/lib` | bibliotecas para `require "nome"` (exemplo: `textutil.rb`) |
| `home/user/` | `/home/user` | exemplos: `hello.rb`, `fib.rb`, `uselib.rb`, `erro.rb`, `loop.rb`, `memhog.rb` |
| `runtime/` | (nao embutido) | `mruby_build_config.rb`: como o mruby e compilado para o kernel |

A ponte com o kernel (`RubyOS::Console/FS/Proc/Sys`) esta em `kernel/ruby/rb_bindings.c`; veja o README
principal ("Como criar aplicativos Ruby") para a API e as limitacoes.
