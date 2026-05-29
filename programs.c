// programs.c -- programas externos da shell
#include <stdint.h>
#include "programs.h"
#include "terminal.h"
#include "keyboard.h"
#include "fs.h"
#include "fs_disk.h"
#include "kmalloc.h"

// ----------------------------------------------------------------
// Utilitários locais
// ----------------------------------------------------------------
static int pg_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static int pg_strcmpi(const char *a, const char *b) {
    while(*a&&*b) {
        char ca=*a,cb=*b;
        if(ca>='A'&&ca<='Z') ca+=32;
        if(cb>='A'&&cb<='Z') cb+=32;
        if(ca!=cb) return ca-cb;
        a++;b++;
    }
    return *a-*b;
}
static const char *pg_startswith(const char *s, const char *p) {
    while(*p) {
        char cs=*s,cp=*p;
        if(cs>='A'&&cs<='Z') cs+=32;
        if(cp>='A'&&cp<='Z') cp+=32;
        if(cs!=cp) return 0;
        s++;p++;
    }
    return s;
}

// ----------------------------------------------------------------
// Editor de texto (original, mantido)
// ----------------------------------------------------------------
#define ED_MAX_LINES  23
#define ED_MAX_COLS   78
static char ed_buf[ED_MAX_LINES][ED_MAX_COLS + 1];
static int  ed_len[ED_MAX_LINES];
static int  ed_nlines;
static int  ed_row, ed_col;
static int  ed_modified;
static char ed_name[9], ed_ext[4];
static char ed_drive;

