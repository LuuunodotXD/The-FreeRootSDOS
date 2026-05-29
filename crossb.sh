#!/bin/bash
set -e

CFLAGS="-m32 -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector -O0"

echo "==> Compilando bootloader..."
nasm -f bin boot.asm -o boot.bin

echo "==> Compilando entry point..."
nasm -f elf32 entry.asm -o entry.o

echo "==> Compilando kernel..."
i686-linux-gnu-gcc $CFLAGS -c kernel.c   -o kernel.o
i686-linux-gnu-gcc $CFLAGS -c terminal.c -o terminal.o
i686-linux-gnu-gcc $CFLAGS -c shell.c    -o shell.o
i686-linux-gnu-gcc $CFLAGS -c keyboard.c -o keyboard.o
i686-linux-gnu-gcc $CFLAGS -c idt.c      -o idt.o
i686-linux-gnu-gcc $CFLAGS -c kmalloc.c  -o kmalloc.o
i686-linux-gnu-gcc $CFLAGS -c fs.c       -o fs.o
i686-linux-gnu-gcc $CFLAGS -c disk.c     -o disk.o
i686-linux-gnu-gcc $CFLAGS -c fs_disk.c  -o fs_disk.o
i686-linux-gnu-gcc $CFLAGS -c programs.c -o programs.o
# e na linha do ld: ... fs.o

echo "==> Linkando..."
i686-linux-gnu-ld -m elf_i386 -T linker.ld -o kernel.elf \
    entry.o kernel.o terminal.o shell.o keyboard.o idt.o kmalloc.o fs.o disk.o fs_disk.o programs.o

echo "==> Verificando entry point..."
nm kernel.elf | grep -E "_start|kernel_main|_bss"

echo "==> Gerando binário..."
objcopy -O binary kernel.elf kernel.bin
cat boot.bin kernel.bin > os_image.bin
truncate -s 163840 os_image.bin   # 320 setores x 512 = 160KB (disquete 5.25")   # 21 setores x 512 bytes

echo ""
echo "Tamanhos:"
wc -c boot.bin kernel.bin os_image.bin

echo ""
echo "Para rodar:"
echo "  qemu-system-i386 -drive format=raw,file=os_image.bin,if=ide"
