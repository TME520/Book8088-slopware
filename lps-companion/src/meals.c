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

/* ---- MEALS.DAT ------------------------------------------------------------
 * The table (palettes and CRCs) is read when the planner opens; a picture is
 * read the first time its card shows, its checksum checked, and kept in far
 * memory (15 KB, malloc in the large model) until the planner closes. */
static char g_meals_path[160]=MEALS_DAT_NAME;
static uint8_t *g_table;                       /* MEAL_PICTURE_COUNT entries */
static uint8_t *g_pictures[MEAL_PICTURE_COUNT];
static int g_in_cwd; /* opened from the current directory's copy */

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
void meals_set_path(const char *path){
    size_t n=strlen(path);
    if(n>=sizeof(g_meals_path)) n=sizeof(g_meals_path)-1;
    memcpy(g_meals_path,path,n); g_meals_path[n]=0;
}
void meals_close(void){
    unsigned i;
    for(i=0;i<MEAL_PICTURE_COUNT;i++) if(g_pictures[i]){ free(g_pictures[i]); g_pictures[i]=0; }
    if(g_table){ free(g_table); g_table=0; }
}
static int open_from(const char *path){
    FILE *f; uint8_t hdr[MEALS_DAT_HEADER]; long size; unsigned i;
    size_t table=(size_t)MEAL_PICTURE_COUNT*MEALS_TABLE_ENTRY;
    f=fopen(path,"rb"); if(!f) return MEALS_MISSING;
    if(fread(hdr,1,sizeof(hdr),f)!=sizeof(hdr) || memcmp(hdr,MEALS_DAT_MAGIC,8)!=0
       || get16(hdr+8)!=MEALS_DAT_VERSION || get16(hdr+10)!=MEAL_PICTURE_COUNT
       || get16(hdr+12)!=MEALS_WIDTH || get16(hdr+14)!=MEALS_HEIGHT
       || get32(hdr+16)!=(uint32_t)MEALS_PICTURE_BYTES
       || fseek(f,0,SEEK_END)!=0 || (size=ftell(f))!=MEALS_DAT_SIZE
       || fseek(f,MEALS_DAT_HEADER,SEEK_SET)!=0){ fclose(f); return MEALS_CORRUPT; }
    g_table=(uint8_t*)malloc(table);
    if(!g_table){ fclose(f); return MEALS_NO_MEMORY; }
    if(fread(g_table,1,table,f)!=table || crc32(g_table,table)!=get32(hdr+20)){ fclose(f); meals_close(); return MEALS_CORRUPT; }
    fclose(f);
    for(i=0;i<MEAL_PICTURE_COUNT*16u;i++) if(g_table[(i/16u)*MEALS_TABLE_ENTRY+i%16u]>63){ meals_close(); return MEALS_CORRUPT; }
    return MEALS_OK;
}
int meals_open(void){
    int r;
    meals_close();
    g_in_cwd=0;
    r=open_from(g_meals_path);
    if(r==MEALS_MISSING && strcmp(g_meals_path,MEALS_DAT_NAME)!=0){ g_in_cwd=1; r=open_from(MEALS_DAT_NAME); }
    return r;
}
const uint8_t *meals_palette(unsigned picture){
    if(!g_table || picture>=MEAL_PICTURE_COUNT) return 0;
    return g_table+picture*MEALS_TABLE_ENTRY;
}
int meals_picture(unsigned picture, const uint8_t **planes){
    FILE *f; uint8_t *p; unsigned i; int ok;
    *planes=0;
    if(!g_table || picture>=MEAL_PICTURE_COUNT) return MEALS_CORRUPT;
    if(g_pictures[picture]){ *planes=g_pictures[picture]; return MEALS_OK; }
    p=(uint8_t*)malloc(MEALS_PICTURE_BYTES);
    /* Short of memory: drop the other pictures (read again when shown). */
    for(i=0;!p && i<MEAL_PICTURE_COUNT;i++)
        if(g_pictures[i]){ free(g_pictures[i]); g_pictures[i]=0; p=(uint8_t*)malloc(MEALS_PICTURE_BYTES); }
    if(!p) return MEALS_NO_MEMORY;
    f=fopen(g_in_cwd?MEALS_DAT_NAME:g_meals_path,"rb");
    if(!f){ free(p); return MEALS_MISSING; }
    ok=fseek(f,MEALS_DAT_HEADER+(long)MEAL_PICTURE_COUNT*MEALS_TABLE_ENTRY+(long)picture*MEALS_PICTURE_BYTES,SEEK_SET)==0
       && fread(p,1,MEALS_PICTURE_BYTES,f)==MEALS_PICTURE_BYTES
       && meals_checksum(p,MEALS_PICTURE_BYTES)==get32(g_table+picture*MEALS_TABLE_ENTRY+16);
    fclose(f);
    if(!ok){ free(p); return MEALS_CORRUPT; }
    g_pictures[picture]=p; *planes=p;
    return MEALS_OK;
}
