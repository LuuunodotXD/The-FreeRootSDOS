; print.asm — rotinas de texto (modo real 16 bits)

; print_str: imprime string em SI terminada em 0
print_str:
    push ax
    push bx
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

; print_char: imprime AL
print_char:
    push ax
    push bx
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    pop bx
    pop ax
    ret

; print_nl: \r\n
print_nl:
    push ax
    mov al, 13
    call print_char
    mov al, 10
    call print_char
    pop ax
    ret

; print_hex16: imprime AX em hex (4 dígitos)
print_hex16:
    push cx
    push ax
    mov cx, 4
.loop:
    rol ax, 4
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'+1
    jl .ok
    add al, 'A'-'0'-10
.ok:
    call print_char
    pop ax
    loop .loop
    pop ax
    pop cx
    ret

; print_hex32: imprime EAX em hex (8 dígitos)
print_hex32:
    push eax
    shr eax, 16
    call print_hex16
    pop eax
    push eax
    and eax, 0xFFFF
    call print_hex16
    pop eax
    ret

; print_dec16: imprime AX em decimal
print_dec16:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.push_loop:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .push_loop
.print_loop:
    pop dx
    mov al, dl
    add al, '0'
    call print_char
    loop .print_loop
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; readline: lê linha do teclado para DI (max CX chars), termina em 0
; retorna comprimento em AX
readline:
    push bx
    push cx
    push di
    xor bx, bx
.loop:
    xor ax, ax
    int 0x16
    cmp al, 13        ; Enter
    je .done
    cmp al, 8         ; Backspace
    je .bs
    cmp al, 27        ; ESC
    je .esc
    cmp bx, cx
    jge .loop
    stosb
    inc bx
    call print_char
    jmp .loop
.bs:
    test bx, bx
    jz .loop
    dec bx
    dec di
    mov al, 8
    call print_char
    mov al, ' '
    call print_char
    mov al, 8
    call print_char
    jmp .loop
.esc:
    ; Limpa linha digitada
    test bx, bx
    jz .loop
    push cx
    mov cx, bx
.esc_loop:
    mov al, 8
    call print_char
    mov al, ' '
    call print_char
    mov al, 8
    call print_char
    loop .esc_loop
    pop cx
    ; Reseta DI para início do buffer
    sub di, bx
    xor bx, bx
    jmp .loop
.done:
    mov byte [di], 0
    call print_nl
    mov ax, bx
    pop di
    pop cx
    pop bx
    ret

; strcmpi_rm: compara SI e DI case-insensitive (real mode)
; retorna ZF=1 se iguais
strcmpi_rm:
    push ax
    push bx
    push si
    push di
.loop:
    lodsb
    mov bl, [di]
    inc di
    ; lowercase ambos
    cmp al, 'A'
    jl .skip1
    cmp al, 'Z'
    jg .skip1
    or al, 0x20
.skip1:
    cmp bl, 'A'
    jl .skip2
    cmp bl, 'Z'
    jg .skip2
    or bl, 0x20
.skip2:
    cmp al, bl
    jne .ne
    test al, al
    jz .eq
    jmp .loop
.eq:
    pop di
    pop si
    pop bx
    pop ax
    xor ax, ax   ; ZF=1
    ret
.ne:
    pop di
    pop si
    pop bx
    pop ax
    or ax, 1     ; ZF=0
    ret
