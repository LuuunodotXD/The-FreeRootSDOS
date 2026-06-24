// programs.c -- programas externos da shell
#include <stdint.h>
#include "programs.h"
#include "terminal.h"
#include "keyboard.h"
#include "fs.h"
#include "fs_disk.h"
#include "kmalloc.h"
#include "vga_mode.h"
#include "balloon.h"
#include "vga12h.h"
#include "idt.h"
#include "adlib.h"

// ----------------------------------------------------------------
// Utilitários locais
// ----------------------------------------------------------------
void reboot(void);

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


// ---- Estado do hexdump (compartilhado entre hx_draw e prog_hexdump) ----
typedef struct {
    char     *buf;
    uint32_t  size;
    uint32_t  pos;
    uint32_t  top_line;
    int       dirty;
} hx_state_t;

static void hx_draw(hx_state_t *hx) {
    terminal_clear();
    terminal_write_at(0, 0, 0x0E,
        "HEX EDITOR - Setas/Home/End/Del, n=insere, !=sai sem salvar, Esc salva e sai");
    uint32_t lines = (hx->size + 15) / 16;
    if (lines < 1) lines = 1;
    for (int i = 0; i < 20; i++) {
        uint32_t line = hx->top_line + i;
        if (line >= lines) break;
        uint32_t addr = line * 16;
        char addr_str[9]; itoa_hex(addr, addr_str, 8);
        char line_buf[80]; int p = 0;
        for (int j = 0; j < 8; j++) line_buf[p++] = addr_str[j];
        line_buf[p++] = ' ';
        for (int j = 0; j < 16; j++) {
            int bi = addr + j;
            if (bi < (int)hx->size) {
                uint8_t b = hx->buf[bi];
                line_buf[p++] = "0123456789ABCDEF"[b >> 4];
                line_buf[p++] = "0123456789ABCDEF"[b & 0xF];
            } else { line_buf[p++] = ' '; line_buf[p++] = ' '; }
            line_buf[p++] = ' ';
            if (j == 7) line_buf[p++] = ' ';
        }
        line_buf[p++] = ' ';
        for (int j = 0; j < 16; j++) {
            int bi = addr + j;
            if (bi < (int)hx->size) {
                char c = hx->buf[bi];
                line_buf[p++] = (c >= 32 && c < 127) ? c : '.';
            } else line_buf[p++] = ' ';
        }
        line_buf[p] = '\0';
        uint8_t color = (addr <= hx->pos && hx->pos < addr+16) ? 0x1E : 0x0F;
        terminal_write_at(0, i+2, color, line_buf);
    }
    int row = (hx->pos / 16) - hx->top_line + 2;
    int col = 9 + (hx->pos % 16) * 3 + (hx->pos % 16 >= 8 ? 1 : 0);
    terminal_goto(col, row);
    // Barra de status
    char status[80]; int p = 0;
    status[p++]='P'; status[p++]='o'; status[p++]='s'; status[p++]=':'; status[p++]=' ';
    char tmp[9]; itoa_hex(hx->pos, tmp, 8);
    for (int i = 0; i < 8; i++) status[p++] = tmp[i];
    status[p++]=' '; status[p++]='S'; status[p++]='i'; status[p++]='z'; status[p++]='e'; status[p++]=':';
    uint32_t n = hx->size; char nstr[12]; int ni = 0;
    if (!n) { nstr[ni++]='0'; } else { while(n){nstr[ni++]='0'+(n%10);n/=10;} }
    while (ni-- > 0) status[p++] = nstr[ni];
    if (hx->dirty) { status[p++]=' '; status[p++]='*'; }
    status[p] = '\0';
    terminal_write_at(0, 22, 0x1F, status);
}

