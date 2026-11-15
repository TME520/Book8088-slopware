/* jsave.c - JSON save files SAVE_A.JSN / SAVE_B.JSN (LPS-DOS-115-CHG04).
 *
 * Same file as PicoCalc LPS Companion v1.15 (schema 2, src/lps_companion.cpp
 * writeSave/readSave), byte for byte: same keys in the same order, history
 * oldest first, and "crc32" last, the CRC-32 (IEEE, lowercase hex) of every
 * byte before the "crc32" key. A save can go from the PicoCalc SD card to
 * C:\LPS\ and back.
 *
 *   {"format":"lps-companion","version":2,"generation":N,
 *   "state":{"language":"en","palette":"green","xp":0,
 *   "activities":[{"id":1,"fr":"Marche","en":"Walk"},...],
 *   "today":{DAY},"history":[DAY,...],"kanban":[TASK,...],"reminders":[REMINDER,...],
 *   "meals":{"played":[3,0,...]},"homeHidden":["kanban",...]},
 *   "crc32":"xxxxxxxx"}
 *   DAY {"date":"YYYY-MM-DD"|null,"number":N,"activities":["Marche",...],
 *        "expected":grams|null,"actual":grams|null,"note":"..."}
 *
 * The DOS port has no activity editor, Meal Planner or home menu settings, so
 * what it does not use is kept here, beside the State, and written back as
 * read: the activity catalog, the names a day ticks that are not among the 16
 * DOS activities (PicoCalc extras, custom or renamed ones), the Meal Planner
 * deck ("meals") and the hidden home entries ("homeHidden"). Unknown keys are
 * skipped, as on the PicoCalc.
 *
 * Text in the file is UTF-8; the State holds CP437 (LPS-DOS-115-CHG03). Notes,
 * task names and reminder texts are converted at this boundary; a character
 * CP437 lacks becomes '?'. Activity names stay UTF-8 here.
 *
 * 8088: the whole file is read into one far block, ended by a NUL, and
 * parsed in one pass by a small pull tokenizer (no tree, no recursion, a few
 * hundred bytes of stack). The CRC-32 (state.c, 1 KB table) runs once over
 * the bytes; the writer fills a 512-byte buffer and folds each chunk into the
 * CRC as it goes to disk. Numbers and dates are formatted with 16-bit
 * arithmetic where they fit: printf and 32-bit division are slow on 8088. */
#include "lps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCHEMA_VERSION 2
#define CATALOG_CAPACITY 30   /* PicoCalc ActivityCapacity */
#define NAME_LENGTH 24        /* PicoCalc ActivityNameLength (UTF-8 bytes) */
#define DAY_NAMES 30          /* ticked names per day */
#define TEXT_CAPACITY 128     /* PicoCalc SaveTextCapacity */
#define SAVE_MAX_BYTES 65000L /* one far block; the PicoCalc accepts 65536 */
#define MEAL_CARDS 16
#define HOME_ENTRIES 9
#define HOME_LOCKED ((1u<<0)|(1u<<(HOME_ENTRIES-1))) /* Date, Close the day */
#define DAY_SLOTS (HISTORY_LENGTH+1)                /* today + history */
#define WRITE_CHUNK 512
#define DEFAULT_ACTIVITIES (ACTIVITY_COUNT+6)
/* Hot tables in DGROUP (near, zero-filled at start-up) rather than behind a far pointer. */
#ifdef __WATCOMC__
#define NEARDATA __near
#else
#define NEARDATA
#endif

typedef struct { uint16_t id; char fr[NAME_LENGTH+1], en[NAME_LENGTH+1]; } CatalogEntry;
/* The names a day ticks, as read, keyed by the day number (the date can
 * still be edited on DOS). The 16 DOS activities among them also live in
 * Day.flags, which wins; the list keeps the others and the PicoCalc order. */
typedef struct { uint32_t number; uint8_t count, used; char names[DAY_NAMES][NAME_LENGTH+1]; } DayNames;
typedef struct {
    char known[ACTIVITY_COUNT][NAME_LENGTH+1]; /* DOS activities, French, UTF-8 */
    CatalogEntry defaults[DEFAULT_ACTIVITIES]; /* PicoCalc default catalog, UTF-8 */
    uint8_t catalogCount;
    uint16_t nextId;
    CatalogEntry catalog[CATALOG_CAPACITY];
    uint8_t mealCount;
    uint8_t meals[MEAL_CARDS];
    uint16_t homeHidden;
    DayNames days[DAY_SLOTS];
} SaveExtras;

static SaveExtras *X;
static char NEARDATA wbuf[WRITE_CHUNK];

static const char *const LANGUAGES[2]={"en","fr"};
static const char *const PALETTES[5]={"red","green","blue","orange","pink"};
static const char *const STATUSES[3]={"todo","doing","done"};
static const char *const HOME_IDS[HOME_ENTRIES]={"date","activities","weight","kanban","rosary","daily_review","reminders","timer","close_day"};
/* PicoCalc default catalog entries 17-22 (the first 16 are the DOS list). */
static const char *const MORE_FR[6]={"Sport","Cuisine","Ménage","Famille","Amis","Méditation"};
static const char *const MORE_EN[6]={"Sport","Cooking","Chores","Family","Friends","Meditation"};

