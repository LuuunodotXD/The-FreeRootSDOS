// shell.c
#include "terminal.h"
#include "keyboard.h"
#include "shell.h"
#include "io.h"
#include "idt.h"
#include "kmalloc.h"
#include "fs.h"
#include "fs_disk.h"

// ----------------------------------------------------------------
// Utilitários de string
// ----------------------------------------------------------------

static char to_lower(char c) {
    return (c>='A'&&c<='Z') ? c-'A'+'a' : c;
}
static int strcmpi(const char *a, const char *b) {
    while(*a&&*b&&to_lower(*a)==to_lower(*b)){a++;b++;}
    return to_lower(*a)-to_lower(*b);
}
static const char *startswith(const char *s, const char *p) {
    while(*p) if(to_lower(*s++)!=to_lower(*p++)) return 0;
    return s;
}
static int hexdigit(char c) {
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}
static void print_dec2(unsigned int n) {
    terminal_putchar('0'+(n/10)%10);
    terminal_putchar('0'+(n%10));
}
static void print_uint(uint32_t n) {
    char buf[12]; int i=0;
    if(!n){terminal_putchar('0');return;}
    while(n){buf[i++]='0'+n%10;n/=10;}
    while(i-->0) terminal_putchar(buf[i]);
}
static void print_kb(uint32_t b) { print_uint(b/1024); terminal_writestring(" KB"); }

// ----------------------------------------------------------------
// Histórico
// ----------------------------------------------------------------

#define HIST_MAX 10
#define HIST_LEN 256
static char hist[HIST_MAX][HIST_LEN];
static int  hist_count=0, hist_head=0;

static void hist_push(const char *cmd) {
    if(!cmd[0]) return;
    if(hist_count>0) {
        int last=(hist_head-1+HIST_MAX)%HIST_MAX;
        const char *a=hist[last],*b=cmd; int eq=1;
        while(*a||*b){if(*a++!=*b++){eq=0;break;}}
        if(eq) return;
    }
    int i=0;
    while(cmd[i]&&i<HIST_LEN-1){hist[hist_head][i]=cmd[i];i++;}
    hist[hist_head][i]='\0';
    hist_head=(hist_head+1)%HIST_MAX;
    if(hist_count<HIST_MAX) hist_count++;
}
static const char *hist_get(int idx) {
    if(idx<0||idx>=hist_count) return 0;
    return hist[(hist_head-1-idx+HIST_MAX*2)%HIST_MAX];
}

// ----------------------------------------------------------------
// Drive ativo: 'H' = heap, 'A' = disco
// ----------------------------------------------------------------

static char active_drive = 'A';   // inicia no A:

static int  on_disk(void) { return active_drive=='A'; }

// Wrappers genéricos que delegam para o drive ativo
static uint8_t     drv_cwd(void)      { return on_disk() ? fsd_cwd()      : fs_cwd(); }
static const char *drv_cwd_name(void) { return on_disk() ? fsd_cwd_name() : fs_cwd_name(); }
static int drv_cd(const char *n)      { return on_disk() ? fsd_cd(n)      : fs_cd(n); }
static int drv_mkdir(const char *n)   { return on_disk() ? fsd_mkdir(n)   : fs_mkdir(n); }
static int drv_write(const char *name,const char *ext,const char *c,uint32_t s) {
    return on_disk() ? fsd_write(name,ext,c,s) : fs_write(name,ext,c,s);
}
static int drv_write_in(uint8_t dir,const char *name,const char *ext,const char *c,uint32_t s) {
    return on_disk() ? fsd_write_in(dir,name,ext,c,s) : fs_write_in(dir,name,ext,c,s);
}
static const char *drv_read(const char *name,const char *ext) {
    return on_disk() ? fsd_read(name,ext) : fs_read(name,ext);
}
static int drv_delete(const char *name,const char *ext,int is_dir) {
    return on_disk() ? fsd_delete(name,ext,is_dir) : fs_delete(name,ext,is_dir);
}
static int drv_rename(const char *n,const char *e,const char *nn,const char *ne) {
    return on_disk() ? fsd_rename(n,e,nn,ne) : fs_rename(n,e,nn,ne);
}
static void drv_split(const char *in,char *name,char *ext) {
    if(on_disk()) fsd_split(in,name,ext); else fs_split(in,name,ext);
}

