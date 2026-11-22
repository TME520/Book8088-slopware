/* Host (Linux/gcc) platform for unit tests and headless demos. */
#include "lps.h"
#include "meals.h"
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

typedef struct {
    Platform iface;
    char rows[ROWS][COLUMNS+1];
    unsigned char rev[ROWS][COLUMNS];
    int palette;
    uint32_t generation;
    int active;
    int mounted;
    int write_fault;
    int capture; /* if set, dump screens to captures/NN.txt */
    int capture_n;
    unsigned nclear, ntext, row_text[ROWS]; /* LPS116-CHG07b */
} HostPlat;

static HostPlat *H(Platform *p){ return (HostPlat*)p->user; }

static void h_clear(Platform *p){
    HostPlat *h=H(p); int r;
    h->nclear++;
    for(r=0;r<ROWS;r++){ memset(h->rows[r],' ',COLUMNS); h->rows[r][COLUMNS]=0; memset(h->rev[r],0,COLUMNS); }
}
static void h_text(Platform *p, int row, int col, const char *s, int reverse){
    HostPlat *h=H(p); int c=col;
    if(row<0||row>=ROWS) return;
    if(c<0) c=0;
    h->ntext++; if(row>=0&&row<ROWS) h->row_text[row]++;
    while(*s && c<COLUMNS){ h->rows[row][c]=*s++; h->rev[row][c]=(unsigned char)(reverse?1:0); c++; }
}
static void h_alert(Platform *p, int row, int col, const char *s){
    char bar[COLUMNS+1]; int n,i;
    for(i=0;i<COLUMNS;i++) bar[i]=' ';
    bar[COLUMNS]=0;
    if(col<0) col=0;
    if(col<COLUMNS){ n=(int)strlen(s); if(n>COLUMNS-col) n=COLUMNS-col; memcpy(bar+col,s,(size_t)n); }
    h_text(p,row,0,bar,1);
}
static void h_large(Platform *p, const char *value, int red){
    char line[COLUMNS+1]; char mid[32]; int n,col,i;
    sprintf(mid,">>>  %s  <<<", value);
    for(i=0;i<COLUMNS;i++) line[i]=' ';
    line[COLUMNS]=0;
    n=(int)strlen(mid); if(n>COLUMNS) n=COLUMNS;
    col=(COLUMNS-n)/2; memcpy(line+col,mid,(size_t)n);
    h_text(p,12,0,line,red);
}
/* Rows hold CP437 like the DOS screen; text files on the host are UTF-8
 * (LPS-DOS-115-CHG03). */
static void put_row_utf8(FILE *f, const char *row){
    char b[3]; size_t n;
    for(;*row;row++){ n=cp437_to_utf8((unsigned char)*row,b); fwrite(b,1,n,f); }
}
static void h_present(Platform *p){
    HostPlat *h=H(p); int r,c,any;
    if(h->capture){
        char path[128]; FILE *f; sprintf(path,"captures/%02d.txt", h->capture_n++);
        f=fopen(path,"w");
        if(f){
            for(r=0;r<ROWS;r++){
                any=0; for(c=0;c<COLUMNS;c++) if(h->rev[r][c]) any=1;
                fputs(any?"*":" ",f); put_row_utf8(f,h->rows[r]); fputc('\n',f);
            }
            fclose(f);
        }
    }
}
static void h_palette(Platform *p, int pal){ H(p)->palette=pal; }
static int h_batt(Platform *p){ (void)p; return 0; }
static enum LoadResult h_load(Platform *p, State *out){
    HostPlat *h=H(p); enum LoadResult r;
    if(!h->mounted) return LOAD_NO_STORAGE;
    r=save_load(lps_data_dir(),out,&h->generation,&h->active);
    if(r==LOAD_MISSING){ h->generation=0; h->active=-1; }
    return r;
}
static int h_save(Platform *p, const State *s){
    HostPlat *h=H(p);
    if(!h->mounted||h->write_fault) return 0;
    lps_ensure_dirs();
    if(!save_store(lps_data_dir(),s,&h->generation,&h->active)){ h->write_fault=1; return 0; }
    return 1;
}
static int h_ics(Platform *p, const Day *day, const State *s, uint32_t totalXp, uint32_t dayXp, int language){
    char d[160]; (void)p; lps_ensure_dirs(); lps_path_join(d,sizeof(d),lps_data_dir(),"EXPORT");
    return write_ics_file(d,day,s,totalXp,dayXp,language);
}
static int h_csv(Platform *p, const Day *day){
    char d[160]; (void)p; lps_ensure_dirs(); lps_path_join(d,sizeof(d),lps_data_dir(),"WEIGHTS");
    return write_weight_csv(d,day);
}

