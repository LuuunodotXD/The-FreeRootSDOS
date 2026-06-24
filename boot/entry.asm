[bits 32]
global _start
extern kernel_main
extern _bss_start
extern _bss_end

section .text
_start:
    ; Zera BSS
    cld
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; EBX = fs_lba (passado pelo bootloader)
    push ebx        ; argumento 1 para kernel_main
    call kernel_main

.hang:
    hlt
    jmp .hang
