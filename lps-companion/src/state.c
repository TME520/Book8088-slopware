/* state.c - dates, banked XP, encode/decode (Companion v1.11 wire formats 1-6). */
#include "lps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The PicoCalc default list (LPS-113-CHG08): the first 16 are the old fixed
 * list, in the binary saves' bit order; 17-22 were added in v1.13. ids 1-22. */
const char *ACTIVITIES_FR[DEFAULT_ACTIVITY_COUNT] = {
    "Marche","Régime","Bible","Messe","Travail","Projet","Sieste","Gurumed",
    "Maladie","Congé","Weekend","Sortie","Jeu","Docteur","Lecture","Shopping",
    "Sport","Cuisine","Ménage","Famille","Amis","Méditation"
};
const char *ACTIVITIES_EN[DEFAULT_ACTIVITY_COUNT] = {
    "Walk","Diet","Bible","Mass","Work","Project","Nap","Gurumed",
    "Illness","Day off","Weekend","Outing","Gaming","Doctor","Reading","Shopping",
    "Sport","Cooking","Chores","Family","Friends","Meditation"
};
const Date FIRST_DATE = {2000,1,1};
const Date LAST_DATE = {2099,12,31};
const Date DEFAULT_DATE = {2026,9,17};

/* DOS stack is tiny: decode into the far scratch then copy out on success. */
LpsBuffers *lps_buf;
DayNames *lps_day_names; /* DAY_SLOTS x 751 bytes, far heap */
int lps_buffers_init(void){
    if(!lps_buf) lps_buf=(LpsBuffers*)calloc(1,sizeof(LpsBuffers));
    if(!lps_day_names) lps_day_names=(DayNames*)calloc(DAY_SLOTS,sizeof(DayNames));
    return lps_buf!=0 && lps_day_names!=0;
}

/* ---- Activity catalog (LPS-DOS-115-CHG05, PicoCalc LPS-113-CHG07/CHG08) ---- */
/* The default catalog in UTF-8, built once from the CP437 literals above. */
static Activity default_catalog[DEFAULT_ACTIVITY_COUNT];
static int default_catalog_built;
static void utf8_of(const char *s, char *out, size_t cap){
    size_t n=0; char u[3];
    for(;*s;s++){ size_t k=cp437_to_utf8((unsigned char)*s,u); if(n+k>=cap) break; memcpy(out+n,u,k); n+=k; }
    out[n]=0;
}
void default_activities(State *s){
    unsigned i;
    if(!default_catalog_built){
        for(i=0;i<DEFAULT_ACTIVITY_COUNT;i++){
            default_catalog[i].id=(uint16_t)(i+1);
            utf8_of(ACTIVITIES_FR[i],default_catalog[i].fr,ACTIVITY_NAME_LENGTH+1);
            utf8_of(ACTIVITIES_EN[i],default_catalog[i].en,ACTIVITY_NAME_LENGTH+1);
        }
        default_catalog_built=1;
    }
    memset(s->activities,0,sizeof(s->activities));
    memcpy(s->activities,default_catalog,sizeof(default_catalog));
    s->activityCount=DEFAULT_ACTIVITY_COUNT; s->nextActivityId=DEFAULT_ACTIVITY_COUNT+1;
}
const char *activity_stored_name(const Activity *a){ return a->fr[0]?a->fr:a->en; }
const char *activity_display_name(const Activity *a, int language){
    if(language==LANG_FR) return a->fr[0]?a->fr:a->en;
    return a->en[0]?a->en:a->fr;
}
unsigned activity_index(const State *s, const char *name){
    unsigned i;
    if(!name||!*name) return ACTIVITY_CAPACITY;
    for(i=0;i<s->activityCount;i++){
        const char *n=activity_stored_name(&s->activities[i]);
        if(n[0]==name[0]&&!strcmp(n,name)) return i;
    }
    return ACTIVITY_CAPACITY;
}
void day_names_clear(void){
    unsigned i;
    if(lps_day_names) for(i=0;i<DAY_SLOTS;i++) lps_day_names[i].count=0;
}
/* As PicoCalc freezeDayNames: the ticked catalog names, in catalog order. */
void day_names_freeze(const State *s, unsigned slot, uint32_t flags){
    DayNames *d=day_names(slot); unsigned i;
    d->count=0;
    for(i=0;i<s->activityCount&&d->count<ACTIVITY_CAPACITY;i++) if(FLAG_BIT(flags,i))
        strcpy(d->names[d->count++],activity_stored_name(&s->activities[i]));
}
/* As PicoCalc resolveDayFlags: a day with stored names gets its flags from
 * them; names the catalog lacks stay as frozen extras (orphans). */