// Para dir: retorna tabela e max do drive ativo
static void *drv_table(void)  { return on_disk() ? (void*)fsd_table() : (void*)fs_table(); }
static int   drv_max(void)    { return on_disk() ? fsd_max() : fs_max(); }
static int   drv_is_disk(void){ return on_disk(); }

// ----------------------------------------------------------------
// RTC
// ----------------------------------------------------------------

static uint8_t bcd_to_bin(uint8_t v) { return (v>>4)*10+(v&0xF); }
static uint8_t rtc_read(uint8_t reg) { outb(0x70,reg); return inb(0x71); }

// ----------------------------------------------------------------
// Reboot / Poweroff
// ----------------------------------------------------------------

void shell_init(void) {}

static void reboot(void) {
    while(inb(0x64)&0x02);
    outb(0x64,0xFE);
    while(1) asm volatile("hlt");
}
static void poweroff(void) {
    outw(0x604,0x2000); outw(0xB004,0x2000);
    terminal_writestring("Poweroff nao suportado.\n");
    while(1) asm volatile("hlt");
}

// ----------------------------------------------------------------
// Readline
// ----------------------------------------------------------------

static void redraw_from(const char *buf,int from,int len,int cur) {
    for(int i=from;i<len;i++) terminal_putchar(buf[i]);
    terminal_putchar(' ');
    terminal_cursor_left(len-from+1);
    terminal_cursor_right(cur-from);
}

static void readline(char *buf,int maxlen) {
    int len=0,cur=0,hist_idx=-1;
    char saved[HIST_LEN]; saved[0]='\0';
    while(1) {
        int k=getchar();
        if(k=='\n'){buf[len]='\0';terminal_putchar('\n');return;}
        if(k=='\b'){
            if(cur==0) continue;
            for(int i=cur-1;i<len-1;i++) buf[i]=buf[i+1];
            len--;cur--;
            terminal_cursor_left(1);
            redraw_from(buf,cur,len,cur);
            continue;
        }
        if(k==KEY_DEL){
            if(cur==len) continue;
            for(int i=cur;i<len-1;i++) buf[i]=buf[i+1];
            len--;
            redraw_from(buf,cur,len,cur);
            continue;
        }
        if(k==KEY_LEFT) {if(cur>0){cur--;terminal_cursor_left(1);}continue;}
        if(k==KEY_RIGHT){if(cur<len){cur++;terminal_cursor_right(1);}continue;}
        if(k==KEY_HOME) {terminal_cursor_left(cur);cur=0;continue;}
        if(k==KEY_END)  {terminal_cursor_right(len-cur);cur=len;continue;}
        if(k==KEY_UP||k==KEY_DOWN) {
            if(k==KEY_UP&&hist_idx+1>=hist_count) continue;
            if(k==KEY_DOWN&&hist_idx==-1) continue;
            if(k==KEY_UP&&hist_idx==-1){
                int i=0; while(i<len){saved[i]=buf[i];i++;} saved[i]='\0';
            }
            hist_idx+=(k==KEY_UP)?1:-1;
            const char *entry=(hist_idx==-1)?saved:hist_get(hist_idx);
            terminal_cursor_left(cur);
            for(int i=0;i<len;i++) terminal_putchar(' ');
            terminal_cursor_left(len);
            len=0;
            while(entry[len]&&len<maxlen-1){buf[len]=entry[len];len++;}
            buf[len]='\0';cur=len;
            terminal_writestring(buf);
            continue;
        }
        if(k>=' '&&k<='~'&&len<maxlen-1) {
            for(int i=len;i>cur;i--) buf[i]=buf[i-1];
            buf[cur]=(char)k;
            len++;cur++;
            for(int i=cur-1;i<len;i++) terminal_putchar(buf[i]);
            terminal_cursor_left(len-cur);
        }
    }
}

// ----------------------------------------------------------------
// Comandos do FS (usam wrappers genéricos)
// ----------------------------------------------------------------