static void ed_display_name(char *out) {
    int i=0;
    const char *n=ed_name;
    while(*n) out[i++]=*n++;
    if(ed_ext[0]){ out[i++]='.'; n=ed_ext; while(*n) out[i++]=*n++; }
    out[i]='\0';
}
static void ed_render(void) {
    uint8_t fg = terminal_get_fg();
    uint8_t bg = terminal_get_bg();
    uint8_t status_color = 0x1F;
    for(int r=0; r<ED_MAX_LINES; r++) {
        terminal_clear_row(r);
        terminal_goto(0, r);
        if(r < ed_nlines) {
            for(int c=0; c<ed_len[r]; c++)
                terminal_putchar(ed_buf[r][c]);
        }
    }
    {
        char sep[81];
        for(int i=0;i<80;i++) sep[i]='-';
        sep[80]='\0';
        terminal_write_at(0, 23, 0x07, sep);
    }
    {
        char status[81];
        for(int i=0;i<80;i++) status[i]=' ';
        status[80]='\0';
        char fname[14]; ed_display_name(fname);
        int pos=0;
        const char *tag=ed_modified?"[edit*] ":"[edit] ";
        while(*tag) status[pos++]=*tag++;
        for(int i=0;fname[i]&&pos<40;i++) status[pos++]=fname[i];
        status[pos++]=' '; status[pos++]=' ';
        status[pos++]='L'; status[pos++]='n';
        status[pos++]=' ';
        int ln=ed_row+1;
        if(ln>=10) status[pos++]='0'+ln/10;
        status[pos++]='0'+ln%10;
        status[pos++]='/';
        int tot=ed_nlines;
        if(tot>=10) status[pos++]='0'+tot/10;
        status[pos++]='0'+tot%10;
        status[pos++]=' '; status[pos++]=' ';
        const char *help="^S salva  ^Q sai  Esc cancela";
        while(*help&&pos<79) status[pos++]=*help++;
        terminal_write_at(0, 24, status_color, status);
    }
    terminal_goto(ed_col, ed_row);
}
static void ed_ensure_lines(void) {
    if(ed_nlines==0) { ed_nlines=1; ed_len[0]=0; ed_buf[0][0]='\0'; }
}
static void ed_load(char drive) {
    const char *data = (drive=='A') ? fsd_read(ed_name,ed_ext) : fs_read(ed_name,ed_ext);
    ed_nlines=0;
    int r=0, c=0;
    if(data) {
        for(int i=0; data[i] && r<ED_MAX_LINES; i++) {
            if(data[i]=='\n') {
                ed_buf[r][c]='\0'; ed_len[r]=c;
                r++; c=0;
            } else if(c<ED_MAX_COLS) {
                ed_buf[r][c++]=data[i];
            }
        }
        if(c>0||r>0) {
            ed_buf[r][c]='\0'; ed_len[r]=c; r++;
        }
        ed_nlines=r;
    }
    ed_ensure_lines();
}
static void ed_save(char drive) {
    uint32_t total=0;
    for(int r=0;r<ed_nlines;r++) {
        total+=ed_len[r];
        if(r<ed_nlines-1) total++;
    }
    char *buf=(char*)kmalloc(total+1);
    if(!buf) return;
    uint32_t pos=0;
    for(int r=0;r<ed_nlines;r++) {
        for(int c=0;c<ed_len[r];c++) buf[pos++]=ed_buf[r][c];
        if(r<ed_nlines-1) buf[pos++]='\n';
    }
    buf[pos]='\0';
    if(drive=='A') fsd_write(ed_name,ed_ext,buf,total);
    else           fs_write(ed_name,ed_ext,buf,total);
    kfree(buf);
    ed_modified=0;
}
static void ed_newline(void) {
    if(ed_nlines>=ED_MAX_LINES) return;
    for(int r=ed_nlines;r>ed_row+1;r--) {
        ed_len[r]=ed_len[r-1];
        for(int c=0;c<=ed_len[r-1];c++) ed_buf[r][c]=ed_buf[r-1][c];
    }
    ed_nlines++;
    int new_r=ed_row+1;
    int tail=ed_len[ed_row]-ed_col;
    ed_len[new_r]=tail;
    for(int c=0;c<tail;c++) ed_buf[new_r][c]=ed_buf[ed_row][ed_col+c];
    ed_buf[new_r][tail]='\0';
    ed_len[ed_row]=ed_col;
    ed_buf[ed_row][ed_col]='\0';
    ed_row=new_r; ed_col=0;
}
static void ed_delete_line(void) {
    if(ed_nlines==1) { ed_len[0]=0; ed_buf[0][0]='\0'; ed_col=0; return; }
    for(int r=ed_row;r<ed_nlines-1;r++) {
        ed_len[r]=ed_len[r+1];
        for(int c=0;c<=ed_len[r+1];c++) ed_buf[r][c]=ed_buf[r+1][c];
    }
    ed_nlines--;
    if(ed_row>=ed_nlines) ed_row=ed_nlines-1;
    if(ed_col>ed_len[ed_row]) ed_col=ed_len[ed_row];
}
static void prog_edit(const char *arg, char drive) {
    if(!arg||!arg[0]) {
        terminal_writestring("Uso: edit <arquivo>\n");
        return;
    }
    if(drive=='A') fsd_split(arg, ed_name, ed_ext);
    else           fs_split(arg, ed_name, ed_ext);
    ed_drive=drive;
    ed_row=0; ed_col=0; ed_modified=0;
    ed_load(drive);
    ed_render();
    while(1) {
        int k=getchar();
        if(k==KEY_ESC) break;
        if(k==KEY_CTRL_Q) { ed_save(drive); break; }
        if(k==KEY_CTRL_S) { ed_save(drive); ed_render(); continue; }
        if(k==KEY_CTRL_D) { ed_delete_line(); ed_modified=1; ed_render(); continue; }
        if(k==KEY_UP) {
            if(ed_row>0) { ed_row--; if(ed_col>ed_len[ed_row]) ed_col=ed_len[ed_row]; }
            ed_render(); continue;
        }
        if(k==KEY_DOWN) {
            if(ed_row<ed_nlines-1) { ed_row++; if(ed_col>ed_len[ed_row]) ed_col=ed_len[ed_row]; }
            ed_render(); continue;
        }
        if(k==KEY_LEFT) {
            if(ed_col>0) ed_col--;
            else if(ed_row>0) { ed_row--; ed_col=ed_len[ed_row]; }
            ed_render(); continue;
        }
        if(k==KEY_RIGHT) {
            if(ed_col<ed_len[ed_row]) ed_col++;
            else if(ed_row<ed_nlines-1) { ed_row++; ed_col=0; }
            ed_render(); continue;
        }
        if(k==KEY_HOME) { ed_col=0; ed_render(); continue; }
        if(k==KEY_END)  { ed_col=ed_len[ed_row]; ed_render(); continue; }
        if(k=='\n') { ed_newline(); ed_modified=1; ed_render(); continue; }
        if(k=='\b') {
            if(ed_col>0) {
                for(int c=ed_col-1;c<ed_len[ed_row]-1;c++) ed_buf[ed_row][c]=ed_buf[ed_row][c+1];
                ed_len[ed_row]--;
                ed_buf[ed_row][ed_len[ed_row]]='\0';
                ed_col--;
            } else if(ed_row>0) {
                int prev=ed_row-1;
                int prev_len=ed_len[prev];
                if(prev_len+ed_len[ed_row]<=ED_MAX_COLS) {
                    for(int c=0;c<ed_len[ed_row];c++) ed_buf[prev][prev_len+c]=ed_buf[ed_row][c];
                    ed_len[prev]+=ed_len[ed_row];
                    ed_buf[prev][ed_len[prev]]='\0';
                    for(int r=ed_row;r<ed_nlines-1;r++) {
                        ed_len[r]=ed_len[r+1];
                        for(int c=0;c<=ed_len[r+1];c++) ed_buf[r][c]=ed_buf[r+1][c];
                    }
                    ed_nlines--;
                    ed_row=prev; ed_col=prev_len;
                }
            }
            ed_modified=1; ed_render(); continue;
        }
        if(k==KEY_DEL) {
            if(ed_col<ed_len[ed_row]) {
                for(int c=ed_col;c<ed_len[ed_row]-1;c++) ed_buf[ed_row][c]=ed_buf[ed_row][c+1];
                ed_len[ed_row]--;
                ed_buf[ed_row][ed_len[ed_row]]='\0';
            } else if(ed_row<ed_nlines-1) {
                int next=ed_row+1;
                if(ed_len[ed_row]+ed_len[next]<=ED_MAX_COLS) {
                    for(int c=0;c<ed_len[next];c++) ed_buf[ed_row][ed_len[ed_row]+c]=ed_buf[next][c];
                    ed_len[ed_row]+=ed_len[next];
                    ed_buf[ed_row][ed_len[ed_row]]='\0';
                    for(int r=next;r<ed_nlines-1;r++) {
                        ed_len[r]=ed_len[r+1];
                        for(int c=0;c<=ed_len[r+1];c++) ed_buf[r][c]=ed_buf[r+1][c];
                    }
                    ed_nlines--;
                }
            }
            ed_modified=1; ed_render(); continue;
        }
        if(k>=' '&&k<='~'&&ed_len[ed_row]<ED_MAX_COLS) {
            for(int c=ed_len[ed_row];c>ed_col;c--) ed_buf[ed_row][c]=ed_buf[ed_row][c-1];
            ed_buf[ed_row][ed_col]=(char)k;
            ed_len[ed_row]++;
            ed_buf[ed_row][ed_len[ed_row]]='\0';
            ed_col++;
            ed_modified=1; ed_render();
        }
    }
    terminal_clear();
}