static void resolve_day(const State *s, Day *day, const DayNames *d){
    unsigned i, k;
    if(!d->count) return;
    day->flags=0;
    for(i=0;i<d->count;i++) if((k=activity_index(s,d->names[i]))<s->activityCount) day->flags|=FLAG_MASK(k);
}
void day_names_resolve(State *s){
    unsigned i;
    resolve_day(s,&s->today,day_names(0));
    for(i=0;i<HISTORY_LENGTH;i++) resolve_day(s,&s->history[i],day_names(1+i));
}
unsigned day_orphan_count(const State *s, unsigned slot){
    const DayNames *d=day_names(slot); unsigned i, n=0;
    for(i=0;i<d->count;i++) if(activity_index(s,d->names[i])>=s->activityCount) n++;
    return n;
}
const char *day_orphan(const State *s, unsigned slot, unsigned index){
    const DayNames *d=day_names(slot); unsigned i;
    for(i=0;i<d->count;i++) if(activity_index(s,d->names[i])>=s->activityCount && index--==0) return d->names[i];
    return "";
}

/* ---- Catalog edits (LPS-DOS-115-CHG06, PicoCalc LPS-113-CHG07/07b) ----
 * The caller checks the day is editable. Each edit keeps today's ticks on the
 * same activities, then activity_refresh freezes today under the new names
 * and rematches every day from its own frozen names, as the PicoCalc's
 * refreshActivityFlags does: a renamed or deleted name stays in history. */
void activity_pin_history(State *s){
    /* A history day without stored names (a .BIN migration in this session)
     * gets them now, so its flags never follow a reorder. A reload would do
     * the same: the save always holds names. */
    unsigned i;
    for(i=0;i<HISTORY_LENGTH;i++)
        if(!day_names(1+i)->count && s->history[i].flags) day_names_freeze(s,1+i,s->history[i].flags);
}
void activity_refresh(State *s){
    day_names_freeze(s,0,s->today.flags);
    day_names_resolve(s);
}
/* Fast path for 8088 speed: a move or a delete renumbers the catalog the same
 * way for every day, so history flags are remapped in place (see
 * activity_swap/rotate/delete) instead of matching 31 x 30 names again
 * (~0.6 s at 4.77 MHz). This equals a full rematch only while no two entries
 * share a stored name; the caller checks activity_names_unique. */
int activity_names_unique(const State *s){
    const char *name[ACTIVITY_CAPACITY]; char first[ACTIVITY_CAPACITY]; unsigned i, j, n=s->activityCount;
    for(i=0;i<n;i++){ name[i]=activity_stored_name(&s->activities[i]); first[i]=name[i][0]; }
    for(i=0;i<n;i++) for(j=i+1;j<n;j++)
        if(first[i]==first[j] && strcmp(name[i],name[j])==0) return 0; /* first byte: cheap filter */
    return 1;
}
void activity_refresh_today(State *s){
    day_names_freeze(s,0,s->today.flags);
    resolve_day(s,&s->today,day_names(0));
}
/* After entry id was added or got a new stored name: each history day ticks
 * it when it stored that name (names unique, so no other entry matches). */
