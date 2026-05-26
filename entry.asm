[bits 32]

global _start

; Símbolos definidos pelo linker.ld — externos ao NASM
extern kernel_main
extern _bss_start
extern _bss_end

section .text

_start:
    mov word [0xB8000], 0x0F42   ; 'B' = boot chegou aqui

    ; Zera a BSS
    cld
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    test ecx, ecx
    jle .bss_done
    xor eax, eax
    rep stosb
.bss_done:

    mov word [0xB8002], 0x0F4B   ; 'K' = kernel_main sendo chamado

    call kernel_main

.hang:
    hlt
    jmp .hang
