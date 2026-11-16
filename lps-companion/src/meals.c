/* meals.c - Meal Planner deck and MEALS.DAT pictures (LPS-DOS-115-CHG07). */
#include "meals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The PicoCalc's MealDeck, in the user's order: 6 drinks, 8 foods, 2 Blank;
 * duplicates are separate cards. */
const MealCard MEAL_DECK[MEAL_CARD_COUNT]={
    {MEAL_DRINKS,MP_LATTE,"Coffee (Latte)","Café (Latte)","1 mug","1 tasse"},
    {MEAL_DRINKS,MP_BLACK,"Coffee (Black)","Café (Noir)","1 cup","1 tasse"},
    {MEAL_DRINKS,MP_BLACK,"Coffee (Black)","Café (Noir)","1 cup","1 tasse"},
    {MEAL_DRINKS,MP_TEA,"Tea","Thé","1 cup","1 tasse"},
    {MEAL_DRINKS,MP_MINT_TEA,"Mint Tea","Thé à la menthe","1 cup","1 tasse"},
    {MEAL_DRINKS,MP_JUICE,"Light Juice","Jus léger","1 glass","1 verre"},
    {MEAL_FOOD,MP_WRAP,"Light Cheese Wrap","Wrap fromage léger","1 wrap","1 wrap"},
    {MEAL_FOOD,MP_WRAP,"Light Cheese Wrap","Wrap fromage léger","1 wrap","1 wrap"},
    {MEAL_FOOD,MP_SALAD,"Tomato Salad","Salade de tomates","1 bowl","1 bol"},
    {MEAL_FOOD,MP_BISCUIT,"Biscuit","Biscuit","1 serving","1 portion"},
    {MEAL_FOOD,MP_CHEESE,"Cheese","Fromage","1 portion","1 portion"},
    {MEAL_FOOD,MP_APPLE,"Apple","Pomme","1 medium","1 moyenne"},
    {MEAL_FOOD,MP_ORANGE,"Orange","Orange","1 medium","1 moyenne"},
    {MEAL_FOOD,MP_SOUP,"Soup","Soupe","1 bowl","1 bol"},
    {MEAL_BLANK,MP_BLANK,"Blank","Blank","?","?"},
    {MEAL_BLANK,MP_BLANK,"Blank","Blank","?","?"}
};
const uint8_t MEAL_ROW_FIRST[MEAL_ROW_COUNT+1]={0,6,14,16};

const char *meal_row_name(unsigned row, int language){
    static const char *const en[MEAL_ROW_COUNT]={"Drinks","Food","Blank"};
    static const char *const fr[MEAL_ROW_COUNT]={"Boissons","Nourriture","Blank"};
    if(row>=MEAL_ROW_COUNT) row=MEAL_BLANK;
    return language==LANG_FR?fr[row]:en[row];
}
/* ---- Meal Planner CSV (LPS-DOS-115-CHG08), line for line the PicoCalc's
 * lps_companion.cpp mealCsvName / writeCsvField / writeMealCsv and main.cpp
 * exportMealCsv (a FatFs sink there, a buffer and stdio here). The names written are the
 * English ones of MEAL_DECK and the row names below: plain ASCII, so the
 * CP437 build step leaves them alone and the bytes match the PicoCalc's. */