/* CP437 literal (the build rewrote it) back to UTF-8. */
static void utf8_of(const char *s, char *out, size_t cap){
    size_t n=0; char u[3];
    for(;*s;s++){ size_t k=cp437_to_utf8((unsigned char)*s,u); if(n+k>=cap) break; memcpy(out+n,u,k); n+=k; }
    out[n]=0;
}
void jsave_reset(void){
    unsigned i;
    if(!jsave_init()) return;
    memset(X->catalog,0,sizeof(X->catalog));
    memcpy(X->catalog,X->defaults,sizeof(X->defaults));
    X->catalogCount=DEFAULT_ACTIVITIES; X->nextId=DEFAULT_ACTIVITIES+1;
    X->mealCount=0; X->homeHidden=0;
    for(i=0;i<DAY_SLOTS;i++){ X->days[i].number=0; X->days[i].count=0; }
}
int jsave_init(void){
    unsigned i;
    if(X) return 1;
    X=(SaveExtras*)calloc(1,sizeof(SaveExtras));
    if(!X) return 0;
    for(i=0;i<ACTIVITY_COUNT;i++) utf8_of(ACTIVITIES_FR[i],X->known[i],NAME_LENGTH+1);
    for(i=0;i<DEFAULT_ACTIVITIES;i++){
        X->defaults[i].id=(uint16_t)(i+1);
        utf8_of(i<ACTIVITY_COUNT?ACTIVITIES_FR[i]:MORE_FR[i-ACTIVITY_COUNT],X->defaults[i].fr,NAME_LENGTH+1);
        utf8_of(i<ACTIVITY_COUNT?ACTIVITIES_EN[i]:MORE_EN[i-ACTIVITY_COUNT],X->defaults[i].en,NAME_LENGTH+1);
    }
    jsave_reset();
    return 1;
}
/* Closing the day deals a new Meal Planner deck, as on the PicoCalc. */
void jsave_new_day(void){ if(X) X->mealCount=0; }

static int known_index(const char *name){
    unsigned i;
    for(i=0;i<ACTIVITY_COUNT;i++) if(X->known[i][0]==name[0]&&!strcmp(X->known[i],name)) return (int)i;
    return -1;
}

/* ---- Reader ------------------------------------------------------------ */
enum { T_END, T_ERROR, T_OBJ, T_OBJ_END, T_ARR, T_ARR_END, T_COLON, T_COMMA, T_STRING, T_NUMBER, T_TRUE, T_FALSE, T_NULL };
static struct {
    const unsigned char *p, *end, *tok, *key; /* tok: current token; key: last object key */
    char text[TEXT_CAPACITY+1];
    unsigned len;
    int overflow;   /* string longer than TEXT_CAPACITY */
    uint32_t number;
    int integer;    /* number is a whole value in 0..4294967295 */
} J;

/* The buffer ends with a NUL at J.end, so a scan stops there without a
 * bounds check: NUL is no token, and a control character inside a string. */
