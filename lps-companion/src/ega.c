/* ega.c - EGA 640 x 350, 16 colours (BIOS mode 10h), for the Meal Planner
 * (LPS-DOS-115-CHG07). DOS only.
 *
 * 8088 speed: everything is written in bytes of 8 pixels with the EGA's own
 * help, never pixel by pixel, and the inner loops are a few 8086
 * instructions (_asm): C loops over far pointers cost 2-3 times more.
 *  - Areas: Set/Reset gives every written pixel one colour in all 4 planes
 *    at once; the Bit Mask keeps the pixels outside the area (the latches
 *    hold them after a read). A row of whole bytes is one rep stosb.
 *  - Text: the 8 x 14 BIOS font (INT 43h). The background is one area; then
 *    each glyph row is a Bit Mask write and one read + write in the text
 *    colour. Spaces cost nothing.
 *  - Pictures: MEALS.DAT keeps each bit plane whole, so a row is 4 x 30
 *    bytes of rep movsb, one Map Mask write each. Row by row, so the picture
 *    appears from top to bottom without mixing planes.
 *  - Played card: the palette registers turn the card grey; no pixel moves.
 *
 * Card layout (PicoCalc README "Meal Planner"): a 320 x 216 card centred in
 * text rows 5-21 with the pastel border and header band (the row's name),
 * the illustration, the name and the portion on a ribbon. The Blank card is
 * the user's design (ribbon and burst lines), centred, with nothing below. */
#include "ega.h"
#include "meals.h"
#include <dos.h>
#include <conio.h>
#include <string.h>

#define SEQ 0x3C4
#define GC 0x3CE
#define BPL 80 /* bytes per scan line */
#define FONT_H 14
#define CARD_X 160
#define CARD_Y 81
#define CARD_W 320
#define CARD_H 216
#define BODY_X (CARD_X+8)
#define BODY_Y (CARD_Y+24)
#define BODY_W (CARD_W-16)
#define BODY_H (CARD_H-30)
#define ART_X (CARD_X+(CARD_W-MEALS_WIDTH)/2)
#define ART_Y (CARD_Y+27)
#define BLANK_ART_Y (BODY_Y+(BODY_H-MEALS_HEIGHT)/2)
#define NAME_Y (CARD_Y+160)
#define RIBBON_Y (CARD_Y+179)
#define RIBBON_H 18

static const unsigned char far *g_font;
static unsigned char g_text_colour; /* palette register 1 (EGA registers are write-only) */
static struct { int valid; uint8_t picture, row, language, played, art; } drawn;

static unsigned char far *vram(int x, int y){ return (unsigned char far *)MK_FP(0xA000,(unsigned)(y*BPL+(x>>3))); }
static void gc(unsigned char index, unsigned char value){ outpw(GC,(unsigned)((value<<8)|index)); }
static void map_mask(unsigned char planes){ outpw(SEQ,(unsigned)((planes<<8)|2)); }

void ega_enter(unsigned char text_colour){
    union REGS r; unsigned char regs[16];
    r.x.ax=0x0010; int86(0x10,&r,&r);
    /* The mode set points INT 43h at the 8 x 14 graphics font. */
    g_font=*(const unsigned char far * far *)MK_FP(0,0x43*4);
    memset(regs,0,sizeof(regs)); regs[1]=text_colour; g_text_colour=text_colour;
    {   union REGS q; unsigned i;
        for(i=0;i<2;i++){ q.x.ax=0x1000; q.h.bl=(unsigned char)i; q.h.bh=regs[i]; int86(0x10,&q,&q); } }
    drawn.valid=0;
}

/* h (> 0) rows of n (> 0) whole bytes, 80 apart. With Set/Reset on, the
 * value written does not matter. */
static void rows_set(unsigned char far *dst, unsigned n, unsigned h){
    _asm {
        les di, dst
        mov dx, n
        mov bx, h
        mov ax, 80
        sub ax, dx
    next_row:
        mov cx, dx
        rep stosb
        add di, ax
        dec bx
        jnz next_row
    }
}
/* h (> 0) bytes, 80 apart, read (into the latches) and written back: the
 * Bit Mask decides which of their pixels take the Set/Reset colour. */
