; disk.asm — leitura de disco via LBA estendido (int 13h/42h)
; Interface:
;   disk_read: EAX=LBA, ES:BX=destino, CX=setores (1-127)
;   boot_drive: variável byte que deve existir no chamador
;   retorna CF=1 em erro

disk_read:
    pusha
    mov [.count],   cx
    mov [.buf_off], bx
    mov [.buf_seg], es
    mov [.lba_lo],  eax
    mov dword [.lba_hi], 0
    mov si, .dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    popa
    ret

.dap:     db 0x10, 0
.count:   dw 0
.buf_off: dw 0
.buf_seg: dw 0
.lba_lo:  dd 0
.lba_hi:  dd 0