static int digit(int c){ return c>='0'&&c<='9'; }
static int hex4(const unsigned char **pp, unsigned long *v){
    const unsigned char *p=*pp; int i; unsigned c, w=0;
    for(i=0;i<4;i++){
        c=*p++; w<<=4;
        if(c>='0'&&c<='9') w|=c-'0'; else if(c>='a'&&c<='f') w|=c-'a'+10; else if(c>='A'&&c<='F') w|=c-'A'+10;
        else return 0;
    }
    *pp=p; *v=w; return 1;
}
static int string_token(void){
    const unsigned char *p=J.p; unsigned c, len=0; unsigned long cp, low; char *text=J.text;
    J.overflow=0;
    for(;;){
        c=*p++;
        if(c=='"') break;
        if(c<0x20) goto bad; /* raw control character, or the end */
        if(c=='\\'){
            switch(c=*p++){
            case '"': case '\\': case '/': break;
            case 'b': c=8; break; case 'f': c=12; break; case 'n': c=10; break;
            case 'r': c=13; break; case 't': c=9; break;
            case 'u':
                if(!hex4(&p,&cp)) goto bad;
                if(cp>=0xd800ul&&cp<=0xdbfful){
                    if(*p!='\\'||p[1]!='u') goto bad;
                    p+=2;
                    if(!hex4(&p,&low)||low<0xdc00ul||low>0xdffful) goto bad;
                    cp=0x10000ul+((cp-0xd800ul)<<10)+(low-0xdc00ul);
                }else if((cp>=0xdc00ul&&cp<=0xdffful)||cp==0) goto bad;
                if(cp<0x80){ c=(unsigned)cp; break; }
                {   unsigned char u[4]; unsigned n, i;
                    if(cp<0x800){ u[0]=(unsigned char)(0xc0|(cp>>6)); u[1]=(unsigned char)(0x80|(cp&0x3f)); n=2; }
                    else if(cp<0x10000ul){ u[0]=(unsigned char)(0xe0|(cp>>12)); u[1]=(unsigned char)(0x80|((cp>>6)&0x3f)); u[2]=(unsigned char)(0x80|(cp&0x3f)); n=3; }
                    else { u[0]=(unsigned char)(0xf0|(cp>>18)); u[1]=(unsigned char)(0x80|((cp>>12)&0x3f)); u[2]=(unsigned char)(0x80|((cp>>6)&0x3f)); u[3]=(unsigned char)(0x80|(cp&0x3f)); n=4; }
                    for(i=0;i<n;i++){ if(len<TEXT_CAPACITY) text[len++]=(char)u[i]; else J.overflow=1; }
                }
                continue;
            default: goto bad;
            }
        }
        if(len<TEXT_CAPACITY) text[len++]=(char)c; else J.overflow=1;
    }
    J.p=p; J.len=len; text[len]=0;
    return T_STRING;
bad: /* the parse stops here; never step past the end mark */
    J.p=J.end; J.len=len; text[len]=0;
    return T_ERROR;
}
static int number_token(int c){
    const unsigned char *p=J.p; int negative=c=='-', big=0; unsigned w; uint32_t v; unsigned d;
    if(negative){ if(!digit(*p)){ J.p=J.end; return T_ERROR; } c=*p++; }
    w=(unsigned)(c-'0');
    while(digit(*p)&&w<6553u) w=w*10u+(unsigned)(*p++-'0'); /* 16-bit while it fits */
    v=w;
    while(digit(*p)){
        d=(unsigned)(*p++-'0');
        if(v>429496729ul||(v==429496729ul&&d>5)) big=1; else v=v*10u+d;
    }
    J.integer=!big && !(negative&&v);
    if(*p=='.'){ p++; J.integer=0; if(!digit(*p)){ J.p=J.end; return T_ERROR; } while(digit(*p)) p++; }
    if(*p=='e'||*p=='E'){
        p++; J.integer=0; if(*p=='+'||*p=='-') p++;
        if(!digit(*p)){ J.p=J.end; return T_ERROR; }
        while(digit(*p)) p++;
    }
    J.p=p; J.number=v; return T_NUMBER;
}
static int literal(const char *rest, int token){
    const unsigned char *p=J.p;
    for(;*rest;rest++,p++) if(*p!=(unsigned char)*rest){ J.p=J.end; return T_ERROR; }
    J.p=p; return token;
}
static int next(void){
    const unsigned char *p=J.p; int c;
    while((c=*p)==' '||c=='\n'||c=='\r'||c=='\t') p++;
    J.tok=p;
    if(p>=J.end){ J.p=p; return T_END; }
    J.p=p+1;
    switch(c){
    case '{': return T_OBJ; case '}': return T_OBJ_END;
    case '[': return T_ARR; case ']': return T_ARR_END;
    case ':': return T_COLON; case ',': return T_COMMA;
    case '"': return string_token();
    case 't': return literal("rue",T_TRUE);
    case 'f': return literal("alse",T_FALSE);
    case 'n': return literal("ull",T_NULL);
    default:
        if(c=='-'||digit(c)) return number_token(c);
        J.p=J.end; return T_ERROR;
    }
}
/* Skip one whole value (unknown key). Nesting is checked up to 32 levels. */
static int skip(int first){
    uint32_t objects; unsigned depth=1; int t;
    if(first==T_STRING||first==T_NUMBER||first==T_TRUE||first==T_FALSE||first==T_NULL) return 1;
    if(first!=T_OBJ&&first!=T_ARR) return 0;
    objects=first==T_OBJ?1u:0u;
    while(depth){
        t=next();
        if(t==T_ERROR||t==T_END) return 0;
        if(t==T_OBJ||t==T_ARR){
            if(depth==32) return 0;
            objects=(objects<<1)|(t==T_OBJ?1u:0u); depth++;
        }else if(t==T_OBJ_END||t==T_ARR_END){
            if(((objects&1u)!=0)!=(t==T_OBJ_END)) return 0;
            objects>>=1; depth--;
        }
    }
    return 1;
}
/* Next member of an object whose '{' was read: 1 with key set and the colon
 * read, 0 at '}', -1 on error. Keys longer than 23 bytes read as "". */