static void rows_rmw(unsigned char far *dst, unsigned h){
    _asm {
        les di, dst
        mov cx, h
    next_byte:
        mov al, es:[di]
        mov es:[di], al
        add di, 80
        loop next_byte
    }
}
/* Set/Reset fill of x..x+w-1, y..y+h-1 with one colour. */
static void fill(int x, int y, int w, int h, unsigned char colour){
    int xb0, xb1, mid; unsigned char lm, rm;
    if(w<=0||h<=0) return;
    xb0=x>>3; xb1=(x+w-1)>>3;
    lm=(unsigned char)(0xFFu>>(x&7)); rm=(unsigned char)(0xFFu<<(7-((x+w-1)&7)));
    gc(0,colour); gc(1,0x0F);
    if(xb0==xb1) lm&=rm;
    if(lm!=0xFF || xb0==xb1){
        gc(8,lm); rows_rmw(vram(x,y),(unsigned)h);
        if(xb0==xb1){ gc(8,0xFF); gc(1,0); return; }
        xb0++;
    }
    if(rm!=0xFF){
        gc(8,rm); rows_rmw(vram(xb1<<3,y),(unsigned)h);
        xb1--;
    }
    gc(8,0xFF);
    mid=xb1-xb0+1;
    if(mid>0) rows_set(vram(xb0<<3,y),(unsigned)mid,(unsigned)h);
    gc(1,0);
}
/* One 8 x 14 glyph in the Set/Reset colour (Bit Mask register selected):
 * each non-empty glyph row is a Bit Mask write and a read + write. */
static void glyph(unsigned char far *dst, const unsigned char far *g){
    _asm {
        push ds
        les di, dst
        lds si, g
        mov dx, 0x3CF
        mov cx, 14
    next_line:
        lodsb
        or al, al
        jz empty
        out dx, al
        mov ah, es:[di]
        mov es:[di], ah
    empty:
        add di, 80
        loop next_line
        pop ds
    }
}
/* n cells of CP437 text at byte column xb, scan line y. */
static void text(int xb, int y, const char *s, int n, unsigned char fg, unsigned char bg){
    int i;
    if(n<=0) return;
    fill(xb<<3,y,n<<3,FONT_H,bg);
    gc(0,fg); gc(1,0x0F);
    outp(GC,8); /* Bit Mask selected: each glyph row is one data write */
    for(i=0;i<n;i++){
        unsigned char c=(unsigned char)s[i];
        if(c==' '||c==0) continue;
        glyph(vram((xb+i)<<3,y),g_font+c*FONT_H);
    }
    outp(GC+1,0xFF); gc(1,0);
}
void ega_text_row(int row, const char *cells, const unsigned char *rev){
    int col=0, end;
    while(col<COLUMNS){
        end=col; while(end<COLUMNS && (rev[end]!=0)==(rev[col]!=0)) end++;
        text(col,row*FONT_H,cells+col,end-col,(unsigned char)(rev[col]?0:1),(unsigned char)(rev[col]?1:0));
        col=end;
    }
}
static void centred(int y, const char *s, unsigned char fg, unsigned char bg){
    int n=(int)strlen(s); if(n>CARD_W/8-4) n=CARD_W/8-4;
    text(CARD_X/8+(CARD_W/8-n)/2,y,s,n,fg,bg);
}
/* A rectangle with rounded corners: inset[i] pixels off rows i and h-1-i. */
static void rounded(int x, int y, int w, int h, unsigned char colour, const unsigned char *inset, int rows){
    int i;
    for(i=0;i<rows;i++){ fill(x+inset[i],y+i,w-2*inset[i],1,colour); fill(x+inset[i],y+h-1-i,w-2*inset[i],1,colour); }
    fill(x,y+rows,w,h-2*rows,colour);
}
/* The picture, row by row: for each of its 128 rows the 4 planes' 30 bytes,
 * each after a Map Mask write (sequencer index 2). The asm hard-codes
 * MEALS_ROW_BYTES 30, MEALS_PLANE_BYTES 3840 and MEALS_HEIGHT 128. */
