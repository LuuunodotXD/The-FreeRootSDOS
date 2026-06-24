; gdt.asm — GDT e entrada em modo protegido
; Chamado do loader.asm depois que o kernel está carregado
; EBX = fs_lba a passar para o kernel

enter_pmode:
    cli
    xor ax, ax
    mov ds, ax

    lgdt [gdt_desc]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump para código 32-bit (flush do pipeline)
    jmp 0x08:pmode32

[bits 32]
pmode32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; EBX = fs_lba (passado pelo loader antes de chamar enter_pmode)
    ; Kernel em 0x10000
    call 0x10000

.hang:
    hlt
    jmp .hang

[bits 16]

; GDT flat 32-bit
gdt_start:
    dq 0                                          ; null
gdt_code:
    dw 0xFFFF, 0x0000, 0x9A00, 0x00CF            ; code 0-4GB
gdt_data:
    dw 0xFFFF, 0x0000, 0x9200, 0x00CF            ; data 0-4GB
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start
