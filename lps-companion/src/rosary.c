/* rosary.c - rosary sequencing; the prayer texts live in ROSARY.DAT
 * (LPS-DOS-115-CHG03b), loaded on demand into far memory. */
#include "lps.h"
#include "rosary.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Loaded payload: offsets table then strings. With the large model,
 * malloc() hands out far-heap memory, outside DGROUP. */
static unsigned char *g_rosary;
static unsigned g_rosary_strings; /* offset of the string area in g_rosary */
static char g_rosary_path[160] = ROSARY_DAT_NAME;

void rosary_set_path(const char *path){
    size_t n=strlen(path);
    if(n>=sizeof(g_rosary_path)) n=sizeof(g_rosary_path)-1;
    memcpy(g_rosary_path,path,n); g_rosary_path[n]=0;
}
const char *rosary_path(void){ return g_rosary_path; }
int rosary_loaded(void){ return g_rosary!=0; }
void rosary_unload(void){ if(g_rosary){ free(g_rosary); g_rosary=0; } }

static unsigned get16(const unsigned char *p){ return (unsigned)p[0]|((unsigned)p[1]<<8); }
static uint32_t get32le(const unsigned char *p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

static int load_from(const char *path){
    FILE *f; unsigned char hdr[ROSARY_DAT_HEADER]; uint32_t size, crc; unsigned char *p; unsigned i, area, table;
    long file_size;
    f=fopen(path,"rb"); if(!f) return ROSARY_MISSING;
    if(fread(hdr,1,sizeof(hdr),f)!=sizeof(hdr) || memcmp(hdr,ROSARY_DAT_MAGIC,8)!=0
       || get16(hdr+8)!=ROSARY_DAT_VERSION || get16(hdr+10)!=ROSARY_DAT_STRINGS){ fclose(f); return ROSARY_CORRUPT; }
    size=get32le(hdr+12); crc=get32le(hdr+16);
    table=ROSARY_DAT_STRINGS*2u;
    if(size<=table || size>ROSARY_DAT_MAX_PAYLOAD){ fclose(f); return ROSARY_CORRUPT; }
    if(fseek(f,0,SEEK_END)!=0){ fclose(f); return ROSARY_CORRUPT; }
    file_size=ftell(f);
    if(file_size!=(long)(ROSARY_DAT_HEADER+size) || fseek(f,ROSARY_DAT_HEADER,SEEK_SET)!=0){ fclose(f); return ROSARY_CORRUPT; }
    p=(unsigned char*)malloc((size_t)size);
    if(!p){ fclose(f); return ROSARY_NO_MEMORY; }
    if(fread(p,1,(size_t)size,f)!=(size_t)size){ fclose(f); free(p); return ROSARY_CORRUPT; }
    fclose(f);
    area=(unsigned)size-table;
    /* Every string must start inside the area and the area must end with NUL;
     * only printable ASCII and CP437 letters are allowed (no raw UTF-8). */
    if(crc32(p,(size_t)size)!=crc || p[size-1]!=0){ free(p); return ROSARY_CORRUPT; }
    for(i=0;i<ROSARY_DAT_STRINGS;i++) if(get16(p+2u*i)>=area){ free(p); return ROSARY_CORRUPT; }
    for(i=table;i<(unsigned)size;i++) if(p[i] && !cp437_text_byte(p[i])){ free(p); return ROSARY_CORRUPT; }
    g_rosary=p; g_rosary_strings=table;
    return ROSARY_OK;
}
int rosary_load(void){
    int r;
    if(g_rosary) return ROSARY_OK;
    r=load_from(g_rosary_path);
    if(r==ROSARY_MISSING && strcmp(g_rosary_path,ROSARY_DAT_NAME)!=0) r=load_from(ROSARY_DAT_NAME);
    return r;
}
static const char *text_of(unsigned id, int lang){
    unsigned index=id*2u+(lang==LANG_FR?1u:0u);
    if(!g_rosary || index>=ROSARY_DAT_STRINGS) return "";
    return (const char*)(g_rosary+g_rosary_strings+get16(g_rosary+2u*index));
}
typedef struct { int part; unsigned decade, count; } RosaryView;

static const char *tr(int lang, const char *en, const char *fr){ return lang==LANG_FR?fr:en; }

const char *mystery_set_name(int lang, int set){
    static const char *en[]={"Joyful","Luminous","Sorrowful","Glorious"};
    static const char *fr[]={"Joyeux","Lumineux","Douloureux","Glorieux"};
    return tr(lang,en[set],fr[set]);
}
const char *mystery_name(int lang, int set, unsigned decade){
    return text_of(ROS_T_MYSTERY+(unsigned)set*5u+decade, lang);
}
static const char *scripture(int lang, int set, unsigned decade){
    return text_of(ROS_T_SCRIPTURE+(unsigned)set*5u+decade, lang);
}
static const char *meditation(int lang, int set, unsigned decade){
    return text_of(ROS_T_MEDITATION+(unsigned)set*5u+decade, lang);
}

RosaryView rosary_view(int mode, unsigned rosaryDecade, unsigned step){
    RosaryView v; unsigned local, extras, block, decade, pos, p;
    memset(&v,0,sizeof(v));
    if(mode==ROS_DECADE){
        if(step==0){ v.part=RP_SIGN; v.decade=rosaryDecade; }
        else if(step==1){ v.part=RP_MYSTERY; v.decade=rosaryDecade; }
        else if(step==2){ v.part=RP_OUR_FATHER; v.decade=rosaryDecade; }
        else if(step<13){ v.part=RP_HAIL; v.decade=rosaryDecade; v.count=step-2; }
        else if(step==13){ v.part=RP_GLORY; v.decade=rosaryDecade; }
        else if(step==14){ v.part=RP_FATIMA; v.decade=rosaryDecade; }
        else if(step==15){ v.part=RP_CLOSING; v.decade=rosaryDecade; }
        else { v.part=RP_END_SIGN; v.decade=rosaryDecade; }
        return v;
    }
    if(step==0){ v.part=RP_SIGN; return v; }
    if(step==1){ v.part=RP_CREED; return v; }
    if(step==2){ v.part=RP_OPEN_OF; return v; }
    if(step>=3&&step<=5){ v.part=RP_OPEN_HAIL; v.count=step-2; return v; }
    if(step==6){ v.part=RP_GLORY; return v; }
    local=step-7;
    extras = mode==ROS_FULL?0: mode==ROS_GUIDED?2:3;
    block=14+extras;
    if(local<5*block){
        decade=local/block; pos=local%block;
        if(pos==0){ v.part=RP_MYSTERY; v.decade=decade; return v; }
        p=pos-1;
        if(extras){ if(p==0){ v.part=RP_SCRIPTURE; v.decade=decade; return v; }
            if(p==1){ v.part=RP_MEDITATION; v.decade=decade; return v; }
            if(extras==3&&p==2){ v.part=RP_SILENCE; v.decade=decade; return v; }
            p-=extras; }
        if(p==0){ v.part=RP_OUR_FATHER; v.decade=decade; return v; }
        if(p<=10){ v.part=RP_HAIL; v.decade=decade; v.count=p; return v; }
        if(p==11){ v.part=RP_GLORY; v.decade=decade; return v; }
        v.part=RP_FATIMA; v.decade=decade; return v;
    }
    local-=5*block;
    if(local==0){ v.part=RP_HHQ; v.decade=4; return v; }
    if(local==1){ v.part=RP_CLOSING; v.decade=4; return v; }
    v.part=RP_END_SIGN; v.decade=4; return v;
}

unsigned rosary_steps(int mode){
    unsigned extras;
    if(mode==ROS_DECADE) return 17;
    extras = mode==ROS_FULL?0: mode==ROS_GUIDED?2:3;
    return 7+5*(14+extras)+3;
}

const char *rosary_text(int lang, int mode, int set, unsigned rosaryDecade, unsigned step){
    RosaryView v=rosary_view(mode,rosaryDecade,step);
    switch(v.part){
    case RP_SIGN: case RP_END_SIGN: return text_of(ROS_T_SIGN,lang);
    case RP_CREED: return text_of(ROS_T_CREED,lang);
    case RP_OPEN_OF: case RP_OUR_FATHER: return text_of(ROS_T_OUR_FATHER,lang);
    case RP_OPEN_HAIL: case RP_HAIL: return text_of(ROS_T_HAIL,lang);
    case RP_GLORY: return text_of(ROS_T_GLORY,lang);
    case RP_MYSTERY: return mystery_name(lang,set,v.decade);
    case RP_SCRIPTURE: return scripture(lang,set,v.decade);
    case RP_MEDITATION: return meditation(lang,set,v.decade);
    case RP_SILENCE: return text_of(ROS_T_SILENCE,lang);
    case RP_FATIMA: return text_of(ROS_T_FATIMA,lang);
    case RP_HHQ: return text_of(ROS_T_HHQ,lang);
    case RP_CLOSING: return text_of(ROS_T_CLOSING,lang);
    default: return "";
    }
}

const char *rosary_part_name(int lang, int mode, unsigned rosaryDecade, unsigned step){
    RosaryView v=rosary_view(mode,rosaryDecade,step);
    switch(v.part){
    case RP_SIGN: case RP_END_SIGN: return tr(lang,"SIGN OF THE CROSS","SIGNE DE CROIX");
    case RP_CREED: return tr(lang,"APOSTLES' CREED","SYMBOLE DES APÔTRES");
    case RP_OPEN_OF: case RP_OUR_FATHER: return tr(lang,"OUR FATHER","NOTRE PÈRE");
    case RP_OPEN_HAIL: case RP_HAIL: return tr(lang,"HAIL MARY","JE VOUS SALUE MARIE");
    case RP_GLORY: return tr(lang,"GLORY BE","GLOIRE AU PÈRE");
    case RP_MYSTERY: return tr(lang,"MYSTERY","MYSTÈRE");
    case RP_SCRIPTURE: return tr(lang,"SCRIPTURE","ÉCRITURE");
    case RP_MEDITATION: return tr(lang,"MÉDITATION","MÉDITATION");
    case RP_SILENCE: return tr(lang,"CONTEMPLATIVE SILENCE","SILENCE CONTEMPLATIF");
    case RP_FATIMA: return tr(lang,"FATIMA PRAYER","PRIÈRE DE FATIMA");
    case RP_HHQ: return tr(lang,"HAIL, HOLY QUEEN","SALUT, Ô REINE");
    case RP_CLOSING: return tr(lang,"CLOSING PRAYER","PRIÈRE FINALE");
    default: return "";
    }
}

int rosary_part(int mode, unsigned rosaryDecade, unsigned step){
    return rosary_view(mode,rosaryDecade,step).part;
}
unsigned rosary_view_decade(int mode, unsigned rosaryDecade, unsigned step){
    return rosary_view(mode,rosaryDecade,step).decade;
}
unsigned rosary_view_count(int mode, unsigned rosaryDecade, unsigned step){
    return rosary_view(mode,rosaryDecade,step).count;
}
