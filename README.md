# FreeRootSDOS™

Sistema operacional minimalista escrito em C e Assembly x86, com bootloader próprio em dois estágios, kernel em modo protegido 32-bit, sistema de arquivos persistente, interface gráfica Balloon e pilha de rede TCP/IP completa.

> **Versão atual: 0.7 "Tsar"**

---

## Estrutura do projeto

```
FreeRootSDOS/
├── boot/               # Bootloader (Stage 1 + Stage 2, GDT, superbloco)
├── kernel/             # Kernel principal, IDT, kmalloc, TTY, terminal
├── fs/                 # Sistema de arquivos e driver de disco
├── net/                # Pilha TCP/IP (ARP, IP, ICMP, UDP, TCP, DNS)
├── shell/              # Shell interativa e parser de scripts
├── gui/                # Interface gráfica Balloon
├── drivers/
│   ├── sound/
│   │   ├── adlib/      # OPL2 FM (Creative AdLib, Yamaha OPL2)
│   │   ├── sb16/       # Sound Blaster 16 (OPL3 FM, DSP 4.xx)
│   │   └── pc_beep/    # PC Speaker (fallback universal)
│   ├── net/
│   │   ├── rtl8139/    # Realtek RTL8139 (PCI)
│   │   ├── e1000/      # Intel E1000 82540EM/82545EM (PCI, MMIO)
│   │   ├── pcnet/      # AMD PCnet-PCI II Am79C970A (PCI)
│   │   └── ne2000/     # NE2000 (ISA/PCI legacy)
│   ├── input/
│   │   └── ps2/        # Teclado e mouse PS/2
│   ├── storage/
│   │   ├── ata/        # ATA/IDE (disco rígido)
│   │   └── floppy/     # Controlador de disquete
│   ├── video/
│   │   └── vga/        # Modo texto VGA e modo gráfico 12h
│   └── bus/            # PCI (detecção e configuração)
├── crossb.sh           # Build para outras arquiteturas (cross-compiler i686)
├── build.sh            # Build para x86 / amd64 nativo (gcc -m32)
└── README.md
```

---

## Funcionalidades

### Bootloader (v0.7 — reescrito do zero)

A v0.6 tinha um bootloader de estágio único sem suporte a LBA, sem GDT real e sem detecção de hardware — apenas suficiente para dar boot. Na v0.7 o bootloader foi reescrito completamente em dois estágios:

- **Stage 1** (512 bytes, setor 0): lê o Stage 2 do disco via LBA estendido (INT 13h/42h), com fallback para CHS em hardware legado
- **Stage 2** (`loader.asm`): ativa A20 por três métodos em cascata (porta 0x92, BIOS INT 15h, controlador KBC 8042), monta a GDT com segmentos flat 32-bit, entra em modo protegido e carrega o kernel do disco via LBA
- **Superbloco** (setor 16): tabela com os LBAs e tamanhos de cada região do disco (Stage 2, kernel, sistema de arquivos), lida pelo Stage 1 antes de qualquer coisa
- Compatível com hardware a partir do 386/486 real

### Núcleo

- Kernel em C com terminal VGA (16 cores fg/bg, scroll, cursor de hardware)
- IDT/IRQ completa: 32 exceções da CPU + timer PIT a 1000 Hz + teclado + mouse PS/2
- Kernel panic com dump de registradores e travamento
- Heap próprio (`kmalloc`) com bitmap de blocos de 64 KB

### Som — HAL com detecção automática

Os drivers de som são registrados por prioridade e o primeiro detectado é o único ativo — nunca há dois drivers de som inicializados ao mesmo tempo, evitando conflitos de porta (OPL2/OPL3 em 0x388):

| Prioridade | Driver | Detecção | Hardware |
|---|---|---|---|
| 1 | **Sound Blaster 16** | Reset DSP + versão ≥ 4 | OPL3 FM via base+0x0/0x1 |
| 2 | **AdLib OPL2** | Timers OPL2 em 0x388 | OPL2 FM |
| 3 | **PC Speaker** | Sempre presente | PIT canal 2 |

