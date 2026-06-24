; boot.asm — Stage 1: carrega Stage 2 e pula para ele
[org 0x7C00]
[bits 16]

%include "superbloco.inc"

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    ; Fast A20 (não espera Stage 2 para ativar)
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al

    ; Imprime "."
    mov ax, 0x0E2E      ; AH=0E, AL='.'
    xor bh, bh
    int 0x10

    ; Carrega Stage 2 (LBA 1, 15 setores) para 0x0800:0x0000
    mov bx, STAGE2_SEG
    mov es, bx
    xor bx, bx
    mov eax, STAGE2_LBA
    mov cx, STAGE2_SECS
    call read_lba
    jc disk_err

    ; Passa drive e pula para Stage 2
    mov dl, [boot_drive]
    jmp STAGE2_SEG:0x0000

disk_err:
    mov si, msg_err
    call print16
.halt:
    hlt
    jmp .halt

; ---- LBA mínimo (só para Stage 1) ----
read_lba:
    pusha
    mov [dap.count],   cx
    mov [dap.seg],     es
    mov [dap.off],     bx
    mov [dap.lba_lo],  eax
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    popa
    ret

dap:
    db 0x10, 0
.count:  dw 0
.off:    dw 0
.seg:    dw 0
.lba_lo: dd 0
         dd 0

print16:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    jmp print16
.done:
    ret

boot_drive db 0
msg_err    db "Boot error", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