void meal_csv_name(const Date *d, char *out){
    sprintf(out,"MP%02u%02u%02u.CSV",(unsigned)(d->year%100),(unsigned)d->month,(unsigned)d->day);
}
static int append(char *out, size_t cap, size_t *used, const char *p, size_t n){
    if(*used+n>=cap) return 0;
    memcpy(out+*used,p,n); *used+=n; out[*used]=0; return 1;
}
int meal_csv_field(char *out, size_t cap, size_t *used, const char *value){
    /* One pass that copies while it looks for , " CR LF: strpbrk on each
     * field took most of the 0.1 s a full deck needed at 8088 speed. */
    size_t u=*used; const char *p=value; char c;
    while((c=*p)!=0 && c!=',' && c!='"' && c!='\r' && c!='\n'){
        if(u+1>=cap) return 0;
        out[u++]=c; p++;
    }
    if(!c){ out[u]=0; *used=u; return 1; }
    /* Quoted, quotes doubled. */
    u=*used;
    if(u+1>=cap) return 0;
    out[u++]='"';
    for(p=value;(c=*p)!=0;p++){
        if(u+(c=='"'?2:1)>=cap) return 0;
        if(c=='"') out[u++]='"';
        out[u++]=c;
    }
    if(u+1>=cap) return 0;
    out[u++]='"'; out[u]=0; *used=u; return 1;
}
size_t meal_csv_text(const State *s, char *out, size_t cap){
    unsigned i; size_t u=0; int ok=append(out,cap,&u,MEAL_CSV_HEADER,sizeof(MEAL_CSV_HEADER)-1);
    for(i=0;i<s->mealCount&&ok;++i){
        const MealCard *card=&MEAL_DECK[s->meals[i]<MEAL_CARD_COUNT?s->meals[i]:0];
        char number[8]; sprintf(number,"%u,",i+1);
        ok=append(out,cap,&u,number,strlen(number))&&meal_csv_field(out,cap,&u,meal_row_name(card->row,LANG_EN))
            &&append(out,cap,&u,",",1)&&meal_csv_field(out,cap,&u,card->en)&&append(out,cap,&u,",",1)
            &&meal_csv_field(out,cap,&u,card->row==MEAL_BLANK?"":card->enPortion)&&append(out,cap,&u,"\r\n",2);
    }
    return ok?u:0;
}
int meal_csv_export(const char *export_dir, const State *s){
    char name[16], path[200]; FILE *f; int ok; size_t n;
    if(!valid_date(s->today.date)) return 0;
    meal_csv_name(&s->today.date,name);
    if(strlen(export_dir)+2+strlen(name)>sizeof(path)) return 0;
    lps_path_join(path,sizeof(path),export_dir,name);
    if(!s->mealCount){
        /* Nothing played: do not create a file, but reset an existing one. */
        f=fopen(path,"rb"); if(!f) return 1;
        fclose(f);
    }
    /* Built in the far export buffer and written at once: 16 lines of small
     * fwrite calls took 144 ms at 8088 speed. */
    n=meal_csv_text(s,lps_buf->ics,sizeof(lps_buf->ics));
    f=fopen(path,"wb"); if(!f) return 0;
    ok=n&&fwrite(lps_buf->ics,1,n,f)==n;
    if(fclose(f)!=0) ok=0;
    if(!ok){ remove(path); return 0; }
    return 1;
}

unsigned meal_play_position(const State *s, unsigned card){
    unsigned i;
    for(i=0;i<s->mealCount;i++) if(s->meals[i]==card) return i+1;
    return 0;
}
void meal_toggle(State *s, unsigned card){
    unsigned position, i;
    if(card>=MEAL_CARD_COUNT) return;
    position=meal_play_position(s,card);
    if(position){
        for(i=position-1;i+1<s->mealCount;i++) s->meals[i]=s->meals[i+1];
        s->meals[--s->mealCount]=0;
    }else if(s->mealCount<MEAL_CARD_COUNT) s->meals[s->mealCount++]=(uint8_t)card;
}
unsigned meal_first_unplayed(const State *s){
    unsigned card=0;
    while(card+1<MEAL_CARD_COUNT && meal_play_position(s,card)) card++;
    return meal_play_position(s,card)?0:card;
}
unsigned meal_move(unsigned card, int key){
    unsigned row, first, n, column, m;
    if(card>=MEAL_CARD_COUNT) card=0;
    row=MEAL_DECK[card].row; first=MEAL_ROW_FIRST[row]; n=MEAL_ROW_FIRST[row+1]-first; column=card-first;
    if(key==KEY_LEFT) return first+(column+n-1)%n;
    if(key==KEY_RIGHT) return first+(column+1)%n;
    if(key==KEY_UP||key==KEY_DOWN){
        row=(row+(key==KEY_UP?MEAL_ROW_COUNT-1u:1u))%MEAL_ROW_COUNT;
        m=MEAL_ROW_FIRST[row+1]-MEAL_ROW_FIRST[row];
        return MEAL_ROW_FIRST[row]+(column<m?column:m-1);
    }
    return card;
}
void meal_view(const State *s, unsigned card, MealView *v){
    const MealCard *c=&MEAL_DECK[card<MEAL_CARD_COUNT?card:0];
    int fr=s->language==LANG_FR;
    v->card=(uint8_t)(card<MEAL_CARD_COUNT?card:0);
    v->picture=c->picture; v->row=c->row;
    v->position=(uint8_t)meal_play_position(s,v->card);
    v->language=s->language;
    v->rowName=meal_row_name(c->row,s->language);
    /* The Blank card is the user's design: no name or portion below it. */
    v->name=c->row==MEAL_BLANK?"":(fr?c->fr:c->en);
    v->portion=c->row==MEAL_BLANK?"":(fr?c->frPortion:c->enPortion);
}