// ----------------------------------------------------------------
// hexdump (editor hexadecimal)
// ----------------------------------------------------------------
static void itoa_hex(uint32_t val, char *buf, int digits) {
    for (int i = digits-1; i >= 0; i--) {
        int nib = (val >> (i*4)) & 0xF;
        buf[i] = (nib < 10) ? '0' + nib : 'A' + nib - 10;
    }
    buf[digits] = '\0';
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void prog_hexdump(const char *arg, char drive) {
    if (!arg || !arg[0]) {
        terminal_writestring("Uso: hexdump <arquivo> (edita hexadecimal)\n");
        return;
    }
    char name[9], ext[4];
    if (drive == 'A')
        fsd_split(arg, name, ext);
    else
        fs_split(arg, name, ext);
    // Carrega ou cria arquivo vazio
    const char *data = (drive == 'A') ? fsd_read(name, ext) : fs_read(name, ext);
    uint32_t size = 0;
    if (data) while (data[size]) size++;
    if (!data) {
        // Cria arquivo vazio
        if (drive == 'A') {
            if (fsd_write(name, ext, "", 0) != 0) {
                terminal_writestring("Erro ao criar arquivo.\n");
                return;
            }
        } else {
            if (fs_write(name, ext, "", 0) != 0) {
                terminal_writestring("Erro ao criar arquivo.\n");
                return;
            }
        }
        data = "";
        size = 0;
    }

    // Copia para área editável (com espaço extra para inserções)
    char *buf = (char*)kmalloc(size + 1 + 4096);
    if (!buf) return;
    for (uint32_t i = 0; i < size; i++) buf[i] = data[i];
    buf[size] = '\0';

    uint8_t old_fg = terminal_get_fg();
    uint8_t old_bg = terminal_get_bg();
    terminal_clear();
    terminal_set_fg(0xF);
    terminal_set_bg(0x0);

    uint32_t pos = 0;
    uint32_t lines = (size + 15) / 16;
    if (lines < 1) lines = 1;
    uint32_t top_line = 0;
    int ch;
    int dirty = 0;
    int hex_pending = 0;   // 0 = esperando primeiro dígito, 1 = esperando segundo
    int pending_high = 0;

    void draw() {
        terminal_clear();
        terminal_write_at(0, 0, 0x0E, "HEX EDITOR - Setas/Home/End/Del, n=insere, !=sai sem salvar, Esc salva e sai");
        for (int i = 0; i < 20; i++) {
            uint32_t line = top_line + i;
            if (line >= lines) break;
            uint32_t addr = line * 16;
            char addr_str[9];
            itoa_hex(addr, addr_str, 8);
            char line_buf[80];
            int p = 0;
            for (int j = 0; j < 8; j++) line_buf[p++] = addr_str[j];
            line_buf[p++] = ' ';
            for (int j = 0; j < 16; j++) {
                int byte_idx = addr + j;
                if (byte_idx < (int)size) {
                    uint8_t b = buf[byte_idx];
                    line_buf[p++] = "0123456789ABCDEF"[b >> 4];
                    line_buf[p++] = "0123456789ABCDEF"[b & 0xF];
                } else {
                    line_buf[p++] = ' ';
                    line_buf[p++] = ' ';
                }
                line_buf[p++] = ' ';
                if (j == 7) line_buf[p++] = ' ';
            }
            line_buf[p++] = ' ';
            for (int j = 0; j < 16; j++) {
                int byte_idx = addr + j;
                if (byte_idx < (int)size) {
                    char c = buf[byte_idx];
                    line_buf[p++] = (c >= 32 && c < 127) ? c : '.';
                } else {
                    line_buf[p++] = ' ';
                }
            }
            line_buf[p] = '\0';
            uint8_t color = (addr <= pos && pos < addr+16) ? 0x1E : 0x0F;
            terminal_write_at(0, i+2, color, line_buf);
        }
        int row = (pos / 16) - top_line + 2;
        int col = 9 + (pos % 16) * 3 + (pos % 16 >= 8 ? 1 : 0);
        terminal_goto(col, row);
        char status[80];
        int p = 0;
        status[p++] = 'P'; status[p++] = 'o'; status[p++] = 's'; status[p++] = ':'; status[p++] = ' ';
        char tmp[12];
        itoa_hex(pos, tmp, 8);
        for (int i = 0; i < 8; i++) status[p++] = tmp[i];
        status[p++] = ' ';
        status[p++] = 'S'; status[p++] = 'i'; status[p++] = 'z'; status[p++] = 'e'; status[p++] = ':';
        uint32_t n = size;
        char nstr[12]; int ni=0;
        do { nstr[ni++] = '0' + (n % 10); n /= 10; } while (n);
        while (ni--) status[p++] = nstr[ni];
        if (dirty) { status[p++] = ' '; status[p++] = '*'; }
        status[p] = '\0';
        terminal_write_at(0, 22, 0x1F, status);
    }

    draw();

    while (1) {
        ch = getchar();
        if (ch == KEY_ESC) break;  // salva e sai
        if (ch == '!') {           // sai sem salvar
            dirty = 0;
            break;
        }
        if (ch == 'n' || ch == 'N') {
            // Inserir novo byte (0x00) na posição atual
            if (size + 1 >= (uint32_t)(size + 4096)) {
                terminal_writestring("Arquivo muito grande para inserir.\n");
                continue;
            }
            for (uint32_t i = size; i > pos; i--) buf[i] = buf[i-1];
            buf[pos] = 0;
            size++;
            dirty = 1;
            lines = (size + 15) / 16;
            if (top_line > (pos / 16)) top_line = pos / 16;
            draw();
            continue;
        }

        // Edição de byte (dois dígitos hexa)
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
            int val = hex_val(ch);
            if (val >= 0) {
                if (hex_pending == 0) {
                    pending_high = val;
                    hex_pending = 1;
                } else {
                    int low = val;
                    int new_byte = (pending_high << 4) | low;
                    if (pos < size) {
                        buf[pos] = new_byte;
                        dirty = 1;
                    }
                    hex_pending = 0;
                    draw();
                }
            }
            continue;
        }

        // Navegação
        if (ch == KEY_LEFT) {
            if (pos > 0) pos--;
            draw();
        } else if (ch == KEY_RIGHT) {
            if (pos < size) pos++;
            draw();
        } else if (ch == KEY_UP) {
            if (pos >= 16) pos -= 16;
            if (top_line > (pos / 16)) top_line = pos / 16;
            draw();
        } else if (ch == KEY_DOWN) {
            if (pos + 16 < size) pos += 16;
            else if (pos < size) pos = size - 1;
            if ((pos / 16) >= top_line + 20) top_line = (pos / 16) - 19;
            draw();
        } else if (ch == KEY_HOME) {
            pos = 0;
            top_line = 0;
            draw();
        } else if (ch == KEY_END) {
            if (size > 0) pos = size - 1;
            else pos = 0;
            if (pos >= 20*16) top_line = (pos / 16) - 19; else top_line = 0;
            draw();
        } else if (ch == KEY_DEL) {
            if (pos < size) {
                for (uint32_t i = pos; i < size-1; i++) buf[i] = buf[i+1];
                size--;
                dirty = 1;
                if (pos >= size && size > 0) pos = size - 1;
                lines = (size + 15) / 16;
                if (top_line > (pos / 16)) top_line = pos / 16;
                draw();
            }
        } else if (ch == '\n') {   // Enter salva e sai
            break;
        }
    }

    if (dirty) {
        if (drive == 'A')
            fsd_write(name, ext, buf, size);
        else
            fs_write(name, ext, buf, size);
        terminal_writestring("Arquivo salvo.\n");
    } else if (ch == '!') {
        terminal_writestring("Edição abortada.\n");
    }

    kfree(buf);
    terminal_clear();
    terminal_set_fg(old_fg);
    terminal_set_bg(old_bg);
}

