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
i686-linux-gnu-gcc $CFLAGS -c tty.c      -o tty.o
i686-linux-gnu-gcc $CFLAGS -c env.c      -o env.o
i686-linux-gnu-gcc $CFLAGS -c vga12h.c   -o vga12h.o
i686-linux-gnu-gcc $CFLAGS -c mouse.c    -o mouse.o
i686-linux-gnu-gcc $CFLAGS -c balloon.c  -o balloon.o
i686-linux-gnu-gcc $CFLAGS -c vga_mode.c -o vga_mode.o

echo "==> Linkando..."
i686-linux-gnu-ld -m elf_i386 -T linker.ld -o kernel.elf \
    entry.o kernel.o terminal.o shell.o keyboard.o idt.o kmalloc.o fs.o disk.o fs_disk.o programs.o tty.o env.o vga12h.o mouse.o balloon.o vga_mode.o

echo "==> Verificando entry point..."
nm kernel.elf | grep -E "_start|kernel_main|_bss"

echo "==> Gerando binário..."
objcopy -O binary kernel.elf kernel.bin
cat boot.bin kernel.bin > os_image.bin
truncate -s 327680 os_image.bin   # 640 setores x 512 = 320 KB (kernel 320 + fs 320)

echo ""
echo "Tamanhos:"
wc -c boot.bin kernel.bin os_image.bin

echo ""
echo "Para rodar:"
echo "  qemu-system-i386 -drive format=raw,file=os_image.bin,if=ide"
