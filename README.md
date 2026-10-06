# RubyOS 0.2

Sistema operacional experimental que pretende usar **Ruby como linguagem da camada de usuario**,
com Assembly e C apenas onde sao tecnicamente necessarios.

> **Estado real da 0.2:** inicializa no QEMU, tem gerenciador de memoria fisica e heap, um sistema de
> arquivos em RAM com os comandos de arquivos, processos com troca de contexto real e uma shell com
> redirecionamento. **Ainda nao ha Ruby rodando dentro do sistema**: a shell continua escrita em C
> (a reescrita em Ruby e a 0.3). Os arquivos **nao persistem** (nao ha driver de disco). Os processos
> sao **cooperativos** e rodam no anel 0, sem isolamento. Veja [Limitacoes atuais](#limitacoes-atuais).

## Objetivos

- Um SO real (bootloader, kernel, hardware, shell) e nao uma simulacao dentro de outro SO.
- Ruby como linguagem da shell, dos apps e das ferramentas do sistema (a partir da 0.3).
- Evolucao incremental: cada versao inicializa e e testada antes de crescer.
- Prioridade: **funcionalidade > complexidade**.

## Arquitetura

```
 Alvo (0.3+)                       O que existe na 0.2
 ───────────                       ───────────────────
 Ruby Applications                 (nada)
        ↓
 Ruby System Services              (nada)
        ↓
 Ruby Runtime                      (nada)
        ↓
 Kernel (C)                        RubyShell em C (kernel/shell*.c), como a tarefa "shell"
        ↓                          processos (process.c)   ramfs (ramfs.c)
        ↓                          memoria: pmm.c + heap.c (memory.c junta os dois)
        ↓                          terminal, GDT/IDT/PIC, timer, teclado, serial, RTC
 Boot (Assembly + GRUB)            boot/boot.asm, boot/isr.asm, boot/switch.asm
```

**Fluxo de boot:** BIOS → GRUB (ou `qemu -kernel`) → cabecalho Multiboot em `boot/boot.asm` →
`kmain()` → GDT, PIC, IDT, timer, teclado → `mm_init` (mapa de memoria, PMM, heap) → `fs_init`
(arvore inicial) → `process_init` (o proprio boot vira o processo 1, `init`) → `shell_start` cria o
processo 2 (`shell`) → o `init` passa a ser a tarefa ociosa.

### Memoria (`kernel/pmm.c`, `kernel/heap.c`, `kernel/memory.c`)

1. O **mapa de memoria do Multiboot** diz quais areas sao RAM utilizavel.
2. O **PMM** (gerenciador de memoria fisica) guarda um bit por quadro de 4 KiB (ate 4 GiB). Ao iniciar,
   reserva o 1 MiB baixo (BIOS/VGA), a imagem do kernel e as estruturas do bootloader.
3. O **heap do kernel** ocupa um bloco fisico contiguo com metade da RAM livre (no maximo 32 MiB).
   E um alocador first-fit com coalescencia imediata, deteccao de double free e de ponteiros invalidos
   (`kernel_panic`), e envenena o conteudo liberado.
4. Cada bloco tem um **dono** (0 = kernel, senao o processo). O heap sabe quanto cada processo usa,
   aplica uma **cota de 1 MiB por processo** e, quando um processo termina, **libera tudo o que ele
   alocou** (inclusive a pilha). Isso aparece no `ps` e no `memory`.

Nao ha paginacao nem memoria virtual: o kernel usa enderecos fisicos diretamente (identidade).

### Sistema de arquivos (`kernel/ramfs.c`)

Arvore de diretorios e arquivos no heap. Caminhos absolutos e relativos, `.` e `..`, barras repetidas.
A arvore inicial (`/bin /system /home/user /etc /tmp /apps /lib` e tres arquivos de texto) e criada a
cada boot. **Tudo some ao reiniciar.** Sem permissoes, donos ou timestamps.

### Processos (`kernel/process.c`, `boot/switch.asm`)

Cada processo e uma tarefa do kernel com **pilha propria** (16 KiB, com canario para detectar estouro)
e **troca de contexto real**. O escalonador e **cooperativo** (round-robin): a troca acontece quando uma
tarefa chama `yield`, `sleep` ou termina. Estados: `READY`, `RUNNING`, `SLEEPING` (e `ZOMBIE`, que dura
so ate a liberacao). `init` e `shell` sao essenciais e nao podem ser encerrados.

**Terminal:** toda saida vai para a tela VGA (80x25, **sem rolagem para tras**) **e** e espelhada na
porta serial COM1. A shell tambem le da serial, o que permite testes automaticos e uso com `-nographic`.

## Requisitos

Testado em **Ubuntu 24.04** (gcc 13.3, NASM, ld 2.42, QEMU 8.2.2, GRUB 2.12, Ruby 3.3).
**Nao foi testado no Windows nativo.**

| Ferramenta | Para que | Obrigatoria |
|---|---|---|
| Ruby | `tools/build.rb`, testes | sim |
| NASM | montar o Assembly | sim |
| GCC (aceitando `-m32`, ou `i686-elf-gcc`) e `ld` (binutils) | compilar/linkar o kernel | sim |
| QEMU (`qemu-system-i386` ou `qemu-system-x86_64`) | executar e testar | sim, para rodar |
| `grub-mkrescue`, `xorriso`, `grub-pc-bin`, `mtools` | gerar `RubyOS.iso` | so para a ISO |
| `make` | atalhos (opcional) | nao |

Para os testes unitarios no hospedeiro, o GCC com AddressSanitizer/UBSan (`libasan`) e usado se existir;
sem ele os testes rodam do mesmo jeito, sem os sanitizers.

## Instalacao

### Linux (Debian/Ubuntu)

```sh
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential nasm qemu-system-x86 xorriso grub-pc-bin grub-common mtools ruby git
```

`gcc-multilib` **nao** e necessario: o kernel e compilado em modo freestanding e linkado diretamente
com `ld` (verificado compilando sem nenhum header da libc).

**Se o `apt` reclamar de "unmet dependencies"** (por exemplo `gcc-14-base` ou `gcc-13-multilib`), o
indice de pacotes esta desatualizado ou ha uma instalacao pela metade. Corrija assim e repita o `install`:

```sh
sudo apt update
sudo apt --fix-broken install
sudo apt upgrade -y
```

Digite o `apt install` em uma unica linha (ou use a barra invertida `\` no fim de cada linha,
sem espaco depois dela) para que o terminal nao quebre o comando ao colar.

### Windows (recomendado: WSL2)

O caminho mais confiavel no Windows e usar o **WSL2 com Ubuntu** e os mesmos pacotes acima:

```powershell
wsl --install -d Ubuntu
```

Depois, dentro do Ubuntu, rode o `apt install` acima. Para ver a janela do QEMU no Windows 11, o WSLg
exibe a janela diretamente. Alternativamente, copie `build/RubyOS.iso` para o Windows e execute com o
QEMU para Windows: `qemu-system-x86_64.exe -cdrom RubyOS.iso`.

**MSYS2:** o MSYS2 oferece `mingw-w64-x86_64-qemu`, `mingw-w64-x86_64-nasm`, `make`, `ruby` e `git`,
mas **nao** tem um GCC `i686-elf` nem `grub-mkrescue`; compilar o kernel e gerar a ISO la exigiria
um cross-compiler construido a mao. Esse caminho nao foi validado, por isso nao e recomendado.

## Compilacao

```sh
ruby tools/build.rb            # gera build/kernel.elf e build/RubyOS.iso
ruby tools/build.rb kernel     # apenas build/kernel.elf (nao precisa de GRUB)
ruby tools/build.rb clean      # apaga build/
```

Equivalentes com Make: `make`, `make kernel`, `make clean`.

## Execucao no QEMU

```sh
qemu-system-x86_64 -cdrom build/RubyOS.iso                 # pela ISO (GRUB), em janela
qemu-system-i386   -kernel build/kernel.elf                # direto, sem GRUB
qemu-system-i386   -kernel build/kernel.elf -nographic     # tudo no terminal (Ctrl+A, X sai)
```

Atalhos: `make run`, `make run-kernel`, `scripts/run.sh [iso|kernel|serial]`.

## Comandos da shell

`help` mostra os comandos agrupados e `help <comando>` mostra o uso.

| Comando | Funcao |
|---|---|
| `ls [-l] [-1] [caminho...]` | lista (`-l` detalhado, `-1` um por linha); diretorios em ciano e com `/` |
| `cd [dir]` | muda o diretorio atual (sem argumento: `/home/user`) |
| `pwd` | mostra o diretorio atual |
| `mkdir <dir>...` / `rmdir <dir>...` | cria / remove diretorios vazios (o pai precisa existir) |
| `touch <arq>...` | cria arquivos vazios |
| `cat <arq>...` | mostra arquivos |
| `rm [-r] <caminho>...` | remove arquivos (`-r`: diretorios com tudo dentro) |
| `cp <origem> <destino>` | copia **arquivo** (destino pode ser um diretorio) |
| `mv <origem> <destino>` | move/renomeia arquivos e diretorios |
| `ps` / `processes` | lista processos: PID, PPID, nome, estado, memoria e tempo de vida |
| `spawn <hello\|worker\|hog> [seg]` | cria uma tarefa de demonstracao (embutida no kernel) |
| `kill <pid>` | encerra um processo e libera a memoria dele |
| `memory` | memoria fisica, heap do kernel e uso do ramfs |
| `echo [texto...]`, `clear`, `about`, `date`, `time`, `uptime`, `reboot`, `shutdown` | sistema |

**Redirecionamento:** `comando > arquivo` (substitui) e `comando >> arquivo` (acrescenta) funcionam com
qualquer comando, por exemplo `echo ola > a.txt`, `ls -l / > lista.txt`, `ps >> log.txt`. Os `>` precisam
estar separados por espacos e ser os dois ultimos argumentos. Nao ha pipes (`|`) nem entrada padrao.

**Tarefas de demonstracao (`spawn`):** `hello` imprime uma linha e termina; `worker [seg]` aloca 1 KiB por
segundo e dorme (mostra `SLEEPING` e a memoria crescendo no `ps`; ao terminar ou levar `kill`, tudo e
liberado); `hog` aloca ate bater na cota de 1 MiB (prova que o limite e aplicado).

Exemplo:

```
RubyOS> mkdir projetos
RubyOS> echo ola > projetos/a.txt
RubyOS> cat projetos/a.txt
ola
RubyOS> spawn worker 60
processo criado: pid 3 (worker)
RubyOS> ps
PID   PPID  NAME            STATE     MEM       UP
1     0     init            READY     16 KB     12s
2     1     shell           RUNNING   16 KB     12s
3     2     worker          SLEEPING  17 KB     0s
```

## Limites do sistema

| Recurso | Limite |
|---|---|
| Nome de arquivo / caminho / profundidade | 63 caracteres (ASCII imprimivel, sem `/`) / 255 / 32 niveis |
| Nos do sistema de arquivos | 1024 |
| Tamanho de um arquivo / soma de todos os arquivos | 256 KiB / 4 MiB |
| Processos simultaneos | 16 (PIDs nao sao reutilizados) |
| Heap por processo (pilha inclusa) / pilha | 1 MiB / 16 KiB |
| Heap do kernel | metade da RAM livre, no maximo 32 MiB |
| RAM enxergada | ate 4 GiB (o kernel e de 32 bits) |
| Saida capturada por um redirecionamento | 64 KiB (excedente e cortado, com aviso) |

Todos sao verificados pelos testes e geram erro claro ao serem atingidos.

## Testes

```sh
ruby tests/run_tests.rb        # ou: make test
```

A suite roda 64 verificacoes:

1. **testes unitarios em C no PC hospedeiro**, com AddressSanitizer e UBSan, do codigo que roda no kernel:
   `util` (conversoes e tokenizador), `heap` (alocador, coalescencia, donos, cotas, double free, estresse
   aleatorio), `pmm` (quadros, alinhamento, fragmentacao, memoria acima de 3 GiB) e `ramfs` (caminhos,
   `mkdir/write/rm/cp/mv`, todos os limites e ausencia de vazamento);
2. compilacao do kernel **sem avisos** e validacao do cabecalho Multiboot (`grub-file`);
3. boot no QEMU sem janela: banner, etapas de init, `help`, `echo`, `about`, `date`, `time`, `uptime`,
   `shutdown` e `reboot`;
4. **comandos de arquivos**: a sequencia do enunciado (`ls /`, `cd`, `mkdir`, `touch`, `cat`), `cp`, `mv`,
   `rm`, erros, protecao do diretorio atual e da raiz, redirecionamento e **ausencia de vazamento de heap**;
5. **processos**: `ps`, `spawn`, `kill`, estados, crescimento e devolucao de memoria, cota de 1 MiB,
   tabela de processos cheia e reaproveitamento das vagas;
6. **memoria fisica com RAM de 16 MiB a 4 GiB**;
7. **teclado PS/2 real**, com teclas injetadas pelo monitor do QEMU (`sendkey`);
8. boot pela ISO via GRUB.

O QEMU roda com `-display none`; nada no computador fisico e alterado. Sem QEMU instalado, as etapas
de 3 a 8 sao puladas (`[SKIP]`).

## Estrutura de diretorios

```
RubyOS/
├── boot/
│   ├── boot.asm        cabecalho Multiboot, pilha de boot e salto para kmain
│   ├── isr.asm         carregadores de GDT/IDT e stubs das 48 interrupcoes
│   ├── switch.asm      troca de contexto entre tarefas
│   └── linker.ld       layout do kernel (1 MiB)
├── kernel/
│   ├── kernel.c/.h     kmain, banner, panic, estruturas do Multiboot
│   ├── memory.c/.h     mapa de memoria -> PMM -> heap
│   ├── pmm.c/.h        quadros fisicos (bitmap)
│   ├── heap.c/.h       kmalloc/kfree com donos e cotas
│   ├── ramfs.c/.h      sistema de arquivos em RAM
│   ├── process.c/.h    processos, escalonador cooperativo
│   ├── terminal.c/.h   VGA 80x25 + serial + kprintf + captura de saida
│   ├── interrupts.c/.h GDT, IDT, PIC, despacho de IRQs/excecoes
│   ├── shell.c/.h      nucleo da shell: entrada, redirecionamento, help
│   ├── shell_fs.c      comandos de arquivos
│   ├── shell_proc.c    comandos de processos (ps, spawn, kill)
│   ├── shell_sys.c     comandos de sistema
│   ├── util.c/.h       funcoes puras (testadas no hospedeiro)
│   ├── kstring.c/.h    memcpy/memset/... exigidos pelo compilador
│   ├── io.h            inb/outb/cli/sti/hlt
│   └── drivers/        serial, timer (PIT), keyboard (PS/2), rtc (CMOS)
├── ruby/               camada Ruby (vazia ate a 0.3; ver ruby/README.md)
├── filesystem/         conteudo a embutir na imagem (vazio; ver filesystem/README.md)
├── tools/              build.rb, create_image.rb, common.rb
├── tests/              run_tests.rb e testes unitarios em C (tests/host/)
├── scripts/run.sh      atalho para executar no QEMU
├── Makefile
├── README.md
└── LICENSE
```

## Como criar aplicativos Ruby

**Ainda nao e possivel.** O runtime Ruby esta planejado para a 0.3. A direcao pretendida, ainda a validar:
embutir um interpretador Ruby reduzido e embarcavel (o **mruby** e o candidato natural, por ser projetado
para embutir e depender de pouco da libc) sobre uma camada minima de libc escrita para o kernel. Rodar o
CRuby completo dentro do kernel e inviavel (depende de um SO completo: processos com isolamento, threads,
arquivos, mmap). Nenhuma dessas integracoes foi implementada ou testada ate agora. A 0.2 prepara o terreno
com o que um runtime precisa: `kmalloc`/`kfree`, um sistema de arquivos para carregar scripts e processos
para executa-los.

## Como contribuir

1. Rode `ruby tests/run_tests.rb` antes e depois de mudar qualquer coisa; todos os testes devem passar.
2. Mantenha a regra do projeto: nada de pseudocodigo; se algo e prototipo, documente aqui.
3. Codigo sem dependencia de hardware (`util`, `heap`, `pmm`, `ramfs`) vai com testes em `tests/host/`.
4. **Novo comando da shell:** adicione a funcao e uma linha na tabela `commands[]` do grupo certo
   (`shell_fs.c`, `shell_proc.c` ou `shell_sys.c`); `help` o lista sozinho. Teste-o em `tests/run_tests.rb`.
5. Strings exibidas pelo kernel devem ser ASCII puro (a tela VGA nao exibe UTF-8: sem acentos).
6. Tarefas do kernel **precisam** chamar `process_yield()` ou `process_sleep()`: nao ha preempcao, e um
   laco sem cessao trava o sistema.

## Limitacoes atuais

- **Sem seguranca real.** Tudo, inclusive a shell e os processos, roda no anel 0 e no mesmo espaco de
  enderecos do kernel. Nao ha modo usuario, isolamento de memoria entre processos, chamadas de sistema
  nem permissoes de arquivos. O que existe de fato: cota de heap por processo, canario de pilha,
  deteccao de double free/ponteiro invalido no heap, limites e validacao de caminhos no filesystem, e
  `kernel_panic` em excecoes da CPU.
- **Escalonador cooperativo.** Sem preempcao por timer (0.4). Uma tarefa que nao cede a CPU trava o sistema.
- **Sem Ruby.** O campo `Ruby Runtime` do `about` mostra "nenhum" de proposito; a shell e escrita em C.
- **Sem persistencia.** O filesystem vive na RAM e some ao reiniciar. Nao ha driver de disco.
- **`cp` nao copia diretorios** (`cp -r` nao existe). Sem links, `chmod`, timestamps nem `find`.
- **Shell simples:** sem pipes, entrada padrao, historico de comandos, setas ou completar com Tab.
  `>` dentro de aspas nao pode ser usado como texto. A tela nao tem rolagem para tras.
- **32 bits.** A arquitetura informada e `i686`, nao `x86_64`; RAM acima de 4 GiB nao e usada.
- **Sem paginacao**, sem memoria virtual, sem liberar memoria de volta ao PMM.
- Teclado: layout US apenas, sem setas/Del/Home. Pelo console serial, setas inserem lixo na linha.
- `shutdown` so desliga em VMs; `date`/`time` mostram o que o RTC informar (assume seculo 20xx).
- Nao foi testado em hardware real nem no Windows nativo.

## Roadmap

| Versao | Conteudo | Estado |
|---|---|---|
| 0.1 | Boot, kernel minimo, terminal, teclado, shell basica | feita e testada |
| **0.2** | Memoria, filesystem, comandos de arquivos, processos basicos | **feita e testada** |
| 0.3 | Runtime Ruby, execucao de scripts, aplicativos Ruby | planejada |
| 0.4 | Scheduler preemptivo, drivers adicionais (disco), sistema de permissoes | planejada |
| 0.5 | Interface grafica (RubyShell Desktop), janelas, gerenciador de arquivos | planejada |
| 1.0 | Sistema integrado, documentacao, instalador/imagem final | planejada |

## Licenca

MIT. Veja [LICENSE](LICENSE).
