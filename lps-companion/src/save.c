/* save.c - A/B saves (PicoCalc v1.15 parity, LPS-DOS-115-CHG04).
 *
 * SAVE_A.JSN / SAVE_B.JSN hold the JSON save (jsave.c). Each save writes the
 * slot that is not active, with generation+1, then reads it back and checks
 * its generation and CRC; a failure blocks further writes (the platform's
 * write_fault). Loading keeps the valid slot with the higher generation.
 *
 * Migration: with no JSON save yet, the old binary SAVE_A.BIN / SAVE_B.BIN
 * (formats 1-6, LPR1 envelope) are read; LOAD_MIGRATED tells the app to write
 * SAVE_A.JSN at once. The BIN files are never written or removed. */
#include "lps.h"
#include <stdio.h>
#include <string.h>

#define g_rec (lps_buf->rec)

static uint32_t get32(const uint8_t *p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

static const char *const JSN[2]={"SAVE_A.JSN","SAVE_B.JSN"};
static const char *const BIN[2]={"SAVE_A.BIN","SAVE_B.BIN"};

/* Legacy record: -1 missing, 0 damaged, 1 valid (decoded into lps_decode_scratch). */
static int read_bin(const char *dir, unsigned slot, uint32_t *generation){
    static const size_t ok_sizes[6]={RECORD_SIZE, WIRE_V5+16, WIRE_V4+16, WIRE_V3+16, WIRE_V2+16, WIRE_V1+16};
    char path[200]; FILE *f; long sz; size_t n, size; unsigned i;
    lps_path_join(path,sizeof(path),dir,BIN[slot]);
    f=fopen(path,"rb"); if(!f) return -1;
    if(fseek(f,0,SEEK_END)!=0 || (sz=ftell(f))<=0 || sz>(long)RECORD_SIZE){ fclose(f); return 0; }
    rewind(f);
    n=fread(g_rec,1,(size_t)sz,f); fclose(f);
    if(n!=(size_t)sz) return 0;
    for(i=0;i<6;i++) if(n==ok_sizes[i]) break;
    if(i==6 || get32(g_rec)!=SAVE_MAGIC) return 0;
    size=(size_t)get32(g_rec+8);
    if(size+16!=n || get32(g_rec+n-4)!=crc32(g_rec,n-4)) return 0;
    if(!decode_state(g_rec+12,size,&lps_decode_scratch)) return 0;
    *generation=get32(g_rec+4);
    return 1;
}
static enum LoadResult load_legacy(const char *dir, State *out, uint32_t *generation, int *active, int damaged_json){
    uint32_t ga=0, gb=0; int a, b, use_b;
    a=read_bin(dir,0,&ga); b=read_bin(dir,1,&gb);
    jsave_reset(); /* default catalog, no extras */
    if(a!=1 && b!=1) return a==-1 && b==-1 && !damaged_json ? LOAD_MISSING : LOAD_ERROR;
    /* lps_decode_scratch holds what B decoded to: re-read A if A is the one
     * and a B file was there to overwrite it. */
    use_b=b==1 && (a!=1 || gb>ga);
    if(!use_b && b!=-1 && read_bin(dir,0,&ga)!=1) return LOAD_ERROR;
    *out=lps_decode_scratch;
    *generation=use_b ? gb : ga;
    *active=-1;
    return LOAD_MIGRATED;
}
/* Both slots read and parsed in full, as on the PicoCalc. */
static enum LoadResult load_full(const char *dir, State *out, uint32_t *generation, int *active){
    char path[200]; uint32_t ga=0, gb=0, ca=0, cb=0, again=0, ag=0;
    enum SaveCheck a, b;
    State *s=&lps_decode_scratch;
    lps_path_join(path,sizeof(path),dir,JSN[0]); a=jsave_read(path,s,&ga,&ca);
    lps_path_join(path,sizeof(path),dir,JSN[1]); b=jsave_read(path,s,&gb,&cb);
    /* A newer schema is protected like an unreadable disk: never overwritten. */
    if(a==SAVE_IOERROR||b==SAVE_IOERROR||a==SAVE_UNSUPPORTED||b==SAVE_UNSUPPORTED){ jsave_reset(); return LOAD_ERROR; }
    if(a==SAVE_VALID||b==SAVE_VALID){
        int slot=b==SAVE_VALID&&(a!=SAVE_VALID||gb>ga)?1:0;
        /* The scratch holds B (read last): read A again if A is the newest. */
        if(slot==0){
            lps_path_join(path,sizeof(path),dir,JSN[0]);
            if(jsave_read(path,s,&ag,&again)!=SAVE_VALID||again!=ca){ jsave_reset(); return LOAD_ERROR; }
        }
        *out=*s; *generation=slot?gb:ga; *active=slot;
        return LOAD_OK;
    }
    /* Both JSON slots damaged: a JSON save existed, so the BIN files are
     * older data. Protect everything rather than fall back silently. */
    if(a==SAVE_DAMAGED&&b==SAVE_DAMAGED){ jsave_reset(); return LOAD_ERROR; }
    return load_legacy(dir,out,generation,active,a==SAVE_DAMAGED||b==SAVE_DAMAGED);
}
/* Same choice as load_full, usually with one parse instead of two or three:
 * the first lines give both generations, so the newer slot is parsed first;
 * if it is valid it wins whatever the other holds (the other cannot be
 * newer, nor a newer schema). Anything unusual goes to load_full. */
enum LoadResult save_load(const char *dir, State *out, uint32_t *generation, int *active){
    char path[2][200]; uint32_t va=0, vb=0, ga=0, gb=0, g=0, c=0;
    enum SaveCheck ha, hb, r;
    int first, other, has_other;
    State *s=&lps_decode_scratch;
    lps_path_join(path[0],sizeof(path[0]),dir,JSN[0]); ha=jsave_peek(path[0],&va,&ga);
    lps_path_join(path[1],sizeof(path[1]),dir,JSN[1]); hb=jsave_peek(path[1],&vb,&gb);
    if(ha==SAVE_MISSING&&hb==SAVE_MISSING){ jsave_reset(); return load_legacy(dir,out,generation,active,0); }
    if(!((ha==SAVE_VALID||ha==SAVE_MISSING)&&(hb==SAVE_VALID||hb==SAVE_MISSING))
       || (ha==SAVE_VALID&&va>2) || (hb==SAVE_VALID&&vb>2))
        return load_full(dir,out,generation,active);
    first=ha==SAVE_VALID&&(hb!=SAVE_VALID||ga>=gb)?0:1; /* a tie goes to A, as on the PicoCalc */
    other=1-first; has_other=(other?hb:ha)==SAVE_VALID;
    r=jsave_read(path[first],s,&g,&c);
    if(r==SAVE_VALID){ *out=*s; *generation=g; *active=first; return LOAD_OK; }
    if(r!=SAVE_DAMAGED){ jsave_reset(); return LOAD_ERROR; }
    if(!has_other){ jsave_reset(); return load_legacy(dir,out,generation,active,1); }
    r=jsave_read(path[other],s,&g,&c);
    if(r==SAVE_VALID){ *out=*s; *generation=g; *active=other; return LOAD_OK; }
    jsave_reset();
    return LOAD_ERROR; /* both damaged, or the other unreadable */
}
int save_store(const char *dir, const State *s, uint32_t *generation, int *active){
    char path[200]; unsigned slot; uint32_t written=0;
    if(*generation==0xfffffffful) return 0;
    /* Alternate ONE slot per save: the active slot is never touched. */
    slot=(*active==0)?1u:0u;
    lps_path_join(path,sizeof(path),dir,JSN[slot]);
    if(!jsave_write(path,s,(*generation)+1,&written)) return 0;
    /* Read-back: the new generation and CRC must be on disk. */
    if(!jsave_verify(path,(*generation)+1,written)) return 0;
    *active=(int)slot; (*generation)++;
    return 1;
}