/* ---- MEALS.DAT and MEALSCGA.DAT ---------------------------------------------
 * The table (palettes and checksums) is read when the planner opens; a
 * picture is read the first time its card shows, its checksum checked, and
 * kept in far memory (malloc in the large model) until the planner closes.
 * LPS-DOS-115-CHG14: the same code reads both files, which differ only in
 * their magic, picture size and table entry (MEALS.DAT: 16 palette bytes,
 * then the checksum; MEALSCGA.DAT: the checksum only). */
typedef struct {
    const char *name, *magic;
    unsigned width, height, entry;   /* entry: table bytes per picture, checksum last */
    unsigned bytes;                  /* per picture */
    long size;                       /* whole file */
    char path[160];
    uint8_t *table;                  /* MEAL_PICTURE_COUNT entries */
    uint8_t *pictures[MEAL_PICTURE_COUNT];
    int in_cwd;                      /* opened from the current directory's copy */
} DatFile;
static DatFile g_ega={MEALS_DAT_NAME,MEALS_DAT_MAGIC,MEALS_WIDTH,MEALS_HEIGHT,MEALS_TABLE_ENTRY,MEALS_PICTURE_BYTES,MEALS_DAT_SIZE,MEALS_DAT_NAME,0,{0},0};
static DatFile g_cga={MEALSCGA_DAT_NAME,MEALSCGA_DAT_MAGIC,MEALSCGA_WIDTH,MEALSCGA_HEIGHT,MEALSCGA_TABLE_ENTRY,MEALSCGA_PICTURE_BYTES,MEALSCGA_DAT_SIZE,MEALSCGA_DAT_NAME,0,{0},0};

