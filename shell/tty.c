// tty.c -- múltiplos terminais virtuais
#include <stdint.h>
#include "tty.h"
#include "terminal.h"
#include "fs_disk.h"
#include "kmalloc.h"
#include "io.h"

#define VGA_ADDR 0xB8000

static tty_t    ttys[TTY_MAX];
static int      current_tty = 0;

// ----------------------------------------------------------------
// Utilitários internos
// ----------------------------------------------------------------

static int tty_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static void tty_strncpy(char *d, const char *s, int max) {
    int i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]='\0';
}
static int tty_strcmp(const char *a, const char *b) {
    while(*a&&*b&&*a==*b){a++;b++;} return *a-*b;
}
static int tty_isspace(char c) { return c==' '||c=='\t'||c=='\r'; }

// Encontra índice do diretório .VAR no A:
static int find_var_dir(void) {
    fsd_entry_t *t = fsd_table();
    for(int i=1;i<fsd_max();i++) {
        if(t[i].type!=FSD_DIR) continue;
        const char *n=t[i].name;
        if(n[0]=='.'&&n[1]=='V'&&n[2]=='A'&&n[3]=='R'&&n[4]=='\0') return i;
    }
    return -1;
}

// Encontra arquivo pelo nome dentro de um diretório
static int find_file_in(int dir_idx, const char *name, const char *ext) {
    fsd_entry_t *t = fsd_table();
    for(int i=1;i<fsd_max();i++) {
        if(t[i].type!=FSD_FILE) continue;
        if(t[i].parent!=(uint8_t)dir_idx) continue;
        if(tty_strcmp(t[i].name,name)!=0) continue;
        if(tty_strcmp(t[i].ext,ext)!=0) continue;
        return i;
    }
    return -1;
}

// ----------------------------------------------------------------
// Parser do tty.var
// ----------------------------------------------------------------

// Extrai numero hex de "0xN)" ou "N)"
static int parse_tty_num(const char *s) {
    while(*s==' ') s++;
    if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')) s+=2;
    int v=0;
    while(*s&&*s!=')') {
        if(*s>='0'&&*s<='9') v=v*16+(*s-'0');
        else if(*s>='a'&&*s<='f') v=v*16+(*s-'a'+10);
        else if(*s>='A'&&*s<='F') v=v*16+(*s-'A'+10);
        s++;
    }
    return v;
}

// Retorna ponteiro para texto após ")"
static const char *after_paren(const char *s) {
    while(*s&&*s!=')') s++;
    if(*s==')') s++;
    while(*s==' ') s++;
    return s;
}

static void parse_tty_var(const char *data) {
    const char *p = data;
    while(*p) {
        while(*p&&(*p=='\n'||tty_isspace(*p))) p++;
        if(!*p) break;
        if(*p=='#') { while(*p&&*p!='\n') p++; continue; }

        // Identifica o comando
        if(tty_strcmp(p,"end")==0||
          (p[0]=='e'&&p[1]=='n'&&p[2]=='d'&&(p[3]=='\n'||p[3]=='\r'||!p[3]))) break;

        // start(0xN)
        if(p[0]=='s'&&p[1]=='t'&&p[2]=='a'&&p[3]=='r'&&p[4]=='t'&&p[5]=='(') {
            int n = parse_tty_num(p+6);
            if(n>0&&n<TTY_MAX) ttys[n].active=1;
            while(*p&&*p!='\n') p++; continue;
        }

        // kill(0xN)
        if(p[0]=='k'&&p[1]=='i'&&p[2]=='l'&&p[3]=='l'&&p[4]=='(') {
            int n = parse_tty_num(p+5);
            if(n>0&&n<TTY_MAX) { ttys[n].killed=1; ttys[n].active=0; }
            while(*p&&*p!='\n') p++; continue;
        }

        // exit(0xN) -- apenas marca que pode ser reaberto mas sai
        // Tratado em runtime, não aqui

        // run(0xN) cmd
        if(p[0]=='r'&&p[1]=='u'&&p[2]=='n'&&p[3]=='(') {
            int n = parse_tty_num(p+4);
            const char *cmd = after_paren(p+4);
            // Avança ate fim da linha
            const char *end = cmd;
            while(*end&&*end!='\n'&&*end!='\r') end++;
            if(n>=0&&n<TTY_MAX) {
                int len = end-cmd;
                if(len>=TTY_HIST_LEN) len=TTY_HIST_LEN-1;
                for(int i=0;i<len;i++) ttys[n].run_cmd[i]=cmd[i];
                ttys[n].run_cmd[len]='\0';
            }
            p=end; continue;
        }

        // goto(0xN) -- switch inicial
        if(p[0]=='g'&&p[1]=='o'&&p[2]=='t'&&p[3]=='o'&&p[4]=='(') {
            int n = parse_tty_num(p+5);
            if(n>=0&&n<TTY_MAX&&ttys[n].active&&!ttys[n].killed)
                current_tty=n;
            while(*p&&*p!='\n') p++; continue;
        }

        while(*p&&*p!='\n') p++;
    }
}

