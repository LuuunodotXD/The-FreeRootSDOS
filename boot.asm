[org 0x7c00]
[bits 16]

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [boot_drive], dl

    ; Ativa A20 (porta 0x92 + fallback)
    in al, 0x92
    test al, 2
    jnz .a20_ok
    or al, 2
    out 0x92, al
    in al, 0x92
    test al, 2
    jnz .a20_ok
    call a20_kbd
.a20_ok:

    mov si, msg_boot
    call print

    ; Leitura 1: LBA 1-127 (127 setores) -> 0x1000:0x0000 = 0x10000
    mov eax, 1
    mov bx, 0x1000
    mov es, bx
    xor bx, bx
    mov cx, 127
    call read_lba
    jc disk_error

    ; Leitura 2: LBA 128-254 (127 setores) -> 0x1FE0:0x0000 = 0x1FE00
    mov eax, 128
    mov bx, 0x1FE0
    mov es, bx
    xor bx, bx
    mov cx, 127
    call read_lba
    jc disk_error

    ; Leitura 3: LBA 255-381 (127 setores) -> 0x2FC0:0x0000 = 0x2FC00  ← corrigido
    mov eax, 255
    mov bx, 0x2FC0       ; era 0x2DC0 — endereço errado
    mov es, bx
    xor bx, bx
    mov cx, 127          ; era 66
    call read_lba
    jc disk_error

    ; Leitura 4: LBA 382-508 (127 setores) -> 0x3FA0:0x0000 = 0x3FA00  ← nova
    mov eax, 382
    mov bx, 0x3FA0
    mov es, bx
    xor bx, bx
    mov cx, 127
    call read_lba
    jc disk_error

    mov si, msg_ok
    call print

    ; Mantém o modo de vídeo padrão do BIOS (modo 3, texto 80x25)
    ; O modo gráfico 13h é ativado pelo comando "balloon" no kernel

    ; Modo protegido
    lgdt [gdt_desc]
    cli
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:pmode

; ------------------------------------------------------------
; read_lba – lê CX setores do LBA em EAX para ES:BX (int 13h AH=42h)
; ------------------------------------------------------------
read_lba:
    pusha
    mov [dap.lba_low], eax
    mov [dap.count], cx
    mov [dap.buf_off], bx
    mov [dap.buf_seg], es
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .ok
    ; Reset do controlador e tentativa única
    mov ah, 0x00
    int 0x13
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .error
.ok:
    popa
    ret
.error:
    popa
    stc
    ret

dap:
    db 0x10
    db 0
.count: dw 0
.buf_off: dw 0
.buf_seg: dw 0
.lba_low: dd 0
.lba_high: dd 0

; ------------------------------------------------------------
; a20_kbd (fallback)
; ------------------------------------------------------------
a20_kbd:
    call .waitin
    mov al, 0xAD
    out 0x64, al
    call .waitin
    mov al, 0xD0
    out 0x64, al
    call .waitout
    in al, 0x60
    push ax
    call .waitin
    mov al, 0xD1
    out 0x64, al
    call .waitin
    pop ax
    or al, 2
    out 0x60, al
    call .waitin
    mov al, 0xAE
    out 0x64, al
    ret
.waitin:
    in al, 0x64
    test al, 2
    jnz .waitin
    ret
.waitout:
    in al, 0x64
    test al, 1
    jz .waitout
    ret

; ------------------------------------------------------------
; print – imprime string terminada em zero (SI)
; ------------------------------------------------------------
print:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print
.done:
    ret

disk_error:
    mov si, msg_error
    call print
.halt:
    hlt
    jmp .halt

boot_drive  db 0
msg_boot    db "FreeRootSDOS", 13, 10, 0
msg_ok      db "Kernel loaded", 13, 10, 0
msg_error   db "Disk error", 13, 10, 0

; GDT
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:
gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[bits 32]
pmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    call 0x10000
.hang:
    hlt
    jmp .hang

[bits 16]
times 510 - ($ - $$) db 0
dw 0xAA55