static void cmd_dir(const char *arg) {
    int count=0;
    uint8_t show_dir = drv_cwd();

    // dir PASTA: lista diretório específico sem entrar
    if(arg && arg[0]) {
        if(drv_is_disk()) {
            fsd_entry_t *t=fsd_table();
            int idx=-1;
            for(int i=1;i<fsd_max();i++) {
                if(t[i].type!=FSD_DIR) continue;
                const char *a=t[i].name,*b=arg; int eq=1;
                while(*a||*b){char ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb){eq=0;break;}a++;b++;}
                if(eq){idx=i;break;}
            }
            if(idx<0){terminal_writestring("Diretorio nao encontrado.\n");return;}
            terminal_writestring(t[idx].name); terminal_writestring(":\n");
            for(int i=1;i<fsd_max();i++) {
                if(t[i].type!=FSD_FILE||t[i].parent!=(uint8_t)idx) continue;
                terminal_writestring(t[i].name);
                if(t[i].ext[0]){terminal_putchar('.');terminal_writestring(t[i].ext);}
                terminal_writestring("  "); print_uint(t[i].size); terminal_writestring(" bytes\n");
                count++;
            }
        } else {
            fs_entry_t *t=fs_table();
            int idx=-1;
            for(int i=1;i<fs_max();i++) {
                if(t[i].type!=FS_DIR) continue;
                const char *a=t[i].name,*b=arg; int eq=1;
                while(*a||*b){char ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb){eq=0;break;}a++;b++;}
                if(eq){idx=i;break;}
            }
            if(idx<0){terminal_writestring("Diretorio nao encontrado.\n");return;}
            terminal_writestring(t[idx].name); terminal_writestring(":\n");
            for(int i=1;i<fs_max();i++) {
                if(t[i].type!=FS_FILE||t[i].parent!=(uint8_t)idx) continue;
                terminal_writestring(t[i].name);
                if(t[i].ext[0]){terminal_putchar('.');terminal_writestring(t[i].ext);}
                terminal_writestring("  "); print_uint(t[i].size); terminal_writestring(" bytes\n");
                count++;
            }
        }
        if(count==0) terminal_writestring("Vazio.\n");
        return;
    }

    // dir do drive atual
    if(drv_is_disk()) {
        fsd_entry_t *t=fsd_table();
        if(show_dir==FSD_ROOT) {
            for(int i=1;i<fsd_max();i++) {
                if(t[i].type!=FSD_DIR) continue;
                terminal_writestring(t[i].name); terminal_writestring("/\n"); count++;
            }
        }
        for(int i=1;i<fsd_max();i++) {
            if(t[i].type!=FSD_FILE||t[i].parent!=show_dir) continue;
            terminal_writestring(t[i].name);
            if(t[i].ext[0]){terminal_putchar('.');terminal_writestring(t[i].ext);}
            terminal_writestring("  "); print_uint(t[i].size); terminal_writestring(" bytes\n");
            count++;
        }
    } else {
        fs_entry_t *t=fs_table();
        if(show_dir==FS_ROOT) {
            for(int i=1;i<fs_max();i++) {
                if(t[i].type!=FS_DIR) continue;
                terminal_writestring(t[i].name); terminal_writestring("/\n"); count++;
            }
        }
        for(int i=1;i<fs_max();i++) {
            if(t[i].type!=FS_FILE||t[i].parent!=show_dir) continue;
            terminal_writestring(t[i].name);
            if(t[i].ext[0]){terminal_putchar('.');terminal_writestring(t[i].ext);}
            terminal_writestring("  "); print_uint(t[i].size); terminal_writestring(" bytes\n");
            count++;
        }
    }
    if(count==0) terminal_writestring("Vazio.\n");
}

static void cmd_write(const char *arg) {
    char name[9],ext[4]; int i=0;
    while(arg[i]&&arg[i]!=' ') i++;
    char fname[16]; for(int j=0;j<i&&j<15;j++) fname[j]=arg[j]; fname[i]='\0';
    if(!fname[0]){terminal_writestring("Uso: write <arquivo> [texto]\n");return;}
    drv_split(fname,name,ext);
    const char *content=arg[i]?arg+i+1:0;
    uint32_t size=0; if(content) while(content[size]) size++;
    int ret=drv_write(name,ext,content,size);
    if(ret==0) terminal_writestring("Arquivo salvo.\n");
    else if(ret==-2) terminal_writestring("Nome invalido.\n");
    else terminal_writestring("Sem espaco.\n");
}