static int member(int *first, char *key){
    int t=next();
    if(*first){ *first=0; if(t==T_OBJ_END) return 0; }
    else { if(t==T_OBJ_END) return 0; if(t!=T_COMMA) return -1; t=next(); }
    if(t!=T_STRING) return -1;
    key[0]=0; if(!J.overflow&&J.len<24) memcpy(key,J.text,J.len+1);
    J.key=J.tok;
    return next()==T_COLON ? 1 : -1;
}
/* Next element of an array whose '[' was read: 1 with *t its first token, 0 at ']', -1 on error. */
static int element(int *first, int *t){
    *t=next();
    if(*first){ *first=0; return *t==T_ARR_END ? 0 : 1; }
    if(*t==T_ARR_END) return 0;
    if(*t!=T_COMMA) return -1;
    *t=next(); return 1;
}
static int read_number(uint32_t min, uint32_t max){
    return next()==T_NUMBER && J.integer && J.number>=min && J.number<=max;
}
/* A stored text: shorter than cap bytes, no control characters. */
static int read_text(unsigned cap, int required){
    unsigned i;
    if(next()!=T_STRING||J.overflow||J.len>=cap||(required&&!J.len)) return 0;
    for(i=0;i<J.len;i++){ unsigned char c=(unsigned char)J.text[i]; if(c<32||c==127) return 0; }
    return 1;
}
/* J.text (UTF-8) to CP437; the result is never longer. */
static void text_to_cp437(char *out){
    const unsigned char *s=(const unsigned char*)J.text, *end=s+J.len; char *o=out;
    while(s<end){
        unsigned char c=*s; unsigned long cp; unsigned need,i; int ok=1, m;
        if(c<0x80){ *o++=(char)c; s++; continue; }
        if((c&0xe0)==0xc0){ cp=c&0x1f; need=1; } else if((c&0xf0)==0xe0){ cp=c&0x0f; need=2; }
        else if((c&0xf8)==0xf0){ cp=c&0x07; need=3; } else { *o++='?'; s++; continue; }
        for(i=1;i<=need;i++){ if(s+i>=end||(s[i]&0xc0)!=0x80){ ok=0; break; } cp=(cp<<6)|(s[i]&0x3f); }
        if(!ok){ *o++='?'; s++; continue; }
        s+=need+1;
        m=unicode_to_cp437(cp);
        *o++=(char)(m>=0x80 ? m : '?');
    }
    *o=0;
}
static int read_date(Date *d, int allow_null){
    int t=next();
    if(t==T_NULL&&allow_null){ d->year=0; d->month=0; d->day=0; return 1; }
    return t==T_STRING&&!J.overflow&&parse_date(J.text,d);
}
static int read_weight(int32_t *grams){
    int t=next();
    if(t==T_NULL){ *grams=-1; return 1; }
    if(t!=T_NUMBER||!J.integer||J.number<1||J.number>999999ul) return 0;
    *grams=(int32_t)J.number; return 1;
}
static int read_choice(const char *const *names, unsigned count, uint8_t *out){
    unsigned i;
    if(next()!=T_STRING||J.overflow) return 0;
    for(i=0;i<count;i++) if(!strcmp(J.text,names[i])){ *out=(uint8_t)i; return 1; }
    return 0;
}
static int read_day(int first, Day *d, DayNames *e){
    char key[24]; int st=1, r, t;
    if(first!=T_OBJ) return 0;
    memset(d,0,sizeof(*d)); d->number=1; d->expected=-1; d->actual=-1;
    e->count=0;
    while((r=member(&st,key))>0){
        if(!strcmp(key,"date")){ if(!read_date(&d->date,1)) return 0; }
        else if(!strcmp(key,"number")){ if(!read_number(1,0xfffffffful)) return 0; d->number=J.number; }
        else if(!strcmp(key,"activities")){
            int ast=1; unsigned named=0;
            if(next()!=T_ARR) return 0;
            d->flags=0; e->count=0;
            while((r=element(&ast,&t))>0){
                int k;
                if(t!=T_STRING||J.overflow||J.len>NAME_LENGTH||named>=DAY_NAMES) return 0;
                named++;
                if((k=known_index(J.text))>=0) d->flags|=(uint16_t)(1u<<k);
                memcpy(e->names[e->count++],J.text,J.len+1);
            }
            if(r<0) return 0;
        }
        else if(!strcmp(key,"expected")){ if(!read_weight(&d->expected)) return 0; }
        else if(!strcmp(key,"actual")){ if(!read_weight(&d->actual)) return 0; }
        else if(!strcmp(key,"note")){ if(!read_text(NOTE_LENGTH+1,0)) return 0; text_to_cp437(d->note); }
        else if(!skip(next())) return 0;
    }
    e->number=d->number;
    return r==0;
}
static int read_catalog(void){
    int st=1, r, t; char key[24];
    if(next()!=T_ARR) return 0;
    memset(X->catalog,0,sizeof(X->catalog)); X->catalogCount=0; X->nextId=1;
    while((r=element(&st,&t))>0){
        CatalogEntry *a; int ms=1; unsigned i;
        if(t!=T_OBJ||X->catalogCount>=CATALOG_CAPACITY) return 0;
        a=&X->catalog[X->catalogCount];
        while((r=member(&ms,key))>0){
            if(!strcmp(key,"id")){ if(!read_number(1,65535ul)) return 0; a->id=(uint16_t)J.number; }
            else if(!strcmp(key,"fr")){ if(!read_text(NAME_LENGTH+1,0)) return 0; memcpy(a->fr,J.text,J.len+1); }
            else if(!strcmp(key,"en")){ if(!read_text(NAME_LENGTH+1,0)) return 0; memcpy(a->en,J.text,J.len+1); }
            else if(!skip(next())) return 0;
        }
        if(r<0||(!a->fr[0]&&!a->en[0])) return 0;
        if(!a->id) a->id=X->nextId;
        for(i=0;i<X->catalogCount;i++) if(X->catalog[i].id==a->id) return 0;
        if(a->id>=X->nextId) X->nextId=(uint16_t)(a->id+1);
        X->catalogCount++;
    }
    return r==0;
}
static int read_state(State *s){
    char key[24]; int st=1, r, t;
    if(next()!=T_OBJ) return 0;
    while((r=member(&st,key))>0){
        if(!strcmp(key,"language")){ if(!read_choice(LANGUAGES,2,&s->language)) return 0; }
        else if(!strcmp(key,"palette")){ if(!read_choice(PALETTES,5,&s->palette)) return 0; }
        else if(!strcmp(key,"xp")){ if(!read_number(0,0xfffffffful)) return 0; s->xp=J.number; }
        else if(!strcmp(key,"activities")){ if(!read_catalog()) return 0; }
        else if(!strcmp(key,"today")){ if(!read_day(next(),&s->today,&X->days[0])) return 0; }
        else if(!strcmp(key,"history")){
            int ast=1;
            if(next()!=T_ARR) return 0;
            s->count=0;
            while((r=element(&ast,&t))>0){
                if(s->count>=HISTORY_LENGTH||!read_day(t,&s->history[s->count],&X->days[1+s->count])) return 0;
                s->count++;
            }
            s->next=(uint8_t)(s->count%HISTORY_LENGTH);
            if(r<0) return 0;
        }
        else if(!strcmp(key,"kanban")){
            int ast=1;
            if(next()!=T_ARR) return 0;
            s->taskCount=0;
            while((r=element(&ast,&t))>0){
                Task *task; int ms=1, named=0;
                if(t!=T_OBJ||s->taskCount>=TASK_CAPACITY) return 0;
                task=&s->tasks[s->taskCount]; memset(task,0,sizeof(*task));
                while((r=member(&ms,key))>0){
                    if(!strcmp(key,"status")){ if(!read_choice(STATUSES,3,&task->status)) return 0; }
                    else if(!strcmp(key,"name")){ if(!read_text(TASK_NAME_LENGTH+1,1)) return 0; text_to_cp437(task->name); named=1; }
                    else if(!skip(next())) return 0;
                }
                if(r<0||!named) return 0;
                s->taskCount++;
            }
            if(r<0) return 0;
        }
        else if(!strcmp(key,"reminders")){
            int ast=1;
            if(next()!=T_ARR) return 0;
            s->reminderCount=0;
            while((r=element(&ast,&t))>0){
                Reminder *rem; int ms=1, dated=0, texted=0;
                if(t!=T_OBJ||s->reminderCount>=REMINDER_CAPACITY) return 0;
                rem=&s->reminders[s->reminderCount]; memset(rem,0,sizeof(*rem)); rem->duration=1;
                while((r=member(&ms,key))>0){
                    if(!strcmp(key,"from")){ if(!read_date(&rem->from,0)) return 0; dated=1; }
                    else if(!strcmp(key,"days")){ if(!read_number(1,365)) return 0; rem->duration=(uint16_t)J.number; }
                    else if(!strcmp(key,"text")){ if(!read_text(REMINDER_TEXT_LENGTH+1,1)) return 0; text_to_cp437(rem->text); texted=1; }
                    else if(!skip(next())) return 0;
                }
                if(r<0||!dated||!texted) return 0;
                s->reminderCount++;
            }
            if(r<0) return 0;
        }
        else if(!strcmp(key,"meals")){
            int ms=1;
            if(next()!=T_OBJ) return 0;
            X->mealCount=0;
            while((r=member(&ms,key))>0){
                int ast=1;
                if(strcmp(key,"played")){ if(!skip(next())) return 0; continue; }
                if(next()!=T_ARR) return 0;
                X->mealCount=0;
                while((r=element(&ast,&t))>0){
                    unsigned i;
                    if(t!=T_NUMBER||!J.integer||J.number>=MEAL_CARDS) return 0;
                    for(i=0;i<X->mealCount;i++) if(X->meals[i]==J.number) return 0; /* each card once */
                    X->meals[X->mealCount++]=(uint8_t)J.number;
                }
                if(r<0) return 0;
            }
            if(r<0) return 0;
        }
        else if(!strcmp(key,"homeHidden")){
            int ast=1;
            if(next()!=T_ARR) return 0;
            X->homeHidden=0;
            while((r=element(&ast,&t))>0){
                unsigned i;
                if(t!=T_STRING) return 0;
                for(i=0;i<HOME_ENTRIES;i++)
                    if(!J.overflow&&!strcmp(J.text,HOME_IDS[i])) X->homeHidden|=(uint16_t)((1u<<i)&~HOME_LOCKED);
            }
            if(r<0) return 0;
        }
        else if(!skip(next())) return 0;
    }
    return r==0;
}
static enum SaveCheck parse(const unsigned char *buf, size_t size, State *out, uint32_t *generation, uint32_t *checksum){
    char key[24]; int st=1, r=-1, formatOk=0, versionSeen=0, unsupported=0, generationSeen=0, stateSeen=0, crcSeen=0, parsed;
    uint32_t stored=0, computed=0, gen=0; unsigned i;
    state_init(out); jsave_reset();
    J.p=buf; J.end=buf+size;
    if(next()==T_OBJ) while((r=member(&st,key))>0){
        if(crcSeen){ r=-1; break; } /* crc32 must be last: nothing after it is unchecked */
        if(!strcmp(key,"format")){
            if(next()!=T_STRING||J.overflow||strcmp(J.text,"lps-companion")){ r=-1; break; }
            formatOk=1;
        }else if(!strcmp(key,"version")){
            if(!read_number(1,0xfffffffful)){ r=-1; break; }
            versionSeen=1; unsupported=J.number>SCHEMA_VERSION;
        }else if(!strcmp(key,"generation")){
            if(!read_number(0,0xfffffffful)){ r=-1; break; }
            generationSeen=1; gen=J.number;
        }else if(!strcmp(key,"state")){
            if(!formatOk||!versionSeen||stateSeen){ r=-1; break; }
            stateSeen=1;
            if(!(unsupported?skip(next()):read_state(out))){ r=-1; break; }
        }else if(!strcmp(key,"crc32")){
            computed=~crc32_update(0xfffffffful,buf,(size_t)(J.key-buf));
            if(next()!=T_STRING||J.len!=8){ r=-1; break; }
            for(i=0;i<8;i++){
                char c=J.text[i]; stored<<=4;
                if(c>='0'&&c<='9') stored|=(uint32_t)(c-'0'); else if(c>='a'&&c<='f') stored|=(uint32_t)(c-'a'+10);
                else break;
            }
            if(i<8){ r=-1; break; }
            crcSeen=1;
        }else if(!skip(next())){ r=-1; break; }
    }
    parsed=r==0&&next()==T_END;
    if(!parsed||!crcSeen||stored!=computed||!formatOk||!versionSeen||!generationSeen||!stateSeen) return SAVE_DAMAGED;
    *generation=gen; *checksum=stored;
    return unsupported?SAVE_UNSUPPORTED:SAVE_VALID;
}
/* The whole file in one far block, NUL-ended. */
static enum SaveCheck read_file(const char *path, unsigned char **buf, size_t *n){
    FILE *f; long size;
    *buf=0;
    f=fopen(path,"rb");
    if(!f) return SAVE_MISSING;
    if(fseek(f,0,SEEK_END)!=0||(size=ftell(f))<0||fseek(f,0,SEEK_SET)!=0){ fclose(f); return SAVE_IOERROR; }
    if(size>SAVE_MAX_BYTES){ fclose(f); return SAVE_DAMAGED; }
    /* One byte more for the NUL end mark (it also keeps the end pointer
     * inside the far block). */
    *buf=(unsigned char*)malloc((size_t)size+1);
    if(!*buf){ fclose(f); return SAVE_IOERROR; }
    if(fread(*buf,1,(size_t)size,f)!=(size_t)size){ free(*buf); *buf=0; fclose(f); return SAVE_IOERROR; }
    fclose(f);
    (*buf)[size]=0; *n=(size_t)size;
    return SAVE_VALID;
}
/* Reads one save file into out (state_init first; partial data on failure). */
enum SaveCheck jsave_read(const char *path, State *out, uint32_t *generation, uint32_t *checksum){
    unsigned char *buf; size_t n=0; enum SaveCheck r;
    if(!jsave_init()) return SAVE_IOERROR;
    if((r=read_file(path,&buf,&n))!=SAVE_VALID) return r;
    r=parse(buf,n,out,generation,checksum);
    free(buf);
    return r;
}
/* The first line as this program and the PicoCalc write it:
 *   {"format":"lps-companion","version":V,"generation":G,
 * Returns SAVE_VALID with V and G when it is there, SAVE_DAMAGED when not
 * (another writer, or damage: the caller then parses the whole file),
 * SAVE_MISSING or SAVE_IOERROR. Nothing is checked beyond the first line. */