// ----------------------------------------------------------------
// Calculadora simples (linha de comando)
// ----------------------------------------------------------------
static int calc_eval(const char *expr) {
    // avaliador simples que suporta + - * / e parênteses? Vamos fazer algo básico.
    // usar algoritmo de precedência
    int values[32];
    char ops[32];
    int vtop = 0, otop = 0;
    int num = 0;
    int have_num = 0;
    const char *p = expr;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            num = num * 10 + (*p - '0');
            have_num = 1;
        } else if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
            if (!have_num) return 0x80000000;
            values[vtop++] = num;
            num = 0;
            have_num = 0;
            while (otop > 0 && ((ops[otop-1] == '*' || ops[otop-1] == '/') ||
                   (*p == '+' || *p == '-') && (ops[otop-1] == '+' || ops[otop-1] == '-'))) {
                char op = ops[--otop];
                int b = values[--vtop];
                int a = values[--vtop];
                if (op == '+') values[vtop++] = a + b;
                else if (op == '-') values[vtop++] = a - b;
                else if (op == '*') values[vtop++] = a * b;
                else if (op == '/') { if (b == 0) return 0x80000000; values[vtop++] = a / b; }
            }
            ops[otop++] = *p;
        } else if (*p == ' ') {
            p++; continue;
        } else {
            return 0x80000000;
        }
        p++;
    }
    if (!have_num) return 0x80000000;
    values[vtop++] = num;
    while (otop > 0) {
        char op = ops[--otop];
        int b = values[--vtop];
        int a = values[--vtop];
        if (op == '+') values[vtop++] = a + b;
        else if (op == '-') values[vtop++] = a - b;
        else if (op == '*') values[vtop++] = a * b;
        else if (op == '/') { if (b == 0) return 0x80000000; values[vtop++] = a / b; }
    }
    return values[0];
}

