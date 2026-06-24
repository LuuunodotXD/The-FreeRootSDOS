; loader.asm — Stage 2: banner + mini-shell + carrega kernel
[org 0x8000]
[bits 16]

jmp loader_start

%include "superbloco.inc"
%include "print.asm"
%include "disk.asm"
%include "a20.asm"
%include "gdt.asm"

; ================================================================
; Variáveis globais do loader
; ================================================================
boot_drive  db 0
kernel_lba  dd KERNEL_SEG    ; sobrescrito pelo superbloco
kernel_secs dd 600
fs_lba      dd 617
fs_secs     dd 2048

cmd_buf     times 64 db 0
sb_buf      times 512 db 0   ; buffer do superbloco

; ================================================================
; Entrada do Stage 2
; ================================================================
loader_start:
    ; Restaura registros de segmento
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; DL contém o drive (passado pelo Stage 1)
    mov [boot_drive], dl

    ; Banner
    mov si, msg_banner
    call print_str

    ; Lê superbloco
    call load_superblock

    ; Countdown + mini-shell
    call shell_run

    ; Chegou aqui = comando "boot" ou timeout
    call load_kernel

    ; A20
    call a20_enable
    jnc .a20_ok
    mov si, msg_a20_err
    call print_str
    jmp .hang
.a20_ok:
    mov si, msg_a20_ok
    call print_str

    ; Passa fs_lba em EBX e entra em modo protegido
    mov ebx, [fs_lba]
    call enter_pmode   ; não retorna

.hang:
    hlt
    jmp .hang

; ================================================================
; Lê superbloco de LBA 16
; ================================================================
load_superblock:
    push ax
    push bx
    push cx
    push es
    mov ax, ds
    mov es, ax
    mov bx, sb_buf
    mov eax, SB_LBA
    mov cx, 1
    call disk_read
    jc .err

    ; Valida magic
    mov eax, [sb_buf + SB_MAGIC]
    cmp eax, FROS_MAGIC
    jne .no_magic

    ; Copia LBAs do superbloco
    mov eax, [sb_buf + SB_KRNL_LBA]
    mov [kernel_lba], eax
    mov eax, [sb_buf + SB_KRNL_SECS]
    mov [kernel_secs], eax
    mov eax, [sb_buf + SB_FS_LBA]
    mov [fs_lba], eax
    mov eax, [sb_buf + SB_FS_SECS]
    mov [fs_secs], eax
    jmp .done

.no_magic:
    mov si, msg_no_sb
    call print_str    ; usa valores padrão
.done:
    pop es
    pop cx
    pop bx
    pop ax
    ret
.err:
    mov si, msg_disk_err
    call print_str
    pop es
    pop cx
    pop bx
    pop ax
    ret

; ================================================================
; Carrega kernel em 0x10000 (em blocos de até 127 setores)
; ================================================================
load_kernel:
    pusha

    mov si, msg_loading
    call print_str

    mov eax, [kernel_lba]
    mov [.cur_lba], eax
    mov eax, [kernel_secs]
    mov [.remaining], eax
    mov word [.cur_seg], KERNEL_SEG

.loop:
    mov eax, [.remaining]
    cmp eax, 0
    je .done

    mov edx, eax
    cmp edx, 127
    jbe .ok
    mov edx, 127
.ok:
    mov bx, [.cur_seg]
    mov es, bx
    xor bx, bx

    mov eax, [.cur_lba]
    mov cx, dx              ; CX só é usado aqui, isolado de qualquer contador
    call disk_read
    jc .disk_err

    ; remaining -= setores lidos
    mov eax, [.remaining]
    sub eax, edx
    mov [.remaining], eax

    ; LBA += setores lidos
    mov eax, [.cur_lba]
    add eax, edx
    mov [.cur_lba], eax

    ; segmento destino += setores_lidos * 32 paragrafos
    mov bx, dx
    shl bx, 5
    add word [.cur_seg], bx

    jmp .loop

.disk_err:
    mov si, msg_disk_err
    call print_str
    popa
    jmp .hang2
.done:
    mov si, msg_ok
    call print_str
    popa
    ret
.hang2:
    hlt
    jmp .hang2

.cur_lba:   dd 0
.remaining: dd 0
.cur_seg:   dw 0

; ================================================================
; Mini-shell com countdown
; ================================================================
shell_run:
    mov si, msg_countdown
    call print_str

    mov ax, [0x046C]
    add ax, 91
    mov [.target_tick], ax
    mov byte [.secs_left], 5

.countdown:
    mov ah, 0x01
    int 0x16
    jz .no_key

    ; Tecla disponivel — verifica se é Enter
    cmp al, 13
    je .boot_now
    jmp .enter_shell

.no_key:
    mov ax, [0x046C]
    mov bx, [.target_tick]
    sub bx, ax
    push ax
    push bx
    xor dx, dx
    mov ax, bx
    mov bx, 18
    div bx
    cmp al, [.secs_left]
    je .no_update
    mov [.secs_left], al
    mov si, msg_cr
    call print_str
    mov si, msg_auto
    call print_str
    movzx ax, al
    call print_dec16
    mov al, 's'
    call print_char
    mov al, '.'
    call print_char
    mov al, ' '
    call print_char
