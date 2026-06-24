; a20.asm — ativação da linha A20 com verificação

; a20_enable: ativa A20 (tenta fast, depois KBC)
; retorna CF=0 se OK, CF=1 se falhou
a20_enable:
    call a20_check
    jnc .done         ; já ativo

    ; Tenta fast A20 (porta 0x92)
    in al, 0x92
    test al, 2
    jnz .try_kbc
    or al, 2
    and al, 0xFE      ; evita reset (bit 0)
    out 0x92, al
    call a20_check
    jnc .done

.try_kbc:
    ; Via controlador de teclado 8042
    call .kbc_wait_in
    mov al, 0xAD       ; desabilita teclado
    out 0x64, al

    call .kbc_wait_in
    mov al, 0xD0       ; lê byte de saída
    out 0x64, al

    call .kbc_wait_out
    in al, 0x60
    push ax

    call .kbc_wait_in
    mov al, 0xD1       ; escreve byte de saída
    out 0x64, al

    call .kbc_wait_in
    pop ax
    or al, 2           ; seta bit A20
    out 0x60, al

    call .kbc_wait_in
    mov al, 0xAE       ; reabilita teclado
    out 0x64, al

    call .kbc_wait_in
    call a20_check
    jnc .done

.fail:
    stc
    ret
.done:
    clc
    ret

.kbc_wait_in:
    in al, 0x64
    test al, 2
    jnz .kbc_wait_in
    ret
.kbc_wait_out:
    in al, 0x64
    test al, 1
    jz .kbc_wait_out
    ret

; a20_check: verifica se A20 está ativo
; CF=0 = ativo, CF=1 = inativo
a20_check:
    push ds
    push es
    push ax
    push bx
    xor ax, ax
    mov es, ax
    mov bx, 0x0500     ; endereço em seg 0

    not ax             ; AX = 0xFFFF
    mov ds, ax
    mov bx, 0x0510     ; endereço em seg 0xFFFF (= 0x100500 com A20)

    mov al, [es:0x0500]
    push ax
    mov al, [ds:0x0510]
    push ax

    mov byte [es:0x0500], 0x00
    mov byte [ds:0x0510], 0xFF

    cmp byte [es:0x0500], 0xFF  ; se A20 inativo: wrap → mesmo endereço

    pop ax
    mov [ds:0x0510], al
    pop ax
    mov [es:0x0500], al

    pop bx
    pop ax
    pop es
    pop ds
    je .inactive
    clc
    ret
.inactive:
    stc
    ret
