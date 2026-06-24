#!/bin/bash
# ============================================================
# FreeRootSDOS - Script de build (x86 / amd64 nativo)
# Use este script em máquinas x86 ou amd64.
# Para outras arquiteturas (ARM, RISC-V, etc.) use crossb.sh.
# ============================================================
set -e

# ------------------------------------------------------------
# Verifica ferramentas necessárias
# ------------------------------------------------------------
check_tool() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERRO: '$1' não encontrado. Instale com:"
        echo "  $2"
        exit 1
    fi
}

check_tool nasm    "sudo apt install nasm"
check_tool ld      "sudo apt install binutils"
check_tool objcopy "sudo apt install binutils"
check_tool python3 "sudo apt install python3"

# Verifica suporte a 32-bit no gcc nativo
if ! gcc -m32 -x c /dev/null -o /dev/null 2>/dev/null; then
    echo "ERRO: gcc não tem suporte a -m32. Instale com:"
    echo "  sudo apt install gcc-multilib"
    exit 1
fi

# ------------------------------------------------------------
# Layout do disco (precisa bater com boot/superbloco.inc)
# ------------------------------------------------------------
STAGE2_LBA=1
STAGE2_SECS=15
SB_LBA=16
KERNEL_LBA=17
KERNEL_SECS=600
FS_LBA=$((KERNEL_LBA + KERNEL_SECS))   # 617
FS_SECS=2048
DISK_SECS=2880                          # tamanho total da imagem (1,44 MB, com folga)

BUILD="build"

# ------------------------------------------------------------
# 1. Prepara diretório de build (flat — todos os fontes juntos)
# ------------------------------------------------------------
echo "==> Preparando $BUILD/"
rm -rf "$BUILD"
mkdir -p "$BUILD"

SRC_DIRS="
    boot
    kernel
    fs
    net
    shell
    gui
    drivers/sound
    drivers/sound/sb16
    drivers/sound/adlib
    drivers/sound/pc_beep
    drivers/net
    drivers/net/rtl8139
    drivers/net/e1000
    drivers/net/pcnet
    drivers/net/ne2000
    drivers/input
    drivers/input/ps2
    drivers/storage
    drivers/storage/ata
    drivers/storage/floppy
    drivers/video
    drivers/video/vga
    drivers/bus
"