static void cmd_append(const char *arg) {
    char name[9],ext[4]; int i=0;
    while(arg[i]&&arg[i]!=' ') i++;
    if(!arg[i]){terminal_writestring("Uso: append <arquivo> <texto>\n");return;}
    char fname[16]; for(int j=0;j<i&&j<15;j++) fname[j]=arg[j]; fname[i]='\0';
    drv_split(fname,name,ext);
    const char *add=arg+i+1;
    if(!add[0]){terminal_writestring("Uso: append <arquivo> <texto>\n");return;}
    const char *cur=drv_read(name,ext);
    uint32_t cur_len=0; if(cur) while(cur[cur_len]) cur_len++;
    uint32_t add_len=0; while(add[add_len]) add_len++;
    uint32_t new_len=cur_len+(cur_len?1:0)+add_len;
    char *buf=(char*)kmalloc(new_len+1);
    if(!buf){terminal_writestring("Sem memoria.\n");return;}
    uint32_t pos=0;
    for(uint32_t j=0;j<cur_len;j++) buf[pos++]=cur[j];
    if(cur_len) buf[pos++]='\n';
    for(uint32_t j=0;j<add_len;j++) buf[pos++]=add[j];
    buf[pos]='\0';
    int ret=drv_write(name,ext,buf,new_len);
    kfree(buf);
    if(ret==0) terminal_writestring("Linha adicionada.\n");
    else terminal_writestring("Erro.\n");
}

static void cmd_cat(const char *arg) {
    char name[9],ext[4];
    drv_split(arg,name,ext);
    const char *data=drv_read(name,ext);
    if(!data) terminal_writestring("Nao encontrado ou vazio.\n");
    else { terminal_writestring(data); terminal_putchar('\n'); }
}

static void cmd_del(const char *arg) {
    char name[9],ext[4];
    drv_split(arg,name,ext);
    // Sem extensão: tenta dir primeiro
    if(ext[0]=='\0') {
        if(drv_is_disk()) {
            fsd_entry_t *t=fsd_table();
            for(int i=1;i<fsd_max();i++) {
                if(t[i].type!=FSD_DIR) continue;
                const char *a=t[i].name,*b=name; int eq=1;
                while(*a||*b){char ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb){eq=0;break;}a++;b++;}
                if(eq){fsd_delete(name,"",1);terminal_writestring("Diretorio removido.\n");return;}
            }
        } else {
            fs_entry_t *t=fs_table();
            for(int i=1;i<fs_max();i++) {
                if(t[i].type!=FS_DIR) continue;
                const char *a=t[i].name,*b=name; int eq=1;
                while(*a||*b){char ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb){eq=0;break;}a++;b++;}
                if(eq){fs_delete(name,"",1);terminal_writestring("Diretorio removido.\n");return;}
            }
        }
    }
    if(drv_delete(name,ext,0)==0) terminal_writestring("Deletado.\n");
    else terminal_writestring("Nao encontrado.\n");
}

static void cmd_rename(const char *arg) {
    char on[9],oe[4],nn[9],ne[4];
    int i=0; while(arg[i]&&arg[i]!=' ') i++;
    if(!arg[i]){terminal_writestring("Uso: rename <antigo> <novo>\n");return;}
    char a[16],b[16]; int j=0;
    while(j<i&&j<15){a[j]=arg[j];j++;} a[j]='\0';
    const char *rest=arg+i+1; j=0;
    while(rest[j]&&j<15){b[j]=rest[j];j++;} b[j]='\0';
    drv_split(a,on,oe); drv_split(b,nn,ne);
    int ret=drv_rename(on,oe,nn,ne);
    if(ret==0) terminal_writestring("Renomeado.\n");
    else if(ret==-1) terminal_writestring("Nao encontrado.\n");
    else terminal_writestring("Destino ja existe.\n");
}