// ----------------------------------------------------------------
// VGA
// ----------------------------------------------------------------

static void vga_save_to(tty_t *t) {
    volatile uint16_t *vga = (volatile uint16_t*)VGA_ADDR;
    for(int i=0;i<TTY_COLS*TTY_ROWS;i++) t->buf[i]=vga[i];
    // Salva posição do cursor via portas
    outb(0x3D4,14); t->cursor_x = inb(0x3D5)<<8;
    outb(0x3D4,15); t->cursor_x |= inb(0x3D5);
    t->cursor_y = t->cursor_x / TTY_COLS;
    t->cursor_x = t->cursor_x % TTY_COLS;
}

static void vga_restore_from(tty_t *t) {
    volatile uint16_t *vga = (volatile uint16_t*)VGA_ADDR;
    for(int i=0;i<TTY_COLS*TTY_ROWS;i++) vga[i]=t->buf[i];
    uint16_t pos = t->cursor_y*TTY_COLS+t->cursor_x;
    outb(0x3D4,14); outb(0x3D5,(pos>>8)&0xFF);
    outb(0x3D4,15); outb(0x3D5,pos&0xFF);
}

// ----------------------------------------------------------------
// API pública
// ----------------------------------------------------------------

void tty_init(void) {
    for(int i=0;i<TTY_MAX;i++) {
        ttys[i].active    = (i==0);
        ttys[i].killed    = 0;
        ttys[i].started   = 0;
        ttys[i].drive     = 'A';
        ttys[i].cwd_disk  = 0;
        ttys[i].cwd_heap  = 0;
        ttys[i].show_time = 0;
        ttys[i].show_drive= 1;
        ttys[i].run_cmd[0]= '\0';
        ttys[i].hist_count= 0;
        ttys[i].hist_head = 0;
        ttys[i].fg        = 0xF;
        ttys[i].bg        = 0x0;
        ttys[i].cursor_x  = 0;
        ttys[i].cursor_y  = 0;
        // Preenche buffer com espaços brancos sobre preto
        for(int j=0;j<TTY_COLS*TTY_ROWS;j++) ttys[i].buf[j]=0x0F20;
    }
    current_tty=0;

    // Carrega tty.var
    int var_dir = find_var_dir();
    if(var_dir<0) return;
    int fidx = find_file_in(var_dir,"tty","var");
    if(fidx<0) return;
    const char *data = fsd_read_idx(fidx);
    if(!data) return;
    parse_tty_var(data);
    fsd_kfree_read(data);
}

int tty_current(void) { return current_tty; }

tty_t *tty_get(int n) {
    if(n<0||n>=TTY_MAX) return 0;
    return &ttys[n];
}

tty_t *tty_active(void) { return &ttys[current_tty]; }

void tty_save(void) {
    vga_save_to(&ttys[current_tty]);
    ttys[current_tty].fg = terminal_get_fg();
    ttys[current_tty].bg = terminal_get_bg();
}

void tty_restore(int n) {
    vga_restore_from(&ttys[n]);
    terminal_set_fg(ttys[n].fg);
    terminal_set_bg(ttys[n].bg);
}

int tty_switch(int n) {
    if(n<0||n>=TTY_MAX) return -1;
    if(!ttys[n].active||ttys[n].killed) return -1;
    if(n==current_tty) return 0;
    tty_save();
    current_tty=n;
    tty_restore(n);
    return 0;
}

int tty_run_init(int n) {
    if(n<0||n>=TTY_MAX) return 0;
    if(ttys[n].started) return 0;  // só executa na primeira vez
    ttys[n].started=1;
    if(!ttys[n].run_cmd[0]) return 0;
    return 1;   // shell vai executar tty_get(n)->run_cmd
}

void tty_exit(void) {
    if (current_tty == 0) return;   // tty 0 nao pode dar exit
    tty_switch(0);
}
