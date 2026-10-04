# RubyOS 0.1

Sistema operacional experimental que pretende usar **Ruby como linguagem da camada de usuario**,
com Assembly e C apenas onde sao tecnicamente necessarios.

> **Estado real da 0.1:** inicializa no QEMU, mostra o banner, executa um kernel x86 de 32 bits
> (terminal, interrupcoes, timer, teclado) e entrega uma shell com 9 comandos.
> **Ainda nao ha Ruby rodando dentro do sistema**, nem sistema de arquivos, memoria dinamica,
> processos ou modo usuario. Isso esta no roadmap (0.2 a 1.0). Veja [Limitacoes atuais](#limitacoes-atuais).

## Objetivos

- Um SO real (bootloader, kernel, hardware, shell) e nao uma simulacao dentro de outro SO.
- Ruby como linguagem da shell, dos apps e das ferramentas do sistema (a partir da 0.3).
- Evolucao incremental: cada versao inicializa e e testada antes de crescer.
- Prioridade: **funcionalidade > complexidade**.

## Arquitetura

```
 Alvo (0.3+)                       O que existe na 0.1
 ───────────                       ───────────────────
 Ruby Applications                 (nada)
        ↓
 Ruby System Services              (nada)
        ↓
 Ruby Runtime                      (nada)
        ↓
 Kernel (C)                        RubyShell em C (kernel/shell.c), no proprio kernel
        ↓                          terminal, GDT/IDT/PIC, timer, teclado, serial, RTC
 Boot (Assembly + GRUB)            boot/boot.asm, boot/isr.asm
```

**Fluxo de boot:** BIOS → GRUB (ou `qemu -kernel`) → cabecalho Multiboot em `boot/boot.asm` →
`_start` configura a pilha → `kmain()` em `kernel/kernel.c` → GDT, PIC, IDT, timer, teclado →
`sti` → `shell_run()`.

**Por que 32 bits (i686) e nao x86_64?** O Multiboot 1 entrega o kernel ja em modo protegido de
32 bits, o que elimina a transicao para modo longo (paginacao, GDT de 64 bits) na primeira versao.
O QEMU `qemu-system-x86_64` executa a imagem normalmente. Migrar para 64 bits e possivel mais
adiante; ate la o comando `about` informa a arquitetura real (`i686`).

**Mapa de memoria:** o kernel e carregado em 1 MiB (`boot/linker.ld`); pilha de 16 KiB no `.bss`;
video VGA texto em `0xB8000`.

**Terminal:** toda saida vai para a tela VGA (80x25) **e** e espelhada na porta serial COM1. A shell
tambem le da serial, o que permite testes automaticos e uso com `-nographic`.

## Requisitos

Testado em **Ubuntu 24.04** (gcc 13.3, NASM, ld 2.42, QEMU 8.2.2, GRUB 2.12, Ruby 3.x).
**Nao foi testado no Windows nativo.**

| Ferramenta | Para que | Obrigatoria |
|---|---|---|
| Ruby | `tools/build.rb`, testes | sim |
| NASM | montar o Assembly | sim |
| GCC (aceitando `-m32`, ou `i686-elf-gcc`) e `ld` (binutils) | compilar/linkar o kernel | sim |
| QEMU (`qemu-system-i386` ou `qemu-system-x86_64`) | executar e testar | sim, para rodar |
| `grub-mkrescue`, `xorriso`, `grub-pc-bin`, `mtools` | gerar `RubyOS.iso` | so para a ISO |
| `make` | atalhos (opcional) | nao |

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

Comandos da shell na 0.1:

| Comando | Funcao |
|---|---|
| `help` | lista os comandos |
| `clear` | limpa a tela |
| `echo ...` | imprime os argumentos (aspas `"a b"` agrupam) |
| `about` | versao, kernel, runtime, CPU, memoria, arquitetura |
| `date` / `time` | data e hora do RTC (o fuso e o configurado na VM/BIOS) |
| `uptime` | tempo desde o boot |
| `reboot` | reinicia |
| `shutdown` | desliga (funciona em VMs QEMU/Bochs/VirtualBox; em hardware real apenas para a CPU) |

## Testes

```sh
ruby tests/run_tests.rb        # ou: make test
```

A suite roda (23 verificacoes na 0.1):

1. testes unitarios em C de `kernel/util.c` no PC hospedeiro;
2. compilacao do kernel e validacao do cabecalho Multiboot (`grub-file`);
3. boot no QEMU sem janela: banner, `help`, `echo`, `about`, `date`, `time`, `uptime`, comando
   desconhecido, backspace, `shutdown` e `reboot`;
4. **teclado PS/2 real**, com teclas injetadas pelo monitor do QEMU (`sendkey`), incluindo Shift e Backspace;
5. boot pela ISO via GRUB.

O QEMU roda com `-display none`; nada no computador fisico e alterado. Sem QEMU instalado, as etapas
3 a 5 sao puladas (`[SKIP]`).

## Estrutura de diretorios

```
RubyOS/
├── boot/
│   ├── boot.asm        cabecalho Multiboot, pilha e salto para kmain
│   ├── isr.asm         carregadores de GDT/IDT e stubs das 48 interrupcoes
│   └── linker.ld       layout do kernel (1 MiB)
├── kernel/
│   ├── kernel.c/.h     kmain, banner, panic
│   ├── terminal.c/.h   VGA 80x25 + serial + kprintf
│   ├── interrupts.c/.h GDT, IDT, PIC, despacho de IRQs/excecoes
│   ├── shell.c/.h      RubyShell (versao bootstrap em C)
│   ├── util.c/.h       funcoes puras (testadas no hospedeiro)
│   ├── kstring.c/.h    memcpy/memset/... exigidos pelo compilador
│   ├── io.h            inb/outb/cli/sti/hlt
│   └── drivers/        serial, timer (PIT), keyboard (PS/2), rtc (CMOS)
├── ruby/               camada Ruby (vazia na 0.1; ver ruby/README.md)
├── filesystem/         arvore de arquivos (vazia na 0.1)
├── tools/              build.rb, create_image.rb, common.rb
├── tests/              run_tests.rb e testes unitarios em C
├── scripts/run.sh      atalho para executar no QEMU
├── Makefile
├── README.md
└── LICENSE
```

Diferencas em relacao a estrutura sugerida: `memory.c/.h` ficam para a 0.2 (nao ha gerenciador de
memoria ainda) e foram acrescentados `isr.asm`, `util.c`, `kstring.c`, `io.h`, `shell.c` e os drivers.

## Como criar aplicativos Ruby

**Ainda nao e possivel.** O runtime Ruby esta planejado para a 0.3. A direcao pretendida, ainda a validar:
embutir um interpretador Ruby reduzido e embarcavel (o **mruby** e o candidato natural, por ser projetado
para embutir e depender de pouco da libc) sobre uma camada minima de libc escrita para o kernel. Rodar o
CRuby completo dentro do kernel e inviavel (depende de um SO completo: processos, threads, arquivos, mmap).
Nenhuma dessas integracoes foi implementada ou testada ate agora; ate la, novos comandos entram em
`kernel/shell.c` (tabela `commands[]`).

## Como contribuir

1. Rode `ruby tests/run_tests.rb` antes e depois de mudar qualquer coisa; todos os testes devem passar.
2. Mantenha a regra do projeto: nada de pseudocodigo; se algo e prototipo, documente aqui.
3. Funcoes sem dependencia de hardware vao em `kernel/util.c` com testes em `tests/host/`.
4. Teste novos comandos da shell em `tests/run_tests.rb` (secao QEMU).
5. Strings exibidas pelo kernel devem ser ASCII puro (a tela VGA nao exibe UTF-8: sem acentos).

## Limitacoes atuais

- **Sem seguranca real.** Tudo, inclusive a shell, roda no anel 0 (privilegio maximo) dentro do kernel.
  Nao ha modo usuario, isolamento de processos, permissoes de arquivos nem limites de memoria.
  O que existe: excecoes da CPU geram um *kernel panic* em vez de travar silenciosamente, e a leitura
  de linha limita o tamanho da entrada.
- **Sem Ruby.** O campo `Ruby Runtime` do `about` mostra "nenhum" de proposito.
- **Sem gerenciador de memoria, sistema de arquivos, processos ou scheduler** (0.2 e 0.4).
- **32 bits.** A arquitetura informada e `i686`, nao `x86_64`.
- Os itens "Initializing memory/filesystem/Ruby runtime" do boot imprimem "nao implementado".
- Teclado: layout US apenas, sem setas/Del/Home. Pelo console serial, setas inserem lixo na linha.
- `shutdown` so desliga em VMs; `date`/`time` mostram o que o RTC informar (assume seculo 20xx).
- Nao foi testado em hardware real nem no Windows nativo.

## Roadmap

| Versao | Conteudo | Estado |
|---|---|---|
| **0.1** | Boot, kernel minimo, terminal, teclado, shell basica | **feita e testada** |
| 0.2 | Memoria, filesystem, comandos de arquivos, processos basicos | planejada |
| 0.3 | Runtime Ruby, execucao de scripts, aplicativos Ruby | planejada |
| 0.4 | Scheduler, drivers adicionais, sistema de permissoes | planejada |
| 0.5 | Interface grafica (RubyShell Desktop), janelas, gerenciador de arquivos | planejada |
| 1.0 | Sistema integrado, documentacao, instalador/imagem final | planejada |

## Licenca

MIT. Veja [LICENSE](LICENSE).