echo "==> Copiando fontes para $BUILD/"
for d in $SRC_DIRS; do
    if [ -d "$d" ]; then
        cp "$d"/*.c   "$BUILD/" 2>/dev/null || true
        cp "$d"/*.h   "$BUILD/" 2>/dev/null || true
        cp "$d"/*.asm "$BUILD/" 2>/dev/null || true
        cp "$d"/*.inc "$BUILD/" 2>/dev/null || true
        cp "$d"/*.ld  "$BUILD/" 2>/dev/null || true
    fi
done

cd "$BUILD"

# ------------------------------------------------------------
# 2. Bootloader Stage 1 (512 bytes, vai no setor 0)
# ------------------------------------------------------------
echo "==> Montando Stage 1 (boot.asm)"
nasm -f bin boot.asm -o boot.bin

SIZE1=$(wc -c < boot.bin)
if [ "$SIZE1" -ne 512 ]; then
    echo "ERRO: boot.bin tem $SIZE1 bytes, deveria ter exatamente 512."
    exit 1
fi

# ------------------------------------------------------------
# 3. Bootloader Stage 2 (loader.asm)
# ------------------------------------------------------------
echo "==> Montando Stage 2 (loader.asm)"
nasm -f bin loader.asm -o loader_raw.bin

SIZE2=$(wc -c < loader_raw.bin)
MAXSIZE2=$((STAGE2_SECS * 512))
if [ "$SIZE2" -gt "$MAXSIZE2" ]; then
    echo "ERRO: loader.bin tem $SIZE2 bytes, mas o limite é $MAXSIZE2 ($STAGE2_SECS setores)."
    echo "Aumente STAGE2_SECS neste script e em boot/superbloco.inc."
    exit 1
fi

dd if=/dev/zero bs=512 count=$STAGE2_SECS of=loader.bin 2>/dev/null
dd if=loader_raw.bin   of=loader.bin bs=512 seek=0 conv=notrunc 2>/dev/null

# ------------------------------------------------------------
# 4. Entry point do kernel (entry.asm)
# ------------------------------------------------------------
echo "==> Montando entry point (entry.asm)"
nasm -f elf32 entry.asm -o entry.o

# ------------------------------------------------------------
# 5. Compila todo o código C do kernel com gcc nativo (-m32)
# ------------------------------------------------------------
echo "==> Compilando kernel (C) com gcc nativo"
CFLAGS="-m32 -ffreestanding -nostdlib -fno-pie -fno-pic -fno-stack-protector -O0 -I."

for src in *.c; do
    obj="${src%.c}.o"
    echo "  CC $src"
    gcc $CFLAGS -c "$src" -o "$obj"
done

# ------------------------------------------------------------
# 6. Linka tudo com ld nativo (-m elf_i386)
# ------------------------------------------------------------
echo "==> Linkando kernel.elf"
OBJS=$(ls *.o | grep -v '^entry\.o$')
ld -m elf_i386 -T linker.ld -o kernel.elf entry.o $OBJS

echo "==> Verificando entry point"
nm kernel.elf | grep -E " _start$| kernel_main$"

# ------------------------------------------------------------
# 7. Extrai binário plano do kernel
# ------------------------------------------------------------
echo "==> Gerando kernel.bin"
objcopy -O binary kernel.elf kernel.bin

KSIZE=$(wc -c < kernel.bin)
MAXKSIZE=$((KERNEL_SECS * 512))
if [ "$KSIZE" -gt "$MAXKSIZE" ]; then
    echo "ERRO: kernel.bin tem $KSIZE bytes, mas o limite é $MAXKSIZE ($KERNEL_SECS setores)."
    echo "Aumente KERNEL_SECS neste script (e ajuste FS_LBA de acordo)."
    exit 1
fi

# ------------------------------------------------------------
# 8. Gera o superbloco (512 bytes com os LBAs do disco)
# ------------------------------------------------------------
echo "==> Gerando superbloco"
python3 -c "
import struct, sys
magic   = b'FROS'
version = 1
sb = magic + struct.pack('<HIIII', version, $KERNEL_LBA, $KERNEL_SECS, $FS_LBA, $FS_SECS)
sb += b'\x00' * (512 - len(sb))
sys.stdout.buffer.write(sb)
" > superbloco.bin

# ------------------------------------------------------------
# 8.5. Valida que tudo cabe no tamanho de disco escolhido
# ------------------------------------------------------------
TOTAL_NEEDED=$((FS_LBA + FS_SECS))
if [ "$TOTAL_NEEDED" -gt "$DISK_SECS" ]; then
    echo "ERRO: layout precisa de $TOTAL_NEEDED setores, mas DISK_SECS=$DISK_SECS."
    echo "Aumente DISK_SECS, ou reduza KERNEL_SECS/FS_SECS."
    exit 1
fi

# ------------------------------------------------------------
# 9. Monta a imagem de disco final
# ------------------------------------------------------------
cd ..
echo "==> Montando imagem de disco (os_image.img)"

dd if=/dev/zero            of=os_image.img bs=512 count=$DISK_SECS 2>/dev/null

dd if="$BUILD/boot.bin"       of=os_image.img bs=512 seek=0           conv=notrunc 2>/dev/null
dd if="$BUILD/loader.bin"     of=os_image.img bs=512 seek=$STAGE2_LBA conv=notrunc 2>/dev/null
dd if="$BUILD/superbloco.bin" of=os_image.img bs=512 seek=$SB_LBA     conv=notrunc 2>/dev/null
dd if="$BUILD/kernel.bin"     of=os_image.img bs=512 seek=$KERNEL_LBA conv=notrunc 2>/dev/null

# ------------------------------------------------------------
# 10. Resumo
# ------------------------------------------------------------
echo ""
echo "==> Build concluído"
echo ""
echo "Layout do disco:"
printf "  %-10s LBA %-6s %s setores\n" "Stage1"   "0"            "1"
printf "  %-10s LBA %-6s %s setores\n" "Stage2"   "$STAGE2_LBA"  "$STAGE2_SECS"
printf "  %-10s LBA %-6s %s setores\n" "SuperBlk" "$SB_LBA"      "1"
printf "  %-10s LBA %-6s %s setores\n" "Kernel"   "$KERNEL_LBA"  "$KERNEL_SECS"
printf "  %-10s LBA %-6s %s setores\n" "FS"       "$FS_LBA"      "$FS_SECS"
echo ""
echo "Tamanhos:"
ls -lh "$BUILD/kernel.bin" os_image.img
echo ""
echo "Para rodar:"
echo "  qemu-system-i386 -drive format=raw,file=os_image.img,if=ide \\"
echo "    -netdev user,id=net0 -device rtl8139,netdev=net0"
