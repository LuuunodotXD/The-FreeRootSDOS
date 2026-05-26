# 🖥️ FreeRootSDOS

**Um sistema operacional minimalista e didático escrito em C e Assembly, com bootloader próprio, kernel em modo protegido, terminal VGA e uma shell de comandos interativa.**
*Feito inteiramente por LuuunoXD*

![Demonstração](https://i.imgur.com/rofwmU0.png)  

## ✨ Funcionalidades

- *Bootloader em Assembly (modo real → modo protegido)*
- *Kernel C com acesso direto à memória VGA (buffer 0xB8000)*
- *Terminal com rolagem, cores e cursor de hardware*
- *Shell com linha de comando e edição (backspace, enter)*
- Comandos internos:
  - `help`   → lista os comandos disponíveis
  - `clear`  → limpa a tela
  - `reboot` → reinicia o sistema via controlador de teclado
  - `info`   → exibe informações do SO

## 🛠️ Compilação e Execução

### Requisitos

- **NASM** (montador)
- **GCC** para x86 (compilador cruzado `gcc-i686-linux-gnu binutils-i686-linux-gnu` se estiver em ARM64)
- **ld** (linker) com suporte a `elf_i386`
- **QEMU** (emulador opcional, mas recomendado)

### Para rodar o sistema:

 - **Compile tudo usando o crossb.sh (compilação cruzada)**
 - *O build.sh pode servir, mas ainda não foi testado*
 - *Após isso, só siga as instruções que apareceram no terminal*
 - Lembrando, quando ele compila com sucesso, é só iniciar o os_image.bin 

### Aviso: a imagem do sistema está em "Releases", você pode baixar e rodar direto no QEMU
 - Use ela quando não puder ou não quiser compilar o código-fonte
 - *Para rodar no QEMU, use o comando:*
 - **qemu-system-i386 -drive format=raw,file=os_image.bin,if=ide"**
 - (você pode usar -vnc 127.0.0.1:0 para usar o vnc.)"
