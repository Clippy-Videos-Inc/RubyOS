# RubyOS 0.3

Sistema operacional experimental que usa **Ruby como linguagem da camada de usuario**, com Assembly e C
apenas onde sao tecnicamente necessarios.

> **Estado real da 0.3:** inicializa no QEMU e **executa Ruby de verdade dentro do kernel**: o interpretador
> [mruby](https://mruby.org) 3.3.0 esta embutido (compilado para i386 sem sistema operacional por baixo,
> sobre uma libc minima escrita para este projeto). A shell (`RubyShell`), a calculadora, o gerenciador de
> arquivos, o editor de texto e o REPL sao **programas Ruby** que rodam como processos do kernel, cada um
> com seu proprio interpretador, memoria contada e limitada, e podem ser interrompidos com Ctrl+C ou `kill`.
> A shell em C da 0.2 continua no kernel como **shell de emergencia**. Limites importantes: o Ruby e o
> **mruby (um subconjunto de Ruby)**, **sem Float** e com inteiros de 32 bits; os arquivos **nao persistem**
> (nao ha driver de disco); os processos sao **cooperativos** e rodam no anel 0, **sem isolamento**.
> Veja [Limitacoes atuais](#limitacoes-atuais).

## Objetivos

- Um SO real (bootloader, kernel, hardware, shell) e nao uma simulacao dentro de outro SO.
- Ruby como linguagem da shell, dos apps e das ferramentas do sistema (**a partir da 0.3, de fato**).
- Evolucao incremental: cada versao inicializa e e testada antes de crescer.
- Prioridade: **funcionalidade > complexidade**.

## Arquitetura

```
 Ruby Applications        /apps/calc.rb  files.rb  edit.rb  irb.rb  about.rb  selftest.rb  (+ seus programas)
        ↓
 Ruby System Services     /system/rubyshell.rb (a shell)   /system/prelude.rb (print, puts, gets, require...)
        ↓
 Ruby Runtime             mruby 3.3.0 (third_party/mruby)  +  modulo RubyOS::{Console,FS,Proc,Sys}
        ↓                 (kernel/ruby/rb_bindings.c)         + libc minima (kernel/libc)
 Kernel (C)               processos, escalonador cooperativo, ramfs, heap com donos, console, terminal,
        ↓                 GDT/IDT/PIC, timer, teclado, serial, RTC; shell C de emergencia (kernel/shell*.c)
 Boot (Assembly + GRUB)   boot/boot.asm, boot/isr.asm, boot/switch.asm, kernel/libc/setjmp.asm
```

**Fluxo de boot:** BIOS → GRUB (ou `qemu -kernel`) → cabecalho Multiboot em `boot/boot.asm` → `kmain()` →
GDT, PIC, IDT, timer, teclado → `mm_init` (mapa de memoria, PMM, heap) → `fs_init` (arvore inicial) →
`process_init` (o boot vira o processo 1, `init`) → `ruby_seed_filesystem` (copia os `.rb` embutidos na imagem
para `/apps`, `/system`, `/lib` e `/home/user`) → `ruby_start_shell` cria o processo 2 (`shell`), que abre um
interpretador mruby, carrega `/system/prelude.rb` e executa `/system/rubyshell.rb`. O `init` vira a tarefa ociosa.

**Se a RubyShell nao subir** (script ausente, erro, falta de memoria), o proprio processo `shell` cai na shell
C: voce nunca fica sem console. Tambem da para pedi-la de proposito: no GRUB, a entrada
"RubyOS 0.3 (shell C de emergencia)" passa `shell=c` na linha de comando do Multiboot.

### O runtime Ruby: como o mruby foi embutido

Investigacao e decisao (com os numeros reais deste projeto):

| Opcao | Resultado |
|---|---|
| CRuby (Ruby "de verdade") dentro do kernel | **Inviavel**: exige um SO completo (processos isolados, threads, `mmap`, arquivos, libc inteira). |
| Interpretador Ruby proprio, escrito do zero | Possivel, mas seria um "Ruby de brinquedo" e levaria a versao toda so nisso. |
| **mruby** (implementacao embarcavel, MIT, usada em firmware) | **Escolhido.** Depende de ~25 funcoes da libc, nao exige Float nem stdio, e e compilado como biblioteca estatica. |

Como funciona na pratica:

1. `tools/build.rb` roda o `rake` do mruby (`third_party/mruby`, copia podada e versionada no repositorio:
   o build funciona **offline**) com `ruby/runtime/mruby_build_config.rb`. Sai a `libmruby.a` **i386
   freestanding** (`-ffreestanding -nostdinc`, sem Float, sem stdio, inteiros de 32 bits) e, no hospedeiro,
   o `mrbc` (compilador de bytecode usado so durante o build para as gems escritas em Ruby).
2. A **libc minima** (`kernel/libc`: `string`, `ctype`, `stdlib`, `errno`, `setjmp.asm` e os headers em
   `kernel/libc/include`) fornece exatamente o que o mruby pede (conferido com `nm -u`): `malloc/realloc/free`
   viram `kmalloc/krealloc/kfree` do heap do kernel, e `setjmp/longjmp` (as excecoes Ruby) sao Assembly.
3. Cada programa Ruby roda em **um processo do kernel proprio** (`kernel/ruby/rb_runtime.c`): `mrb_open_allocf`
   com um alocador que usa o heap do kernel, entao **toda a memoria do interpretador pertence ao processo**,
   e contada no `ps`, **limitada (cota de 6 MiB)** e **devolvida por inteiro** quando o processo termina ou
   leva `kill` (inclusive no meio de um laco).
4. O mruby chama um gancho a cada instrucao da maquina virtual (`MRB_USE_DEBUG_HOOK`). O RubyOS o usa para
   **ceder a CPU a cada 1024 instrucoes** (um `while true` Ruby nao trava o sistema, e a shell consegue
   atender Ctrl+C) e para **proteger a pilha**: se a recursao chegar perto do fim da pilha de 256 KiB o
   programa e encerrado com uma mensagem, em vez de corromper o kernel.
5. O Ruby so enxerga o kernel por **`RubyOS::Console`, `RubyOS::FS`, `RubyOS::Proc` e `RubyOS::Sys`**
   (`kernel/ruby/rb_bindings.c`, uma camada fina: a logica fica em Ruby). Erros do kernel viram excecoes
   `RubyOS::Error` com a mesma mensagem que a shell C mostraria.
6. Os arquivos `.rb` do repositorio sao **embutidos na imagem** por `tools/embed.rb` (gera
   `build/generated/rb_embedded.c`) e copiados para o ramfs a cada boot. O compilador do mruby tambem esta
   no kernel: voce pode escrever um `.rb` com o editor e rodar na hora.

O mruby foi alterado em **um unico ponto** (um bug de comparacao de inteiros fora de ±2^30 com `MRB_NO_FLOAT`),
descrito em `third_party/mruby/PATCHES.md`; a licenca dele (MIT) esta em `third_party/mruby/LICENSE`.

### Memoria (`kernel/pmm.c`, `kernel/heap.c`, `kernel/memory.c`)

1. O **mapa de memoria do Multiboot** diz quais areas sao RAM utilizavel.
2. O **PMM** guarda um bit por quadro de 4 KiB (ate 4 GiB). Reserva o 1 MiB baixo, a imagem do kernel e as
   estruturas do bootloader.
3. O **heap do kernel** ocupa um bloco fisico contiguo com metade da RAM livre (no maximo 32 MiB):
   first-fit com coalescencia, deteccao de double free e de ponteiros invalidos (`kernel_panic`), conteudo
   liberado envenenado, e `krealloc` (novo na 0.3) que preserva o dono do bloco.
4. Cada bloco tem um **dono** (0 = kernel, senao o processo). O heap sabe quanto cada processo usa, aplica a
   **cota** (1 MiB para tarefas em C, 6 MiB para processos Ruby) e libera tudo o que o processo alocou quando ele
   termina. `memory` mostra o que pertence ao kernel (ramfs) e o que pertence a processos.

Nao ha paginacao nem memoria virtual.

### Sistema de arquivos (`kernel/ramfs.c`)

Arvore de diretorios e arquivos no heap; caminhos absolutos e relativos, `.` e `..`. A arvore inicial
(`/bin /system /home/user /etc /tmp /apps /lib`) mais os arquivos Ruby embutidos sao criados a cada boot.
**Tudo o que voce criar some ao reiniciar.** Sem permissoes, donos ou timestamps.

### Processos (`kernel/process.c`, `boot/switch.asm`)

Cada processo e uma tarefa do kernel com **pilha propria** (com canario) e **troca de contexto real**. O
escalonador e **cooperativo** (round-robin); no Ruby a troca acontece sozinha pelo gancho da VM. Estados:
`READY`, `RUNNING`, `SLEEPING` (e `ZOMBIE`, so ate a liberacao). `init` e `shell` sao essenciais.

**Console** (`kernel/console.c`): uma fila de entrada unica alimentada pelo teclado PS/2 e pela serial
(com Ctrl+C detectado mesmo se o programa nao esta lendo). Toda saida vai para a tela VGA 80x25 **e** para a
serial COM1.

## Requisitos

Testado em **Ubuntu 24.04** (gcc 13.3, NASM, ld 2.42, QEMU 8.2.2, GRUB 2.12, Ruby 3.3, rake 13.1).
**Nao foi testado no Windows nativo.**

| Ferramenta | Para que | Obrigatoria |
|---|---|---|
| Ruby **e o comando `rake`** | `tools/build.rb`, build do mruby, testes | sim |
| NASM | montar o Assembly | sim |
| GCC (aceitando `-m32`, ou `i686-elf-gcc`) e `ld` (binutils) | compilar/linkar o kernel e o mruby | sim |
| QEMU (`qemu-system-i386` ou `qemu-system-x86_64`) | executar e testar | sim, para rodar |
| `grub-mkrescue`, `xorriso`, `grub-pc-bin`, `mtools` | gerar `RubyOS.iso` | so para a ISO |
| `make` | atalhos | nao |

**Nenhum download e necessario durante o build**: o mruby ja esta em `third_party/mruby`.

## Instalacao

### Linux (Debian/Ubuntu)

```sh
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential nasm qemu-system-x86 xorriso grub-pc-bin grub-common mtools ruby rake git
```

(`rake` e um pacote separado no Ubuntu: sem ele o build do mruby falha com "rake nao encontrado".)
`gcc-multilib` **nao** e necessario: o kernel e o mruby sao compilados em modo freestanding (sem headers
nem libc do hospedeiro) e linkados diretamente com `ld`.

**Se o `apt` reclamar de "unmet dependencies"** (por exemplo `gcc-14-base`), o indice de pacotes esta
desatualizado ou ha uma instalacao pela metade:

```sh
sudo apt update
sudo apt --fix-broken install
sudo apt upgrade -y
```

Digite o `apt install` em uma unica linha (ou use `\` no fim de cada linha, sem espaco depois).

### Windows (recomendado: WSL2)

Use o **WSL2 com Ubuntu** e os mesmos pacotes acima:

```powershell
wsl --install -d Ubuntu
```

Para ver a janela do QEMU no Windows 11, o WSLg a exibe diretamente. Alternativamente, copie
`build/RubyOS.iso` para o Windows e execute: `qemu-system-x86_64.exe -cdrom RubyOS.iso`.

**MSYS2:** oferece `mingw-w64-x86_64-qemu`, `mingw-w64-x86_64-nasm`, `make`, `ruby` e `git`, mas **nao** tem
GCC `i686-elf` nem `grub-mkrescue`; esse caminho nao foi validado e nao e recomendado.

## Compilacao

```sh
ruby tools/build.rb            # gera build/kernel.elf e build/RubyOS.iso
ruby tools/build.rb kernel     # apenas build/kernel.elf (nao precisa de GRUB)
ruby tools/build.rb clean      # apaga build/
```

Equivalentes com Make: `make`, `make kernel`, `make clean`. O **primeiro** build compila o mruby (alguns
minutos, dependendo da maquina); os seguintes reaproveitam `build/mruby` e levam segundos. O kernel final tem
cerca de 2,5 MiB (a maior parte e o mruby com informacoes de depuracao, que nao sao carregadas na memoria).

## Execucao no QEMU

```sh
qemu-system-x86_64 -cdrom build/RubyOS.iso                 # pela ISO (GRUB), em janela
qemu-system-i386   -kernel build/kernel.elf                # direto, sem GRUB
qemu-system-i386   -kernel build/kernel.elf -nographic     # tudo no terminal (Ctrl+A, X sai)
qemu-system-i386   -kernel build/kernel.elf -append shell=c   # shell C de emergencia
```

Atalhos: `make run`, `make run-kernel`, `scripts/run.sh [iso|kernel|serial]`. Use `-m 64M` ou mais
(com 16 MiB o sistema inicia, mas sobra pouco para varios programas Ruby).

## Usando a RubyShell

`help` lista os comandos e os programas; `help <comando>` mostra o uso.

### Comandos internos (escritos em Ruby, em `ruby/system/rubyshell.rb`)

| Comando | Funcao |
|---|---|
| `ls [-l] [-1] [caminho...]` | lista (`-l` detalhado); diretorios em ciano e com `/` |
| `cd [dir]`, `pwd` | muda / mostra o diretorio atual (sem argumento: `/home/user`) |
| `mkdir`, `rmdir`, `touch`, `cat`, `rm [-r]`, `cp`, `mv` | arquivos (`cp` copia so arquivos) |
| `ps` / `processes` | PID, PPID, nome, estado, memoria e tempo de vida |
| `spawn <hello\|worker\|hog\|prog.rb> [seg]` | cria uma tarefa em segundo plano (demo em C ou programa Ruby) |
| `run <prog.rb> [args]` | executa um programa Ruby em primeiro plano (Ctrl+C interrompe) |
| `kill <pid>` | encerra um processo e libera a memoria dele |
| `rb <expressao>` | avalia Ruby na propria shell (ex.: `rb 6 * 7`) |
| `memory`, `echo`, `clear`, `date`, `time`, `uptime`, `reboot`, `shutdown` | sistema |

**Qualquer outro nome e um programa Ruby**: a shell procura `/apps/NOME.rb`, `/bin/NOME.rb` e `NOME.rb`
no diretorio atual. Argumentos viram `ARGV`. Entao `calc`, `hello.rb Jota` e `fib.rb 10` funcionam direto.

**Redirecionamento:** `comando > arquivo` e `comando >> arquivo` funcionam com comandos internos **e** com
programas Ruby (`hello.rb Jota > saida.txt`). Os `>` precisam estar separados por espacos e ser os dois
ultimos argumentos. **Aspas:** abrem uma palavra apenas se estiverem no *inicio* dela (`echo "a   b"`); nao
ha escape, entao `rb` nao aceita aspas dentro da expressao (use `irb` ou um script).

**Ctrl+C** encerra o programa em primeiro plano (sem executar `ensure`) e, no prompt, cancela a linha.
**Ctrl+D** numa linha vazia encerra a entrada de `calc`, `irb`, `edit` e `files`.

### Programas incluidos (`/apps`)

| Programa | O que faz |
|---|---|
| `calc` | **Ruby Calculator**: `10 + 20` → `30`. Precedencia, parenteses, `**`, `%`, hexadecimal (`0xff`), variaveis (`x = 7`, `ans`). Divisao inteira. `calc 6 * 7` imprime `42` e sai. Tem analisador proprio (nao usa `eval`). |
| `about` | versao, kernel, runtime, CPU, memoria, arquitetura, pid |
| `files` | gerenciador de arquivos em modo texto: lista numerada; `N` abre/entra, `u` sobe, `v` le (paginado), `e` edita (chama o `edit`), `d` apaga (com confirmacao), `r` renomeia, `c` copia, `n` arquivo novo, `m` diretorio novo, `g` vai a um caminho |
| `edit arquivo.txt` | editor de texto por linhas (estilo `ed`): `l` lista, `a` acrescenta, `i N` insere, `c N` troca, `d N [M]` apaga, `w` grava, `x` grava e sai, `q`/`q!` sai |
| `irb` | REPL de Ruby; variaveis locais persistem entre as linhas |
| `selftest` | autoteste do interpretador e da ponte com o kernel (imprime `selftest: N verificacoes, 0 falhas`) |

Exemplos em `/home/user`: `hello.rb`, `fib.rb`, `uselib.rb` (usa `require "textutil"`), `erro.rb` (mostra um
erro Ruby), `loop.rb` (laco infinito para testar Ctrl+C), `memhog.rb` (bate na cota de memoria).

```
RubyOS> calc
Ruby Calculator
Digite uma expressao (help para ajuda, exit para sair).
> 10 + 20
30
> x = 7
7
> x * x + ans
56
> exit
RubyOS> loop.rb
^C
RubyOS> memhog.rb
cota atingida com 5800 KB alocados (NoMemoryError)
RubyOS> erro.rb
vou dividir por zero...
/home/user/erro.rb:3:in divide: divided by 0 (ZeroDivisionError)
	from /home/user/erro.rb:6
```

## Como criar aplicativos Ruby

**Rapido (nao persiste):** escreva com `edit`, rode com `run`:

```
RubyOS> edit /apps/ola.rb
: a
  | puts "Ola, #{ARGV[0] || 'mundo'}!"
  | .
: x
RubyOS> ola Jota
Ola, Jota!
```

Como o sistema de arquivos e a RAM, o arquivo **some ao reiniciar**. **Permanente:** coloque o arquivo em
`ruby/apps/` (ou `ruby/lib/`, `ruby/home/`, `ruby/system/`) no repositorio e rode `ruby tools/build.rb`:
`tools/embed.rb` o embute na imagem e ele existira em todo boot.

### O que um programa Ruby pode usar

- **A linguagem do mruby 3.3:** classes, modulos, blocos, `Struct`, `Comparable`, `Enumerable`, `Fiber`,
  `Enumerator` (inclusive `lazy`), `Set`, `catch/throw`, `eval`/`binding`, `Method`, metaprogramacao,
  `sprintf/format`, excecoes com `rescue/ensure/retry`. `Kernel`: `puts print p pp printf gets putc sleep rand
  srand exit abort require`. `Time.now` (relogio do RTC, so campos inteiros).
- **`RubyOS::Console`:** `write(str)`, `getc` (bloqueia; devolve o byte), `poll` (`nil` se nao ha tecla),
  `readline(prompt)`, `clear`, `color(fg, bg)`, `capture { }`.
- **`RubyOS::FS`:** `read`, `write(path, dados, anexar=false)`, `touch`, `mkdir`, `rmdir`, `rm(path, recursivo)`,
  `cp`, `mv`, `stat`, `entries`, `cwd`, `chdir`. Erros: `RubyOS::Error`.
- **`RubyOS::Proc`:** `spawn(caminho, args)` → pid, `wait(pid)`, `kill(pid)`, `alive?`, `list`, `pid`, `ppid`,
  `sleep_ms`, `yield`, `find(nome)`.
- **`RubyOS::Sys`:** `memory` (Hash), `cpu`, `rtc`, `uptime`, `ticks`, `process_memory`, `gc`, `reboot`,
  `shutdown`. Constantes `RubyOS::VERSION`, `KERNEL`, `RUNTIME`, `ARCH`.

### O que **nao** existe no Ruby da RubyOS (e por que)

- **`Float`** (`1.5`, `Math`, `10.fdiv(3)`): compilado com `MRB_NO_FLOAT` porque o kernel nao tem libm/FPU
  inicializada. `7 / 2` e `3`.
- **Inteiros de 32 bits**: passar de ±2147483647 levanta `RangeError` (nao ha bignum).
- **`Regexp`**, `File`, `IO`, `Dir`, `Thread`, `Process`, `Time` completo, `require` de gems: nao fazem parte
  desta build do mruby; use `RubyOS::FS` e `RubyOS::Proc`.
- `String#encode`/Unicode: strings sao bytes; a tela VGA mostra ASCII (sem acentos).

## Comandos da shell C de emergencia

Sao os da 0.2 (`ls cd pwd mkdir rmdir touch cat rm cp mv ps spawn kill memory echo clear about date time uptime
reboot shutdown help`) mais `run`, e ela tambem executa programas Ruby pelo nome. Nao captura a saida de
programas Ruby em `>`, e nao tem `rb`.

## Limites do sistema

| Recurso | Limite |
|---|---|
| Nome de arquivo / caminho / profundidade | 63 caracteres (ASCII imprimivel, sem `/`) / 255 / 32 niveis |
| Nos do sistema de arquivos | 1024 |
| Tamanho de um arquivo / soma de todos os arquivos | 256 KiB / 4 MiB |
| Processos simultaneos | 16 (PIDs nao sao reutilizados) |
| Tarefa em C: heap (pilha inclusa) / pilha | 1 MiB / 16 KiB |
| **Processo Ruby**: heap (pilha inclusa) / pilha | **6 MiB / 256 KiB** |
| Profundidade de chamadas Ruby | 300 niveis (depois, `SystemStackError`) |
| Argumentos de um programa Ruby | 10 |
| Heap do kernel | metade da RAM livre, no maximo 32 MiB |
| RAM enxergada | ate 4 GiB (kernel de 32 bits) |
| Saida capturada por um redirecionamento | 64 KiB (excedente e cortado, com aviso) |

Consumo medido: a RubyShell ocupa cerca de **400 KB**; um programa Ruby simples, cerca de **250 KB**.

## Testes

```sh
ruby tests/run_tests.rb        # ou: make test       (contra a RubyShell)
make test-c                    # a mesma suite contra a shell C de emergencia
```

A suite faz **verificacoes** no QEMU (89 contra a RubyShell, 85 contra a shell C: alguns testes so fazem sentido na Ruby):

1. **testes unitarios em C no PC hospedeiro**, com AddressSanitizer e UBSan: `util`, `heap` (agora com
   `krealloc`: 165 verificacoes), `pmm`, `ramfs` e `libc` (a libc minima do runtime, 32 verificacoes);
2. compilacao do kernel (incluindo o mruby) **sem avisos** e validacao do cabecalho Multiboot;
3. boot no QEMU: banner, `help`, `echo`, `about`, `date`, `time`, `uptime`, `shutdown`, `reboot`;
4. comandos de arquivos, redirecionamento e **ausencia de vazamento de heap do kernel**;
5. processos: `ps`, `spawn`, `kill`, estados, cota, tabela cheia e reaproveitamento das vagas;
6. memoria fisica com RAM de 16 MiB a 4 GiB;
7. teclado PS/2 real (teclas injetadas pelo monitor do QEMU);
8. boot pela ISO via GRUB;
9. **runtime Ruby**: programas com argumentos, erros com arquivo:linha, erro de sintaxe, `selftest` (37
   verificacoes dentro do proprio sistema), `calc`, `edit`, `files` e `irb` conduzidos pela serial, **Ctrl+C**,
   `spawn`/`kill` de Ruby, varios programas concorrentes, **cota de 6 MiB com `NoMemoryError`**, recursao
   infinita, 12 execucoes seguidas sem rastro no heap do kernel, redirecionamento de programas Ruby, `rb`,
   **`shell=c`** e **o fallback quando a RubyShell nao inicia** (`rubyshell=/nao/existe.rb`).

`RUBYOS_TEST_ONLY=regex ruby tests/run_tests.rb` roda so os testes cujo nome casa (para depurar). O QEMU roda
com `-display none`; nada no computador fisico e alterado.

## Estrutura de diretorios

```
RubyOS/
├── boot/                  boot.asm (Multiboot), isr.asm, switch.asm, linker.ld
├── kernel/
│   ├── kernel.c/.h        kmain, linha de comando do Multiboot, panic
│   ├── memory/pmm/heap    mapa de memoria -> PMM -> heap (kmalloc/krealloc/kfree, donos, cotas)
│   ├── ramfs.c/.h         sistema de arquivos em RAM
│   ├── process.c/.h       processos, escalonador cooperativo
│   ├── console.c/.h       fila de entrada (teclado + serial) e Ctrl+C
│   ├── terminal.c/.h      VGA 80x25 + serial + kprintf + captura de saida
│   ├── interrupts.c/.h    GDT, IDT, PIC
│   ├── sysinfo.c/.h       CPUID, reboot, shutdown
│   ├── shell*.c           shell C de emergencia
│   ├── ruby/              rb_runtime.c (processos Ruby), rb_bindings.c (RubyOS::*), rb_embedded.h
│   ├── libc/              string, ctype, stdlib, errno, setjmp.asm + include/ (libc minima do mruby)
│   └── drivers/           serial, timer (PIT), keyboard (PS/2), rtc (CMOS)
├── ruby/
│   ├── system/            prelude.rb, rubyshell.rb
│   ├── apps/              calc, about, files, edit, irb, selftest
│   ├── lib/               textutil.rb (exemplo de biblioteca)
│   ├── home/user/         programas de exemplo
│   └── runtime/           mruby_build_config.rb (como o mruby e compilado)
├── third_party/mruby/     mruby 3.3.0 podado (MIT) + PATCHES.md
├── filesystem/            arquivos extras a embutir na imagem (alem dos .rb)
├── tools/                 build.rb, embed.rb, create_image.rb, common.rb
├── tests/                 run_tests.rb e testes unitarios em C (tests/host/)
├── scripts/run.sh   Makefile   README.md   LICENSE
```

## Como contribuir

1. Rode `make test-all` antes e depois de mudar qualquer coisa; tudo deve passar.
2. Nada de pseudocodigo; se algo e prototipo, documente aqui.
3. Codigo sem dependencia de hardware (`util`, `heap`, `pmm`, `ramfs`, `libc`) vai com testes em `tests/host/`.
4. **Novo programa Ruby:** crie `ruby/apps/nome.rb`; ele aparece em `help` e roda pelo nome. **Novo comando
   interno da RubyShell:** `command("Grupo:", "nome", "uso", "ajuda") { |args| ... }` em `ruby/system/rubyshell.rb`
   (devolva `0` se ok). Para a shell C, linha na tabela `commands[]` de `shell_fs.c/shell_proc.c/shell_sys.c`.
5. **Nova funcao do kernel para o Ruby:** funcao nativa e linha `DEF(...)` em `kernel/ruby/rb_bindings.c`.
   Mantenha-a fina: a logica deve ficar em Ruby.
6. Strings exibidas pelo kernel e pelos programas devem ser ASCII puro (a VGA nao exibe UTF-8).
7. Tarefas em **C** precisam chamar `process_yield()`/`process_sleep()`; em Ruby isso e automatico.
8. Opcoes `MRB_*` mudam o layout das estruturas do mruby: se alterar uma, altere **igual** em
   `ruby/runtime/mruby_build_config.rb` e em `MRUBY_DEFINES` (`tools/build.rb`) e apague `build/mruby`.

## Limitacoes atuais

- **Sem seguranca real.** Tudo (kernel, shell, programas Ruby) roda no anel 0 e no mesmo espaco de enderecos.
  Um programa Ruby so alcanca o kernel pela API `RubyOS::*`, mas **nao ha isolamento imposto pelo hardware**:
  um bug no interpretador (C) pode derrubar o sistema. Existem: cota de heap por processo, canario de pilha,
  protecao de pilha no gancho da VM, deteccao de double free no heap, `kernel_panic` em excecoes da CPU.
- **Escalonador cooperativo.** Sem preempcao por timer (0.4). Programas Ruby cedem a CPU sozinhos (a cada 1024
  instrucoes), mas uma chamada nativa longa ou uma tarefa em C que nao cede trava o sistema. Ctrl+C mata sem
  rodar `ensure`.
- **Ruby = mruby**, subconjunto: sem `Float`, `Regexp`, `File/IO`, bignum nem threads (ver secao acima).
- **Sem persistencia.** O ramfs vive na RAM: programas escritos com `edit` somem ao reiniciar.
- **Editor e gerenciador de arquivos sem tela cheia**: o teclado so entrega caracteres simples (sem setas,
  Del, Home, F-keys; so Shift, Ctrl+letra e Esc). Eles sao orientados a linha de comando de proposito.
- **Shell simples:** sem pipes, entrada padrao redirecionavel, historico, globbing, variaveis nem Tab. Aspas
  so no inicio de uma palavra e sem escape. A tela nao tem rolagem para tras.
- **`cp` nao copia diretorios.** Sem links, `chmod`, timestamps nem `find`.
- **32 bits** (i686), RAM acima de 4 GiB nao e usada. Sem paginacao.
- Teclado: layout US. Pela serial, setas inserem lixo na linha.
- `shutdown` so desliga em VMs; `date`/`time` mostram o que o RTC informar (assume seculo 20xx).
- Nao foi testado em hardware real nem no Windows nativo. O primeiro build demora (compila o mruby).

## Roadmap

| Versao | Conteudo | Estado |
|---|---|---|
| 0.1 | Boot, kernel minimo, terminal, teclado, shell basica | feita e testada |
| 0.2 | Memoria, filesystem, comandos de arquivos, processos basicos | feita e testada |
| **0.3** | **Runtime Ruby (mruby), scripts, apps Ruby (calc, files, edit, irb), RubyShell em Ruby** | **feita e testada** |
| 0.4 | Scheduler preemptivo, drivers (disco), permissoes | planejada |
| 0.5 | Interface grafica (RubyShell Desktop), janelas | planejada |
| 1.0 | Sistema integrado, documentacao, imagem final | planejada |

## Licenca

MIT. Veja [LICENSE](LICENSE). O mruby (`third_party/mruby`) e distribuido sob a licenca MIT do proprio
projeto (`third_party/mruby/LICENSE`, Copyright (c) mruby developers).