.no_update:
    pop bx
    pop ax

    mov ax, [0x046C]
    cmp ax, [.target_tick]
    jl .countdown

    ; Timeout — boot automatico
    call print_nl
    ret

.boot_now:
    xor ax, ax       ; consome a tecla (Enter) do buffer
    int 0x16
    call print_nl
    ret              ; volta para load_kernel imediatamente

.enter_shell:
    xor ax, ax       ; consome a tecla que abriu o shell
    int 0x16
    call print_nl

.prompt:
    mov si, msg_prompt
    call print_str
    mov di, cmd_buf
    mov cx, 63
    call readline

    mov si, cmd_buf
    mov di, cmd_boot
    call strcmpi_rm
    jz .do_boot

    mov si, cmd_buf
    mov di, cmd_help
    call strcmpi_rm
    jz .do_help

    mov si, cmd_buf
    mov di, cmd_ver
    call strcmpi_rm
    jz .do_ver

    mov si, cmd_buf
    mov di, cmd_cls
    call strcmpi_rm
    jz .do_cls

    mov si, cmd_buf
    mov di, cmd_mem
    call strcmpi_rm
    jz .do_mem

    mov si, cmd_buf
    mov di, cmd_reboot
    call strcmpi_rm
    jz .do_reboot

    mov si, cmd_buf
    mov al, [si]
    test al, al
    jz .do_boot         ; linha vazia + Enter = boot tambem

    mov si, msg_unknown
    call print_str
    mov si, cmd_buf
    call print_str
    call print_nl
    jmp .prompt

.do_boot:
    ret

.do_help:
    mov si, msg_help
    call print_str
    jmp .prompt

.do_ver:
    mov si, msg_ver
    call print_str
    jmp .prompt

.do_cls:
    mov ax, 0x0003
    int 0x10
    mov si, msg_banner
    call print_str
    jmp .prompt

.do_mem:
    call cmd_mem_map
    jmp .prompt

.do_reboot:
    mov si, msg_reboot
    call print_str
    xor ax, ax
    int 0x16            ; aguarda tecla antes de reiniciar

.kbc_wait:
    in al, 0x64
    test al, 0x02
    jnz .kbc_wait
    mov al, 0xFE
    out 0x64, al        ; pulso de reset via 8042

.halt_reboot:
    hlt
    jmp .halt_reboot

.target_tick dw 0
.secs_left   db 5

; ================================================================
; Mapa de memória E820
; ================================================================
cmd_mem_map:
    push es
    mov si, msg_mem_header
    call print_str

    xor ebx, ebx        ; contador (0 no início)
    mov di, 0x5000      ; buffer temporário
    mov ax, ds
    mov es, ax

.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150 ; "SMAP"
    mov ecx, 20
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done

    ; Imprime entrada: base, tamanho, tipo
    mov eax, [di]       ; base low
    call print_hex32
    mov al, ':'
    call print_char
    mov eax, [di+4]     ; base high
    call print_hex32
    mov si, msg_mem_sep
    call print_str
    mov eax, [di+8]     ; size low
    call print_hex32
    mov al, ' '
    call print_char
    mov eax, [di+16]    ; type
    cmp eax, 1
    je .usable
    mov si, msg_mem_rsv
    call print_str
    jmp .next
.usable:
    mov si, msg_mem_use
    call print_str
.next:
    call print_nl
    test ebx, ebx
    jz .e820_done
    jmp .e820_loop

.e820_done:
    pop es
    ret

; ================================================================
; Strings e comandos
; ================================================================
msg_banner   db 13, 10
             db "  FreeRootSDOS Bootloader v1.0", 13, 10
             db "  Digite 'help' para ver os comandos.", 13, 10
             db 13, 10, 0
msg_countdown db "  ENTER para iniciar; auto em ", 0
msg_auto      db "  ENTER para iniciar; auto em ", 0
msg_cr        db 13, 0
msg_prompt    db "BOOT> ", 0
msg_loading   db "Carregando kernel...", 13, 10, 0
msg_ok        db "OK", 13, 10, 0
msg_a20_ok    db "A20: OK", 13, 10, 0
msg_a20_err   db "A20: ERRO", 13, 10, 0
msg_disk_err  db "ERRO: disco", 13, 10, 0
msg_no_sb     db "Sem superbloco, usando padroes", 13, 10, 0
msg_reboot    db "Aperte qualquer tecla para reiniciar...", 13, 10, 0
msg_unknown   db "Comando desconhecido: ", 0
msg_mem_header db "Mapa de memoria (E820):", 13, 10, 0
msg_mem_sep   db "h  len=", 0
msg_mem_use   db "h  USAVEL", 0
msg_mem_rsv   db "h  RESERVADO", 0

msg_help db \
    "  boot    Inicia o FreeRootSDOS", 13, 10, \
    "  ver     Versao do bootloader", 13, 10, \
    "  mem     Mapa de memoria (E820)", 13, 10, \
    "  cls     Limpa a tela", 13, 10, \
    "  reboot  Reinicia o computador", 13, 10, \
    "  help    Esta mensagem", 13, 10, 0

msg_ver db "FreeRootSDOS Bootloader v1.0 (c) 2026", 13, 10, 0

cmd_boot   db "boot", 0
cmd_help   db "help", 0
cmd_ver    db "ver", 0
cmd_cls    db "cls", 0
cmd_mem    db "mem", 0
cmd_reboot db "reboot", 0