static void int_to_str(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return; }
    int i = 0;
    if (n < 0) { buf[i++] = '-'; n = -n; }
    char tmp[16];
    int j = 0;
    while (n) { tmp[j++] = '0' + (n % 10); n /= 10; }
    while (j--) buf[i++] = tmp[j];
    buf[i] = 0;
}

static void prog_calc(const char *arg) {
    if (!arg || !arg[0]) {
        terminal_writestring("Uso: calc <expressao>\n");
        terminal_writestring("Ex: calc 2+3*4\n");
        return;
    }
    int res = calc_eval(arg);
    if (res == 0x80000000) {
        terminal_writestring("Erro na expressao.\n");
    } else {
        char buf[32];
        int_to_str(res, buf);
        terminal_writestring(buf);
        terminal_putchar('\n');
    }
}

// ----------------------------------------------------------------
// Dispatcher de programas
// ----------------------------------------------------------------
int prog_run(const char *cmd, char drive) {
    const char *arg;
    if ((arg = pg_startswith(cmd, "edit "))) {
        prog_edit(arg, drive);
        return 1;
    }
    if ((arg = pg_startswith(cmd, "hexdump "))) {
        prog_hexdump(arg, drive);
        return 1;
    }
    if ((arg = pg_startswith(cmd, "calc "))) {
        prog_calc(arg);
        return 1;
    }
    return 0;
}