static void cmd_copy(const char *arg) {
    // Sintaxe: copy arquivo.txt [PASTA | A: | H: | H:PASTA]
    char fname[16]; int i=0;
    while(arg[i]&&arg[i]!=' ') i++;
    for(int j=0;j<i&&j<15;j++) fname[j]=arg[j]; fname[i]='\0';
    if(!fname[0]){terminal_writestring("Uso: copy <arquivo> [destino]\n");return;}

    char name[9],ext[4];
    drv_split(fname,name,ext);
    const char *data=drv_read(name,ext);
    // Verifica existência
    int src_size=0;
    if(data) while(data[src_size]) src_size++;

    const char *dest_arg=arg[i]?arg+i+1:0;

    // Determina drive e diretório de destino
    char dest_drive=active_drive;
    const char *dest_dir_name=0;
    uint8_t dest_dir=0;   // raiz

    if(dest_arg&&dest_arg[0]) {
        // Verifica prefixo de drive: A: ou H:
        if((dest_arg[0]=='A'||dest_arg[0]=='a')&&dest_arg[1]==':') {
            dest_drive='A'; dest_dir_name=dest_arg[2]?dest_arg+2:0;
        } else if((dest_arg[0]=='H'||dest_arg[0]=='h')&&dest_arg[1]==':') {
            dest_drive='H'; dest_dir_name=dest_arg[2]?dest_arg+2:0;
        } else {
            dest_dir_name=dest_arg;
        }
    }

    // Resolve índice do diretório de destino
    if(dest_dir_name&&dest_dir_name[0]) {
        if(dest_drive=='A') {
            fsd_entry_t *t=fsd_table();
            for(int j=1;j<fsd_max();j++) {
                if(t[j].type!=FSD_DIR) continue;
                const char *a=t[j].name,*b=dest_dir_name; int eq=1;
                while(*a||*b){char ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb){eq=0;break;}a++;b++;}
                if(eq){dest_dir=(uint8_t)j;break;}
            }
        } else {
            fs_entry_t *t=fs_table();
            for(int j=1;j<fs_max();j++) {
                if(t[j].type!=FS_DIR) continue;
                const char *a=t[j].name,*b=dest_dir_name; int eq=1;
                while(*a||*b){char ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb){eq=0;break;}a++;b++;}
                if(eq){dest_dir=(uint8_t)j;break;}
            }
        }
    }

    int ret;
    if(dest_drive=='A')
        ret=fsd_write_in(dest_dir,name,ext,data,(uint32_t)src_size);
    else
        ret=fs_write_in(dest_dir,name,ext,data,(uint32_t)src_size);

    if(ret==0) terminal_writestring("Copiado.\n");
    else terminal_writestring("Erro ao copiar.\n");
}

// ----------------------------------------------------------------
// Shell principal
// ----------------------------------------------------------------

static int show_time  = 0;
static int show_drive = 1;