void activity_rematch_entry(State *s, unsigned id){
    unsigned d, i; const char *name;
    if(id>=s->activityCount) return;
    name=activity_stored_name(&s->activities[id]);
    for(d=0;d<HISTORY_LENGTH;d++){
        const DayNames *dn=day_names(1+d);
        if(!dn->count) continue;
        s->history[d].flags&=~FLAG_MASK(id);
        for(i=0;i<dn->count;i++) if(strcmp(dn->names[i],name)==0){ s->history[d].flags|=FLAG_MASK(id); break; }
    }
}
static uint32_t swap_bits(uint32_t f, unsigned a, unsigned b){
    int hadA=FLAG_BIT(f,a)!=0, hadB=FLAG_BIT(f,b)!=0;
    f&=~(FLAG_MASK(a)|FLAG_MASK(b));
    if(hadA) f|=FLAG_MASK(b);
    if(hadB) f|=FLAG_MASK(a);
    return f;
}
static uint32_t rotate_bits(uint32_t f, unsigned n, int firstToEnd){
    uint32_t r=0; unsigned i;
    for(i=0;i<n;i++) if(FLAG_BIT(f,i)) r|=FLAG_MASK(firstToEnd ? (i?i-1:n-1) : (i+1<n?i+1:0));
    return r;
}
static uint32_t delete_bit(uint32_t f, unsigned n, unsigned id){
    uint32_t r=0; unsigned i;
    for(i=0;i<n;i++) if(i!=id && FLAG_BIT(f,i)) r|=FLAG_MASK(i<id?i:i-1);
    return r;
}
/* Today and every history day with stored names get the same renumbering. */
#define EACH_DAY_FLAGS(s, expr) do{ unsigned d_; uint32_t f; \
    f=(s)->today.flags; (s)->today.flags=(expr); \
    for(d_=0;d_<HISTORY_LENGTH;d_++) if(day_names(1+d_)->count){ f=(s)->history[d_].flags; (s)->history[d_].flags=(expr); } \
    }while(0)
void activity_swap(State *s, unsigned a, unsigned b){
    Activity keep;
    if(a>=s->activityCount||b>=s->activityCount||a==b) return;
    keep=s->activities[a]; s->activities[a]=s->activities[b]; s->activities[b]=keep;
    EACH_DAY_FLAGS(s, swap_bits(f,a,b));
}
void activity_rotate(State *s, int firstToEnd){
    unsigned n=s->activityCount, i; Activity keep;
    if(n<2) return;
    if(firstToEnd){
        keep=s->activities[0];
        for(i=0;i+1<n;i++) s->activities[i]=s->activities[i+1];
        s->activities[n-1]=keep;
    }else{
        keep=s->activities[n-1];
        for(i=n-1;i>0;i--) s->activities[i]=s->activities[i-1];
        s->activities[0]=keep;
    }
    EACH_DAY_FLAGS(s, rotate_bits(f,n,firstToEnd));
}
void activity_delete(State *s, unsigned id){
    unsigned i, n=s->activityCount;
    if(id>=n) return;
    for(i=id;i+1<n;i++) s->activities[i]=s->activities[i+1];
    memset(&s->activities[--s->activityCount],0,sizeof(Activity));
    EACH_DAY_FLAGS(s, delete_bit(f,n,id));
}
/* As the PicoCalc's namesEqualIgnoreCase: ASCII letters, and the UTF-8
 * Latin-1 letters U+00C0-U+00DE (except U+00D7) against their lower case. */
static char fold_ascii(unsigned char c){ return (char)(c>='A'&&c<='Z'?c+32:c); }
static char fold_c3(unsigned char c){ return (char)(c>=0x80&&c<=0x9e&&c!=0x97?c+0x20:c); }
int activity_names_equal(const char *a, const char *b){
    while(*a && *b){
        unsigned char ca=(unsigned char)*a++, cb=(unsigned char)*b++;
        if(ca==0xc3 && cb==0xc3 && *a && *b){ if(fold_c3((unsigned char)*a++)!=fold_c3((unsigned char)*b++)) return 0; }
        else if(fold_ascii(ca)!=fold_ascii(cb)) return 0;
    }
    return !*a && !*b;
}
int activity_duplicate(const State *s, const char *name, unsigned except, int language){
    unsigned i;
    for(i=0;i<s->activityCount;i++)
        if(i!=except && activity_names_equal(activity_display_name(&s->activities[i],language),name)) return 1;
    return 0;
}
int activity_valid_name(const char *name){
    const unsigned char *p=(const unsigned char*)name;
    if(!*p || strlen(name)>ACTIVITY_NAME_LENGTH) return 0;
    for(;*p;p++) if(*p<32||*p==127) return 0;
    return 1;
}