#if MEALS_ROW_BYTES != 30 || MEALS_HEIGHT != 128
#error "picture() assumes 240 x 128 pictures"
#endif
static void blit(unsigned char far *dst, const unsigned char far *src){
    _asm {
        push ds
        les di, dst
        lds si, src
        mov bx, 128
        mov dx, 0x3C4
    next_row:
        mov ah, 1
    next_plane:
        mov al, 2
        out dx, ax
        mov cx, 15
        rep movsw
        sub di, 30
        add si, 3810
        shl ah, 1
        cmp ah, 16
        jb next_plane
        sub si, 15330
        add di, 80
        dec bx
        jnz next_row
        pop ds
    }
}
static void picture(int y, const uint8_t *planes){
    gc(1,0); gc(8,0xFF);
    blit(vram(ART_X,y),planes);
    map_mask(0x0F);
}
/* Played: the card in faded greys. White stays white (body, white cups);
 * dark colours turn dark grey, all others light grey, so the border, band
 * and ribbon still show against the body. */
static unsigned char faded(unsigned char v){
    unsigned r=170u*((v>>2)&1)+85u*((v>>5)&1), g=170u*((v>>1)&1)+85u*((v>>4)&1), b=170u*(v&1)+85u*((v>>3)&1);
    unsigned luma=(r*77u+g*150u+b*29u)>>8;
    if(v==0x3F) return 0x3F;
    return (unsigned char)(luma<96u?0x38:0x07);
}
static void card_palette(const uint8_t *palette16, int played){
    union REGS r; struct SREGS s; static unsigned char regs[17]; unsigned i;
    for(i=2;i<16;i++){ unsigned char v=palette16?palette16[i]:(unsigned char)(i==2?0x3F:i==4?0x07:0x00); regs[i]=played?faded(v):v; }
    regs[0]=0; regs[1]=g_text_colour; regs[16]=0; /* the screen's, and a black border */
    segread(&s); r.x.ax=0x1002; r.x.dx=FP_OFF(regs); s.es=FP_SEG(regs);
    int86x(0x10,&r,&r,&s);
}
void ega_card(const MealView *v, const uint8_t *planes, const uint8_t *palette16){
    static const unsigned char outer[5]={7,4,2,1,1}, inner[2]={3,1};
    int played=v->position!=0, blank=v->row==MEAL_BLANK, art=planes!=0;
    int whole=!drawn.valid;
    int newPicture=whole||drawn.picture!=v->picture||drawn.language!=v->language||drawn.art!=art;
    int newHeader=whole||drawn.row!=v->row||drawn.language!=v->language;
    int newPalette=whole||drawn.picture!=v->picture||drawn.played!=played||drawn.art!=art;
    if(whole) rounded(CARD_X,CARD_Y,CARD_W,CARD_H,MEAL_INK_EDGE,outer,5);
    if(newPicture){
        /* Clear what the new card replaces, in the body colour, before its
         * colours arrive: no flash of the old picture in the new palette. */
        if(whole||blank||drawn.row==MEAL_BLANK) rounded(BODY_X,BODY_Y,BODY_W,BODY_H,MEAL_INK_BODY,inner,2);
        else { fill(ART_X,ART_Y,MEALS_WIDTH,MEALS_HEIGHT,MEAL_INK_BODY); fill(BODY_X,NAME_Y-2,BODY_W,RIBBON_Y+RIBBON_H+2-(NAME_Y-2),MEAL_INK_BODY); }
    }
    if(newPalette) card_palette(palette16,played);
    if(newHeader){ fill(BODY_X,CARD_Y+5,BODY_W,FONT_H,MEAL_INK_EDGE); centred(CARD_Y+5,v->rowName,MEAL_INK_TEXT,MEAL_INK_EDGE); }
    if(newPicture){
        if(art) picture(blank?BLANK_ART_Y:ART_Y,planes);
        if(!blank){
            int n=(int)strlen(v->portion), rw=n*8+48, rx, j;
            if(rw<160) rw=160;
            rw=(rw+15)&~15; rx=CARD_X+(CARD_W-rw)/2;
            centred(NAME_Y,v->name,MEAL_INK_TEXT,MEAL_INK_BODY);
            /* Portion ribbon with notched ends, like the paper cards. */
            for(j=0;j<RIBBON_H;j++){ int notch=j<RIBBON_H/2?j:RIBBON_H-1-j; fill(rx+notch,RIBBON_Y+j,rw-2*notch,1,MEAL_INK_RIBBON); }
            centred(RIBBON_Y+2,v->portion,MEAL_INK_TEXT,MEAL_INK_RIBBON);
        }
    }
    drawn.valid=1; drawn.picture=v->picture; drawn.row=v->row; drawn.language=v->language;
    drawn.played=(uint8_t)played; drawn.art=(uint8_t)art;
}
