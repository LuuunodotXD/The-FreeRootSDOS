// idt.c
#include <stdint.h>
#include "idt.h"
#include "io.h"
#include "keyboard.h"

// ----------------------------------------------------------------
// Estruturas da IDT
// ----------------------------------------------------------------

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

static void idt_set(uint8_t num, uint32_t handler) {
    idt[num].offset_low  = handler & 0xFFFF;
    idt[num].selector    = 0x08;
    idt[num].zero        = 0;
    idt[num].type_attr   = 0x8E;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
}

// ----------------------------------------------------------------
// PIC 8259
// ----------------------------------------------------------------

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_remap(void) {
    outb(PIC1_CMD,  0x11); io_wait();
    outb(PIC2_CMD,  0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0b11111100);
    outb(PIC2_DATA, 0b11111111);
}

// ----------------------------------------------------------------
// PIT / Tick counter (1000 Hz = 1 tick por ms)
// ----------------------------------------------------------------

#define PIT_CMD   0x43
#define PIT_DATA  0x40
#define PIT_HZ    1000u
#define PIT_BASE  1193182u
#define PIT_DIV   (PIT_BASE / PIT_HZ)

static volatile uint32_t ticks = 0;

static void pit_init(void) {
    outb(PIT_CMD,  0x36);
    outb(PIT_DATA, (uint8_t)(PIT_DIV & 0xFF));
    outb(PIT_DATA, (uint8_t)(PIT_DIV >> 8));
}

uint32_t timer_get_ticks(void) { return ticks; }

void timer_sleep(uint32_t ms) {
    uint32_t end = ticks + ms;
    while (ticks < end)
        asm volatile ("hlt");
}

// ----------------------------------------------------------------
// Exceções da CPU (ISRs 0–31)
//
// Algumas exceções empurram um error code extra na pilha (EC=1).
// O stub empurra um dummy 0 para as que não têm, mantendo a pilha
// uniforme: sempre [dummy/ec, eip, cs, eflags] acima de pusha.
//
// O handler C recebe um struct com todos os registradores + info.
// ----------------------------------------------------------------

struct regs {
    // Empurrados pelo pusha (ordem inversa)
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    // Empurrados pelo stub
    uint32_t isr_num;   // número da exceção
    uint32_t err_code;  // error code (ou 0 se não tem)
    // Empurrados pela CPU ao entrar na exceção
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};

static const char *exception_names[] = {
    "Division by Zero",          // 0
    "Debug",                     // 1
    "Non-Maskable Interrupt",    // 2
    "Breakpoint",                // 3
    "Overflow",                  // 4
    "Bound Range Exceeded",      // 5
    "Invalid Opcode",            // 6
    "Device Not Available",      // 7
    "Double Fault",              // 8
    "Coprocessor Segment Overrun", // 9
    "Invalid TSS",               // 10
    "Segment Not Present",       // 11
    "Stack-Segment Fault",       // 12
    "General Protection Fault",  // 13
    "Page Fault",                // 14
    "Reserved",                  // 15
    "x87 Floating-Point",        // 16
    "Alignment Check",           // 17
    "Machine Check",             // 18
    "SIMD Floating-Point",       // 19
    "Virtualization",            // 20
    "Control Protection",        // 21
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", // 22-27
    "Hypervisor Injection",      // 28
    "VMM Communication",         // 29
    "Security Exception",        // 30
    "Reserved"                   // 31
};

// Imprime uint32 em hex sem depender de nada externo
static void print_hex(uint32_t val) {
    // Acessa VGA diretamente para não depender do terminal (que pode estar corrompido)
    // Mas usamos terminal_writestring pois estamos em modo protegido seguro
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        int nibble = val & 0xF;
        buf[i] = (nibble < 10) ? '0' + nibble : 'A' + nibble - 10;
        val >>= 4;
    }
    buf[10] = '\0';
    terminal_writestring(buf);
}

// Handler C — chamado por todos os stubs de exceção
void isr_handler(struct regs *r) {
    // Barra vermelha de erro: muda cor para branco sobre vermelho
    terminal_set_fg(0xF);
    terminal_set_bg(0x4);
    terminal_clear();

    terminal_writestring("*** KERNEL PANIC ***\n\n");

    if (r->isr_num < 32) {
        terminal_writestring("Excecao: #");
        // Imprime numero
        char num[3];
        int n = r->isr_num, i = 0;
        if (n >= 10) { num[i++] = '0' + n / 10; }
        num[i++] = '0' + n % 10;
        num[i] = '\0';
        terminal_writestring(num);
        terminal_writestring(" - ");
        terminal_writestring(exception_names[r->isr_num]);
        terminal_writestring("\n");
    }

    terminal_writestring("Error Code: "); print_hex(r->err_code); terminal_writestring("\n");
    terminal_writestring("EIP: ");        print_hex(r->eip);      terminal_writestring("\n");
    terminal_writestring("CS:  ");        print_hex(r->cs);       terminal_writestring("\n");
    terminal_writestring("EFLAGS: ");     print_hex(r->eflags);   terminal_writestring("\n\n");
    terminal_writestring("EAX: "); print_hex(r->eax);
    terminal_writestring("  EBX: "); print_hex(r->ebx); terminal_writestring("\n");
    terminal_writestring("ECX: "); print_hex(r->ecx);
    terminal_writestring("  EDX: "); print_hex(r->edx); terminal_writestring("\n");
    terminal_writestring("ESI: "); print_hex(r->esi);
    terminal_writestring("  EDI: "); print_hex(r->edi); terminal_writestring("\n\n");
    terminal_writestring("\nPressione Enter para reiniciar (auto em 20s)...\n");

    // Aguarda Enter ou timeout de 20 000 ms
    uint32_t deadline = ticks + 20000u;
    while (ticks < deadline) {
        if (keyboard_available()) {
            int k = getchar();
            if (k == '\n') break;
        }
        asm volatile ("hlt");
    }

    // Reinicia via pulse no controlador de teclado
    while (inb(0x64) & 0x02);
    outb(0x64, 0xFE);
    while (1) asm volatile ("hlt");
}

