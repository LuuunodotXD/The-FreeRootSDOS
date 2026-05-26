# FreeRootSDOS

**Um sistema operacional mínimo e didático de 32 bits, escrito em C e Assembly, com bootloader próprio, kernel em modo protegido, interrupt IDT/IRQ, terminal VGA e um shell de comandos.**
*Feito por LuuunoXD*

![Demonstração](https://i.imgur.com/Z3NJtOB.png)  

## Funcionalidades

- *Bootloader em Assembly (modo real → modo protegido)*
- *Kernel C com acesso direto à memória VGA (buffer 0xB8000)*
- *Terminal com rolagem, cores e cursor de hardware*
- *Shell com linha de comando e edição (backspace, enter)*
- *Interrupts IDT/IRQ (novidade)*
- Para ver os comandos (estão na foto também):
  - `help`   → lista os comandos disponíveis

## Compilação e Execução

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