int leap(uint16_t year){ return year%4==0 && (year%100!=0 || year%400==0); }
unsigned days_in_month(uint16_t year, uint8_t month){
    static const uint8_t normal[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(month<1||month>12) return 0;
    if(month==2 && leap(year)) return 29;
    return normal[month-1];
}
int valid_date(Date d){
    return d.year>=FIRST_DATE.year && d.year<=LAST_DATE.year && d.month>=1 && d.month<=12
        && d.day>=1 && d.day<=days_in_month(d.year,d.month);
}
int next_date(Date *d){
    if(!valid_date(*d) || (d->year==LAST_DATE.year && d->month==12 && d->day==31)) return 0;
    if(++d->day <= days_in_month(d->year,d->month)) return 1;
    d->day=1; if(++d->month<=12) return 1; d->month=1; ++d->year; return 1;
}
void date_text(Date d, char *out, size_t n){ sprintf(out, "%04u-%02u-%02u", (unsigned)d.year, (unsigned)d.month, (unsigned)d.day); (void)n; }
int parse_date(const char *text, Date *d){
    unsigned i; Date t;
    if(!text || strlen(text)!=10 || text[4]!='-' || text[7]!='-') return 0;
    for(i=0;i<10;i++) if(i!=4 && i!=7 && (text[i]<'0'||text[i]>'9')) return 0;
    t.year=(uint16_t)((text[0]-'0')*1000+(text[1]-'0')*100+(text[2]-'0')*10+text[3]-'0');
    t.month=(uint8_t)((text[5]-'0')*10+text[6]-'0');
    t.day=(uint8_t)((text[8]-'0')*10+text[9]-'0');
    if(!valid_date(t)) return 0;
    *d=t;
    return 1;
}
/* 16 bits at a time: 32-bit shifts are slow loops on the 8088. */
unsigned count_flags(uint32_t flags){
    unsigned n=0, w=(unsigned)(flags&0xffffu), h=(unsigned)(flags>>16);
    for(;w;w>>=1) n+=w&1u;
    for(;h;h>>=1) n+=h&1u;
    return n;
}
uint32_t points(const Day *d){ unsigned n=count_flags(d->flags); return n*10u + (n>=3?10u:0u); }
int parse_weight(const char *s, int32_t *grams){
    uint32_t whole=0, fraction=0; unsigned digits=0, decimals=0;
    if(!s || !*s){ *grams=-1; return 1; }
    while(*s>='0'&&*s<='9'){ if(++digits>3) return 0; whole=whole*10u+(unsigned)(*s++-'0'); }
    if(!digits) return 0;
    if(*s=='.'||*s==','){ ++s; while(*s>='0'&&*s<='9'){ if(++decimals>3) return 0; fraction=fraction*10u+(unsigned)(*s++-'0'); }
        if(!decimals) return 0; }
    if(*s) return 0;
    for(;decimals<3;++decimals) fraction*=10u;
    *grams=(int32_t)(whole*1000u+fraction); return *grams>0;
}
void weight_text(int32_t grams, char *out, size_t n){
    if(grams<0){ sprintf(out,"--"); (void)n; }
    else sprintf(out,"%ld.%03ld",(long)(grams/1000),(long)(grams%1000));
}
/* CRC-32 (IEEE). Table-driven since LPS-DOS-115-CHG04: the bitwise loop cost
 * ~8x more on the 8088. The table is split in 16-bit halves so the loop has
 * no 32-bit shift (a shift loop on the 8086); 1 KB in DGROUP, filled on first use. */
#ifdef __WATCOMC__
static uint16_t __near crc_lo[256], __near crc_hi[256];
#else
static uint16_t crc_lo[256], crc_hi[256];
#endif
uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n){
    uint16_t lo=(uint16_t)c, hi=(uint16_t)(c>>16); unsigned i;
    if(!crc_lo[1]){
        unsigned k; uint32_t v;
        for(i=0;i<256;i++){
            v=i; for(k=0;k<8;k++) v=(v&1)?(v>>1)^0xedb88320ul:v>>1;
            crc_lo[i]=(uint16_t)v; crc_hi[i]=(uint16_t)(v>>16);
        }
    }
    while(n--){
        i=(unsigned)((lo^*p++)&0xff);
        lo=(uint16_t)(((lo>>8)|(hi<<8))^crc_lo[i]);
        hi=(uint16_t)((hi>>8)^crc_hi[i]);
    }
    return ((uint32_t)hi<<16)|lo;
}
uint32_t crc32(const uint8_t *p, size_t n){ return ~crc32_update(0xfffffffful,p,n); }
uint32_t day_index(Date d){
    uint32_t n=0; unsigned y,m;
    for(y=2000;y<d.year;++y) n+=leap((uint16_t)y)?366u:365u;
    for(m=1;m<d.month;++m) n+=days_in_month(d.year,(uint8_t)m);
    return n+d.day;
}
unsigned weekday(Date d){
    static const unsigned offsets[12]={0,3,2,5,0,3,5,1,4,6,2,4};
    unsigned y=d.year; if(d.month<3) --y;
    return (y+y/4-y/100+y/400+offsets[d.month-1]+d.day)%7;
}
/* ISO 8601 week: weeks start on Monday; week 1 holds the year's first Thursday.
 * Same rule as PicoCalc v1.12 (LPS-112-CHG03). */
