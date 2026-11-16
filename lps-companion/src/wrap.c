/* Word wrap for the 80-column screen (LPS-DOS-115-CHG03c).
 *
 * Text is CP437, one byte per screen cell, so byte counts are cell counts and
 * accented letters (0x80-0xA5) are ordinary letters here. Lines break at the
 * last space that fits; the spaces at a break are dropped. A word longer than
 * the line is cut at the line width. '\n' forces a break. */
#include "lps.h"
#include <string.h>

const char *wrap_line(const char *text, unsigned width, unsigned *len){
    unsigned i, cut;
    if(!width){ *len=0; while(*text) text++; return text; }
    for(i=0;i<width && text[i] && text[i]!='\n';i++){}
    if(!text[i]){ *len=i; return text+i; }
    if(text[i]=='\n'){ *len=i; return text+i+1; }
    /* The line is full and more text follows. */
    if(text[i]==' '){ cut=i; }
    else{
        cut=i;
        while(cut>0 && text[cut]!=' ') cut--;
        if(cut==0){ *len=width; return text+width; } /* one long word: hard break */
    }
    *len=cut;
    while(*len>0 && text[*len-1]==' ') (*len)--;
    text+=cut;
    while(*text==' ') text++;
    return text;
}

unsigned wrap_count(const char *text, unsigned width){
    unsigned n=0, len;
    while(*text){ text=wrap_line(text,width,&len); n++; }
    return n;
}

/* LPS-DOS-115-CHG13: an 80-cell screen row folded to `width` cells for the
 * CGA 40-column graphics screen. The row is cut into blocks of text
 * separated by FOLD_GAP or more spaces (title and date, label and counter);
 * each block keeps its own text and is placed by where it sat on the
 * 80-column row: ending at the right edge -> right-aligned, centred ->
 * centred, otherwise its left indent is halved. Blocks never overlap (one
 * space at least, the left ones give way); what does not fit is cut. A row
 * reversed from end to end (title bar, footer) stays reversed across the
 * whole width; otherwise each cell keeps its block's reverse flag. */
#define FOLD_GAP 6
#define FOLD_BLOCKS 8
void lps_fold_row(const char *cells, const unsigned char *rev, unsigned width, char *out, unsigned char *outRev){
    int s[FOLD_BLOCKS], n[FOLD_BLOCKS], x[FOLD_BLOCKS], k=0, c=0, i, w=(int)width, all=1, lim;
    for(i=0;i<COLUMNS;i++) if(!rev[i]){ all=0; break; }
    memset(out,' ',width); memset(outRev,all,width);
    while(c<COLUMNS && k<FOLD_BLOCKS){
        int e, gap=0;
        while(c<COLUMNS && (cells[c]==' '||!cells[c])) c++;
        if(c>=COLUMNS) break;
        e=c;
        for(i=c+1;i<COLUMNS;i++){
            if(cells[i]==' '||!cells[i]){ if(++gap>=FOLD_GAP) break; }
            else { gap=0; e=i; }
        }
        s[k]=c; n[k]=e-c+1; c=e+1;
        if(s[k]+n[k]>=COLUMNS-2) x[k]=w-n[k]-(COLUMNS-(s[k]+n[k]))/2;
        else if(2*s[k]+n[k]-COLUMNS<=2 && COLUMNS-(2*s[k]+n[k])<=2) x[k]=(w-n[k])/2;
        else x[k]=s[k]/2;
        k++;
    }
    for(i=k-2;i>=0;i--) if(x[i]+n[i]+1>x[i+1]) x[i]=x[i+1]-1-n[i];
    lim=0;
    for(i=0;i<k;i++){
        int m=n[i];
        if(x[i]<lim) x[i]=lim;
        if(x[i]+m>w) m=w-x[i];
        if(m>0){
            memcpy(out+x[i],cells+s[i],(size_t)m);
            if(!all) memcpy(outRev+x[i],rev+s[i],(size_t)m);
        }
        lim=x[i]+n[i]+1;
    }
}