static int h_meal_csv(Platform *p, const State *s){
    char d[160]; (void)p; lps_ensure_dirs(); lps_path_join(d,sizeof(d),lps_data_dir(),"EXPORT");
    return meal_csv_export(d,s);
}


static void h_scroll_text(Platform *p, int top, int left, int bottom, int right, int lines){
    HostPlat *h=H(p);
    int r, n, len;
    if(top<0) top=0;
    if(bottom>=ROWS) bottom=ROWS-1;
    if(left<0) left=0;
    if(right>=COLUMNS) right=COLUMNS-1;
    if(top>bottom||left>right||lines==0) return;
    len=right-left+1;
    if(lines>0){
        if(lines>bottom-top+1) lines=bottom-top+1;
        for(r=top;r<=bottom-lines;r++){
            memmove(h->rows[r]+left,h->rows[r+lines]+left,(size_t)len);
            memmove(h->rev[r]+left,h->rev[r+lines]+left,(size_t)len);
        }
        for(r=bottom-lines+1;r<=bottom;r++){
            memset(h->rows[r]+left,' ',(size_t)len);
            memset(h->rev[r]+left,0,(size_t)len);
        }
    }else{
        n=-lines;
        if(n>bottom-top+1) n=bottom-top+1;
        for(r=bottom;r>=top+n;r--){
            memmove(h->rows[r]+left,h->rows[r-n]+left,(size_t)len);
            memmove(h->rev[r]+left,h->rev[r-n]+left,(size_t)len);
        }
        for(r=top;r<top+n;r++){
            memset(h->rows[r]+left,' ',(size_t)len);
            memset(h->rev[r]+left,0,(size_t)len);
        }
    }
}

Platform *plat_host_create(int capture){
    static HostPlat h;
    memset(&h,0,sizeof(h));
    h.iface.user=&h;
    h.iface.clear=h_clear; h.iface.text=h_text; h.iface.alert=h_alert; h.iface.large_text=h_large;
    h.iface.present=h_present; h.iface.set_palette=h_palette; h.iface.battery_low=h_batt;
    h.iface.load=h_load; h.iface.save=h_save; h.iface.export_ics=h_ics; h.iface.export_weight_csv=h_csv;
    h.iface.export_meal_csv=h_meal_csv;
    h.iface.scroll_text=h_scroll_text;
    h.palette=PAL_GREEN; h.mounted=1; h.capture=capture;
    return &h.iface;
}

void plat_host_dump(Platform *p, FILE *out){
    HostPlat *h=H(p); int r;
    for(r=0;r<ROWS;r++){ put_row_utf8(out,h->rows[r]); fputc('\n',out); }
}
const char *plat_host_row(Platform *p, int row){ return H(p)->rows[row]; }
int plat_host_rev(Platform *p, int row, int col){ return H(p)->rev[row][col]; }

/* LPS-DOS-115-CHG06b: the BIOS pair (character, scan code) a key typed on a
 * US (azerty 0) or French AZERTY (azerty 1, KEYB FR) keyboard gives, run
 * through app_bios_key as plat_dos does. Top-row keys with or without Shift
 * get scan codes 02h-0Bh; anything else gets a scan code off the top row. */
int plat_host_key(int azerty, unsigned char ch){
    static const char us_plain[]="1234567890", us_shift[]="!@#$%^&*()";
    static const char fr_plain[]="&é\"'(-è_çà", fr_shift[]="1234567890";
    const char *plain=azerty?fr_plain:us_plain, *shift=azerty?fr_shift:us_shift;
    unsigned i, scan=0x39; /* the space bar: not on the top row */
    for(i=0;i<10;i++) if((unsigned char)plain[i]==ch || (unsigned char)shift[i]==ch) scan=0x02+i;
    if(ch==13) scan=0x1C;
    return app_bios_key(ch,scan);
}

void plat_host_reset_counts(Platform *p){
    HostPlat *h=H(p); int r;
    h->nclear=0; h->ntext=0;
    for(r=0;r<ROWS;r++) h->row_text[r]=0;
}
unsigned plat_host_clear_count(Platform *p){ return H(p)->nclear; }
unsigned plat_host_text_count(Platform *p){ return H(p)->ntext; }
unsigned plat_host_row_text_count(Platform *p, int row){
    if(row<0||row>=ROWS) return 0;
    return H(p)->row_text[row];
}