static unsigned get16(const uint8_t *p){ return (unsigned)p[0]|((unsigned)p[1]<<8); }
static uint32_t get32(const uint8_t *p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

uint32_t meals_checksum(const uint8_t *p, unsigned n){
    uint16_t s1=0, s2=0;
#if defined(__WATCOMC__) && defined(LPS_DOS)
    /* 8088: 4 instructions a word (the C loop over a far pointer takes 3.5
     * times longer). A block from malloc never crosses its segment's end. */
    _asm {
        push ds
        lds si, p
        mov cx, n
        shr cx, 1
        xor bx, bx
        xor dx, dx
        jcxz sums_done
    next_word:
        lodsw
        add bx, ax
        add dx, bx
        loop next_word
    sums_done:
        pop ds
        mov s1, bx
        mov s2, dx
    }
#else
    for(n/=2u;n;n--,p+=2){ s1=(uint16_t)(s1+(p[0]|((unsigned)p[1]<<8))); s2=(uint16_t)(s2+s1); }
#endif
    return ((uint32_t)s2<<16)|s1;
}
static void dat_set_path(DatFile *d, const char *path){
    size_t n=strlen(path);
    if(n>=sizeof(d->path)) n=sizeof(d->path)-1;
    memcpy(d->path,path,n); d->path[n]=0;
}
static void dat_close(DatFile *d){
    unsigned i;
    for(i=0;i<MEAL_PICTURE_COUNT;i++) if(d->pictures[i]){ free(d->pictures[i]); d->pictures[i]=0; }
    if(d->table){ free(d->table); d->table=0; }
}
static int dat_open_from(DatFile *d, const char *path){
    FILE *f; uint8_t hdr[MEALS_DAT_HEADER]; long size;
    size_t table=(size_t)MEAL_PICTURE_COUNT*d->entry;
    f=fopen(path,"rb"); if(!f) return MEALS_MISSING;
    if(fread(hdr,1,sizeof(hdr),f)!=sizeof(hdr) || memcmp(hdr,d->magic,8)!=0
       || get16(hdr+8)!=MEALS_DAT_VERSION || get16(hdr+10)!=MEAL_PICTURE_COUNT
       || get16(hdr+12)!=d->width || get16(hdr+14)!=d->height
       || get32(hdr+16)!=(uint32_t)d->bytes
       || fseek(f,0,SEEK_END)!=0 || (size=ftell(f))!=d->size
       || fseek(f,MEALS_DAT_HEADER,SEEK_SET)!=0){ fclose(f); return MEALS_CORRUPT; }
    d->table=(uint8_t*)malloc(table);
    if(!d->table){ fclose(f); return MEALS_NO_MEMORY; }
    if(fread(d->table,1,table,f)!=table || crc32(d->table,table)!=get32(hdr+20)){ fclose(f); dat_close(d); return MEALS_CORRUPT; }
    fclose(f);
    return MEALS_OK;
}
static int dat_open(DatFile *d){
    int r;
    dat_close(d);
    d->in_cwd=0;
    r=dat_open_from(d,d->path);
    if(r==MEALS_MISSING && strcmp(d->path,d->name)!=0){ d->in_cwd=1; r=dat_open_from(d,d->name); }
    return r;
}
static int dat_picture(DatFile *d, unsigned picture, const uint8_t **out){
    FILE *f; uint8_t *p; unsigned i; int ok;
    *out=0;
    if(!d->table || picture>=MEAL_PICTURE_COUNT) return MEALS_CORRUPT;
    if(d->pictures[picture]){ *out=d->pictures[picture]; return MEALS_OK; }
    p=(uint8_t*)malloc(d->bytes);
    /* Short of memory: drop the other pictures (read again when shown). */
    for(i=0;!p && i<MEAL_PICTURE_COUNT;i++)
        if(d->pictures[i]){ free(d->pictures[i]); d->pictures[i]=0; p=(uint8_t*)malloc(d->bytes); }
    if(!p) return MEALS_NO_MEMORY;
    f=fopen(d->in_cwd?d->name:d->path,"rb");
    if(!f){ free(p); return MEALS_MISSING; }
    ok=fseek(f,MEALS_DAT_HEADER+(long)MEAL_PICTURE_COUNT*d->entry+(long)picture*d->bytes,SEEK_SET)==0
       && fread(p,1,d->bytes,f)==d->bytes
       && meals_checksum(p,d->bytes)==get32(d->table+picture*d->entry+d->entry-4);
    fclose(f);
    if(!ok){ free(p); return MEALS_CORRUPT; }
    d->pictures[picture]=p; *out=p;
    return MEALS_OK;
}

void meals_set_path(const char *path){ dat_set_path(&g_ega,path); }
void meals_close(void){ dat_close(&g_ega); }
int meals_open(void){
    unsigned i; int r=dat_open(&g_ega);
    /* EGA palette values are 0-63. */
    if(r==MEALS_OK) for(i=0;i<MEAL_PICTURE_COUNT*16u;i++) if(g_ega.table[(i/16u)*MEALS_TABLE_ENTRY+i%16u]>63){ dat_close(&g_ega); return MEALS_CORRUPT; }
    return r;
}
const uint8_t *meals_palette(unsigned picture){
    if(!g_ega.table || picture>=MEAL_PICTURE_COUNT) return 0;
    return g_ega.table+picture*MEALS_TABLE_ENTRY;
}
int meals_picture(unsigned picture, const uint8_t **planes){ return dat_picture(&g_ega,picture,planes); }

void mealscga_set_path(const char *path){ dat_set_path(&g_cga,path); }
void mealscga_close(void){ dat_close(&g_cga); }
int mealscga_open(void){ return dat_open(&g_cga); }
int mealscga_picture(unsigned picture, const uint8_t **pixels){ return dat_picture(&g_cga,picture,pixels); }
