// fs_disk.c -- sistema de arquivos persistente no disco
#include <stdint.h>
#include "fs_disk.h"
#include "disk.h"
#include "kmalloc.h"

// ----------------------------------------------------------------
// Tabela em memória (espelho do disco)
// ----------------------------------------------------------------

static fsd_entry_t table[FSD_MAX];
static uint8_t     cwd = FSD_ROOT;

// Bitmap de setores de dados usados (112 bits = 14 bytes)
static uint8_t sec_bitmap[FSD_DATA_SECTORS / 8 + 1];

// Buffer temporário para leitura de conteúdo (reutilizado por fsd_read)
static char read_buf[FSD_DATA_SECTORS * 512 + 1];

// ----------------------------------------------------------------
// Utilitários internos
// ----------------------------------------------------------------

static int fsd_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }

static int fsd_strcmpi(const char *a, const char *b) {
    while (*a && *b) {
        char ca=*a, cb=*b;
        if(ca>='A'&&ca<='Z') ca+=32;
        if(cb>='A'&&cb<='Z') cb+=32;
        if(ca!=cb) return ca-cb;
        a++;b++;
    }
    return *a-*b;
}

static void fsd_lower(char *dst, const char *src, int max) {
    int i=0;
    while(i<max-1 && src[i]) {
        char c=src[i];
        if(c>='A'&&c<='Z') c+=32;
        dst[i++]=c;
    }
    dst[i]='\0';
}

static void fsd_upper(char *dst, const char *src, int max) {
    int i=0;
    while(i<max-1 && src[i]) {
        char c=src[i];
        if(c>='a'&&c<='z') c-=32;
        dst[i++]=c;
    }
    dst[i]='\0';
}