static unsigned jan1_weekday(unsigned y){ unsigned p=y-1; return (p+1+p/4-p/100+p/400)%7; } /* 0=Sunday */
static unsigned iso_weeks_in(unsigned y){ unsigned j=jan1_weekday(y); return (j==4 || (leap((uint16_t)y) && j==3))?53u:52u; }
unsigned iso_week(Date d){
    unsigned ordinal=d.day, m, iso; int week;
    for(m=1;m<d.month;++m) ordinal+=days_in_month(d.year,(uint8_t)m);
    iso=(weekday(d)+6)%7+1;
    week=((int)ordinal-(int)iso+10)/7;
    if(week<1) return iso_weeks_in(d.year-1u);
    return (unsigned)week>iso_weeks_in(d.year)?1u:(unsigned)week;
}
/* Line 1 of every screen: "Sunday 8 November 2026 (week 45)".
 * French accents are ASCII-folded until CP437 output lands (LPS-DOS-115-CHG03). */
void long_date_text(Date d, int language, char *out, size_t n){
    static const char *en_days[7]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    static const char *fr_days[7]={"Dimanche","Lundi","Mardi","Mercredi","Jeudi","Vendredi","Samedi"};
    static const char *en_months[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};
    static const char *fr_months[12]={"Janvier","Février","Mars","Avril","Mai","Juin","Juillet","Août","Septembre","Octobre","Novembre","Décembre"};
    int fr=language==LANG_FR; unsigned w=weekday(d);
    (void)n; /* longest: "Wednesday 30 September 2099 (week 40)" = 38 chars */
    sprintf(out, fr?"%s %u %s %u (semaine %u)":"%s %u %s %u (week %u)",
        fr?fr_days[w]:en_days[w], (unsigned)d.day, fr?fr_months[d.month-1]:en_months[d.month-1], (unsigned)d.year, iso_week(d));
}
void state_init(State *s){
    unsigned i;
    memset(s,0,sizeof(*s));
    default_activities(s);
    s->language=LANG_EN; s->palette=PAL_GREEN;
    s->today.number=1; s->today.date=DEFAULT_DATE;
    s->today.expected=-1; s->today.actual=-1;
    /* Companion default-constructs every history Day with number=1; decode requires number>0. */
    for(i=0;i<HISTORY_LENGTH;i++){
        s->history[i].number=1;
        s->history[i].expected=-1;
        s->history[i].actual=-1;
    }
}