static void prog_hexdump(const char *arg, char drive) {
    if (!arg || !arg[0]) {
        terminal_writestring("Uso: hexdump <arquivo>\n");
        return;
    }
    char name[9], ext[4];
    if (drive == 'A') fsd_split(arg, name, ext);
    else              fs_split(arg, name, ext);

    const char *data = (drive == 'A') ? fsd_read(name, ext) : fs_read(name, ext);
    uint32_t size = 0;
    if (data) while (data[size]) size++;

    char *buf = (char*)kmalloc(size + 1 + 4096);
    if (!buf) { if (drive == 'A' && data) kfree((void*)data); return; }
    for (uint32_t i = 0; i < size; i++) buf[i] = data[i];
    buf[size] = '\0';
    if (drive == 'A' && data) kfree((void*)data);

    uint8_t old_fg = terminal_get_fg();
    uint8_t old_bg = terminal_get_bg();

    hx_state_t hx;
    hx.buf = buf; hx.size = size; hx.pos = 0;
    hx.top_line = 0; hx.dirty = 0;

    int ch = 0;
    int hex_pending = 0;
    int pending_high = 0;

    hx_draw(&hx);

    while (1) {
        ch = getchar();

        if (ch == KEY_ESC) break;  // salva e sai
        if (ch == '!') { hx.dirty = 0; break; }  // sai sem salvar

        // Inserir byte 0x00 na posicao atual
        if (ch == 'n' || ch == 'N') {
            if (hx.size + 1 >= hx.size + 4096) continue;
            for (uint32_t i = hx.size; i > hx.pos; i--) hx.buf[i] = hx.buf[i-1];
            hx.buf[hx.pos] = 0;
            hx.size++;
            hx.dirty = 1;
            if (hx.top_line > hx.pos / 16) hx.top_line = hx.pos / 16;
            hx_draw(&hx); continue;
        }

        // Edicao: dois digitos hex
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
            int v = hex_val(ch);
            if (hex_pending == 0) {
                pending_high = v; hex_pending = 1;
            } else {
                if (hx.pos < hx.size) {
                    hx.buf[hx.pos] = (char)((pending_high << 4) | v);
                    hx.dirty = 1;
                }
                hex_pending = 0;
                hx_draw(&hx);
            }
            continue;
        }

        // Navegacao
        if (ch == KEY_LEFT) {
            if (hx.pos > 0) hx.pos--;
            if (hx.top_line > hx.pos / 16) hx.top_line = hx.pos / 16;
            hx_draw(&hx);
        } else if (ch == KEY_RIGHT) {
            if (hx.pos < hx.size) hx.pos++;
            if (hx.pos / 16 >= hx.top_line + 20) hx.top_line = hx.pos / 16 - 19;
            hx_draw(&hx);
        } else if (ch == KEY_UP) {
            if (hx.pos >= 16) hx.pos -= 16;
            if (hx.top_line > hx.pos / 16) hx.top_line = hx.pos / 16;
            hx_draw(&hx);
        } else if (ch == KEY_DOWN) {
            if (hx.pos + 16 < hx.size) hx.pos += 16;
            else if (hx.size > 0) hx.pos = hx.size - 1;
            if (hx.pos / 16 >= hx.top_line + 20) hx.top_line = hx.pos / 16 - 19;
            hx_draw(&hx);
        } else if (ch == KEY_HOME) {
            hx.pos = 0; hx.top_line = 0;
            hx_draw(&hx);
        } else if (ch == KEY_END) {
            if (hx.size > 0) hx.pos = hx.size - 1;
            hx.top_line = (hx.pos >= 20*16) ? hx.pos/16 - 19 : 0;
            hx_draw(&hx);
        } else if (ch == KEY_DEL) {
            if (hx.pos < hx.size) {
                for (uint32_t i = hx.pos; i < hx.size-1; i++) hx.buf[i] = hx.buf[i+1];
                hx.size--;
                hx.dirty = 1;
                if (hx.pos >= hx.size && hx.size > 0) hx.pos = hx.size - 1;
                if (hx.top_line > hx.pos / 16) hx.top_line = hx.pos / 16;
                hx_draw(&hx);
            }
        }
    }

    if (hx.dirty) {
        if (drive == 'A') fsd_write(name, ext, hx.buf, hx.size);
        else               fs_write(name, ext, hx.buf, hx.size);
        terminal_writestring("Arquivo salvo.\n");
    } else if (ch == '!') {
        terminal_writestring("Edição abortada.\n");
    }

    kfree(buf);
    terminal_set_fg(old_fg);
    terminal_set_bg(old_bg);
    terminal_clear();
}


static int calc_parse_num(const char **pp) {
    const char *p = *pp;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    int val = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        while ((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F')) {
            int d = (*p>='0'&&*p<='9') ? *p-'0' :
                    (*p>='a'&&*p<='f') ? *p-'a'+10 : *p-'A'+10;
            val = val*16+d; p++;
        }
    } else {
        while (*p>='0'&&*p<='9') { val=val*10+(*p-'0'); p++; }
    }
    *pp = p;
    return neg ? -val : val;
}

static int calc_prec(char op) {
    if (op=='+'||op=='-') return 1;
    if (op=='*'||op=='/'||op=='%') return 2;
    if (op=='^') return 3;
    return 0;
}

static int calc_pow(int base, int exp) {
    if (exp<0) return 0;
    int r=1; while(exp-->0) r*=base; return r;
}

static int calc_apply(char op, int a, int b, int *err) {
    if ((op=='/'||op=='%')&&b==0) { *err=1; return 0; }
    if (op=='+') return a+b;
    if (op=='-') return a-b;
    if (op=='*') return a*b;
    if (op=='/') return a/b;
    if (op=='%') return a%b;
    if (op=='^') return calc_pow(a,b);
    return 0;
}

