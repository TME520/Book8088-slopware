/* DOS platform: BIOS-safe text for 8088 (Book 8088 / CGA / MDA). C89. */
#include "lps.h"
#include "meals.h"
#include "ega.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos.h>
#include <conio.h>

typedef struct {
    Platform iface;
    char cells[ROWS][COLUMNS+1];
    unsigned char rev[ROWS][COLUMNS];
    char sh_ch[ROWS][COLUMNS];          /* last frame copied to VRAM */
    unsigned char sh_rev[ROWS][COLUMNS];
    int sh_palette;
    int painted; /* 0 until the first frame is copied to VRAM */
    int palette;
    uint32_t generation;
    int active; /* 0=A, 1=B, -1=none yet */
    int write_fault;
    int mounted;
    unsigned video_seg; /* 0xB800 colour or 0xB000 mono */
    int mono;
    int use_bios; /* 1 = int 10h teletype only (safest) */
    int ega;            /* EGA/VGA BIOS present (INT 10h AH=12h answers) */
    int blink_was_on;   /* attribute bit 7 meant blink when we started */
    unsigned cursor_shape; /* BIOS cursor start/end lines, restored on exit */
    /* LPS-DOS-115-CHG07: Meal Planner in EGA 640 x 350 x 16. */
    int ega_colour;     /* an EGA/VGA colour display with 128 KB or more */
    unsigned char text_mode; /* BIOS text mode to come back to (2, 3 or 7) */
    int in_gfx;         /* mode 10h is on */
    int want_card;      /* this frame shows a card (meal_card since clear) */
    MealView card;
    const uint8_t *card_planes;
} DosPlat;

static DosPlat *self_of(Platform *p){ return (DosPlat*)p->user; }

/* Early banner via BIOS teletype — works before any mode / VRAM setup. */
void plat_dos_bios_puts(const char *s){
    union REGS r;
    while(*s){
        if(*s=='\n'){
            r.h.ah=0x0E; r.h.al='\r'; r.h.bl=0x07; int86(0x10,&r,&r);
            r.h.ah=0x0E; r.h.al='\n'; r.h.bl=0x07; int86(0x10,&r,&r);
        }else{
            r.h.ah=0x0E; r.h.al=(unsigned char)*s; r.h.bl=0x07; int86(0x10,&r,&r);
        }
        s++;
    }
}

static void detect_video(DosPlat *d){
    union REGS r;
    unsigned equip;
    /* Equipment word at 0040:0010 — bit 4-5: 3=mono, else CGA/colour */
#if defined(__WATCOMC__)
    equip = *(unsigned far *)MK_FP(0x40, 0x10);
#else
    equip = *(unsigned *)0x410;
#endif
    d->mono = ((equip >> 4) & 3) == 3;

    /* Current BIOS mode */
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    if(r.h.al == 0x07) d->mono = 1;

    d->video_seg = d->mono ? 0xB000 : 0xB800;
    d->use_bios = 0; /* prefer direct VRAM with correct segment; BIOS fallback available */
}

static void ensure_text_mode(DosPlat *d){
    union REGS r;
    /* Only switch if not already in a usable 80-col text mode */
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    if(d->mono){
        if(r.h.al != 0x07){
            r.h.ah = 0x00; r.h.al = 0x07; /* 80x25 mono */
            int86(0x10, &r, &r);
        }
    }else{
        /* Modes 2/3 are 80x25 colour; leave alone if already there */
        if(r.h.al != 0x02 && r.h.al != 0x03){
            r.h.ah = 0x00; r.h.al = 0x03;
            int86(0x10, &r, &r);
        }
    }
    /* Hide hardware cursor (optional; ignore failure) */
    r.h.ah = 0x01; r.h.ch = 0x20; r.h.cl = 0x00;
    int86(0x10, &r, &r);
    r.h.ah = 0x0F; int86(0x10, &r, &r);
    d->text_mode = r.h.al;
}

/* LPS-DOS-115-CHG03c: highlighted bars use a bright background (attribute
 * bit 7). By default CGA/EGA/VGA read bit 7 as "blink", so the bar text
 * flashed. We switch bit 7 to "bright background" at start-up and put the
 * blink setting back on exit.
 *  - EGA/VGA: INT 10h AX=1003h, BL=0 off / 1 on.
 *  - CGA (no AX=1003h): the mode control register 3D8h, bit 5; the BIOS keeps
 *    its last value at 0040:0065, which we update too.
 *  - MDA/mono: attribute 70h has no bit 7; nothing to do. */