### Rede — HAL com detecção automática

Mesma arquitetura do som: primeiro driver detectado via PCI vira o ativo. Ordem por desempenho e prevalência:

| Prioridade | Driver | PCI ID | Detecção |
|---|---|---|---|
| 1 | **RTL8139** | 10EC:8139 | BAR0 I/O, ring buffer simples |
| 2 | **E1000** | 8086:100E / 100F | BAR0 MMIO, descriptor rings 64-bit |
| 3 | **PCnet-PCI II** | 1022:2000 | DWIO 32-bit, descriptor rings |
| 4 | **NE2000** | — | ISA/PCI legacy |

O `ifconfig` exibe o driver ativo e funciona com qualquer uma das quatro placas.

### Sistema de arquivos

- Dois drives: **A:** (disco, persistente) e **H:** (heap/RAM, volátil)
- Diretórios, formato 8.3, case-insensitive
- Comandos: `dir`, `cd`, `md`, `copy`, `move`, `del`, `rename`, `write`, `cat`, `append`, `format`

### Interface gráfica Balloon

- Ambiente gráfico estilo Macintosh/Windows 95
- Modo VGA 12h (640×480, 16 cores)
- Mouse PS/2 com cursor desenhado por hardware
- Janelas arrastáveis, redimensionáveis, minimizáveis e maximizáveis
- Menu de contexto (botão direito) com opções dinâmicas
- Relógio na barra de menus (formato 12h, atualização a cada segundo)
- Ícones na área de trabalho: Terminal, Arquivos, Rede, Browser, Campo Minado, Editor, Sobre

### Terminal gráfico

- Emulação de terminal dentro de uma janela Balloon
- Histórico de comandos (setas ↑/↓)
- Teclas Home/End, Insert/Delete, Ctrl+←/→ (pular palavras)
- Autocompletar comandos e nomes de arquivos com Tab

### Editor de texto gráfico

- Janela com barra de status (nome do arquivo, linha:coluna)
- Atalhos: Ctrl+S (salvar), Ctrl+Q (fechar)
- Prompt de salvamento ao fechar arquivo não salvo

### Pilha de rede

- Protocolos: ARP, IP, ICMP (ping), UDP, TCP, DNS
- Cliente HTTP (`wget`) que baixa arquivos e salva no disco (A:)
- Comandos: `ping`, `resolve`, `arp`, `arping`, `ifconfig`, `tcptest`

### Jogos

- **Campo Minado** — grade 9×9, 10 minas, bandeiras, timer, botão reiniciar

### Shell

- Histórico de comandos, edição de linha, autocompletar com Tab
- Expansão de variáveis de ambiente (`$VAR`)
- Execução de scripts `.cha` e binários `.bin` (carregados em 0x20000)
- Comandos separados por `;`
- Múltiplos terminais virtuais (Alt+F1–F8, Ctrl+Alt+F1–F8)
- Variáveis de ambiente persistentes (`A:.VAR/env.var`)

---

## Compilar e rodar

### Requisitos

```
nasm
gcc + gcc-multilib      (para build.sh — x86/amd64 nativo)
i686-linux-gnu-gcc      (para crossb.sh — outras arquiteturas)
i686-linux-gnu-ld
qemu-system-i386
```

### Build

Em x86 ou amd64 (recomendado):
```bash
chmod +x build.sh
./build.sh
```

Em ARM, RISC-V ou qualquer outra arquitetura (requer cross-compiler):
```bash
chmod +x crossb.sh
./crossb.sh
```

### Rodar no QEMU

Configuração mínima (E1000 + AdLib, padrões modernos do QEMU):
```bash
qemu-system-i386 -drive format=raw,file=os_image.img,if=ide \
  -device adlib \
  -netdev user,id=net0 -device e1000,netdev=net0
```

