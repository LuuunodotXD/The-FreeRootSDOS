#ifndef VGA_MODE_H
#define VGA_MODE_H

// Troca de modo VGA por escrita direta nas portas (sem BIOS).
// Pode ser chamado em modo protegido.

void vga_set_mode12h(void);  // 640×480, 16 cores (Balloon/GUI HD)
void vga_set_mode13h(void);  // 320×200, 256 cores (Balloon/GUI)
void vga_set_mode03h(void);  // 80×25 texto (shell) — inclui recarga automática da fonte
void vga_load_font_8x8(void); // recarrega fonte 8×8 no plano 2 (chamado por vga_set_mode03h)

#endif
