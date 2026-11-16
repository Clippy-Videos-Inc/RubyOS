# RubyOS 0.6 ("Anel")

Sistema operacional experimental que usa **Ruby como linguagem da camada de usuario**, com Assembly e C
apenas onde sao tecnicamente necessarios.

> **Estado real da 0.6:** inicializa no QEMU e **executa Ruby de verdade dentro do kernel**: o interpretador
> [mruby](https://mruby.org) 3.3.0 esta embutido (compilado para i386 sem sistema operacional por baixo,
> sobre uma libc minima escrita para este projeto). A shell (`RubyShell`), a calculadora, o gerenciador de
> arquivos, o editor de texto e o REPL sao **programas Ruby** que rodam como processos do kernel, cada um
> com seu proprio interpretador, memoria contada e limitada, e podem ser interrompidos com Ctrl+C ou `kill`.
> A shell em C da 0.2 continua no kernel como **shell de emergencia**. O escalonador e **preemptivo** (um
> timer troca de tarefa sozinho), o sistema de arquivos tem **donos e permissoes** (estilo Unix, so sem
> grupos), e os arquivos **podem persistir em disco** (`save`/`load`, um driver ATA PIO simples). Uma
> **interface grafica** opcional (`gfx=1` na linha de boot) oferece modo VBE (via a interface Bochs
> "dispi", a mesma usada pelo QEMU/Bochs/VirtualBox), mouse PS/2 e um gerenciador de janelas simples (o
> "RubyShell Desktop"): a shell de sempre passa a rodar dentro de uma janela arrastavel, ao lado de uma
> janela "Sobre" e de uma barra de tarefas com Menu Iniciar. Novo na 0.6 ("Anel"): **paginacao de memoria
> de verdade** (identidade de 4 GiB com paginas de 4 MiB, PSE) e **isolamento real em anel 3**: processos de
> demonstracao (`spawn ring3`) rodam com hardware impedindo acesso ao kernel, falam com ele so por
> `int 0x80` (syscall), e uma falha de pagina ou protecao geral (`spawn ring3fault`, `spawn ring3gpf`) **e
> tratada e recuperada** -- so o processo que falhou morre, o sistema continua de pe (ver
> [Paginacao e modo usuario](#paginacao-e-modo-usuario-anel-3)). Limites importantes: o Ruby e o **mruby (um
> subconjunto de Ruby)**, **sem Float** e com inteiros de 32 bits; a persistencia em disco e so
> serializacao (nao e um sistema de arquivos em disco de verdade); so os processos de demonstracao em
> Assembly rodam de fato em anel 3 -- os processos Ruby e os comandos da shell continuam em anel 0 (nao ha
> ainda um carregador de executaveis de usuario nem CR3 por processo; ver
> [Limitacoes atuais](#limitacoes-atuais)); a interface grafica e so uma "pele" por
> cima do terminal de texto de sempre (ver [Interface grafica](#interface-grafica-rubyshell-desktop)).
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
 Kernel (C)               processos, escalonador preemptivo (PIT), ramfs com donos/permissoes, disco (ATA
        ↓                 PIO) e persistencia, heap com donos, console, terminal, GDT/IDT/PIC, timer,
        ↓                 teclado, mouse PS/2, serial, RTC; interface grafica opcional (kernel/gfx/:
        ↓                 VBE/Bochs dispi, framebuffer, gerenciador de janelas); shell C de emergencia
        ↓                 (kernel/shell*.c)
 Boot (Assembly + GRUB)   boot/boot.asm, boot/isr.asm, boot/switch.asm, kernel/libc/setjmp.asm
```

**Fluxo de boot:** BIOS → GRUB (ou `qemu -kernel`) → cabecalho Multiboot em `boot/boot.asm` → `kmain()` →
GDT, PIC, IDT, timer, teclado → `mm_init` (mapa de memoria, PMM, heap) → `fs_init` (arvore inicial) →
`process_init` (o boot vira o processo 1, `init`) → `ruby_seed_filesystem` (copia os `.rb` embutidos na imagem
para `/apps`, `/system`, `/lib` e `/home/user`) → `ruby_start_shell` cria o processo 2 (`shell`), que abre um
interpretador mruby, carrega `/system/prelude.rb` e executa `/system/rubyshell.rb` → (se `gfx=1`) `desktop_init`
liga o modo grafico e cria a janela do terminal. O `init` vira a tarefa ociosa e, em modo grafico, tambem
conduz o compositor (`desktop_tick`, chamado a cada interrupcao que acorda o `init`).

**Se a RubyShell nao subir** (script ausente, erro, falta de memoria), o proprio processo `shell` cai na shell
C: voce nunca fica sem console. Tambem da para pedi-la de proposito: no GRUB, a entrada
"RubyOS 0.5 (shell C de emergencia)" passa `shell=c` na linha de comando do Multiboot. Tanto a RubyShell
quanto a shell C de emergencia rodam como o usuario comum (uid 1000, nao root) desde a 0.4.

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
Sem timestamps. Desde a 0.4, cada no tem **dono (uid) e modo** (9 bits estilo Unix, so com duas classes:
dono e outros, sem grupos) -- veja [Permissoes](#permissoes). Por padrao ainda e so RAM (some ao
reiniciar), a menos que voce use `save`/`load` -- veja [Disco e persistencia](#disco-e-persistencia).

### Processos (`kernel/process.c`, `boot/switch.asm`)

Cada processo e uma tarefa do kernel com **pilha propria** (com canario), **troca de contexto real** e um
**uid** (dono do processo, ver Permissoes). Desde a 0.4 o escalonador e **preemptivo**: o timer (PIT, IRQ0)
troca de tarefa sozinho a cada 5 interrupcoes (`PROC_TIMESLICE_TICKS`), entao uma tarefa em C sem nenhum
ponto de cessao (`while (1) { ... }`, sem `yield`/`sleep`) **nao trava mais o sistema** -- experimente
`spawn spin`. No Ruby a troca tambem continua acontecendo pelo gancho da VM a cada 1024 instrucoes, como
antes. O modo cooperativo das versoes anteriores continua disponivel: `preempt=off` na linha de comando do
Multiboot desliga a preempcao pelo timer. Estados: `READY`, `RUNNING`, `SLEEPING` (e `ZOMBIE`, so ate a
liberacao). `init` e `shell` sao essenciais.

### Permissoes

Donos e um modo de 9 bits (estilo Unix), mas com **duas classes apenas**: dono (`rwx`) e outros (`rwx`) --
sem grupos, ja que o sistema nao tem usuarios cadastrados. `UID_ROOT` (0) ignora toda permissao, como o
root de um Unix de verdade; `UID_USER` (1000) e o usuario comum, dono do seu `/home/user` e de tudo o que
cria. A RubyShell e a shell C de emergencia comecam como `UID_USER`; `su <uid>` troca de usuario **sem
senha** (nao ha autenticacao nesta versao -- isso organiza quem e dono do que e permite testar as
permissoes de verdade, mas nao e uma fronteira de seguranca). `whoami`/`id` mostram o usuario atual;
`chmod <octal> <caminho>` muda o modo (dono ou root); `chown <uid> <caminho>` troca o dono (so root).
Rodar um arquivo (`run`/`spawn`, ou pelo nome) exige o bit de execucao; criar/apagar/escrever exige escrita
no diretorio (apagar) ou no proprio arquivo (escrever); negado vira `RubyOS::Error` ("permissao negada") na
shell Ruby ou uma mensagem equivalente na shell C. **Nao e isolamento real pelas permissoes em si**: elas
sao um modelo sobre um unico espaco de memoria, nao uma fronteira de seguranca -- mas a 0.6 adiciona uma
fronteira de verdade, em hardware, para quem roda em anel 3 (ver a seguir).

### Paginacao e modo usuario (anel 3) (`kernel/paging.c`, `kernel/syscall.c`, `kernel/ring3_demo.c`)

Novo na 0.6. `kernel/paging.c` liga a paginacao x86 (`pg_init()`, chamado no boot) com um **Page Directory
unico, identidade de 4 GiB usando paginas de 4 MiB (PSE, CR4.PSE)**: todo endereco fisico fica mapeado no
mesmo endereco virtual, supervisor-only por padrao. Isso significa **zero mudanca** em qualquer driver ou
subsistema existente (framebuffer, heap, pilhas) -- eles continuam enxergando os mesmos enderecos de
sempre. Quando um processo precisa de uma pagina acessivel em anel 3, `pg_make_user_accessible()` faz
"remendo de janela": troca so a PDE de 4 MiB daquele endereco por uma tabela de 4 KiB normal, com a pagina
pedida marcada `U=1` e todas as outras 1023 entradas daquela janela replicando a identidade original (como
supervisor-only) -- as outras 4092 paginas inalteradas continuam como paginas de 4 MiB.

A GDT ganhou segmentos de anel 3 (codigo/dados, DPL 3) e uma TSS (`esp0`/`ss0` atualizados a cada troca de
contexto, para que uma interrupcao vinda do anel 3 va parar numa pilha do kernel valida). `enter_usermode()`
(`boot/switch.asm`) monta manualmente o quadro que a CPU monta sozinha ao entrar numa interrupcao, e executa
`iret`: como o CS de destino tem RPL 3, o `iret` troca de privilegio, troca `SS:ESP` tambem, e o processo
passa a rodar em anel 3 de verdade. `kernel/syscall.c` atende `int 0x80` (unico vetor com DPL 3 na IDT --
todos os outros 47 continuam DPL 0, entao um processo de anel 3 nao consegue disparar excecoes/IRQs por
conta propria, so causar as suas de verdade): `SYS_WRITE` (so dentro da unica pagina que aquele processo tem
acesso), `SYS_EXIT` e `SYS_YIELD`.

Falhas de protecao (#PF -- falha de pagina, vetor 14; #GP -- protecao geral, vetor 13) sao interceptadas
antes do caminho generico de kernel panic: se o processo que falhou nao for essencial, ele e marcado
`ZOMBIE` e o escalonador segue para o proximo -- **so aquele processo morre, o sistema continua**. So cai em
kernel panic se nao houver processo atual ou se o processo essencial (`init`) falhar.

`spawn ring3` (fala com o kernel via `SYS_WRITE` e termina), `spawn ring3fault` (desreferencia NULL de
proposito, dispara #PF) e `spawn ring3gpf` (executa `cli`, instrucao privilegiada, dispara #GP) sao
processos de demonstracao escritos a mao em Assembly (posicao-independente, ja que cada um carrega num
endereco fisico diferente) -- disponiveis nas duas shells (`help spawn`). O comando `memory` ganhou uma
secao "Paginacao" mostrando se esta ligada. Ver [Limitacoes atuais](#limitacoes-atuais) para o que ainda nao
existe (CR3 por processo, um carregador de executaveis de usuario de verdade).

### Disco e persistencia (`kernel/drivers/ata.c`, `kernel/fs_persist.c`)

Um driver **ATA PIO** simples (barramento primario, disco mestre, LBA28, por polling -- sem IRQ 14, sem
DMA, sem escravo nem ATAPI) le e escreve setores de 512 bytes. `fs_save`/`fs_load` (expostos como os
comandos `save`/`load`, **so para root**) serializam a arvore inteira (nomes, tipos, conteudo, dono, modo)
num formato proprio e gravam/leem a partir do setor 0 do disco. No boot, se houver uma imagem valida no
disco ela e carregada automaticamente (substituindo a semeadura padrao); sem disco anexado (sem `-drive` no
QEMU) a persistencia fica so desligada, sem erro. **Isto nao e um sistema de arquivos em disco de
verdade**: sem bitmap de blocos, sem journaling, um unico "arquivo" serializado ocupando uma faixa de
setores -- de proposito simples, para dar persistencia real sem reimplementar um FS em disco inteiro.
`scripts/run.sh kernel`/`serial` ja anexam `build/disk.img` (criado na primeira vez, 8 MiB) como disco IDE.

**Console** (`kernel/console.c`): uma fila de entrada unica alimentada pelo teclado PS/2 e pela serial
(com Ctrl+C detectado mesmo se o programa nao esta lendo). Toda saida vai para a tela VGA 80x25 **e** para a
serial COM1 -- isso continua valendo em modo grafico (abaixo): a serial nunca para de espelhar o terminal.

### Interface grafica ("RubyShell Desktop", `kernel/gfx/`)

Opcional, ligada com `gfx=1` na linha de boot (ver [Execucao no QEMU](#execucao-no-qemu)). Sem esse
parametro o sistema continua exatamente como nas versoes anteriores (texto, VGA 80x25).

- **Modo grafico** (`kernel/drivers/vbe.c`): resolucao fixa 1024x768x32bpp, ligada atraves da interface
  **Bochs VBE** ("dispi", portas 0x1CE/0x1CF) -- a mesma interface usada pelo QEMU (`-vga std`, o padrao),
  pelo Bochs e pela VirtualBox. Nao precisa de chamadas de BIOS em modo real: so E/S em portas, depois de
  achar o framebuffer linear pelo barramento PCI (`kernel/drivers/pci.c`, BAR0 do controlador de video,
  classe 0x03). Sem VBE (placa de video incompativel) ou sem controlador de video no PCI, o boot avisa e
  **cai de volta para o modo texto**, sem travar.
- **Framebuffer** (`kernel/gfx/fb.c`): um back buffer em RAM onde tudo e desenhado -- so depois vai de uma
  vez para a memoria de video de verdade (`fb_present`), para nao cintilar nem fazer leituras lentas da
  placa. O texto usa a **fonte 8x16 de verdade da VGA**, extraida do hardware (plano 2 da memoria de
  caracteres) antes de trocar de modo -- nao e uma fonte inventada.
- **Mouse PS/2** (`kernel/drivers/mouse.c`): porta auxiliar do controlador 8042, IRQ 12, pacotes padrao de
  3 bytes (posicao relativa + botoes).
- **Gerenciador de janelas** (`kernel/gfx/wm.c`): so logica (geometria, ordem Z, foco, arrastar pela barra
  de titulo, o Menu Iniciar) -- sem nenhuma chamada de hardware, de proposito, para dar para testar no PC
  hospedeiro (`tests/host/test_wm.c`, como o ramfs/heap/pmm). Quem desenha de verdade e quem le o
  mouse/teclado fica em `kernel/gfx/desktop.c`.
- **A area de trabalho**: a shell de sempre (RubyShell ou a shell C) continua rodando exatamente igual --
  o terminal de texto so passa a desenhar numa janela "Terminal" (arrastavel, mas nao fecha: e a shell do
  sistema) em vez de ir direto para a tela. Uma segunda janela, "Sobre" (fechavel), mostra a versao do
  sistema. Uma barra de tarefas com **Menu Iniciar** (o icone da gema de rubi) lista Terminal, Calculadora,
  Arquivos e Sobre -- escolher Calculadora/Arquivos foca o terminal e digita o comando (`calc`/`files`) por
  voce, como se voce tivesse digitado.
- **Icones** (`assets/icons/*.png`, convertidos para `kernel/gfx/icons_data.c` por `tools/gen_icons.py`):
  desenhados para este projeto. `tools/gen_icons.py` e uma ferramenta de desenvolvimento (precisa de Python
  e Pillow) usada so quando os icones mudam -- o `.c` gerado ja fica no repositorio, entao compilar o
  RubyOS nunca depende de Python.

A interface grafica e, de proposito, **uma pele sobre o terminal de texto de sempre**, nao um ambiente
grafico completo: nao ha aplicativos com sua propria janela grafica (so a shell de texto dentro de uma
moldura), nem redimensionar janelas, nem suporte a varios terminais ao mesmo tempo. A troca de modo (texto
↔ grafico) nunca muda o que a shell/o ramfs/os processos fazem -- so onde (e como) a saida e desenhada.

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
qemu-system-i386   -kernel build/kernel.elf -append preempt=off   # sem preempcao pelo timer (0.3)
qemu-system-i386   -kernel build/kernel.elf -drive file=build/disk.img,format=raw,if=ide   # com disco
qemu-system-i386   -kernel build/kernel.elf -append gfx=1   # com a interface grafica (0.5)
```

Atalhos: `make run`, `make run-kernel`, `scripts/run.sh [iso|kernel|serial]`. Use `-m 64M` ou mais
(com 16 MiB o sistema inicia, mas sobra pouco para varios programas Ruby). `scripts/run.sh kernel`/`serial`
ja criam e anexam `build/disk.img` (8 MiB) automaticamente, para `save`/`load` funcionarem; a ISO (`iso`)
continua sem disco anexado. Os parametros de boot combinam: `-append "gfx=1 shell=c"` liga a interface
grafica com a shell C de emergencia dentro da janela do terminal, por exemplo.

## Usando a RubyShell

`help` lista os comandos e os programas; `help <comando>` mostra o uso.

### Comandos internos (escritos em Ruby, em `ruby/system/rubyshell.rb`)

| Comando | Funcao |
|---|---|
| `ls [-l] [-1] [caminho...]` | lista (`-l` detalhado: tipo, modo, dono, tamanho); diretorios em ciano e com `/` |
| `cd [dir]`, `pwd` | muda / mostra o diretorio atual (sem argumento: `/home/user`) |
| `mkdir`, `rmdir`, `touch`, `cat`, `rm [-r]`, `cp`, `mv` | arquivos (`cp` copia so arquivos) |
| `chmod <octal> <caminho>...` | muda o modo (dono ou root) |
| `chown <uid> <caminho>...` | troca o dono (so root) |
| `whoami`, `id` | usuario atual (nome / `uid=N(nome)`) |
| `su <uid>` | troca de usuario (0 = root, 1000 = user; sem senha nesta versao) |
| `save` / `load` | grava o ramfs no disco / substitui pela imagem do disco (so root) |
| `ps` / `processes` | PID, PPID, nome, estado, memoria e tempo de vida |
| `spawn <hello\|worker\|hog\|spin\|ring3\|ring3fault\|ring3gpf\|prog.rb> [seg]` | cria uma tarefa em segundo plano (demo em C, demo de anel 3, ou programa Ruby) |
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
  `cp`, `mv`, `stat`, `entries`, `cwd`, `chdir`, `perm(path)` → `[dono, modo]`, `chmod(path, modo)`,
  `chown(path, dono)`, `save`, `load`, `disk_image?` (os tres ultimos exigem root). Erros: `RubyOS::Error`.
- **`RubyOS::Proc`:** `spawn(caminho, args)` → pid, `wait(pid)`, `kill(pid)`, `alive?`, `list`, `pid`, `ppid`,
  `sleep_ms`, `yield`, `find(nome)`, `uid`, `setuid(uid)`.
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
reboot shutdown help`) mais `run`, e ela tambem executa programas Ruby pelo nome. Desde a 0.4 tambem tem
`chmod`, `chown`, `whoami`, `id`, `su`, `save` e `load` (mesmo significado que na RubyShell). Nao captura a
saida de programas Ruby em `>`, e nao tem `rb`.

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

A suite faz **111 verificacoes** no QEMU contra a RubyShell (menos contra a shell C: alguns testes so fazem
sentido na Ruby):

1. **testes unitarios em C no PC hospedeiro**, com AddressSanitizer e UBSan: `util`, `heap` (com
   `krealloc`: 165 verificacoes), `pmm`, `ramfs` (com donos/permissoes e `fs_reset`: 3472 verificacoes),
   `libc` (a libc minima do runtime, 32 verificacoes) e `wm` (o gerenciador de janelas da interface
   grafica, 0.5: geometria, ordem Z, foco, arrastar, Menu Iniciar -- 43 verificacoes);
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
   **`shell=c`** e **o fallback quando a RubyShell nao inicia** (`rubyshell=/nao/existe.rb`);
10. **scheduler preemptivo**: `spawn spin` (tarefa sem ponto de cessao) nao trava o sistema, e `preempt=off`
    continua disponivel para o modo cooperativo das versoes anteriores;
11. **permissoes**: a shell roda como `user`, dono de arquivo novo, `chmod`/`chown`, `su` e a negacao real
    de escrita entre usuarios, e o bit de execucao exigido por `run`/`spawn`;
12. **disco e persistencia**: `save`/`load` sobrevivem a uma reinicializacao completa do QEMU com o mesmo
    disco, e `load` tambem funciona sob demanda sem reiniciar;
13. **interface grafica**: `gfx=1` liga o modo VBE (mensagem de sucesso com a resolucao), cai de volta para
    o modo texto sem travar quando nao ha placa compativel (`-vga none`), e a shell pela serial continua
    respondendo identica com a area de trabalho ativa (nenhuma regressao);
14. **paginacao e anel 3 (0.6)**: `memory` mostra a paginacao ligada, `spawn ring3` roda em anel 3 de
    verdade e fala com o kernel via `int 0x80`, `spawn ring3fault`/`ring3gpf` disparam #PF/#GP de proposito
    e **o sistema se recupera** (so o processo que falhou morre, sem zumbis no `ps` depois), nas duas
    shells (Ruby e `shell=c`).

`RUBYOS_TEST_ONLY=regex ruby tests/run_tests.rb` roda so os testes cujo nome casa (para depurar). O QEMU roda
com `-display none`; nada no computador fisico e alterado. **Nota sobre o teste da interface grafica:** com
`-display none` nao ha nenhum console "ativo" no QEMU para onde o monitor (`mouse_move`/`mouse_button`) ou o
QMP (`input-send-event`) possam rotear eventos de mouse -- so chegam ao convidado com um backend de video de
verdade (janela SDL/GTK, ou VNC). Por isso os testes automatizados cobrem o boot, o fallback e a ausencia de
regressao por serial, e a logica de arrastar/focar/Menu Iniciar e coberta pelos 43 testes do `wm` no PC
hospedeiro (sem hardware nenhum). O que foi confirmado com capturas de tela (`screendump` do QEMU) nesta
versao: o modo grafico liga, a fonte (extraida do hardware VGA) desenha corretamente, e a area de trabalho
(janela do terminal com o boot/a shell, barra de tarefas, icone da gema no Menu Iniciar, cursor) aparece como
esperado; a propria IRQ do mouse foi confirmada recebendo e decodificando pacotes reais do dispositivo PS/2
durante a inicializacao. A interacao de verdade (arrastar, clicar) depende de um display real (`make run` /
`scripts/run.sh kernel` numa maquina com janela) e nao foi verificada dentro deste ambiente sem video --
quem rodar localmente com `gfx=1` e uma janela do QEMU de verdade pode confirmar.

## Estrutura de diretorios

```
RubyOS/
├── boot/                  boot.asm (Multiboot), isr.asm, switch.asm, linker.ld
├── kernel/
│   ├── kernel.c/.h        kmain, linha de comando do Multiboot, panic
│   ├── memory/pmm/heap    mapa de memoria -> PMM -> heap (kmalloc/krealloc/kfree, donos, cotas)
│   ├── ramfs.c/.h         sistema de arquivos em RAM
│   ├── process.c/.h       processos, escalonador cooperativo/preemptivo, processos de anel 3
│   ├── paging.c/.h        paginacao (0.6): identidade de 4 GiB com PSE, remendo de janela p/ anel 3
│   ├── syscall.c/.h       despacho de "int 0x80" (0.6): SYS_WRITE, SYS_EXIT, SYS_YIELD
│   ├── ring3_demo.c/.h    processos de demonstracao em anel 3 (0.6), Assembly posicao-independente
│   ├── console.c/.h       fila de entrada (teclado + serial) e Ctrl+C
│   ├── terminal.c/.h      VGA 80x25 + serial + kprintf + captura de saida
│   ├── interrupts.c/.h    GDT (com segmentos de anel 3 e TSS, 0.6), IDT, PIC
│   ├── sysinfo.c/.h       CPUID, reboot, shutdown
│   ├── shell*.c           shell C de emergencia
│   ├── ruby/              rb_runtime.c (processos Ruby), rb_bindings.c (RubyOS::*), rb_embedded.h
│   ├── libc/              string, ctype, stdlib, errno, setjmp.asm + include/ (libc minima do mruby)
│   ├── fs_persist.c       save/load: serializa o ramfs e grava/le pelo driver ATA (depende de hardware)
│   ├── drivers/           serial, timer (PIT), keyboard e mouse (PS/2), rtc (CMOS), ata (disco IDE, PIO),
│   │                      pci (so leitura, acha o controlador de video), vbe (modo grafico, Bochs dispi)
│   └── gfx/               interface grafica opcional (0.5): fb (framebuffer + fonte), icons (dados dos
│                          icones), wm (gerenciador de janelas, logica pura/testavel), desktop (compositor)
├── assets/icons/          PNGs de origem dos icones (ver tools/gen_icons.py)
├── ruby/
│   ├── system/            prelude.rb, rubyshell.rb
│   ├── apps/              calc, about, files, edit, irb, selftest
│   ├── lib/               textutil.rb (exemplo de biblioteca)
│   ├── home/user/         programas de exemplo
│   └── runtime/           mruby_build_config.rb (como o mruby e compilado)
├── third_party/mruby/     mruby 3.3.0 podado (MIT) + PATCHES.md
├── filesystem/            arquivos extras a embutir na imagem (alem dos .rb)
├── tools/                 build.rb, embed.rb, create_image.rb, common.rb, gen_icons.py (icones, opcional)
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
7. Desde a 0.4 o escalonador e preemptivo (o timer troca de tarefa sozinho), mas tarefas em **C**
   cooperativas continuam chamando `process_yield()`/`process_sleep()` quando fizer sentido ceder de
   proposito (a preempcao evita que uma tarefa sem isso trave o sistema, nao substitui ceder bem); em Ruby
   isso e automatico (gancho da VM).
8. Opcoes `MRB_*` mudam o layout das estruturas do mruby: se alterar uma, altere **igual** em
   `ruby/runtime/mruby_build_config.rb` e em `MRUBY_DEFINES` (`tools/build.rb`) e apague `build/mruby`.

## Limitacoes atuais

- **Isolamento real existe desde a 0.6, mas so para os processos de demonstracao de anel 3** (`spawn ring3`,
  `ring3fault`, `ring3gpf` -- ver [Paginacao e modo usuario](#paginacao-e-modo-usuario-anel-3)). O kernel, a
  shell e **todos os programas Ruby continuam rodando no anel 0 e no mesmo espaco de enderecos**: um
  programa Ruby so alcanca o kernel pela API `RubyOS::*`, mas nada no hardware o impede de ir alem -- um bug
  no interpretador (C) ainda pode derrubar o sistema. Existem: cota de heap por processo, canario de pilha,
  protecao de pilha no gancho da VM, deteccao de double free no heap, `kernel_panic` em excecoes da CPU.
- **Um unico Page Directory, compartilhado por todos os processos (sem CR3 por processo).** Se dois
  processos de anel 3 rodassem ao mesmo tempo, cada um enxergaria a pagina de usuario do outro -- nao ha
  espacos de enderecos separados ainda, so a mesma identidade de 4 GiB remendada em pontos especificos. Nao
  ha tambem um carregador de executaveis de usuario de verdade (ELF ou outro formato): so os tres programas
  de demonstracao embutidos (`kernel/ring3_demo.c`), escritos a mao em Assembly.
- **Escalonador preemptivo desde a 0.4, mas so entre tarefas.** O timer troca de tarefa sozinho (uma tarefa
  sem ponto de cessao nao trava mais o sistema), mas nao ha preempcao *dentro* de uma unica chamada nativa
  longa no kernel (uma rotina em C que nunca retorna ainda trava tudo). Ctrl+C mata sem rodar `ensure`.
- **Ruby = mruby**, subconjunto: sem `Float`, `Regexp`, `File/IO`, bignum nem threads (ver secao acima).
- **Permissoes sem isolamento real.** Donos e modo (ver [Permissoes](#permissoes)) organizam quem pode
  fazer o que, mas tudo roda no anel 0: nenhuma permissao vale para o kernel em C, e `su` troca de usuario
  sem senha (nao ha autenticacao).
- **Persistencia so por serializacao.** `save`/`load` gravam/leem a arvore inteira de uma vez (ver
  [Disco e persistencia](#disco-e-persistencia)); nao e um sistema de arquivos em disco de verdade (sem
  bitmap de blocos, sem journaling, sem escrita incremental). Sem `save`, tudo continua so em RAM.
- **Editor e gerenciador de arquivos sem tela cheia**: o teclado so entrega caracteres simples (sem setas,
  Del, Home, F-keys; so Shift, Ctrl+letra e Esc). Eles sao orientados a linha de comando de proposito.
- **Shell simples:** sem pipes, entrada padrao redirecionavel, historico, globbing, variaveis nem Tab. Aspas
  so no inicio de uma palavra e sem escape. A tela nao tem rolagem para tras.
- **`cp` nao copia diretorios.** Sem links nem `find`. Timestamps continuam sem existir.
- **Driver de disco minimo:** so ATA PIO, barramento primario, disco mestre, LBA28 (sem DMA, sem escravo,
  sem ATAPI, sem discos > 128 GiB).
- **Interface grafica e so uma "pele" por cima do console.** O RubyShell Desktop (`kernel/gfx/`) desenha a
  mesma tela de texto 80x25 de sempre dentro de uma janela, mais uma janela de informacoes e o Menu
  Iniciar; nao ha varios terminais, redimensionar janelas, mover por arrastar-e-soltar testado de ponta a
  ponta, nem apps graficos de verdade. So existe quando o kernel e iniciado com `gfx=1` na linha de comando;
  sem isso (ou sem VBE disponivel), o sistema cai de volta no console VGA de texto puro, igual as versoes
  anteriores.
- **Mouse PS/2 funciona, mas a interacao por clique/arraste nao foi verificada automaticamente.** O driver
  (`kernel/drivers/mouse.c`) troca pacotes com o controlador 8042 corretamente (confirmado por log serial
  durante o desenvolvimento), mas o ambiente de testes automatizado roda QEMU com `-display none`, e nessa
  configuracao os comandos de mouse do QEMU (HMP `mouse_move`/`mouse_button`, QMP `input-send-event`) nao
  tem para onde entregar eventos sinteticos — por isso os testes automatizados cobrem boot/fallback/regressao
  da shell com `gfx=1`, mas nao clique ou arrasto de janela real; isso so foi visto funcionando manualmente
  com tela de verdade.
- **32 bits** (i686), RAM acima de 4 GiB nao e usada. A paginacao (0.6) e so identidade (mesmo endereco
  fisico e virtual); nao ha memoria virtual de verdade nem espacos de enderecos distintos por processo.
- Teclado: layout US. Pela serial, setas inserem lixo na linha.
- `shutdown` so desliga em VMs; `date`/`time` mostram o que o RTC informar (assume seculo 20xx).
- Nao foi testado em hardware real nem no Windows nativo. O primeiro build demora (compila o mruby).

## Roadmap

| Versao | Conteudo | Estado |
|---|---|---|
| 0.1 | Boot, kernel minimo, terminal, teclado, shell basica | feita e testada |
| 0.2 | Memoria, filesystem, comandos de arquivos, processos basicos | feita e testada |
| 0.3 | Runtime Ruby (mruby), scripts, apps Ruby (calc, files, edit, irb), RubyShell em Ruby | feita e testada |
| **0.4** | **Scheduler preemptivo (timer), driver de disco (ATA PIO) com persistencia (save/load), permissoes (donos/modo, su, chmod/chown)** | **feita e testada** |
| 0.5 | Interface grafica (RubyShell Desktop), janelas | feita e testada |
| **0.6 ("Anel")** | **Paginacao de memoria, isolamento kernel/processo em anel 3, excecoes de protecao (page fault, GPF tratados sem derrubar o sistema)** | **feita e testada** |
| 0.7 | Kit de widgets graficos (botao, campo de texto, lista, menu) para o gerenciador de janelas, mais apps Ruby de exemplo com interface grafica | planejada |
| 0.8 | Disco mais amplo (segundo disco/escravo, deteccao melhor do controlador, bitmap de blocos real) | planejada |
| 0.9 | Video real via VESA BIOS (alem do Bochs dispi ja existente); melhorias de shell (pipes, historico, Tab, variaveis) | planejada |
| 1.0 | Sistema integrado, documentacao, imagem final, testado em hardware real alem de QEMU/VirtualBox | planejada |

A ordem 0.6 → 0.7 → 0.8 → 0.9 e proposital: a paginacao/anel 3 (0.6) entra primeiro para que os apps
graficos da 0.7 ja nascam isolados do kernel, e o video real (0.9) fica por ultimo porque uma chamada de
BIOS em modo real exige um trampolim mais simples de acertar depois que o esquema de memoria da 0.6 ja
estiver definido.

**Depois da 1.0:** um navegador de internet de verdade (placa de rede, pilha TCP/IP, parser HTML/CSS,
motor de renderizacao) e, sozinho, tao grande quanto todo o roadmap 0.6-1.0 junto -- por isso fica como um
arco proprio de versoes 1.x (1.1 rede, 1.2 motor HTML/CSS, 1.3 o app navegador), reaproveitando o
isolamento da 0.6 e o kit de widgets da 0.7.

## Licenca

MIT. Veja [LICENSE](LICENSE). O mruby (`third_party/mruby`) e distribuido sob a licenca MIT do proprio
projeto (`third_party/mruby/LICENSE`, Copyright (c) mruby developers).