#define HEADER "{\"format\":\"lps-companion\",\"version\":"
static int header_number(const char **p, char end, uint32_t *v){
    unsigned digits=0;
    *v=0;
    while(**p>='0'&&**p<='9'){ if(++digits>9) return 0; *v=*v*10u+(uint32_t)(*(*p)++-'0'); }
    return digits && *(*p)++==end;
}
enum SaveCheck jsave_peek(const char *path, uint32_t *version, uint32_t *generation){
    char head[80]; const char *p; size_t n; FILE *f=fopen(path,"rb");
    if(!f) return SAVE_MISSING;
    n=fread(head,1,sizeof(head)-1,f);
    if(ferror(f)){ fclose(f); return SAVE_IOERROR; }
    fclose(f);
    head[n]=0;
    if(strncmp(head,HEADER,sizeof(HEADER)-1)) return SAVE_DAMAGED;
    p=head+sizeof(HEADER)-1;
    if(!header_number(&p,',',version)||strncmp(p,"\"generation\":",13)) return SAVE_DAMAGED;
    p+=13;
    return header_number(&p,',',generation) ? SAVE_VALID : SAVE_DAMAGED;
}
/* Read-back after a write: the file on disk must be, byte for byte, the one
 * just written. It ends with "crc32":"xxxxxxxx"}\n; the CRC of every byte
 * before it, the stored value and the first line's generation must match.
 * Cheaper than a second parse on the 8088, as strict on what reached the disk. */
