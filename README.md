# FreeRootSDOS

Sistema operacional minimalista escrito em C e Assembly, com bootloader próprio e kernel em modo protegido 32-bit.

## Funcionalidades

- Bootloader em Assembly (modo real → modo protegido)
- Kernel em C com terminal VGA (cores, scroll, cursor)
- IDT/IRQ com teclado por interrupção e timer a 1000 Hz
- Shell interativa com histórico, edição de linha e teclas especiais
- Sistema de arquivos flat em memória (write, cat, append, del, rename)
- Heap próprio (`kmalloc`) de 64 KB

## Compilar e rodar

**Requisitos:** `nasm`, `i686-linux-gnu-gcc`, `i686-linux-gnu-ld`, `qemu-system-i386`

```bash
./crossb.sh
qemu-system-i386 -drive format=raw,file=os_image.bin,if=ide
```
**Aviso: é possível baixar a os_image.bin em Releases, caso não queira compilar.**

## Comandos disponíveis

| Comando | Descrição |
|---|---|
| `help [2]` | lista de comandos (página 1 ou 2) |
| `clear` | limpa a tela |
| `reboot` / `poweroff` | reinicia ou desliga |
| `date` / `uptime` | data/hora e tempo de boot |
| `color` / `bgcolor` | cor do texto e do fundo (0-F) |
| `log:<cor> <texto>` | imprime texto colorido |
| `time` | ativa/desativa hora no prompt |
| `write <arq> [texto]` | cria ou edita arquivo |
| `cat <arq>` | exibe conteúdo |
| `append <arq> <texto>` | adiciona linha ao arquivo |
| `del <arq>` | remove arquivo |
| `rename <old> <new>` | renomeia arquivo |
| `dir` | lista arquivos |
| `format` | apaga todos os arquivos |
| `meminfo` | uso do heap |
| `sleep <ms>` | pausa em milissegundos |
