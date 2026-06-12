# FreeRootSDOS™

Sistema operacional minimalista escrito em C e Assembly x86, com bootloader próprio, kernel em modo protegido 32-bit, sistema de arquivos persistente, interface gráfica Balloon e pilha de rede TCP/IP.

## Funcionalidades

### Núcleo
- Bootloader em Assembly com suporte a CHS e LBA estendido (compatível com 386/486+)
- Ativação de A20 com três métodos em cascata (BIOS, porta 0x92, kbd 8042)
- Kernel em C com terminal VGA (16 cores fg/bg, scroll, cursor de hardware)
- IDT/IRQ completa: 32 exceções da CPU + timer PIT a 1000 Hz + teclado + mouse PS/2
- Kernel panic com dump de registradores e travamento
- Heap próprio (`kmalloc`) de 64 KB com bitmap de blocos

### Sistema de arquivos
- Dois drives: **A:** (disco, persistente) e **H:** (heap/RAM, volátil)
- Sistema de arquivos com diretórios, formato 8.3, case-insensitive
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
- Histórico de comandos (setas para cima/baixo)
- Teclas Home/End, Insert/Delete, Ctrl+←/→ (pular palavras)
- Suporte a tabs (autocompletar comandos e nomes de arquivos)

### Editor de texto gráfico
- Janela com barra de status (nome do arquivo, linha:coluna)
- Atalhos: Ctrl+S (salvar), Ctrl+Q (fechar)
- Suporte a seleção de bloco (Shift+setas) – opcional
- Prompt de salvamento ao fechar arquivo não salvo

### Pilha de rede
- Driver RTL8139 (PCI) com recepção por polling/IRQ
- Protocolos: ARP, IP, ICMP (ping), UDP, TCP, DNS
- Cliente HTTP (`wget`) que baixa arquivos da web
- Comandos: `ping`, `resolve`, `arp`, `arping`, `ifconfig`, `tcptest`

### Jogos
- **Campo Minado** – grade 9×9 com 10 minas, bandeiras, timer, botão smile para reiniciar

### Programas de usuário
- Editor de texto (`edit` e versão gráfica)
- Editor hexadecimal (`hexdump`)
- Calculadora (`calc`)
- Visualizador BMP (`view`)
- Navegador HTML simples (interpreta tags básicas, suporta HTTP/1.0)
- Cliente de e‑mail (planejado para v0.7)

### Shell
- Histórico de comandos, edição de linha, autocompletar com TAB
- Expansão de variáveis de ambiente (`$VAR`)
- Execução de scripts `.cha` e binários `.bin` (carregados em 0x20000)
- Comandos separados por `;`
- Múltiplos terminais virtuais (Alt+F1..F8, Ctrl+Alt+F1..F8)
- Variáveis de ambiente persistentes (arquivo `A:.VAR/env.var`)

## Compilar e rodar

**Requisitos:** `nasm`, `i686-linux-gnu-gcc`, `i686-linux-gnu-ld`, `qemu-system-i386`

```bash
./crossb.sh
qemu-system-i386 -drive format=raw,file=os_image.bin,if=ide -soundhw adlib -net user -net nic,model=rtl8139
```
Para testar compatibilidade com hardware mais antigo:
```bash
qemu-system-i386 -cpu 486 -drive format=raw,file=os_image.bin,if=ide
```

## Comandos

| Comando | Descrição |
|---|---|
| `help [2/3]` | ajuda (página 1 ou 2) |
| `balloon` | abre a interface gráfica |
| `info` | mostra informações do OS |
| `clear` | limpa a tela |
| `reboot` / `poweroff` | reinicia ou desliga |
| `date` / `uptime` | data/hora e tempo de boot |
| `sleep <ms>` | pausa em milissegundos |
| `time` | ativa/desativa hora no prompt |
| `drive` | ativa/desativa unidade no prompt |
| `color / bgcolor <0-F>` | cor do texto e do fundo |
| `log <texto>` | imprime mensagem |
| `log:<X> <texto>` | imprime na cor X (0-F) |
| `meminfo` | uso do heap e do disco A: |
| `a:` / `h:` | troca de drive |
| `dir [pasta]` | lista conteúdo |
| `md <nome>` | cria diretório |
| `pwd` | mostra caminho atual |
| `cd <nome>` / `cd ..` | navega diretórios |
| `write <arq> [texto]` | cria ou edita arquivo |
| `cat <arq>` | exibe conteúdo |
| `append <arq> <texto>` | adiciona linha ao arquivo |
| `copy / move <arq> [dest]` | copia ou move arquivo |
| `del <arq/dir>` | remove arquivo ou diretório |
| `rename <old> <new>` | renomeia arquivo |
| `format [a:/h:]` | formata drive |
| `edit <arq>` | editor de texto (Ctrl+S salva, Ctrl+Q sai) |
| `hexdump <arq>` | editor hexadecimal |
| `calc <expressão>` | calculadora (+ - * / % ^ parênteses, hex 0x) |
| `view <imagem.bmp>` | visualizador BMP (320x200 216 cores) |
| `wget <url>` | baixa arquivo HTTP e salva no disco (max 8kb |
| `ping <example.org>` | testa conexão com o site |
| `resolve <example.org>` | consulta DNS (mostra IP) |
| `arping <ip>` | resolução ARP (IP -> MAC) |
| `arp` | mostra tabela ARP |
| `ifconfig` | exibe configuração de rede |
| `tcptest` | teste TCP (conecta e faz GET) |
| `beep` | toca nota na placa adlib|


### Licença
- Projeto com Licença permissiva BSD-C2
- Este projeto é livre para alterações e comércio, com devidos créditos ao autor
- Feito por LuuunoXD