void shell_run(void) {
    char buffer[256];

    while(1) {
        // Prompt: [HH:MM:SS] A:PASTA>
        terminal_putchar('\n');
        if(show_time) {
            uint8_t h =bcd_to_bin(rtc_read(0x04));
            uint8_t m =bcd_to_bin(rtc_read(0x02));
            uint8_t sc=bcd_to_bin(rtc_read(0x00));
            terminal_putchar('[');
            print_dec2(h); terminal_putchar(':');
            print_dec2(m); terminal_putchar(':');
            print_dec2(sc);
            terminal_writestring("] ");
        }
        if(show_drive) {
            terminal_putchar(active_drive);
            terminal_putchar(':');
        }
        const char *dname=drv_cwd_name();
        if(dname&&dname[0]) terminal_writestring(dname);
        terminal_writestring("> ");

        readline(buffer,sizeof(buffer));
        hist_push(buffer);

        // Troca de drive
        if(strcmpi(buffer,"a:")==0) {
            active_drive='A'; terminal_writestring("Drive A: (disco)\n");
            continue;
        }
        if(strcmpi(buffer,"h:")==0) {
            active_drive='H'; terminal_writestring("Drive H: (heap/RAM)\n");
            continue;
        }

        // ---- Sistema ----
        if(strcmpi(buffer,"help")==0||startswith(buffer,"help ")) {
            const char *arg=buffer[4]?buffer+5:"1";
            while(*arg==' ') arg++;
            if(*arg=='2') {
                terminal_writestring("Comandos (2/2):\n");
                terminal_writestring("  dir [pasta]     - lista conteudo\n");
                terminal_writestring("  md <nome>       - cria diretorio\n");
                terminal_writestring("  cd <nome>/..    - navega dirs\n");
                terminal_writestring("  write <arq> [txt]\n");
                terminal_writestring("  cat <arq>       - mostra conteudo\n");
                terminal_writestring("  append <arq> <txt>\n");
                terminal_writestring("  copy <arq> [destino]\n");
                terminal_writestring("  del <arq/dir>   - remove\n");
                terminal_writestring("  rename <old> <new>\n");
                terminal_writestring("  format [a:/h:]  - formata drive\n");
                terminal_writestring("  a: / h:         - troca de drive\n");
            } else {
                terminal_writestring("Comandos (1/2):\n");
                terminal_writestring("  help [2]        - ajuda\n");
                terminal_writestring("  clear           - limpa tela\n");
                terminal_writestring("  reboot/poweroff\n");
                terminal_writestring("  info            - sobre o sistema\n");
                terminal_writestring("  date / uptime\n");
                terminal_writestring("  sleep <ms>\n");
                terminal_writestring("  time            - hora no prompt\n");
                terminal_writestring("  drive           - unidade no prompt\n");
                terminal_writestring("  log <txt>\n");
                terminal_writestring("  log:<X> <txt>   - cor X (0-F)\n");
                terminal_writestring("  color/bgcolor <0-F>\n");
                terminal_writestring("  meminfo\n");
            }
        }
        else if(strcmpi(buffer,"clear")==0) terminal_clear();
        else if(strcmpi(buffer,"reboot")==0) { terminal_writestring("Reiniciando...\n"); reboot(); }
        else if(strcmpi(buffer,"poweroff")==0) { terminal_writestring("Desligando...\n"); poweroff(); }
        else if(strcmpi(buffer,"info")==0) {
            terminal_writestring("FreeRootSDOS v0.4\n");
            terminal_writestring("Drive A: disco persistente | Drive H: heap RAM\n");
        }
        else if(strcmpi(buffer,"date")==0) {
            uint8_t sec=bcd_to_bin(rtc_read(0x00));
            uint8_t min=bcd_to_bin(rtc_read(0x02));
            uint8_t hr =bcd_to_bin(rtc_read(0x04));
            uint8_t day=bcd_to_bin(rtc_read(0x07));
            uint8_t mon=bcd_to_bin(rtc_read(0x08));
            uint8_t yr =bcd_to_bin(rtc_read(0x09));
            print_dec2(day);terminal_putchar('/');
            print_dec2(mon);terminal_putchar('/');
            terminal_writestring("20");
            print_dec2(yr); terminal_writestring("  ");
            print_dec2(hr); terminal_putchar(':');
            print_dec2(min);terminal_putchar(':');
            print_dec2(sec);terminal_putchar('\n');
        }
        else if(strcmpi(buffer,"uptime")==0) {
            uint32_t s=timer_get_ticks()/1000;
            terminal_writestring("Uptime: ");
            print_dec2(s/3600);terminal_putchar(':');
            print_dec2((s%3600)/60);terminal_putchar(':');
            print_dec2(s%60);terminal_putchar('\n');
        }
        else if(startswith(buffer,"sleep ")) {
            const char *a=startswith(buffer,"sleep ");
            uint32_t ms=0; while(*a>='0'&&*a<='9') ms=ms*10+(*a++-'0');
            if(!ms||*a) terminal_writestring("Uso: sleep <ms>\n");
            else { timer_sleep(ms); terminal_writestring("Pronto.\n"); }
        }
        else if(strcmpi(buffer,"time")==0) {
            show_time^=1;
            terminal_writestring(show_time?"Hora ativada.\n":"Hora desativada.\n");
        }
        else if(strcmpi(buffer,"drive")==0) {
            show_drive^=1;
            terminal_writestring(show_drive?"Unidade ativada.\n":"Unidade desativada.\n");
        }
        else if(startswith(buffer,"log:")) {
            const char *a=startswith(buffer,"log:");
            int v=hexdigit(a[0]);
            if(v<0||a[1]!=' ') terminal_writestring("Uso: log:<0-F> <texto>\n");
            else {
                uint8_t old=terminal_get_fg();
                terminal_set_fg((uint8_t)v);
                terminal_writestring(a+2);terminal_putchar('\n');
                terminal_set_fg(old);
            }
        }
        else if(startswith(buffer,"log ")) {
            terminal_writestring(startswith(buffer,"log "));
            terminal_putchar('\n');
        }
        else if(startswith(buffer,"color ")) {
            const char *a=startswith(buffer,"color ");
            int v=hexdigit(a[0]);
            if(v<0||a[1]) terminal_writestring("Uso: color <0-F>\n");
            else { terminal_set_fg((uint8_t)v); terminal_writestring("Cor alterada.\n"); }
        }
        else if(startswith(buffer,"bgcolor ")) {
            const char *a=startswith(buffer,"bgcolor ");
            int v=hexdigit(a[0]);
            if(v<0||a[1]) terminal_writestring("Uso: bgcolor <0-F>\n");
            else { terminal_set_bg((uint8_t)v);terminal_clear();terminal_writestring("Cor alterada.\n"); }
        }
        else if(strcmpi(buffer,"meminfo")==0) {
            uint32_t total=64u*1024u,free=kmalloc_free();
            terminal_writestring("Heap total: ");print_kb(total);terminal_putchar('\n');
            terminal_writestring("Heap usado: ");print_kb(total-free);terminal_putchar('\n');
            terminal_writestring("Heap livre: ");print_kb(free);terminal_putchar('\n');
            // Disco A:
	    uint32_t disk_total = FSD_DATA_SECTORS * 512u;
	    uint32_t disk_used  = 0;
	    fsd_entry_t *t = fsd_table();
            for (int i = 1; i < fsd_max(); i++) {
	        if (t[i].type == FSD_FILE)
	            disk_used += t[i].num_secs * 512u;
	    }
	    terminal_writestring("Disco total: "); print_kb(disk_total); terminal_putchar('\n');
	    terminal_writestring("Disco usado: "); print_kb(disk_used);  terminal_putchar('\n');
	    terminal_writestring("Disco livre: "); print_kb(disk_total - disk_used); terminal_putchar('\n');
        }

        // ---- FS ----
        else if(strcmpi(buffer,"dir")==0) cmd_dir(0);
        else if(startswith(buffer,"dir ")) cmd_dir(startswith(buffer,"dir "));
        else if(startswith(buffer,"md ")) {
            const char *a=startswith(buffer,"md ");
            if(drv_cwd()!=0) terminal_writestring("md so pode ser usado na raiz.\n");
            else {
                int r=drv_mkdir(a);
                if(r>=0) terminal_writestring("Diretorio criado.\n");
                else if(r==-3) terminal_writestring("Ja existe.\n");
                else if(r==-2) terminal_writestring("Nome invalido.\n");
                else terminal_writestring("Sem espaco.\n");
            }
        }
        else if(startswith(buffer,"cd ")) drv_cd(startswith(buffer,"cd "))==0?0:(terminal_writestring("Nao encontrado.\n"),0);
        else if(strcmpi(buffer,"cd..")==0||strcmpi(buffer,"cd ..")==0) drv_cd("..");
        else if(startswith(buffer,"write "))  cmd_write(startswith(buffer,"write "));
        else if(startswith(buffer,"cat "))    cmd_cat(startswith(buffer,"cat "));
        else if(startswith(buffer,"append ")) cmd_append(startswith(buffer,"append "));
        else if(startswith(buffer,"copy "))   cmd_copy(startswith(buffer,"copy "));
        else if(startswith(buffer,"del "))    cmd_del(startswith(buffer,"del "));
        else if(startswith(buffer,"rename ")) cmd_rename(startswith(buffer,"rename "));
        else if(strcmpi(buffer,"format")==0||startswith(buffer,"format ")) {
            const char *a=startswith(buffer,"format ");
            char target=active_drive;
            if(a&&(a[0]=='A'||a[0]=='a')) target='A';
            else if(a&&(a[0]=='H'||a[0]=='h')) target='H';
            if(target=='A') { fsd_format(); terminal_writestring("A: formatado.\n"); }
            else            { fs_format();  terminal_writestring("H: formatado.\n"); }
        }
        else if(buffer[0]) {
            terminal_writestring("Comando desconhecido: ");
            terminal_writestring(buffer);terminal_putchar('\n');
        }
    }
}