int jsave_verify(const char *path, uint32_t generation, uint32_t checksum){
    unsigned char *buf; size_t n=0; char tail[24]; uint32_t v=0, g=0; int ok;
    if(read_file(path,&buf,&n)!=SAVE_VALID) return 0;
    sprintf(tail,"\"crc32\":\"%08lx\"}\n",(unsigned long)checksum);
    ok=n>20 && !memcmp(buf+n-20,tail,20) && ~crc32_update(0xfffffffful,buf,n-20)==checksum;
    free(buf);
    return ok && jsave_peek(path,&v,&g)==SAVE_VALID && g==generation;
}

/* ---- Writer ------------------------------------------------------------ */
static struct { FILE *f; unsigned used; uint32_t crc; int ok; } W;

/* Each chunk is folded into the CRC on its way to disk. */
static void flush(void){
    W.crc=crc32_update(W.crc,(const unsigned char*)wbuf,W.used);
    if(W.used&&W.ok&&fwrite(wbuf,1,W.used,W.f)!=W.used) W.ok=0;
    W.used=0;
}
#define PUT(c) do{ if(W.used==WRITE_CHUNK) flush(); wbuf[W.used++]=(char)(c); }while(0)
static void put(unsigned char c){ PUT(c); }
static void raw(const char *s){ while(*s){ PUT(*s); s++; } }
/* One byte of a JSON string, escaped as the PicoCalc writer does. */
static void escaped(unsigned char c){
    static const char hex[]="0123456789abcdef";
    if(c>=0x20&&c!='"'&&c!='\\'){ PUT(c); return; }
    put('\\');
    if(c=='"'||c=='\\') put(c);
    else if(c=='\n') put('n'); else if(c=='\r') put('r'); else if(c=='\t') put('t');
    else { raw("u00"); put((unsigned char)hex[c>>4]); put((unsigned char)hex[c&15]); }
}
static void text(const char *s){
    unsigned char c;
    PUT('"');
    while((c=(unsigned char)*s++)!=0){ if(c>=0x20&&c!='"'&&c!='\\') PUT(c); else escaped(c); }
    PUT('"');
}
/* CP437 State text as UTF-8, at most cap bytes (the PicoCalc field size). */
static void text_cp437(const char *s, unsigned cap){
    char u[3]; unsigned used=0, k, i; unsigned char c;
    PUT('"');
    while((c=(unsigned char)*s++)!=0){
        if(c<0x80){ if(used+1>cap) break; used++; escaped(c); continue; }
        k=(unsigned)cp437_to_utf8(c,u);
        if(used+k>cap) break;
        used+=k;
        for(i=0;i<k;i++) PUT(u[i]);
    }
    PUT('"');
}
static void key(const char *k){ text(k); PUT(':'); }
/* Decimal with 16-bit division once the value fits. */
static void number(uint32_t v){
    char b[12]; unsigned i=11, w;
    b[11]=0;
    while(v>0xffffu){ b[--i]=(char)('0'+(unsigned)(v%10u)); v/=10u; }
    w=(unsigned)v;
    do{ b[--i]=(char)('0'+w%10u); w/=10u; }while(w);
    raw(b+i);
}
static void weight(int32_t grams){ if(grams<0) raw("null"); else number((uint32_t)grams); }
static void date(Date d){
    char b[13]; unsigned y=d.year;
    if(d.year==0){ raw("null"); return; }
    b[0]='"'; b[1]=(char)('0'+y/1000u); b[2]=(char)('0'+y/100u%10u); b[3]=(char)('0'+y/10u%10u); b[4]=(char)('0'+y%10u);
    b[5]='-'; b[6]=(char)('0'+d.month/10u); b[7]=(char)('0'+d.month%10u);
    b[8]='-'; b[9]=(char)('0'+d.day/10u); b[10]=(char)('0'+d.day%10u); b[11]='"'; b[12]=0;
    raw(b);
}
static const char *stored_name(const CatalogEntry *a){ return a->fr[0]?a->fr:a->en; }
static int in_catalog(const char *name){
    unsigned i; for(i=0;i<X->catalogCount;i++) if(!strcmp(stored_name(&X->catalog[i]),name)) return 1;
    return 0;
}
static int has_name(const DayNames *e, const char *name){
    unsigned i; for(i=0;i<e->count;i++) if(!strcmp(e->names[i],name)) return 1;
    return 0;
}
static void emit_name(const char *name, unsigned *n){
    if(*n>=DAY_NAMES) return;
    if((*n)++) put(',');
    text(name);
}
/* As the PicoCalc writes them: ticked names in catalog order, then the names
 * the catalog lacks in the order they were read, then new DOS ticks the
 * catalog lacks. A DOS activity is written only while its flag is set. */