#ifndef LPS_DOS
/* Binary format 6 (LPS-DOS-115-CHG04: the app writes JSON; tests build old saves). */
static void put_n(Blob *b, uint32_t v, unsigned n){
    while(n--){ b->bytes[b->size++]=(uint8_t)v; v>>=8; }
}
static void put_day(Blob *b, const Day *d){
    unsigned i;
    put_n(b,d->date.year,2); put_n(b,d->date.month,1); put_n(b,d->date.day,1);
    put_n(b,d->number,4); put_n(b,d->flags&LEGACY_ACTIVITY_MASK,2);
    put_n(b, d->expected<0 ? 0xfffffffful : (uint32_t)d->expected, 4);
    put_n(b, d->actual<0 ? 0xfffffffful : (uint32_t)d->actual, 4);
    for(i=0;i<=NOTE_LENGTH;i++) put_n(b,(uint8_t)d->note[i],1);
}
void encode_state(const State *s, Blob *b){
    unsigned i,j; uint32_t crc;
    b->size=0;
    put_n(b,WIRE_MAGIC,4); put_n(b,6,2); put_n(b,s->xp,4); put_n(b,s->next,1); put_n(b,s->count,1);
    put_n(b,s->language,1); put_n(b,s->palette,1);
    put_day(b,&s->today);
    for(i=0;i<HISTORY_LENGTH;i++) put_day(b,&s->history[i]);
    put_n(b,s->taskCount,1);
    for(i=0;i<TASK_CAPACITY;i++){ put_n(b,s->tasks[i].status,1); for(j=0;j<=TASK_NAME_LENGTH;j++) put_n(b,(uint8_t)s->tasks[i].name[j],1); }
    put_n(b,s->reminderCount,1);
    for(i=0;i<REMINDER_CAPACITY;i++){
        put_n(b,s->reminders[i].from.year,2); put_n(b,s->reminders[i].from.month,1); put_n(b,s->reminders[i].from.day,1);
        put_n(b,s->reminders[i].duration,2);
        for(j=0;j<=REMINDER_TEXT_LENGTH;j++) put_n(b,(uint8_t)s->reminders[i].text[j],1);
    }
    crc=crc32(b->bytes,b->size); put_n(b,crc,4);
}
#endif