// Macro para gerar stubs: SEM error code (empurra dummy 0)
#define ISR_NOERR(n) \
    static void __attribute__((naked)) isr##n(void) { \
        asm volatile ( \
            "push $0\n\t"        /* dummy error code */ \
            "push $" #n "\n\t"   /* numero da excecao */ \
            "pusha\n\t" \
            "push %esp\n\t"      /* ponteiro para struct regs */ \
            "call isr_handler\n\t" \
            "add $4, %esp\n\t" \
            "popa\n\t" \
            "add $8, %esp\n\t"   /* remove isr_num e err_code */ \
            "iret" \
        ); \
    }

// Macro para stubs COM error code (CPU já empurrou o code)
#define ISR_ERR(n) \
    static void __attribute__((naked)) isr##n(void) { \
        asm volatile ( \
            "push $" #n "\n\t" \
            "pusha\n\t" \
            "push %esp\n\t" \
            "call isr_handler\n\t" \
            "add $4, %esp\n\t" \
            "popa\n\t" \
            "add $8, %esp\n\t" \
            "iret" \
        ); \
    }

// ISRs 0–31 — quais têm error code: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30
ISR_NOERR(0)  ISR_NOERR(1)  ISR_NOERR(2)  ISR_NOERR(3)
ISR_NOERR(4)  ISR_NOERR(5)  ISR_NOERR(6)  ISR_NOERR(7)
ISR_ERR(8)    ISR_NOERR(9)  ISR_ERR(10)   ISR_ERR(11)
ISR_ERR(12)   ISR_ERR(13)   ISR_ERR(14)   ISR_NOERR(15)
ISR_NOERR(16) ISR_ERR(17)   ISR_NOERR(18) ISR_NOERR(19)
ISR_NOERR(20) ISR_ERR(21)   ISR_NOERR(22) ISR_NOERR(23)
ISR_NOERR(24) ISR_NOERR(25) ISR_NOERR(26) ISR_NOERR(27)
ISR_NOERR(28) ISR_ERR(29)   ISR_ERR(30)   ISR_NOERR(31)

// ----------------------------------------------------------------
// IRQ handlers (IRQ0 = timer, IRQ1 = teclado)
// ----------------------------------------------------------------

static void timer_handler(void) {
    ticks++;
    outb(PIC1_CMD, PIC_EOI);
}

void keyboard_irq(void);

static void __attribute__((naked)) irq0_stub(void) {
    asm volatile ("pusha\n\t" "call timer_handler\n\t" "popa\n\t" "iret");
}

static void __attribute__((naked)) irq1_stub(void) {
    asm volatile ("pusha\n\t" "call keyboard_irq\n\t" "popa\n\t" "iret");
}

// ----------------------------------------------------------------
// Inicialização
// ----------------------------------------------------------------

void idt_init(void) {
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low  = 0;
        idt[i].selector    = 0x08;
        idt[i].zero        = 0;
        idt[i].type_attr   = 0x8E;
        idt[i].offset_high = 0;
    }

    pic_remap();
    pit_init();

    // Registra as 32 exceções da CPU
    idt_set(0,  (uint32_t)isr0);  idt_set(1,  (uint32_t)isr1);
    idt_set(2,  (uint32_t)isr2);  idt_set(3,  (uint32_t)isr3);
    idt_set(4,  (uint32_t)isr4);  idt_set(5,  (uint32_t)isr5);
    idt_set(6,  (uint32_t)isr6);  idt_set(7,  (uint32_t)isr7);
    idt_set(8,  (uint32_t)isr8);  idt_set(9,  (uint32_t)isr9);
    idt_set(10, (uint32_t)isr10); idt_set(11, (uint32_t)isr11);
    idt_set(12, (uint32_t)isr12); idt_set(13, (uint32_t)isr13);
    idt_set(14, (uint32_t)isr14); idt_set(15, (uint32_t)isr15);
    idt_set(16, (uint32_t)isr16); idt_set(17, (uint32_t)isr17);
    idt_set(18, (uint32_t)isr18); idt_set(19, (uint32_t)isr19);
    idt_set(20, (uint32_t)isr20); idt_set(21, (uint32_t)isr21);
    idt_set(22, (uint32_t)isr22); idt_set(23, (uint32_t)isr23);
    idt_set(24, (uint32_t)isr24); idt_set(25, (uint32_t)isr25);
    idt_set(26, (uint32_t)isr26); idt_set(27, (uint32_t)isr27);
    idt_set(28, (uint32_t)isr28); idt_set(29, (uint32_t)isr29);
    idt_set(30, (uint32_t)isr30); idt_set(31, (uint32_t)isr31);

    // IRQs
    idt_set(0x20, (uint32_t)irq0_stub);
    idt_set(0x21, (uint32_t)irq1_stub);

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;
    asm volatile ("lidt %0" : : "m"(idtp));
    asm volatile ("sti");
}