static void write_day(const Day *d){
    DayNames *e=0; unsigned i, n=0; uint16_t in_cat=0, listed=0;
    for(i=0;i<DAY_SLOTS;i++) if(!X->days[i].used&&X->days[i].number==d->number){ e=&X->days[i]; e->used=1; break; }
    put('{'); key("date"); date(d->date); put(','); key("number"); number(d->number);
    put(','); key("activities"); put('[');
    for(i=0;i<X->catalogCount;i++){
        const char *name=stored_name(&X->catalog[i]); int k=known_index(name);
        if(k>=0){ in_cat|=(uint16_t)(1u<<k); if(d->flags&(1u<<k)) emit_name(name,&n); }
        else if(e&&has_name(e,name)) emit_name(name,&n);
    }
    if(e) for(i=0;i<e->count;i++) if(!in_catalog(e->names[i])){
        int k=known_index(e->names[i]);
        if(k<0) emit_name(e->names[i],&n);
        else { listed|=(uint16_t)(1u<<k); if(d->flags&(1u<<k)) emit_name(e->names[i],&n); }
    }
    for(i=0;i<ACTIVITY_COUNT;i++) if((d->flags&(1u<<i))&&!((in_cat|listed)&(1u<<i))) emit_name(X->known[i],&n);
    raw("],"); key("expected"); weight(d->expected); put(','); key("actual"); weight(d->actual);
    put(','); key("note"); text_cp437(d->note,NOTE_LENGTH); put('}');
}
int jsave_write(const char *path, const State *s, uint32_t generation, uint32_t *checksum){
    unsigned i, n; char hex[12]; int closed;
    if(!jsave_init()) return 0;
    W.f=fopen(path,"wb");
    if(!W.f) return 0;
    W.used=0; W.crc=0xfffffffful; W.ok=1;
    for(i=0;i<DAY_SLOTS;i++) X->days[i].used=0;
    put('{'); key("format"); text("lps-companion"); put(','); key("version"); number(SCHEMA_VERSION);
    put(','); key("generation"); number(generation); raw(",\n");
    key("state"); put('{'); key("language"); text(LANGUAGES[s->language&1]);
    put(','); key("palette"); text(PALETTES[s->palette<5?s->palette:PAL_GREEN]); put(','); key("xp"); number(s->xp);
    raw(",\n"); key("activities"); put('[');
    for(i=0;i<X->catalogCount;i++){
        if(i) put(',');
        raw("\n{"); key("id"); number(X->catalog[i].id);
        put(','); key("fr"); text(X->catalog[i].fr);
        put(','); key("en"); text(X->catalog[i].en); put('}');
    }
    raw("],\n"); key("today"); write_day(&s->today);
    raw(",\n"); key("history"); put('[');
    for(i=0;i<s->count;i++){
        if(i) put(',');
        put('\n'); write_day(&s->history[(s->next+HISTORY_LENGTH-s->count+i)%HISTORY_LENGTH]);
    }
    raw("],\n"); key("kanban"); put('[');
    for(i=0;i<s->taskCount;i++){
        if(i) put(',');
        raw("\n{"); key("status"); text(STATUSES[s->tasks[i].status<3?s->tasks[i].status:0]);
        put(','); key("name"); text_cp437(s->tasks[i].name,TASK_NAME_LENGTH); put('}');
    }
    raw("],\n"); key("reminders"); put('[');
    for(i=0;i<s->reminderCount;i++){
        const Reminder *r=&s->reminders[i];
        if(i) put(',');
        raw("\n{"); key("from"); date(r->from); put(','); key("days"); number(r->duration);
        put(','); key("text"); text_cp437(r->text,REMINDER_TEXT_LENGTH); put('}');
    }
    raw("],\n"); key("meals"); put('{'); key("played"); put('[');
    for(i=0;i<X->mealCount;i++){ if(i) put(','); number(X->meals[i]); }
    raw("]},\n"); key("homeHidden"); put('[');
    for(i=0,n=0;i<HOME_ENTRIES;i++) if(X->homeHidden&(1u<<i)&~HOME_LOCKED){ if(n++) put(','); text(HOME_IDS[i]); }
    raw("]},\n");
    flush(); /* the CRC covers every byte before "crc32" */
    *checksum=~W.crc;
    sprintf(hex,"%08lx",(unsigned long)*checksum);
    key("crc32"); text(hex); raw("}\n"); flush();
    closed=fclose(W.f);
    return W.ok&&closed==0;
}