static unsigned char far *bios_mode_ctl(void){ return (unsigned char far *)MK_FP(0x40,0x65); }
static void set_blink(DosPlat *d, int on){
    union REGS r;
    if(d->mono) return;
    if(d->ega){
        r.x.ax=0x1003; r.h.bl=(unsigned char)(on?1:0); r.h.bh=0;
        int86(0x10,&r,&r);
    }else{
        unsigned char v=*bios_mode_ctl();
        if(!(v&0x08)) v=0x09; /* no valid copy: 80x25 colour text, video on */
        v=(unsigned char)(on ? (v|0x20) : (v&~0x20));
        outp(0x3D8,v);
        *bios_mode_ctl()=v;
    }
}
static void detect_adapter(DosPlat *d){
    union REGS r; unsigned char v;
    r.h.ah=0x12; r.h.bl=0x10; r.h.bh=0xFF; r.x.cx=0xFFFF;
    int86(0x10,&r,&r);
    d->ega = r.h.bl!=0x10; /* a CGA/MDA BIOS leaves BL unchanged */
    /* LPS-DOS-115-CHG07: mode 10h needs a colour display (BH 0) and 128 KB
     * of video memory (BL 1+; 64 KB gives 4 colours only). VGA answers too. */
    d->ega_colour = d->ega && !d->mono && r.h.bh==0 && r.h.bl>=1 && r.h.bl<=3;
    v=*bios_mode_ctl();
    d->blink_was_on = !(v&0x08) || (v&0x20); /* unknown -> DOS default: on */
    r.h.ah=0x03; r.h.bh=0; int86(0x10,&r,&r);
    d->cursor_shape=r.x.cx;
}
/* Leave DOS as found: last frame kept but without bit 7 (so it cannot start
 * blinking), blink setting and cursor shape restored. */
static void leave_graphics(DosPlat *d);
void plat_dos_shutdown(Platform *p){
    DosPlat *d=self_of(p); union REGS r; int i;
    if(d->in_gfx) leave_graphics(d);
    if(!d->mono && !d->use_bios){
        unsigned char far *vid=(unsigned char far *)MK_FP(d->video_seg,0);
        for(i=0;i<ROWS*COLUMNS;i++) vid[i*2+1]&=0x7F;
    }
    set_blink(d,d->blink_was_on);
    r.h.ah=0x01; r.x.cx=d->cursor_shape; int86(0x10,&r,&r);
    r.h.ah=0x02; r.h.bh=0; r.h.dh=ROWS-1; r.h.dl=0; int86(0x10,&r,&r);
}

static void dos_clear(Platform *p){
    DosPlat *d=self_of(p); int r;
    d->want_card=0;
    for(r=0;r<ROWS;r++){
        memset(d->cells[r],' ',COLUMNS); d->cells[r][COLUMNS]=0;
        memset(d->rev[r],0,COLUMNS);
    }
}
static void dos_text(Platform *p, int row, int col, const char *s, int reverse){
    DosPlat *d=self_of(p); size_t n;
    if(row<0||row>=ROWS) return;
    if(col<0) col=0;
    if(col>=COLUMNS) return;
    /* LPS-DOS-115-CHG05: block copies (rep movsb), ~10x faster than a
     * cell-by-cell loop on the 8088. */
    n=strlen(s); if(n>(size_t)(COLUMNS-col)) n=(size_t)(COLUMNS-col);
    memcpy(&d->cells[row][col],s,n);
    memset(&d->rev[row][col],reverse?1:0,n);
}
static void dos_alert(Platform *p, int row, int col, const char *s){
    char bar[COLUMNS+1]; int n,i;
    for(i=0;i<COLUMNS;i++) bar[i]=' ';
    bar[COLUMNS]=0;
    if(col<0) col=0;
    if(col<COLUMNS){
        n=(int)strlen(s);
        if(n>COLUMNS-col) n=COLUMNS-col;
        memcpy(bar+col,s,(size_t)n);
    }
    dos_text(p,row,0,bar,1);
}
static void dos_large(Platform *p, const char *value, int red){
    char line[COLUMNS+1]; char mid[32]; int n,col,i;
    sprintf(mid,">>>  %s  <<<", value);
    for(i=0;i<COLUMNS;i++) line[i]=' ';
    line[COLUMNS]=0;
    n=(int)strlen(mid);
    if(n>COLUMNS) n=COLUMNS;
    col=(COLUMNS-n)/2;
    memcpy(line+col,mid,(size_t)n);
    dos_text(p, 12, 0, line, red);
}
static unsigned char attr_for(DosPlat *d, int reverse){
    unsigned char fg;
    if(d->mono){
        /* MDA: bright = 0x0F, normal 0x07, reverse 0x70 */
        if(reverse) return 0x70;
        return 0x07;
    }
    switch(d->palette){
    case PAL_RED: fg=0x0C; break;
    case PAL_BLUE: fg=0x09; break;
    case PAL_ORANGE: fg=0x06; break;
    case PAL_PINK: fg=0x0D; break;
    default: fg=0x0A; break;
    }
    if(reverse) return (unsigned char)((fg<<4)|0x00);
    return (unsigned char)(fg);
}

