#!/bin/bash
set -e

CFLAGS="-m32 -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector -O0"

echo "==> Compilando bootloader..."
nasm -f bin boot.asm -o boot.bin

echo "==> Compilando entry point..."
nasm -f elf32 entry.asm -o entry.o

echo "==> Compilando kernel..."
gcc $CFLAGS -c kernel.c   -o kernel.o
gcc $CFLAGS -c terminal.c -o terminal.o
gcc $CFLAGS -c shell.c    -o shell.o
gcc $CFLAGS -c keyboard.c -o keyboard.o

echo "==> Linkando..."
ld -m elf_i386 -T linker.ld -o kernel.elf \
    entry.o kernel.o terminal.o shell.o keyboard.o

echo "==> Verificando entry point..."
nm kernel.elf | grep -E "_start|kernel_main|_bss"

echo "==> Gerando binário..."
objcopy -O binary kernel.elf kernel.bin

# Monta a imagem de disco
cat boot.bin kernel.bin > os_image.bin

# Padding: expande para 21 setores (1 boot + 20 kernel) - 10752 bytes
truncate -s 10752 os_image.bin

echo ""
echo "Tamanhos:"
wc -c boot.bin kernel.bin os_image.bin

echo ""
echo "Primeiros bytes do kernel.bin (deve ser código x86):"
xxd kernel.bin | head -3

echo ""
echo "Para rodar:"
echo "  qemu-system-i386 -drive format=raw,file=os_image.bin,if=ide"
echo "  (ou use -vnc :0 para acesso remoto)"

