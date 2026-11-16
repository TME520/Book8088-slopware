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
 * The activity catalog lives in the State and each day's stored names in
 * lps_day_names (LPS-DOS-115-CHG05): a day's flags are resolved from its
 * names against the catalog after the read, as on the PicoCalc, and names the
 * catalog lacks stay as frozen extras. The Meal Planner deck ("meals") is in
 * the State (LPS-DOS-115-CHG07). The DOS port has no home menu settings yet,
 * so the hidden home entries ("homeHidden") are kept here and written back
 * as read.
 * Unknown keys are skipped, as on the PicoCalc.
 *
 * Text in the file is UTF-8 and the State keeps those exact bytes for every
 * text (LPS-DOS-115-CHG06d; CHG03 held CP437 and turned what CP437 lacks
 * into '?'): only the screen converts to CP437.
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
#define CATALOG_CAPACITY ACTIVITY_CAPACITY
#define NAME_LENGTH ACTIVITY_NAME_LENGTH
#define DAY_NAMES ACTIVITY_CAPACITY /* stored names per day */
#define TEXT_CAPACITY 128     /* PicoCalc SaveTextCapacity */
#define SAVE_MAX_BYTES 65000L /* one far block; the PicoCalc accepts 65536 */
#define HOME_ENTRIES 9
#define HOME_LOCKED ((1u<<0)|(1u<<(HOME_ENTRIES-1))) /* Date, Close the day */
#define WRITE_CHUNK 512
/* Hot tables in DGROUP (near, zero-filled at start-up) rather than behind a far pointer. */
#ifdef __WATCOMC__
#define NEARDATA __near
#else
#define NEARDATA
#endif

typedef struct {
    uint16_t homeHidden;
} SaveExtras;

static SaveExtras *X;
static char NEARDATA wbuf[WRITE_CHUNK];

