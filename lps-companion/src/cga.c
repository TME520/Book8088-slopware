/* cga.c - CGA 320 x 200, 4 colours (BIOS mode 04h), for the Meal Planner on
 * a colour CGA without EGA (LPS-DOS-115-CHG13). DOS only.
 *
 * Video memory at B800h, 2 bits per pixel (left pixel in bits 7-6), 80
 * bytes per line; even lines from offset 0, odd lines from 2000h.
 *  - Text: 8 x 8 glyphs (cgafont.c) on byte boundaries. Each glyph row is
 *    one word written to VRAM: a 256-word table spreads the 8 font bits to
 *    8 two-bit pixels, then masks pick the text and background colours.
 *  - Areas: whole bytes are one _fmemset per line; edges keep their other
 *    pixels with a mask.
 * Mode 04h shows no "snow" even on an IBM CGA (snow only hits its 80-column
 * text mode), so VRAM is written at any time.
 *
 * Card (EGA layout scaled to 320 x 200): 176 x 136 at (72, 40), text rows
 * 5-21. Yellow rounded border with the row's name on its top band, then the
 * body in colour 0: picture box 160 x 96 (a dotted frame until CHG14), the
 * name, and the portion on a red ribbon with notched ends. Played: the
 * border is dithered (yellow / colour 0) and PLAYED #n sits in the box. */
#include "cga.h"
#include "meals.h"
#include <dos.h>
#include <string.h>
#include <stdio.h>

#define CGA_SEG 0xB800
#define CARD_X 72
#define CARD_Y 40
#define CARD_W 176
#define CARD_H 136
#define BAND_H 14
#define BODY_X (CARD_X+4)
#define BODY_Y (CARD_Y+BAND_H)
#define BODY_W (CARD_W-8)
#define BODY_H (CARD_H-BAND_H-4)
#define ART_W 160
#define ART_H 96
#define ART_X (CARD_X+(CARD_W-ART_W)/2)
#define ART_Y (BODY_Y+1)
#define NAME_Y (ART_Y+ART_H+2)
#define RIBBON_Y (NAME_Y+10)
#define RIBBON_H 10
#define INK_EDGE 3
#define INK_RIBBON 2
#define INK_BOX 1

static unsigned short g_spread[256]; /* font byte -> 8 pixels of colour 3 */
static unsigned char g_text;
static struct { int valid; uint8_t card, language, played; } drawn;

static unsigned char far *line_at(int y){ return (unsigned char far *)MK_FP(CGA_SEG,(unsigned)((y&1)*0x2000+(y>>1)*80)); }
static unsigned char fill_byte(unsigned char colour){ return (unsigned char)(colour*0x55u); }

void cga_enter(unsigned char text, unsigned char background){
    union REGS r; unsigned i, b;
    r.x.ax=0x0004; int86(0x10,&r,&r);
    r.h.ah=0x0B; r.h.bh=0; r.h.bl=(unsigned char)((background&15)|0x10); int86(0x10,&r,&r); /* bit 4: high intensity */
    r.h.ah=0x0B; r.h.bh=1; r.h.bl=0; int86(0x10,&r,&r); /* palette 0: green / red / yellow */
    for(i=0;i<256 && !g_spread[255];i++){
        unsigned short w=0;
        for(b=0;b<8;b++) if(i&(0x80u>>b)) w|=(unsigned short)(3u<<(b<4?(6-2*b):(14-2*(b-4))));
        g_spread[i]=w; /* low byte: left 4 pixels, high byte: right 4 */
    }
    g_text=text; drawn.valid=0;
}

/* x..x+w-1 on lines y..y+h-1: even lines take `even`, odd lines `odd`
 * (equal for a plain colour, alternating pixels for the dithered border). */
static void pattern(int x, int y, int w, int h, unsigned char even, unsigned char odd){
    int xb0, xb1, j; unsigned char lm, rm;
    if(w<=0||h<=0) return;
    xb0=x>>2; xb1=(x+w-1)>>2;
    lm=(unsigned char)(0xFFu>>(2*(x&3))); rm=(unsigned char)(0xFFu<<(2*(3-((x+w-1)&3))));
    if(xb0==xb1) lm&=rm;
    for(j=0;j<h;j++){
        unsigned char far *p=line_at(y+j); unsigned char v=((y+j)&1)?odd:even;
        p[xb0]=(unsigned char)((p[xb0]&~lm)|(v&lm));
        if(xb1>xb0){
            if(xb1-xb0>1) _fmemset(p+xb0+1,v,(size_t)(xb1-xb0-1));
            p[xb1]=(unsigned char)((p[xb1]&~rm)|(v&rm));
        }
    }
}
static void fill(int x, int y, int w, int h, unsigned char colour){ unsigned char v=fill_byte(colour); pattern(x,y,w,h,v,v); }
/* Even/odd follow the screen line, so a dithered pattern stays a checkerboard. */
static void rounded(int x, int y, int w, int h, unsigned char even, unsigned char odd, const unsigned char *inset, int rows){
    int i;
    for(i=0;i<rows;i++){ pattern(x+inset[i],y+i,w-2*inset[i],1,even,odd); pattern(x+inset[i],y+h-1-i,w-2*inset[i],1,even,odd); }
    pattern(x,y+rows,w,h-2*rows,even,odd);
}
/* n cells of CP437 text at cell column cx (8-pixel cells), line y. The 8
 * scan lines' addresses are worked out once; each glyph row is one word. */
