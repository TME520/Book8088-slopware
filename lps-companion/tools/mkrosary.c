/* mkrosary - writes ROSARY.DAT from src/rosary_text.c (LPS-DOS-115-CHG03b).
 * Built from the CP437 copy of rosary_text.c made by tools/cp437
 * (LPS-DOS-115-CHG03), so the file holds CP437 text, version 2.
 * Host tool (gcc). Layout: see rosary.h. Output is byte-for-byte deterministic.
 * Usage: mkrosary [output]   (default ROSARY.DAT) */
#include "lps.h"
#include "rosary.h"
#include <stdio.h>
#include <string.h>

static unsigned char g_out[ROSARY_DAT_HEADER + ROSARY_DAT_MAX_PAYLOAD];

static void put16(unsigned char *p, unsigned v){ p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); }
static void put32(unsigned char *p, uint32_t v){ put16(p,(unsigned)(v&0xFFFFu)); put16(p+2,(unsigned)(v>>16)); }

int main(int argc, char **argv){
    const char *path=argc>1?argv[1]:ROSARY_DAT_NAME;
    unsigned char *payload=g_out+ROSARY_DAT_HEADER;
    size_t table=ROSARY_DAT_STRINGS*2u, used=table; unsigned i; FILE *f;
    for(i=0;i<ROSARY_DAT_STRINGS;i++){
        const char *s=rosary_source_text(i/2u,(int)(i%2u)); size_t n; const char *c;
        if(!s){ fprintf(stderr,"mkrosary: missing text %u\n",i); return 1; }
        for(c=s;*c;c++) if(!cp437_text_byte((unsigned char)*c)){ fprintf(stderr,"mkrosary: text %u has a byte that is not printable CP437 text\n",i); return 1; }
        n=strlen(s)+1;
        if(used-table>0xFFFFu || used+n>ROSARY_DAT_MAX_PAYLOAD){ fprintf(stderr,"mkrosary: texts too large\n"); return 1; }
        put16(payload+2u*i,(unsigned)(used-table));
        memcpy(payload+used,s,n); used+=n;
    }
    memcpy(g_out,ROSARY_DAT_MAGIC,8);
    put16(g_out+8,ROSARY_DAT_VERSION);
    put16(g_out+10,ROSARY_DAT_STRINGS);
    put32(g_out+12,(uint32_t)used);
    put32(g_out+16,crc32(payload,used));
    f=fopen(path,"wb");
    if(!f || fwrite(g_out,1,ROSARY_DAT_HEADER+used,f)!=ROSARY_DAT_HEADER+used || fclose(f)!=0){ fprintf(stderr,"mkrosary: cannot write %s\n",path); return 1; }
    printf("%s: %u strings, %lu bytes\n",path,(unsigned)ROSARY_DAT_STRINGS,(unsigned long)(ROSARY_DAT_HEADER+used));
    return 0;
}