static const char *const LANGUAGES[2]={"en","fr"};
static const char *const PALETTES[5]={"red","green","blue","orange","pink"};
static const char *const STATUSES[3]={"todo","doing","done"};
static const char *const HOME_IDS[HOME_ENTRIES]={"date","activities","weight","kanban","rosary","daily_review","reminders","timer","close_day"};
void jsave_reset(void){
    if(!jsave_init()) return;
    X->homeHidden=0;
    day_names_clear();
}
int jsave_init(void){
    if(X) return 1;
    if(!lps_buffers_init()) return 0;
    X=(SaveExtras*)calloc(1,sizeof(SaveExtras));
    return X!=0;
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
/* LPS-DOS-115-CHG06d: J.text kept as read, byte for byte (read_text bounds it). */
static void text_copy(char *out){ memcpy(out,J.text,J.len+1); }
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
            int ast=1;
            if(next()!=T_ARR) return 0;
            d->flags=0; e->count=0; /* resolved against the catalog after the read */
            while((r=element(&ast,&t))>0){
                if(t!=T_STRING||J.overflow||J.len>NAME_LENGTH||e->count>=DAY_NAMES) return 0;
                memcpy(e->names[e->count++],J.text,J.len+1);
            }
            if(r<0) return 0;
        }
        else if(!strcmp(key,"expected")){ if(!read_weight(&d->expected)) return 0; }
        else if(!strcmp(key,"actual")){ if(!read_weight(&d->actual)) return 0; }
        else if(!strcmp(key,"note")){ if(!read_text(NOTE_LENGTH+1,0)) return 0; text_copy(d->note); }
        else if(!skip(next())) return 0;
    }
    return r==0;
}
static int read_catalog(State *s){
    int st=1, r, t; char key[24];
    if(next()!=T_ARR) return 0;
    memset(s->activities,0,sizeof(s->activities)); s->activityCount=0; s->nextActivityId=1;
    while((r=element(&st,&t))>0){
        Activity *a; int ms=1; unsigned i;
        if(t!=T_OBJ||s->activityCount>=CATALOG_CAPACITY) return 0;
        a=&s->activities[s->activityCount];
        while((r=member(&ms,key))>0){
            if(!strcmp(key,"id")){ if(!read_number(1,65535ul)) return 0; a->id=(uint16_t)J.number; }
            else if(!strcmp(key,"fr")){ if(!read_text(NAME_LENGTH+1,0)) return 0; memcpy(a->fr,J.text,J.len+1); }
            else if(!strcmp(key,"en")){ if(!read_text(NAME_LENGTH+1,0)) return 0; memcpy(a->en,J.text,J.len+1); }
            else if(!skip(next())) return 0;
        }
        if(r<0||(!a->fr[0]&&!a->en[0])) return 0;
        if(!a->id) a->id=s->nextActivityId;
        for(i=0;i<s->activityCount;i++) if(s->activities[i].id==a->id) return 0;
        if(a->id>=s->nextActivityId) s->nextActivityId=(uint16_t)(a->id+1);
        s->activityCount++;
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
        else if(!strcmp(key,"activities")){ if(!read_catalog(s)) return 0; }
        else if(!strcmp(key,"today")){ if(!read_day(next(),&s->today,day_names(0))) return 0; }
        else if(!strcmp(key,"history")){
            int ast=1;
            if(next()!=T_ARR) return 0;
            s->count=0;
            while((r=element(&ast,&t))>0){
                if(s->count>=HISTORY_LENGTH||!read_day(t,&s->history[s->count],day_names(1+s->count))) return 0;
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
                    else if(!strcmp(key,"name")){ if(!read_text(TASK_NAME_LENGTH+1,1)) return 0; text_copy(task->name); named=1; }
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
                    else if(!strcmp(key,"text")){ if(!read_text(REMINDER_TEXT_LENGTH+1,1)) return 0; text_copy(rem->text); texted=1; }
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
            s->mealCount=0;
            while((r=member(&ms,key))>0){
                int ast=1;
                if(strcmp(key,"played")){ if(!skip(next())) return 0; continue; }
                if(next()!=T_ARR) return 0;
                s->mealCount=0;
                while((r=element(&ast,&t))>0){
                    unsigned i;
                    if(t!=T_NUMBER||!J.integer||J.number>=MEAL_CARD_COUNT) return 0;
                    for(i=0;i<s->mealCount;i++) if(s->meals[i]==J.number) return 0; /* each card once */
                    s->meals[s->mealCount++]=(uint8_t)J.number;
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
    if(r!=0) return 0;
    day_names_resolve(s); /* the catalog may come after the days */
    return 1;
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
static void emit_name(const char *name, unsigned *n){
    if(*n>=DAY_NAMES) return; /* the PicoCalc reader takes 30 names a day */
    if((*n)++) put(',');
    text(name);
}
/* As PicoCalc writeDay: the ticked catalog names in catalog order, then the
 * stored names the catalog lacks, in the order they were read. */
static void write_day(const State *s, const Day *d, unsigned slot){
    const DayNames *e=day_names(slot); unsigned i, n=0;
    put('{'); key("date"); date(d->date); put(','); key("number"); number(d->number);
    put(','); key("activities"); put('[');
    for(i=0;i<s->activityCount;i++) if(FLAG_BIT(d->flags,i)) emit_name(activity_stored_name(&s->activities[i]),&n);
    for(i=0;i<e->count;i++) if(activity_index(s,e->names[i])>=s->activityCount) emit_name(e->names[i],&n);
    raw("],"); key("expected"); weight(d->expected); put(','); key("actual"); weight(d->actual);
    put(','); key("note"); text(d->note); put('}');
}
int jsave_write(const char *path, const State *s, uint32_t generation, uint32_t *checksum){
    unsigned i, n; char hex[12]; int closed;
    if(!jsave_init()) return 0;
    W.f=fopen(path,"wb");
    if(!W.f) return 0;
    W.used=0; W.crc=0xfffffffful; W.ok=1;
    put('{'); key("format"); text("lps-companion"); put(','); key("version"); number(SCHEMA_VERSION);
    put(','); key("generation"); number(generation); raw(",\n");
    key("state"); put('{'); key("language"); text(LANGUAGES[s->language&1]);
    put(','); key("palette"); text(PALETTES[s->palette<5?s->palette:PAL_GREEN]); put(','); key("xp"); number(s->xp);
    raw(",\n"); key("activities"); put('[');
    for(i=0;i<s->activityCount;i++){
        if(i) put(',');
        raw("\n{"); key("id"); number(s->activities[i].id);
        put(','); key("fr"); text(s->activities[i].fr);
        put(','); key("en"); text(s->activities[i].en); put('}');
    }
    raw("],\n"); key("today"); write_day(s,&s->today,0);
    raw(",\n"); key("history"); put('[');
    for(i=0;i<s->count;i++){
        unsigned ring=(s->next+HISTORY_LENGTH-s->count+i)%HISTORY_LENGTH;
        if(i) put(',');
        put('\n'); write_day(s,&s->history[ring],1+ring);
    }
    raw("],\n"); key("kanban"); put('[');
    for(i=0;i<s->taskCount;i++){
        if(i) put(',');
        raw("\n{"); key("status"); text(STATUSES[s->tasks[i].status<3?s->tasks[i].status:0]);
        put(','); key("name"); text(s->tasks[i].name); put('}');
    }
    raw("],\n"); key("reminders"); put('[');
    for(i=0;i<s->reminderCount;i++){
        const Reminder *r=&s->reminders[i];
        if(i) put(',');
        raw("\n{"); key("from"); date(r->from); put(','); key("days"); number(r->duration);
        put(','); key("text"); text(r->text); put('}');
    }
    raw("],\n"); key("meals"); put('{'); key("played"); put('[');
    for(i=0;i<s->mealCount;i++){ if(i) put(','); number(s->meals[i]); }
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
