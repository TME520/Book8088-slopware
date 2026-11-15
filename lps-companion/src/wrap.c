/* Word wrap for the 80-column screen (LPS-DOS-115-CHG03c).
 *
 * Text is CP437, one byte per screen cell, so byte counts are cell counts and
 * accented letters (0x80-0xA5) are ordinary letters here. Lines break at the
 * last space that fits; the spaces at a break are dropped. A word longer than
 * the line is cut at the line width. '\n' forces a break. */
#include "lps.h"

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
