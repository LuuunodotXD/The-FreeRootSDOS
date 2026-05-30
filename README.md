# FreeRootSDOS

Sistema operacional minimalista escrito em C e Assembly x86, com bootloader próprio, kernel em modo protegido 32-bit e sistema de arquivos persistente em disco.

## Funcionalidades

- Bootloader em Assembly com suporte a CHS e LBA estendido (compatível com 386/486+)
- Ativação de A20 com três métodos em cascata (BIOS, porta 0x92, kbd 8042)
- Kernel em C com terminal VGA (16 cores fg/bg, scroll, cursor de hardware)
- IDT/IRQ completa: 32 exceções da CPU + timer PIT a 1000 Hz + teclado por interrupção
- Kernel panic com dump de registradores e reinício automático
- Shell interativa com histórico, edição de linha, teclas especiais e suporte a scripts `.cha`
- Execução de múltiplos comandos na mesma linha com `;`
- Dois drives: **A:** (disco, persistente) e **H:** (heap/RAM, volátil)
- Sistema de arquivos com diretórios, formato 8.3, case-insensitive
- Heap próprio (`kmalloc`) de 64 KB com bitmap de blocos
- Programas: editor de texto (`edit`), editor hexadecimal (`hexdump`), calculadora (`calc`)

## Compilar e rodar

**Requisitos:** `nasm`, `i686-linux-gnu-gcc`, `i686-linux-gnu-ld`, `qemu-system-i386`

```bash
./crossb.sh
qemu-system-i386 -drive format=raw,file=os_image.bin,if=ide
```

Para testar compatibilidade com hardware mais antigo:
```bash
qemu-system-i386 -cpu 486 -drive format=raw,file=os_image.bin,if=ide
```
**Aviso: a imagem do sistema está em Releases, é só  baixar e rodar no QEMU, sem precisar compilar.**

## Comandos

| Comando | Descrição |
|---|---|
| `help [2]` | ajuda (página 1 ou 2) |
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