Com Sound Blaster 16:
```bash
qemu-system-i386 -drive format=raw,file=os_image.img,if=ide \
  -device sb16 \
  -netdev user,id=net0 -device e1000,netdev=net0
```

Com RTL8139 (driver mais simples, útil para debug):
```bash
qemu-system-i386 -drive format=raw,file=os_image.img,if=ide \
  -device sb16 \
  -netdev user,id=net0 -device rtl8139,netdev=net0
```

Sem parâmetros de rede (o OS detecta automaticamente o que o QEMU emular):
```bash
qemu-system-i386 -drive format=raw,file=os_image.img,if=ide
```

Teste de compatibilidade com hardware legado:
```bash
qemu-system-i386 -cpu 486 -drive format=raw,file=os_image.img,if=ide
```

---

## Comandos

| Comando | Descrição |
|---|---|
| `help [2/3]` | ajuda (página 1, 2 ou 3) |
| `balloon` | abre a interface gráfica |
| `info` | mostra informações do OS |
| `clear` | limpa a tela |
| `reboot` / `poweroff` | reinicia ou desliga |
| `date` / `uptime` | data/hora e tempo de boot |
| `sleep <ms>` | pausa em milissegundos |
| `time` | ativa/desativa hora no prompt |
| `drive` | ativa/desativa unidade no prompt |
| `color` / `bgcolor <0-F>` | cor do texto e do fundo |
| `log <texto>` | imprime mensagem |
| `log:<X> <texto>` | imprime na cor X (0–F) |
| `meminfo` | uso do heap e do disco A: |
| `a:` / `h:` | troca de drive |
| `dir [pasta]` | lista conteúdo |
| `md <nome>` | cria diretório |
| `pwd` | mostra caminho atual |
| `cd <nome>` / `cd ..` | navega diretórios |
| `write <arq> [texto]` | cria ou edita arquivo |
| `cat <arq>` | exibe conteúdo |
| `append <arq> <texto>` | adiciona linha ao arquivo |
| `copy` / `move <arq> [dest]` | copia ou move arquivo |
| `del <arq/dir>` | remove arquivo ou diretório |
| `rename <old> <new>` | renomeia arquivo |
| `format [a:/h:]` | formata drive |
| `edit <arq>` | editor de texto (Ctrl+S salva, Ctrl+Q sai) |
| `hexdump <arq>` | editor hexadecimal |
| `calc <expressão>` | calculadora (+ - * / % ^ parênteses, hex 0x) |
| `view <imagem.bmp>` | visualizador BMP (320×200, 216 cores) |
| `wget <url>` | baixa arquivo HTTP e salva no disco (máx. 8 KB) |
| `ping <host>` | ICMP echo (4 pacotes) |
| `resolve <host>` | consulta DNS (mostra IP) |
| `arping <ip>` | resolução ARP (IP → MAC) |
| `arp` | mostra tabela ARP |
| `ifconfig` | exibe driver ativo, MAC e status do link |
| `tcptest` | teste TCP (conecta e faz GET) |
| `beep` | toca nota pelo driver de som ativo |

---

## Histórico de versões

| Versão | Destaques |
|---|---|
| **0.7 "Tsar"** | Bootloader reescrito (dois estágios, LBA, A20, GDT), reestruturação de pastas, HAL de som (SB16 + AdLib + PC Beeper), HAL de rede (RTL8139 + E1000 + PCnet + NE2000), `build.sh` nativo x86/amd64, `ifconfig` agnóstico de driver |
| 0.6 "Reforged" | Bootloader mínimo, tudo na raiz, RTL8139 hardcoded, AdLib hardcoded |

---

## Licença

Licença BSD 2-Clause — livre para uso, modificação e distribuição comercial com créditos ao autor.

Feito por **LuuunoXD**
