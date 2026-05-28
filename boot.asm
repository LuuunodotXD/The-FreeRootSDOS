[org 0x7c00]
[bits 16]

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [boot_drive], dl

    mov si, msg_boot
    call print_string

    ; Lê 20 setores a partir do setor 2 para 0x1000:0000 = 0x10000
    mov ah, 0x02
    mov al, 80
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    mov bx, 0x1000
    mov es, bx
    xor bx, bx
    int 0x13
    jc disk_error

    mov si, msg_ok
    call print_string

    ; Ativa A20
    in al, 0x92
    or al, 2
    out 0x92, al

    lgdt [gdt_desc]
    cli
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode     ; far jump — flush pipeline

; ----------------------------------------------------------------
; Código 32-bit — DEVE estar antes do times 510 para ficar nos
; primeiros 512 bytes que o BIOS carrega em 0x7C00
; ----------------------------------------------------------------
[bits 32]
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov word [0xB8000], 0x0F42  ; 'B' branco = modo protegido OK

    mov eax, 0x10000
    call eax

    mov word [0xB8002], 0x0C52  ; 'R' vermelho = kernel retornou
.hang:
    hlt
    jmp .hang

; ----------------------------------------------------------------
; Rotinas e dados 16-bit
; ----------------------------------------------------------------
[bits 16]
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print_string
.done:
    ret

disk_error:
    mov si, msg_error
    call print_string
.hang:
    hlt
    jmp .hang

boot_drive  db 0
msg_boot    db "Bootloader carregado...", 13, 10, 0
msg_ok      db "Disco lido OK! Entrando em modo protegido...", 13, 10, 0
msg_error   db "ERRO: falha na leitura do disco!", 13, 10, 0

; ----------------------------------------------------------------
; GDT
; ----------------------------------------------------------------
gdt_start:
    dq 0                        ; descriptor nulo
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

; Padding para completar o setor de 512 bytes
times 510 - ($ - $$) db 0
dw 0xAA55