static int calc_eval(const char *expr, int *err) {
    int  vals[32]; int vtop=0;
    char ops[32];  int otop=0;
    *err=0;
    const char *p=expr;
    int expect_val=1;
    while (*p) {
        if (*p==' ') { p++; continue; }
        if (expect_val && (*p=='-'||(*p>='0'&&*p<='9'))) {
            if (*p=='-' && !(*(p+1)>='0'&&*(p+1)<='9') && !(*(p+1)=='0')) {
                ops[otop++]='~'; p++; continue;
            }
            if (vtop>=32) { *err=1; return 0; }
            vals[vtop++]=calc_parse_num(&p);
            expect_val=0; continue;
        }
        if (*p=='(') { ops[otop++]='('; expect_val=1; p++; continue; }
        if (*p==')') {
            while (otop>0&&ops[otop-1]!='(') {
                char op=ops[--otop];
                if (vtop<2) { *err=1; return 0; }
                int b=vals[--vtop],a=vals[--vtop];
                vals[vtop++]=calc_apply(op,a,b,err);
                if (*err) return 0;
            }
            if (otop==0) { *err=1; return 0; }
            otop--; expect_val=0; p++; continue;
        }
        if (*p=='+'||*p=='-'||*p=='*'||*p=='/'||*p=='%'||*p=='^') {
            char op=*p;
            int ra=(op=='^');
            while (otop>0&&ops[otop-1]!='('&&
                   (ra?calc_prec(ops[otop-1])>calc_prec(op)
                      :calc_prec(ops[otop-1])>=calc_prec(op))) {
                char top=ops[--otop];
                if (top=='~') { if(vtop<1){*err=1;return 0;} vals[vtop-1]=-vals[vtop-1]; }
                else { if(vtop<2){*err=1;return 0;} int b=vals[--vtop],a=vals[--vtop]; vals[vtop++]=calc_apply(top,a,b,err); if(*err) return 0; }
            }
            ops[otop++]=op; expect_val=1; p++; continue;
        }
        *err=1; return 0;
    }
    while (otop>0) {
        char op=ops[--otop];
        if (op=='(') { *err=1; return 0; }
        if (op=='~') { if(vtop<1){*err=1;return 0;} vals[vtop-1]=-vals[vtop-1]; }
        else { if(vtop<2){*err=1;return 0;} int b=vals[--vtop],a=vals[--vtop]; vals[vtop++]=calc_apply(op,a,b,err); if(*err) return 0; }
    }
    if (vtop!=1) { *err=1; return 0; }
    return vals[0];
}

static void int_to_str(int n, char *buf) {
    if (!n) { buf[0]='0'; buf[1]=0; return; }
    int i=0; if (n<0) { buf[i++]='-'; n=-n; }
    char tmp[16]; int j=0;
    while (n) { tmp[j++]='0'+n%10; n/=10; }
    while (j-->0) buf[i++]=tmp[j];
    buf[i]=0;
}

static void int_to_hex(int n, char *buf) {
    buf[0]='0'; buf[1]='x';
    uint32_t u=(uint32_t)n;
    int shift=28,started=0,i=2;
    while (shift>=0) {
        int nib=(u>>shift)&0xF;
        if (nib||started||shift==0) { buf[i++]="0123456789ABCDEF"[nib]; started=1; }
        shift-=4;
    }
    buf[i]=0;
}

static void prog_calc(const char *arg) {
    if (!arg || !arg[0]) {
        terminal_writestring("Uso: calc <expressao>\n");
        terminal_writestring("Operadores: + - * / % ^ ( )\n");
        terminal_writestring("Hex: 0x1F, negativos: -5+3\n");
        return;
    }
    int err = 0;
    int res = calc_eval(arg, &err);
    if (err) {
        terminal_writestring("Erro: expressao invalida.\n");
    } else {
        char buf[32], hexbuf[32];
        int_to_str(res, buf);
        int_to_hex(res, hexbuf);
        terminal_writestring(buf);
        terminal_writestring("  (");
        terminal_writestring(hexbuf);
        terminal_writestring(")\n");
    }
}

// ----------------------------------------------------------------
// Balloon — interface gráfica
// ----------------------------------------------------------------

// Conteúdo da janela de boas-vindas do Balloon
static void draw_balloon_welcome(int x, int y, int w, int h) {
    (void)w; (void)h;
    vga12h_string(x + 6, y + 10, "FreeRootSDOS v0.7 Tsar",        COL_WHITE, COL_BLACK);
    vga12h_string(x + 6, y + 26, "Interface Balloon v0.3",    COL_WHITE, COL_BLACK);
    vga12h_string(x + 6, y + 42, "Modo VGA 12h 640x480",         COL_WHITE, COL_BLACK);
    vga12h_string(x + 6, y + 58, "Aperte no [X] para fechar.", COL_WHITE, COL_BLACK);
}

static void prog_balloon(void) {
    vga_set_mode12h();
    balloon_init();
    balloon_open("Balloon", 80, 60, 380, 120, draw_balloon_welcome);
    balloon_run();          // retorna quando "Sair" é selecionado ou última janela fecha
    // reiniciar ao invés de tentar sair direto
    reboot();
}

// ----------------------------------------------------------------
// Dispatcher de programas
// ----------------------------------------------------------------
int prog_run(const char *cmd, char drive) {
    if (pg_strcmpi(cmd, "balloon") == 0) {
        prog_balloon();
        return 1;
    }
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
    if (pg_strcmpi(cmd, "beep") == 0) {
        adlib_note_on(0, 440, 55, ADLIB_PIANO);
        return 1;
    }
    return 0;
}