static void text(int cx, int y, const char *s, int n, unsigned char fg, unsigned char bg){
    unsigned short fm=(unsigned short)(fg*0x5555u), bm=(unsigned short)(bg*0x5555u);
    unsigned short far *row[8]; int i, j;
    for(j=0;j<8;j++) row[j]=(unsigned short far *)(line_at(y+j)+cx*2);
    for(i=0;i<n;i++){
        const unsigned char *g=cga_font8+((unsigned char)s[i])*8u;
        for(j=0;j<8;j++){ unsigned short e=g_spread[g[j]]; row[j][i]=(unsigned short)((e&fm)|(~e&bm)); }
    }
}
void cga_text_row(int row, const char *cells, const unsigned char *rev){
    char out[CGA_COLUMNS]; unsigned char orev[CGA_COLUMNS]; int col=0, end;
    lps_fold_row(cells,rev,CGA_COLUMNS,out,orev);
    while(col<CGA_COLUMNS){
        end=col; while(end<CGA_COLUMNS && (orev[end]!=0)==(orev[col]!=0)) end++;
        text(col,row*8,out+col,end-col,(unsigned char)(orev[col]?0:g_text),(unsigned char)(orev[col]?g_text:0));
        col=end;
    }
}
static void centred(int y, const char *s, unsigned char fg, unsigned char bg){
    int n=(int)strlen(s); if(n>CARD_W/8-2) n=CARD_W/8-2;
    text(CARD_X/8+(CARD_W/8-n)/2,y,s,n,fg,bg);
}
/* The picture box, dotted until the pictures come (LPS-DOS-115-CHG14). */
static void art_box(void){
    unsigned char dots=fill_byte(INK_BOX)&0xCC; /* every other pixel */
    pattern(ART_X,ART_Y,ART_W,1,dots,dots); pattern(ART_X,ART_Y+ART_H-1,ART_W,1,dots,dots);
    pattern(ART_X,ART_Y+1,1,ART_H-2,0,fill_byte(INK_BOX)); pattern(ART_X+ART_W-1,ART_Y+1,1,ART_H-2,0,fill_byte(INK_BOX));
}
void cga_card(const MealView *v){
    static const unsigned char outer[4]={4,2,1,1};
    char b[24]; int played=v->position!=0, blank=v->row==MEAL_BLANK;
    unsigned char edge=fill_byte(INK_EDGE);
    if(drawn.valid && drawn.card==v->card && drawn.language==v->language && drawn.played==(uint8_t)played) return;
    /* Border: solid, or yellow / colour 0 every other pixel when played. */
    if(played) rounded(CARD_X,CARD_Y,CARD_W,CARD_H,(unsigned char)(edge&0xCC),(unsigned char)(edge&0x33),outer,4);
    else rounded(CARD_X,CARD_Y,CARD_W,CARD_H,edge,edge,outer,4);
    fill(BODY_X,BODY_Y,BODY_W,BODY_H,0);
    if(played){ int n=(int)strlen(v->rowName); fill(CARD_X+(CARD_W/8-n)/2*8-4,CARD_Y+2,n*8+8,10,0); centred(CARD_Y+3,v->rowName,INK_EDGE,0); }
    else centred(CARD_Y+3,v->rowName,0,INK_EDGE);
    art_box();
    if(blank) centred(ART_Y+ART_H/2-(played?16:4),"~ Blank ~",INK_EDGE,0);
    else {
        int n=(int)strlen(v->portion), rw=n*8+24, rx, j;
        if(rw<96) rw=96;
        if(rw>BODY_W-8) rw=BODY_W-8;
        rw=(rw+15)&~15; rx=CARD_X+(CARD_W-rw)/2;
        centred(NAME_Y,v->name,INK_EDGE,0);
        for(j=0;j<RIBBON_H;j++){ int notch=j<RIBBON_H/2?j:RIBBON_H-1-j; fill(rx+notch,RIBBON_Y+j,rw-2*notch,1,INK_RIBBON); }
        centred(RIBBON_Y+1,v->portion,0,INK_RIBBON);
    }
    if(played){
        sprintf(b,v->language==LANG_FR?"JOUÉE #%u":"PLAYED #%u",(unsigned)v->position);
        centred(ART_Y+ART_H/2-4,b,INK_RIBBON,0);
    }
    drawn.valid=1; drawn.card=v->card; drawn.language=v->language; drawn.played=(uint8_t)played;
}