static int fsd_valid(const char *s, int maxlen) {
    if(!s||!s[0]||fsd_strlen(s)>maxlen) return 0;
    for(int i=0;s[i];i++) {
        char c=s[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'||c=='-')) return 0;
    }
    return 1;
}

static int fsd_find(const char *name, const char *ext, uint8_t parent) {
    for(int i=0;i<FSD_MAX;i++) {
        if(table[i].type==FSD_FREE) continue;
        if(table[i].parent!=parent) continue;
        if(fsd_strcmpi(table[i].name,name)!=0) continue;
        if(table[i].type==FSD_FILE &&
           fsd_strcmpi(table[i].ext, ext?ext:"")!=0) continue;
        return i;
    }
    return -1;
}

static int fsd_slot(void) {
    for(int i=1;i<FSD_MAX;i++)
        if(table[i].type==FSD_FREE) return i;
    return -1;
}

// ----------------------------------------------------------------
// Bitmap de setores
// ----------------------------------------------------------------

static int  sec_used(int s)      { return (sec_bitmap[s/8]>>(s%8))&1; }
static void sec_set(int s, int v) {
    if(v) sec_bitmap[s/8]|=(1<<(s%8));
    else  sec_bitmap[s/8]&=~(1<<(s%8));
}

// Encontra N setores contíguos livres; retorna índice inicial ou -1
static int sec_alloc(int n) {
    int start=0, count=0;
    for(int i=0;i<FSD_DATA_SECTORS;i++) {
        if(!sec_used(i)) {
            if(count==0) start=i;
            if(++count==n) goto found;
        } else count=0;
    }
    return -1;
found:
    for(int i=start;i<start+n;i++) sec_set(i,1);
    return start;
}

static void sec_free(int start, int n) {
    for(int i=start;i<start+n;i++) sec_set(i,0);
}

// ----------------------------------------------------------------
// Persistência: salva/carrega tabela no disco
// ----------------------------------------------------------------

// Superbloco: magic(4) + bitmap(14) + pad(494)
static void fsd_save_super(void) {
    uint8_t buf[512];
    for(int i=0;i<512;i++) buf[i]=0;
    buf[0]=(FSD_MAGIC>>0)&0xFF;
    buf[1]=(FSD_MAGIC>>8)&0xFF;
    buf[2]=(FSD_MAGIC>>16)&0xFF;
    buf[3]=(FSD_MAGIC>>24)&0xFF;
    for(int i=0;i<(int)sizeof(sec_bitmap);i++) buf[4+i]=sec_bitmap[i];
    disk_write(FSD_SECTOR_SB, buf);
}

static int fsd_load_super(void) {
    uint8_t buf[512];
    if(disk_read(FSD_SECTOR_SB, buf)!=0) return -1;
    uint32_t magic = buf[0]|(buf[1]<<8)|(buf[2]<<16)|((uint32_t)buf[3]<<24);
    if(magic!=FSD_MAGIC) return -1;
    for(int i=0;i<(int)sizeof(sec_bitmap);i++) sec_bitmap[i]=buf[4+i];
    return 0;
}

// Tabela: 64 entradas × 32 bytes = 2048 bytes = 4 setores
static void fsd_save_table(void) {
    uint8_t buf[512];
    // 16 entradas por setor (16 × 32 = 512)
    for(int s=0;s<4;s++) {
        for(int i=0;i<512;i++) buf[i]=0;
        for(int e=0;e<16;e++) {
            fsd_entry_t *en=&table[s*16+e];
            uint8_t *p=buf+e*32;
            p[0]=en->type;
            p[1]=en->parent;
            for(int i=0;i<9;i++)  p[2+i]=en->name[i];
            for(int i=0;i<4;i++)  p[11+i]=en->ext[i];
            p[15]=(en->size)&0xFF;
            p[16]=(en->size>>8)&0xFF;
            p[17]=(en->size>>16)&0xFF;
            p[18]=(en->size>>24)&0xFF;
            p[19]=en->start_sec;
            p[20]=en->num_secs;
        }
        disk_write(FSD_SECTOR_TABLE+s, buf);
    }
}

static void fsd_load_table(void) {
    uint8_t buf[512];
    for(int s=0;s<4;s++) {
        if(disk_read(FSD_SECTOR_TABLE+s, buf)!=0) continue;
        for(int e=0;e<16;e++) {
            fsd_entry_t *en=&table[s*16+e];
            uint8_t *p=buf+e*32;
            en->type=p[0];
            en->parent=p[1];
            for(int i=0;i<9;i++)  en->name[i]=p[2+i];
            for(int i=0;i<4;i++)  en->ext[i]=p[11+i];
            en->size=(uint32_t)p[15]|(p[16]<<8)|(p[17]<<16)|((uint32_t)p[18]<<24);
            en->start_sec=p[19];
            en->num_secs=p[20];
        }
    }
}

// ----------------------------------------------------------------
// API pública
// ----------------------------------------------------------------

void fsd_init(void) {
    for(int i=0;i<FSD_MAX;i++) {
        table[i].type=FSD_FREE;
        table[i].name[0]='\0';
        table[i].ext[0]='\0';
    }
    for(int i=0;i<(int)sizeof(sec_bitmap);i++) sec_bitmap[i]=0;

    if(fsd_load_super()==0) {
        fsd_load_table();
    } else {
        // Disco novo: cria raiz e grava
        table[FSD_ROOT].type=FSD_DIR;
        table[FSD_ROOT].parent=FSD_ROOT;
        table[FSD_ROOT].name[0]='\0';
        fsd_save_super();
        fsd_save_table();
    }
    cwd=FSD_ROOT;
}

uint8_t     fsd_cwd(void)      { return cwd; }
const char *fsd_cwd_name(void) { return table[cwd].name; }
fsd_entry_t *fsd_table(void)   { return table; }
int          fsd_max(void)     { return FSD_MAX; }

void fsd_split(const char *input, char *name, char *ext) {
    const char *dot=0;
    for(const char *p=input;*p;p++) if(*p=='.') dot=p;
    if(dot && dot!=input) {
        int nlen=dot-input; if(nlen>FSD_NAME_LEN) nlen=FSD_NAME_LEN;
        char tmp[FSD_NAME_LEN+1];
        for(int i=0;i<nlen;i++) tmp[i]=input[i]; tmp[nlen]='\0';
        fsd_lower(name,tmp,FSD_NAME_LEN+1);
        fsd_lower(ext,dot+1,FSD_EXT_LEN+1);
    } else {
        fsd_lower(name,input,FSD_NAME_LEN+1);
        ext[0]='\0';
    }
}

int fsd_cd(const char *name) {
    if(fsd_strcmpi(name,"..")==0) { cwd=table[cwd].parent; return 0; }
    int idx=fsd_find(name,"",FSD_ROOT);
    if(idx<0||table[idx].type!=FSD_DIR) return -1;
    cwd=(uint8_t)idx;
    return 0;
}

int fsd_mkdir(const char *name) {
    if(!fsd_valid(name,FSD_NAME_LEN)) return -2;
    if(fsd_find(name,"",FSD_ROOT)>=0) return -3;
    int idx=fsd_slot(); if(idx<0) return -1;
    table[idx].type=FSD_DIR;
    table[idx].parent=FSD_ROOT;
    table[idx].size=0;
    table[idx].start_sec=0;
    table[idx].num_secs=0;
    table[idx].ext[0]='\0';
    fsd_upper(table[idx].name,name,FSD_NAME_LEN+1);
    fsd_save_table();
    return idx;
}

int fsd_write_in(uint8_t dir, const char *name, const char *ext,
                 const char *content, uint32_t size) {
    if(!fsd_valid(name,FSD_NAME_LEN)) return -2;
    if(ext&&ext[0]&&!fsd_valid(ext,FSD_EXT_LEN)) return -2;

    int idx=fsd_find(name,ext,dir);
    if(idx>=0) {
        // Libera setores antigos
        if(table[idx].num_secs>0)
            sec_free(table[idx].start_sec, table[idx].num_secs);
        table[idx].size=0;
        table[idx].num_secs=0;
    } else {
        idx=fsd_slot(); if(idx<0) return -1;
        table[idx].type=FSD_FILE;
        table[idx].parent=dir;
        table[idx].num_secs=0;
        fsd_lower(table[idx].name,name,FSD_NAME_LEN+1);
        fsd_lower(table[idx].ext,ext?ext:"",FSD_EXT_LEN+1);
    }

    table[idx].size=0;
    table[idx].start_sec=0;

    if(content && size>0) {
        int nsecs=(size+511)/512;
        int start=sec_alloc(nsecs);
        if(start<0) { table[idx].type=FSD_FREE; return -1; }

        // Escreve setores
        uint8_t buf[512];
        uint32_t written=0;
        for(int s=0;s<nsecs;s++) {
            for(int i=0;i<512;i++) buf[i]=0;
            for(int i=0;i<512&&written<size;i++) buf[i]=content[written++];
            disk_write(FSD_SECTOR_DATA+start+s,buf);
        }
        table[idx].size=size;
        table[idx].start_sec=(uint8_t)start;
        table[idx].num_secs=(uint8_t)nsecs;
    }

    fsd_save_super();
    fsd_save_table();
    return 0;
}

int fsd_write(const char *name, const char *ext,
              const char *content, uint32_t size) {
    return fsd_write_in(cwd,name,ext,content,size);
}

const char *fsd_read(const char *name, const char *ext) {
    int idx=fsd_find(name,ext?ext:"",cwd);
    if(idx<0||table[idx].type!=FSD_FILE||table[idx].size==0) return 0;

    uint32_t size=table[idx].size;
    uint8_t buf[512];
    uint32_t pos=0;
    for(int s=0;s<table[idx].num_secs;s++) {
        disk_read(FSD_SECTOR_DATA+table[idx].start_sec+s,buf);
        for(int i=0;i<512&&pos<size;i++) read_buf[pos++]=buf[i];
    }
    read_buf[pos]='\0';
    return read_buf;
}

int fsd_delete(const char *name, const char *ext, int is_dir) {
    if(is_dir) {
        int idx=fsd_find(name,"",FSD_ROOT);
        if(idx<0||table[idx].type!=FSD_DIR||idx==FSD_ROOT) return -1;
        for(int i=1;i<FSD_MAX;i++) {
            if(table[i].type!=FSD_FREE&&table[i].parent==(uint8_t)idx) {
                if(table[i].num_secs>0) sec_free(table[i].start_sec,table[i].num_secs);
                table[i].type=FSD_FREE;
            }
        }
        if(cwd==(uint8_t)idx) cwd=FSD_ROOT;
        table[idx].type=FSD_FREE;
    } else {
        int idx=fsd_find(name,ext?ext:"",cwd);
        if(idx<0||table[idx].type!=FSD_FILE) return -1;
        if(table[idx].num_secs>0) sec_free(table[idx].start_sec,table[idx].num_secs);
        table[idx].type=FSD_FREE;
    }
    fsd_save_super();
    fsd_save_table();
    return 0;
}

int fsd_rename(const char *name,    const char *ext,
               const char *newname, const char *newext) {
    if(!fsd_valid(newname,FSD_NAME_LEN)) return -1;
    int src=fsd_find(name,ext?ext:"",cwd);
    if(src<0||table[src].type!=FSD_FILE) return -1;
    if(fsd_find(newname,newext?newext:"",cwd)>=0) return -2;
    fsd_lower(table[src].name,newname,FSD_NAME_LEN+1);
    fsd_lower(table[src].ext, newext?newext:"",FSD_EXT_LEN+1);
    fsd_save_table();
    return 0;
}

void fsd_format(void) {
    for(int i=1;i<FSD_MAX;i++) {
        if(table[i].type!=FSD_FREE&&table[i].num_secs>0)
            sec_free(table[i].start_sec,table[i].num_secs);
        table[i].type=FSD_FREE;
    }
    for(int i=0;i<(int)sizeof(sec_bitmap);i++) sec_bitmap[i]=0;
    cwd=FSD_ROOT;
    fsd_save_super();
    fsd_save_table();
}