static uint32_t get_n(const uint8_t *bytes, size_t *pos, unsigned n){
    uint32_t v=0; unsigned i; for(i=0;i<n;i++) v|=(uint32_t)bytes[(*pos)++]<<(i*8); return v;
}
int decode_state(const uint8_t *bytes, size_t size, State *out){
    size_t pos; uint32_t version; int valid; unsigned i,j;
    State *s = &lps_decode_scratch;
    if(size!=WIRE_SIZE && size!=WIRE_V5 && size!=WIRE_V4 && size!=WIRE_V3 && size!=WIRE_V2 && size!=WIRE_V1) return 0;
    pos=size-4;
    if(get_n(bytes,&pos,4)!=crc32(bytes,size-4)) return 0;
    pos=0; if(get_n(bytes,&pos,4)!=WIRE_MAGIC) return 0;
    version=get_n(bytes,&pos,2);
    if(!((version==1&&size==WIRE_V1)||(version==2&&size==WIRE_V2)||(version==3&&size==WIRE_V3)
        ||(version==4&&size==WIRE_V4)||(version==5&&size==WIRE_V5)||(version==6&&size==WIRE_SIZE))) return 0;
    memset(s,0,sizeof(*s));
    default_activities(s); /* 16 bits per day: the first 16 default activities */
    s->today.expected=s->today.actual=-1;
    for(i=0;i<HISTORY_LENGTH;i++){ s->history[i].expected=-1; s->history[i].actual=-1; }
    s->xp=get_n(bytes,&pos,4); s->next=(uint8_t)get_n(bytes,&pos,1); s->count=(uint8_t)get_n(bytes,&pos,1);
    if(version>=2){ uint32_t language=get_n(bytes,&pos,1); if(language>1) return 0; s->language=(uint8_t)language; }
    if(version>=4){ uint32_t palette=get_n(bytes,&pos,1); if(palette>4) return 0; s->palette=(uint8_t)palette; }
    else s->palette=PAL_GREEN;
    valid = s->next<HISTORY_LENGTH && s->count<=HISTORY_LENGTH;
    {
        int di;
        for(di=-1; di<(int)HISTORY_LENGTH; di++){
            Day *d = (di<0)? &s->today : &s->history[di];
            uint32_t wv;
            d->date.year=0; d->date.month=0; d->date.day=0;
            if(version>=3){ d->date.year=(uint16_t)get_n(bytes,&pos,2); d->date.month=(uint8_t)get_n(bytes,&pos,1); d->date.day=(uint8_t)get_n(bytes,&pos,1); }
            d->number=get_n(bytes,&pos,4); d->flags=(uint16_t)get_n(bytes,&pos,2);
            wv=get_n(bytes,&pos,4);
            if(wv==0xfffffffful) d->expected=-1; else if(wv==0||wv>999999ul){ valid=0; d->expected=-1; } else d->expected=(int32_t)wv;
            wv=get_n(bytes,&pos,4);
            if(wv==0xfffffffful) d->actual=-1; else if(wv==0||wv>999999ul){ valid=0; d->actual=-1; } else d->actual=(int32_t)wv;
            for(j=0;j<=NOTE_LENGTH;j++) d->note[j]=(char)get_n(bytes,&pos,1);
            valid = valid && d->number>0 && (d->flags & ~(uint32_t)LEGACY_ACTIVITY_MASK)==0 && d->note[NOTE_LENGTH]==0
                && (d->date.year==0 || valid_date(d->date));
            { int ended=0; for(j=0;j<=NOTE_LENGTH;j++){ char c=d->note[j]; if(!c) ended=1; else if(!ended && (c<32||c>126)) valid=0; } }
        }
    }
    if(version>=5){
        s->taskCount=(uint8_t)get_n(bytes,&pos,1); valid=valid && s->taskCount<=TASK_CAPACITY;
        for(i=0;i<TASK_CAPACITY;i++){
            uint32_t status=get_n(bytes,&pos,1); if(status>2) valid=0; s->tasks[i].status=(uint8_t)status;
            for(j=0;j<=TASK_NAME_LENGTH;j++) s->tasks[i].name[j]=(char)get_n(bytes,&pos,1);
            valid=valid && s->tasks[i].name[TASK_NAME_LENGTH]==0;
            { int ended=0; for(j=0;j<=TASK_NAME_LENGTH;j++){ char c=s->tasks[i].name[j]; if(!c) ended=1; else if(!ended && (c<32||c>126)) valid=0; } }
            if(i<s->taskCount && s->tasks[i].name[0]==0) valid=0;
        }
    }
    if(version>=6){
        s->reminderCount=(uint8_t)get_n(bytes,&pos,1); valid=valid && s->reminderCount<=REMINDER_CAPACITY;
        for(i=0;i<REMINDER_CAPACITY;i++){
            Reminder *r=&s->reminders[i];
            r->from.year=(uint16_t)get_n(bytes,&pos,2); r->from.month=(uint8_t)get_n(bytes,&pos,1); r->from.day=(uint8_t)get_n(bytes,&pos,1);
            r->duration=(uint16_t)get_n(bytes,&pos,2);
            for(j=0;j<=REMINDER_TEXT_LENGTH;j++) r->text[j]=(char)get_n(bytes,&pos,1);
            valid=valid && r->text[REMINDER_TEXT_LENGTH]==0;
            { int ended=0; for(j=0;j<=REMINDER_TEXT_LENGTH;j++){ char c=r->text[j]; if(!c) ended=1; else if(!ended && (c<32||c>126)) valid=0; } }
            if(i<s->reminderCount) valid=valid && valid_date(r->from) && r->duration>=1 && r->duration<=365 && r->text[0]!=0;
        }
    }
    if(!valid) return 0;
    if(out!=s) *out=*s;
    return 1;
}