static void present_bios(DosPlat *d){
    union REGS r; int row, col;
    /* Home cursor. Fallback path only (use_bios stays 0). */
    r.h.ah=0x02; r.h.bh=0; r.h.dh=0; r.h.dl=0; int86(0x10,&r,&r);
    for(row=0;row<ROWS;row++){
        for(col=0;col<COLUMNS;col++){
            unsigned char attr = attr_for(d, d->rev[row][col]);
            r.h.ah=0x09; r.h.al=(unsigned char)d->cells[row][col]; r.h.bh=0; r.h.bl=attr;
            r.x.cx=1; int86(0x10,&r,&r);
            r.h.ah=0x02; r.h.bh=0; r.h.dh=(unsigned char)row;
            r.h.dl=(unsigned char)(col+1);
            if(r.h.dl>=COLUMNS){ r.h.dl=0; r.h.dh=(unsigned char)(row+1); }
            int86(0x10,&r,&r);
        }
    }
}

/* Write only the rows that changed since the last frame. A full clear each
 * key flickers on a 4.77 MHz 8088. LPS-DOS-115-CHG05: rows are compared with
 * memcmp (rep cmpsb) instead of cell by cell, which cost ~370 ms per key at
 * 8088 speed; a changed row is rewritten whole. */
static void present_vram(DosPlat *d){
    int r,c;
    unsigned char normal=attr_for(d,0), reverse=attr_for(d,1);
#if defined(__WATCOMC__)
    unsigned char far *vid = (unsigned char far *)MK_FP(d->video_seg, 0);
#else
    unsigned char *vid = (unsigned char*)((unsigned long)d->video_seg << 4);
#endif
    if(d->sh_palette!=d->palette){ d->painted=0; d->sh_palette=d->palette; }
    for(r=0;r<ROWS;r++,vid+=COLUMNS*2){
        const char *ch=d->cells[r]; const unsigned char *rev=d->rev[r];
        if(d->painted && memcmp(d->sh_ch[r],ch,COLUMNS)==0 && memcmp(d->sh_rev[r],rev,COLUMNS)==0) continue;
        for(c=0;c<COLUMNS;c++){
            vid[c*2]=(unsigned char)ch[c];
            vid[c*2+1]=rev[c]?reverse:normal;
        }
        memcpy(d->sh_ch[r],ch,COLUMNS);
        memcpy(d->sh_rev[r],rev,COLUMNS);
    }
    d->painted=1;
}

/* ---- Meal Planner in EGA (LPS-DOS-115-CHG07) -------------------------------
 * A frame with a card is drawn in mode 10h; the first frame without one goes
 * back to the text mode we started in, with blink off and the cursor hidden
 * again (the mode set resets both, and the palette), then repaints. */
static unsigned char ega_value(unsigned char cga){
    static const unsigned char v[16]={0,1,2,3,4,5,0x14,7,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F};
    return v[cga&15];
}
static void leave_graphics(DosPlat *d){
    union REGS r;
    r.h.ah=0x00; r.h.al=d->text_mode; int86(0x10,&r,&r);
    set_blink(d,0);
    r.h.ah=0x01; r.h.ch=0x20; r.h.cl=0x00; int86(0x10,&r,&r);
    d->in_gfx=0; d->painted=0;
}
static void present_graphics(DosPlat *d){
    int r;
    if(!d->in_gfx){ ega_enter(ega_value(attr_for(d,0))); d->in_gfx=1; d->painted=0; }
    for(r=0;r<ROWS;r++){
        if(r>=5 && r<=21) continue; /* the card's rows */
        if(d->painted && memcmp(d->sh_ch[r],d->cells[r],COLUMNS)==0 && memcmp(d->sh_rev[r],d->rev[r],COLUMNS)==0) continue;
        ega_text_row(r,d->cells[r],d->rev[r]);
        memcpy(d->sh_ch[r],d->cells[r],COLUMNS);
        memcpy(d->sh_rev[r],d->rev[r],COLUMNS);
    }
    d->painted=1;
    ega_card(&d->card,d->card_planes,meals_palette(d->card.picture));
}
static int dos_meal_open(Platform *p){
    DosPlat *d=self_of(p);
    if(!d->ega_colour || d->use_bios) return MEALS_NO_EGA;
    return meals_open();
}
static int dos_meal_card(Platform *p, const MealView *v){
    DosPlat *d=self_of(p);
    d->card=*v; d->want_card=1;
    return meals_picture(v->picture,&d->card_planes);
}
static void dos_meal_close(Platform *p){ (void)p; meals_close(); }

