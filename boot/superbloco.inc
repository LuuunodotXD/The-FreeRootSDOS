; superbloco.inc — layout do disco FreeRootSDOS

; Stage 2 (loader.asm)
STAGE2_LBA   equ 1        ; LBA de início
STAGE2_SECS  equ 15       ; quantidade de setores (~7.5 KB)
STAGE2_SEG   equ 0x0800   ; segmento de destino → flat 0x8000

; Superbloco (descreve onde está o kernel e o filesystem)
SB_LBA       equ 16       ; LBA do superbloco

; Kernel
KERNEL_SEG   equ 0x1000   ; destino do kernel → flat 0x10000

; Offsets dentro do superbloco (512 bytes)
; Layout: magic(4) + version(2) + krnl_lba(4) + krnl_secs(4) + fs_lba(4) + fs_secs(4)
SB_MAGIC     equ 0        ; dd  "FROS"
SB_VER       equ 4        ; dw  versão
SB_KRNL_LBA  equ 6        ; dd  LBA do kernel
SB_KRNL_SECS equ 10       ; dd  setores do kernel
SB_FS_LBA    equ 14       ; dd  LBA do filesystem
SB_FS_SECS   equ 18       ; dd  setores do filesystem

FROS_MAGIC   equ 0x534F5246  ; "FROS" little-endian
