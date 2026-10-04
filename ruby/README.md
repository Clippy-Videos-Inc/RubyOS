# ruby/ - camada de usuario em Ruby

**Vazio na versao 0.1.** Ainda nao existe runtime Ruby no RubyOS (ele chega na 0.3);
por isso a shell da 0.1 esta em `kernel/shell.c`.

Layout planejado (nao implementado):

- `runtime/` - integracao do interpretador Ruby reduzido com o kernel
- `shell/`   - RubyShell reescrita em Ruby
- `system/`  - servicos do sistema (arquivos, processos, configuracoes)
- `apps/`    - aplicativos (calculadora, gerenciador de arquivos, editor)