static void dos_present(Platform *p){
    DosPlat *d=self_of(p);
    if(d->want_card){ present_graphics(d); return; }
    if(d->in_gfx) leave_graphics(d);
    if(d->use_bios) present_bios(d);
    else present_vram(d);
}
static void dos_set_palette(Platform *p, int palette){ self_of(p)->palette=palette; }
static int dos_battery(Platform *p){ (void)p; return 0; }

static enum LoadResult dos_load(Platform *p, State *out){
    DosPlat *d=self_of(p); enum LoadResult r;
    if(!d->mounted) return LOAD_NO_STORAGE;
    r=save_load(lps_data_dir(),out,&d->generation,&d->active);
    if(r==LOAD_MISSING){ d->generation=0; d->active=-1; }
    return r;
}
static int dos_save(Platform *p, const State *s){
    DosPlat *d=self_of(p);
    if(!d->mounted || d->write_fault) return 0;
    lps_ensure_dirs();
    if(!save_store(lps_data_dir(),s,&d->generation,&d->active)){ d->write_fault=1; return 0; }
    return 1;
}
static int dos_export_ics(Platform *p, const Day *day, const State *s, uint32_t totalXp, uint32_t dayXp, int language){
    char export_dir[160];
    (void)p;
    lps_ensure_dirs();
    lps_path_join(export_dir,sizeof(export_dir),lps_data_dir(),"EXPORT");
    return write_ics_file(export_dir, day, s, totalXp, dayXp, language);
}
static int dos_export_csv(Platform *p, const Day *day){
    char weights_dir[160];
    (void)p;
    lps_ensure_dirs();
    lps_path_join(weights_dir,sizeof(weights_dir),lps_data_dir(),"WEIGHTS");
    return write_weight_csv(weights_dir, day);
}

static int dos_export_meal_csv(Platform *p, const State *s){
    char export_dir[160];
    (void)p;
    lps_ensure_dirs();
    lps_path_join(export_dir,sizeof(export_dir),lps_data_dir(),"EXPORT");
    return meal_csv_export(export_dir,s);
}

static int probe_storage(void){
    char test[160]; FILE *f;
    lps_ensure_dirs();
    lps_path_join(test,sizeof(test),lps_data_dir(),"WRTEST.TMP");
    f=fopen(test,"wb"); if(!f) return 0;
    fputc('x',f); fclose(f); remove(test); return 1;
}

Platform *plat_dos_create(void){
    /* Far heap (large model): about 8 KB kept out of DGROUP and the EXE. */
    DosPlat *dp=(DosPlat*)calloc(1,sizeof(DosPlat));
    if(!dp) return 0;
    dp->iface.user=dp;
    dp->iface.clear=dos_clear;
    dp->iface.text=dos_text;
    dp->iface.alert=dos_alert;
    dp->iface.large_text=dos_large;
    dp->iface.present=dos_present;
    dp->iface.set_palette=dos_set_palette;
    dp->iface.battery_low=dos_battery;
    dp->iface.load=dos_load;
    dp->iface.save=dos_save;
    dp->iface.export_ics=dos_export_ics;
    dp->iface.export_weight_csv=dos_export_csv;
    dp->iface.meal_open=dos_meal_open;
    dp->iface.meal_card=dos_meal_card;
    dp->iface.meal_close=dos_meal_close;
    dp->iface.export_meal_csv=dos_export_meal_csv;
    dp->palette=PAL_GREEN;
    detect_video(dp);
    detect_adapter(dp);
    ensure_text_mode(dp);
    set_blink(dp,0); /* after any mode set, which turns blink back on */
    /* Storage probe AFTER video so a slow/failing disk never blanks with no banner */
    dp->mounted=probe_storage();
    return &dp->iface;
}

int plat_dos_get_key(void){
    /* Pure BIOS int 16h — no conio, no int 33h mouse. */
    union REGS r;
    unsigned ascii, scan;
    /* Keyboard buffer empty? BIOS data area 0040:001A head, 0040:001C tail. */
    {
        unsigned far *head = (unsigned far *)MK_FP(0x40, 0x1A);
        unsigned far *tail = (unsigned far *)MK_FP(0x40, 0x1C);
        if(*head == *tail) return -1;
    }
    r.h.ah = 0x00; /* consume one key */
    int86(0x16, &r, &r);
    ascii = r.h.al;
    scan = r.h.ah;
    /* LPS-DOS-115-CHG06b: the scan code tells top-row digits apart from the
     * layout's characters; app_bios_key does the mapping (host-testable). */
    return app_bios_key(ascii, scan);
}

lps_tick_t plat_dos_millis(void){
    unsigned long ticks;
#if defined(__WATCOMC__)
    ticks = *(unsigned long far *)MK_FP(0x40,0x6C);
#else
    ticks = *(unsigned long*)0x46C;
#endif
    return ticks * 55ul;
}
