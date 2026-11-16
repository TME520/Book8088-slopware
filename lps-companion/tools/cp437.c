/* cp437 - build-time text conversion (LPS-DOS-115-CHG03).
 *
 * The C sources stay UTF-8, with the same French wording and accents as the
 * PicoCalc. This tool copies one source file and rewrites every non-ASCII
 * character found inside a string or character literal as a three-digit octal
 * escape of its CP437 byte, so the compiled program holds CP437 text for the
 * PC text-mode font. Comments and code are copied unchanged, line for line.
 *
 * Characters CP437 lacks are folded (A grave capital -> A, E grave capital -> E,
 * O circumflex capital -> O, oe ligature -> oe, curly quotes -> straight
 * quotes, dashes -> '-', ellipsis -> "..."). Anything else stops the build
 * with the file and line, so no character is ever dropped silently.
 *
 * Usage: cp437 input.c output.c
 * Host tool (gcc); links src/cp437.c for the table. Deterministic output. */
#include "lps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { unsigned long cp; const char *ascii; } FOLDS[]={
    {0xC0,"A"},{0xC1,"A"},{0xC2,"A"},{0xC3,"A"},{0xC8,"E"},{0xCA,"E"},{0xCB,"E"},
    {0xCC,"I"},{0xCD,"I"},{0xCE,"I"},{0xCF,"I"},{0xD2,"O"},{0xD3,"O"},{0xD4,"O"},
    {0xD5,"O"},{0xD9,"U"},{0xDA,"U"},{0xDB,"U"},{0xDD,"Y"},{0xE3,"a"},{0xF5,"o"},
    {0xFD,"y"},{0x152,"OE"},{0x153,"oe"},{0x178,"Y"},{0x2018,"'"},{0x2019,"'"},
    {0x201C,"\\\""},{0x201D,"\\\""},{0x2013,"-"},{0x2014,"-"},{0x2026,"..."},
    {0xA0," "},{0x202F," "}
};

static const char *g_name; static unsigned g_line=1;
static void die(const char *what){ fprintf(stderr,"%s:%u: cp437: %s\n",g_name,g_line,what); exit(1); }

/* Decodes one UTF-8 sequence starting at p[0] (>= 0x80); returns its length. */
static size_t utf8(const unsigned char *p, size_t left, unsigned long *cp){
    size_t n,i;
    if(p[0]>=0xC2 && p[0]<=0xDF){ n=2; *cp=p[0]&0x1Fu; }
    else if(p[0]>=0xE0 && p[0]<=0xEF){ n=3; *cp=p[0]&0x0Fu; }
    else if(p[0]>=0xF0 && p[0]<=0xF4){ n=4; *cp=p[0]&0x07u; }
    else die("invalid UTF-8 in a literal");
    if(n>left) die("truncated UTF-8 in a literal");
    for(i=1;i<n;i++){ if((p[i]&0xC0)!=0x80) die("invalid UTF-8 in a literal"); *cp=(*cp<<6)|(p[i]&0x3Fu); }
    return n;
}

int main(int argc, char **argv){
    FILE *f; unsigned char *src; long size; size_t i=0, n; FILE *out;
    enum { CODE, LINE_COMMENT, BLOCK_COMMENT, STRING, CHAR } st=CODE;
    if(argc!=3){ fprintf(stderr,"usage: cp437 input.c output.c\n"); return 2; }
    g_name=argv[1];
    f=fopen(argv[1],"rb"); if(!f){ perror(argv[1]); return 1; }
    fseek(f,0,SEEK_END); size=ftell(f); fseek(f,0,SEEK_SET);
    src=(unsigned char*)malloc((size_t)size+1);
    if(!src || fread(src,1,(size_t)size,f)!=(size_t)size){ fprintf(stderr,"%s: read error\n",argv[1]); return 1; }
    fclose(f); n=(size_t)size;
    out=fopen(argv[2],"wb"); if(!out){ perror(argv[2]); return 1; }
    while(i<n){
        unsigned char c=src[i];
        if(c=='\n'){ g_line++; if(st==LINE_COMMENT) st=CODE; if(st==STRING||st==CHAR) die("newline inside a literal"); }
        switch(st){
        case CODE:
            if(c=='/' && i+1<n && src[i+1]=='*'){ fputs("/*",out); i+=2; st=BLOCK_COMMENT; continue; }
            if(c=='/' && i+1<n && src[i+1]=='/'){ fputs("//",out); i+=2; st=LINE_COMMENT; continue; }
            if(c=='"') st=STRING; else if(c=='\'') st=CHAR;
            break;
        case BLOCK_COMMENT:
            if(c=='*' && i+1<n && src[i+1]=='/'){ fputs("*/",out); i+=2; st=CODE; continue; }
            break;
        case LINE_COMMENT: break;
        case STRING: case CHAR:
            if(c=='\\' && i+1<n){ fputc(c,out); fputc(src[i+1],out); i+=2; continue; }
            if((st==STRING && c=='"') || (st==CHAR && c=='\'')){ st=CODE; break; }
            if(c>=0x80){
                unsigned long cp; size_t len=utf8(src+i,n-i,&cp); int b=unicode_to_cp437(cp);
                if(b>=0x80) fprintf(out,"\\%03o",(unsigned)b);
                else{
                    size_t k; const char *fold=0;
                    for(k=0;k<sizeof(FOLDS)/sizeof(FOLDS[0]);k++) if(FOLDS[k].cp==cp) fold=FOLDS[k].ascii;
                    if(!fold){ char msg[80]; sprintf(msg,"U+%04lX has no CP437 form and no fold",cp); die(msg); }
                    if(st==CHAR && strlen(fold)!=1) die("folded character literal is not one byte");
                    fputs(fold,out);
                }
                i+=len; continue;
            }
            break;
        }
        fputc(c,out); i++;
    }
    if(st==STRING||st==CHAR) die("unterminated literal");
    if(fclose(out)!=0){ perror(argv[2]); return 1; }
    free(src);
    return 0;
}
