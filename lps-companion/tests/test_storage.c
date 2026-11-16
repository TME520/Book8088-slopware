/* Host unit tests: JSON A/B saves and BIN migration, ICS, CSV, ROSARY.DAT, screens. */
#define _POSIX_C_SOURCE 200112L
#include "lps.h"
#include "rosary.h"
#include "meals.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

Platform *plat_host_create(int capture);
const char *plat_host_row(Platform *p, int row);
int plat_host_rev(Platform *p, int row, int col);
int plat_host_key(int azerty, unsigned char ch);

static void put32(uint8_t *p, uint32_t v){
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static void rm_tree(const char *dir){
    char cmd[256]; int rc;
    sprintf(cmd,"rm -rf '%s'", dir);
    rc=system(cmd);
    if(rc!=0) fprintf(stderr,"cleanup failed for %s\n", dir);
}

/* Wrap the PicoCalc v1.2 payload in an LPR1 A-slot record (same envelope as firmware). */
static int write_v12_fixture(const char *dir){
    static const char *fix="tests/fixtures/v12_payload.bin";
    uint8_t raw[BLOB_CAPACITY];
    uint8_t rec[WIRE_V1+16];
    FILE *f; size_t n, recsz; char path[256];
    f=fopen(fix,"rb");
    if(!f){ fprintf(stderr,"missing %s (run from the lps-companion directory)\n", fix); return 0; }
    n=fread(raw,1,sizeof(raw),f);
    fclose(f);
    if(n!=WIRE_V1) return 0;
    recsz=n+16;
    memset(rec,0,sizeof(rec));
    put32(rec, SAVE_MAGIC);
    put32(rec+4, 50);
    put32(rec+8, (uint32_t)n);
    memcpy(rec+12, raw, n);
    put32(rec+recsz-4, crc32(rec, recsz-4));
    sprintf(path,"%s/SAVE_A.BIN", dir);
    f=fopen(path,"wb");
    if(!f) return 0;
    if(fwrite(rec,1,recsz,f)!=recsz){ fclose(f); return 0; }
    return fclose(f)==0;
}

/* LPS-DOS-115-CHG01: XP must not appear on any screen row. */
static int any_xp(Platform *hw){
    int r; for(r=0;r<ROWS;r++) if(strstr(plat_host_row(hw,r),"XP")) return 1; return 0;
}
static int row_has(Platform *hw, int row, const char *text){ return strstr(plat_host_row(hw,row),text)!=NULL; }

/* Independent ISO 8601 check: the week's Thursday decides the ISO year,
 * and its ordinal day gives the week number. Covers 2000-01-01..2099-12-31. */
static void check_iso_weeks(void){
    static int32_t start[102]; /* day_index of Jan 1, years 1999..2100 */
    Date d={2000,1,1}; unsigned y, days=0;
    start[0]=1-365; /* 1999 */
    for(y=2000;y<=2099;y++){ Date j; j.year=(uint16_t)y; j.month=1; j.day=1; start[y-1999]=(int32_t)day_index(j); }
    start[101]=start[100]+365; /* 2100-01-01; 2099 is not a leap year */
    for(;;){
        int32_t idx=(int32_t)day_index(d), thursday=idx+4-(int32_t)((weekday(d)+6)%7+1);
        unsigned k=(unsigned)(d.year-1999);
        unsigned expect;
        while(thursday<start[k]) k--;
        while(k<101 && thursday>=start[k+1]) k++;
        expect=(unsigned)((thursday-start[k])/7+1);
        if(iso_week(d)!=expect){ fprintf(stderr,"iso_week %04u-%02u-%02u: %u != %u\n",d.year,d.month,d.day,iso_week(d),expect); assert(0); }
        days++;
        if(!next_date(&d)) break;
    }
    assert(days==36525);
}

/* LPS-DOS-115-CHG03b: ROSARY.DAT (built by tools/mkrosary in this directory). */
static long read_file(const char *path, unsigned char *buf, size_t cap){
    FILE *f=fopen(path,"rb"); size_t n; if(!f) return -1;
    n=fread(buf,1,cap,f); fclose(f); return (long)n;
}
static void write_file(const char *path, const unsigned char *buf, size_t n){
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(buf,1,n,f)==n); assert(fclose(f)==0);
}
static int load_variant(const char *path){ rosary_unload(); rosary_set_path(path); return rosary_load(); }
static void check_rosary_dat(const char *tmpdir){
    static unsigned char dat[0x10000], bad[0x10000];
    char path[256]; long n; unsigned id; int lang;
    n=read_file(ROSARY_DAT_NAME,dat,sizeof(dat));
    if(n<0){ fprintf(stderr,"missing ROSARY.DAT (run `make rosary` in lps-companion)\n"); assert(0); }
    assert(memcmp(dat,ROSARY_DAT_MAGIC,8)==0 && dat[8]==ROSARY_DAT_VERSION && dat[10]==(ROSARY_DAT_STRINGS&0xFF));
    /* Every text in the file is the single source text, EN and FR. */
    assert(load_variant(ROSARY_DAT_NAME)==ROSARY_OK && rosary_loaded());
    for(id=0;id<ROS_T_COUNT;id++) for(lang=0;lang<2;lang++){
        const char *want=rosary_source_text(id,lang); const char *got;
        assert(want && *want);
        if(id<ROS_T_PRAYER_COUNT) continue; /* prayers checked through rosary_text below */
        if(id<ROS_T_SCRIPTURE){ got=mystery_name(lang,(int)((id-ROS_T_MYSTERY)/5),(id-ROS_T_MYSTERY)%5); assert(strcmp(got,want)==0); }
    }
    assert(strcmp(rosary_text(LANG_EN,ROS_FULL,0,0,1),rosary_source_text(ROS_T_CREED,LANG_EN))==0);
    assert(strcmp(rosary_text(LANG_FR,ROS_DECADE,2,3,3),rosary_source_text(ROS_T_HAIL,LANG_FR))==0);
    assert(strcmp(rosary_text(LANG_FR,ROS_CONTEMP,1,0,10),rosary_source_text(ROS_T_SILENCE,LANG_FR))==0);
    assert(strcmp(rosary_text(LANG_EN,ROS_GUIDED,3,0,8),rosary_source_text(ROS_T_SCRIPTURE+15,LANG_EN))==0);
    assert(strcmp(rosary_text(LANG_EN,ROS_GUIDED,3,0,9),rosary_source_text(ROS_T_MEDITATION+15,LANG_EN))==0);
    /* Not next to the EXE: falls back to the current directory's copy. */
    sprintf(path,"%s/ROSARY.DAT",tmpdir); remove(path);
    assert(load_variant(path)==ROSARY_OK);
    /* Damaged copies: each one is rejected and nothing stays loaded. */
    {
        struct { size_t at; unsigned char x; long len; } cases[]={
            {0,'X',0},            /* magic */
            {8,2,0},              /* version */
            {10,1,0},             /* string count */
            {12,1,0},             /* payload size */
            {16,1,0},             /* CRC field */
            {ROSARY_DAT_HEADER+3,0x55,0},  /* offset table */
            {ROSARY_DAT_HEADER+ROSARY_DAT_STRINGS*2+5,'#',0}, /* text byte */
            {0,0,-7},             /* truncated */
            {0,0,9},              /* trailing junk */
            {0,0,-(long)0x7fff}   /* header only */
        };
        unsigned i;
        for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
            long len=n; memcpy(bad,dat,(size_t)n);
            if(cases[i].len<0) len=n+cases[i].len<ROSARY_DAT_HEADER?ROSARY_DAT_HEADER:n+cases[i].len;
            else if(cases[i].len>0){ memset(bad+n,'J',(size_t)cases[i].len); len=n+cases[i].len; }
            else bad[cases[i].at]^=cases[i].x;
            write_file(path,bad,(size_t)len);
            if(load_variant(path)!=ROSARY_CORRUPT){ fprintf(stderr,"damaged case %u accepted\n",i); assert(0); }
            assert(!rosary_loaded());
            assert(rosary_text(LANG_EN,ROS_FULL,0,0,1)[0]==0); /* empty, no crash */
        }
        remove(path);
    }
    {   /* LPS-DOS-115-CHG03: a stale version 1 file (ASCII-folded) is refused even
         * with a valid CRC, and so is raw UTF-8 that missed the CP437 step. */
        unsigned long at=ROSARY_DAT_HEADER+ROSARY_DAT_STRINGS*2+5; uint32_t crc;
        memcpy(bad,dat,(size_t)n); bad[8]=1; bad[9]=0; write_file(path,bad,(size_t)n);
        assert(load_variant(path)==ROSARY_CORRUPT && !rosary_loaded());
        memcpy(bad,dat,(size_t)n); bad[at]=0xC3; bad[at+1]=0xA9;
        crc=crc32(bad+ROSARY_DAT_HEADER,(size_t)n-ROSARY_DAT_HEADER);
        bad[16]=(unsigned char)crc; bad[17]=(unsigned char)(crc>>8); bad[18]=(unsigned char)(crc>>16); bad[19]=(unsigned char)(crc>>24);
        write_file(path,bad,(size_t)n);
        assert(load_variant(path)==ROSARY_CORRUPT && !rosary_loaded());
        remove(path);
    }
    rosary_unload(); rosary_set_path(ROSARY_DAT_NAME);
}

/* LPS-DOS-115-CHG02 (PicoCalc LPS-112-CHG05): no notepad, archive menu 1-2,
 * legacy notes kept in the save and exported to the ICS. LPS-DOS-115-CHG10
 * (PicoCalc LPS-115-CHG01): no Config entry, menu 0-8, footer Tab:Config. */
static int any_row(Platform *hw, const char *text){
    int r; for(r=0;r<ROWS;r++) if(strstr(plat_host_row(hw,r),text)) return 1; return 0;
}
static void check_menu_and_legacy_note(const char *tmpdir){
    static unsigned char store3[16384]; App *a=(App*)store3; Platform *hw; State s;
    uint32_t gen=0; int active=-1; unsigned i; char path[256], got[2048]; FILE *f; size_t n;
    static const char *labels[9]={"0  Date","1  Activities","2  Weight tracker","3  Kanban","4  Christian Rosary",
        "5  Daily review","6  Reminders","7  Timer","8  Close the day"};
    static const int screens[9]={SCR_DATE,SCR_ACTIVITIES,SCR_WEIGHT,SCR_KANBAN,SCR_ROSARY_MODE,
        SCR_DAILY_REVIEW,SCR_REMINDERS,SCR_TIMER_SELECT,SCR_FINISH};
    rm_tree(tmpdir); mkdir(tmpdir,0755); lps_set_data_dir(tmpdir); lps_ensure_dirs();
    /* A save written before v1.12 with a note on today. */
    state_init(&s); s.today.flags=3; strcpy(s.today.note,"Legacy note, kept; really");
    assert(save_store(tmpdir,&s,&gen,&active));
    hw=plat_host_create(0); app_init(a,hw); app_start(a);
    assert(app_screen(a)==SCR_HOME);
    assert(row_has(hw,0,"LPS COMPANION v1.15") && !strcmp(LPS_VERSION_STR,"1.15")); /* LPS-DOS-115-CHG12 */
    for(i=0;i<9;i++) assert(strstr(plat_host_row(hw,7+(int)i),labels[i]));
    for(i=7;i<=17;i++) assert(!strstr(plat_host_row(hw,(int)i),"Config") && !strstr(plat_host_row(hw,(int)i),"9  "));
    assert(row_has(hw,24,"Up/Down  Enter:OK  Tab:Config"));
    assert(!any_row(hw,"Notepad") && !any_row(hw,"NOTEPAD") && !any_row(hw,"Legacy note") && !any_row(hw,"10 "));
    assert(strstr(plat_host_row(hw,7),"WEIGHT TRACKER") && strstr(plat_host_row(hw,11),"REMINDERS") && strstr(plat_host_row(hw,16),"KANBAN"));
    app_key(a,'9'); assert(app_screen(a)==SCR_HOME && app_selection(a)==0); /* no entry 9 */
    for(i=0;i<9;i++){
        rosary_set_path(ROSARY_DAT_NAME);
        app_key(a,(int)('0'+i));
        if((int)app_screen(a)!=screens[i]){ fprintf(stderr,"key %u -> screen %d\n",i,(int)app_screen(a)); assert(0); }
        app_key(a,KEY_ESCAPE); assert(app_screen(a)==SCR_HOME);
    }
    for(i=0;i<9;i++) app_key(a,KEY_DOWN); /* wraps over exactly 9 entries */
    app_key(a,KEY_UP); app_key(a,KEY_ENTER); assert(app_screen(a)==SCR_FINISH);
    /* Close the day: the legacy note goes to the ICS and stays in the history. */
    app_key(a,KEY_ENTER); assert(app_screen(a)==SCR_REWARD); app_key(a,KEY_ENTER);
    assert(strcmp(app_state(a)->history[0].note,"Legacy note, kept; really")==0 && app_state(a)->today.note[0]==0);
    sprintf(path,"%s/EXPORT\\20260917.ICS",tmpdir);
    f=fopen(path,"rb"); assert(f); n=fread(got,1,sizeof(got)-1,f); fclose(f); got[n]=0;
    assert(strstr(got,"DESCRIPTION:Activities: Walk, Diet\\NXP:\\NNote: Legacy note\\, kept\\; really\r\nEND:VEVENT"));
    { /* LPS-DOS-115-CHG02c: 8.3 name per day, so two days of one month give two files. */
        static Day d2; char p2[256], exp_dir[256];
        d2=app_state(a)->history[0]; d2.date.day=18; sprintf(exp_dir,"%s/EXPORT",tmpdir);
        assert(write_ics_file(exp_dir,&d2,app_state(a),0,0,LANG_EN));
        sprintf(p2,"%s/EXPORT\\20260918.ICS",tmpdir);
        f=fopen(p2,"rb"); assert(f); fclose(f);
        f=fopen(path,"rb"); assert(f); n=fread(got,1,sizeof(got)-1,f); fclose(f); got[n]=0;
        assert(strstr(got,"DTSTART;VALUE=DATE:20260917\r\n"));
        sprintf(p2,"%s/EXPORT\\2026-09-17_LPS-Companion.ics",tmpdir);
        f=fopen(p2,"rb"); assert(!f);
    }
    /* Archived day: 1 Activities, 2 Weight; other digits are read-only. */
    app_key(a,KEY_LEFT);
    assert(strstr(plat_host_row(hw,7),"1  Activities") && strstr(plat_host_row(hw,8),"2  Weight tracker") && !strstr(plat_host_row(hw,9),"Kanban"));
    assert(!any_row(hw,"Legacy note"));
    app_key(a,'3'); assert(app_screen(a)==SCR_HOME && strstr(plat_host_row(hw,23),"read-only"));
    app_key(a,'1'); assert(app_screen(a)==SCR_ACTIVITIES); app_key(a,KEY_ESCAPE);
    app_key(a,'2'); assert(app_screen(a)==SCR_WEIGHT); app_key(a,KEY_ESCAPE);
    app_key(a,KEY_DOWN); app_key(a,KEY_DOWN); app_key(a,KEY_ENTER); assert(app_screen(a)==SCR_ACTIVITIES); /* 2 entries wrap */
    app_key(a,KEY_ESCAPE); app_key(a,KEY_RIGHT);
}

/* LPS-DOS-115-CHG02b (PicoCalc LPS-112-CHG01/CHG02): 5/30/60 minutes on keys 1-3. */
static void check_timer(const char *tmpdir){
    static unsigned char store4[16384]; App *a=(App*)store4; Platform *hw; lps_tick_t t=1000;
    rm_tree(tmpdir); mkdir(tmpdir,0755); lps_set_data_dir(tmpdir); lps_ensure_dirs();
    hw=plat_host_create(0); app_init(a,hw); app_start(a); app_tick(a,t);
    app_key(a,'7'); assert(app_screen(a)==SCR_TIMER_SELECT);
    assert(strstr(plat_host_row(hw,8),"1  5 minutes") && strstr(plat_host_row(hw,10),"2  30 minutes") && strstr(plat_host_row(hw,12),"3  60 minutes"));
    assert(!any_row(hw,"15 minutes"));
    /* Each key starts its duration at once, without Enter. */
    app_key(a,'1'); assert(app_screen(a)==SCR_TIMER_RUNNING && strstr(plat_host_row(hw,12),"05:00")); app_key(a,KEY_ESCAPE);
    app_key(a,'7'); app_key(a,'2'); assert(app_screen(a)==SCR_TIMER_RUNNING && strstr(plat_host_row(hw,12),"30:00")); app_key(a,KEY_ESCAPE);
    app_key(a,'7'); app_key(a,'3'); assert(app_screen(a)==SCR_TIMER_RUNNING && strstr(plat_host_row(hw,12),"60:00"));
    /* Keys 1-3 are ignored while it runs; Enter pauses. */
    app_key(a,'1'); assert(strstr(plat_host_row(hw,12),"60:00"));
    t+=1000; app_tick(a,t); assert(strstr(plat_host_row(hw,12),"59:59"));
    app_key(a,KEY_ENTER); t+=600000ul; app_tick(a,t); assert(strstr(plat_host_row(hw,12),"59:59") && any_row(hw,"PAUSED"));
    app_key(a,KEY_ENTER); t+=3599000ul-60000ul; app_tick(a,t); assert(strstr(plat_host_row(hw,12),"01:00"));
    t+=1000; app_tick(a,t); assert(strstr(plat_host_row(hw,12),"00:59"));
    t+=59000; app_tick(a,t); assert(app_screen(a)==SCR_TIMER_COMPLETE && any_row(hw,"TIMER FINISHED!"));
    assert(any_row(hw,"1-3: Restart  Enter: Menu  Esc: Back"));
    /* On the completion screen 1-3 restart a countdown; Enter goes back to the menu. */
    app_key(a,'2'); assert(app_screen(a)==SCR_TIMER_RUNNING && strstr(plat_host_row(hw,12),"30:00"));
    t+=1800000ul; app_tick(a,t); assert(app_screen(a)==SCR_TIMER_COMPLETE);
    app_key(a,KEY_ENTER); assert(app_screen(a)==SCR_TIMER_SELECT);
    app_key(a,KEY_DOWN); app_key(a,KEY_DOWN); app_key(a,KEY_ENTER); assert(strstr(plat_host_row(hw,12),"60:00"));
    app_key(a,KEY_ESCAPE); app_key(a,KEY_ESCAPE); assert(app_screen(a)==SCR_HOME);
    /* French wording */
    app_key(a,KEY_TAB); app_key(a,'1'); app_key(a,'2'); app_key(a,'4');
    app_key(a,'7'); app_key(a,'1'); assert(app_state(a)->language==LANG_FR && any_row(hw,"MINUTEUR") && any_row(hw,"Enter: Pause   Esc: Annuler"));
    t+=300000ul; app_tick(a,t); assert(any_row(hw,"MINUTEUR TERMINÉ !") && any_row(hw,"1-3: Relancer  Enter: Menu  Esc: Retour"));
}

/* LPS-DOS-115-CHG03: French on screen is CP437 with accents, files stay UTF-8.
 * This file is UTF-8 and goes through tools/cp437 like the program, so "é"
 * below is the CP437 byte 0x82 at run time. Byte checks spell the bytes out. */
static const char *const FOLDED_FR[]={
    "Activites","activites","ACTIVITES","Journee","journee","Entree","Echap","Rosaire chretien",
    "Revue journaliere","TERMINE","Fevrier","Aout","Decembre","Tache","tache",
    "enregistre","ENREGISTRE","Francais","Mystere","mystere","seance","priere",
    "Ecart","precedent","deja","etre","Debut","Verifiez","Memoire","endommage","caracteres",
    "Regime","Conge","Pere","Esprit Saint courage","Jesus","Ecriture","meditation",0
};
/* Capitals CP437 lacks (A/E/O grave or circumflex) fold by design: TÂCHE shows
 * as TACHE, MYSTÈRES as MYSTERES, NOTRE PÈRE as NOTRE PERE, À FAIRE as A FAIRE. */
static int is_letter(char c){ return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(unsigned char)c>=0x80; }
static int has_word(const char *row, const char *word){ /* whole words: "enregistrer" is fine */
    const char *p=row; size_t n=strlen(word);
    while((p=strstr(p,word))!=NULL){
        if((p==row || !is_letter(p[-1])) && !is_letter(p[n])) return 1;
        p++;
    }
    return 0;
}
static void french_screen_ok(Platform *hw, const char *where){
    int r; unsigned i; const unsigned char *p;
    for(r=0;r<ROWS;r++){
        for(p=(const unsigned char*)plat_host_row(hw,r);*p;p++)
            if(!cp437_text_byte(*p)){ fprintf(stderr,"%s row %d: byte 0x%02X is not CP437 text\n",where,r,*p); assert(0); }
        for(i=0;FOLDED_FR[i];i++) if(has_word(plat_host_row(hw,r),FOLDED_FR[i])){
            fprintf(stderr,"%s row %d: folded \"%s\" in \"%s\"\n",where,r,FOLDED_FR[i],plat_host_row(hw,r)); assert(0); }
    }
}
static void check_french_cp437(const char *tmpdir){
    static unsigned char store5[16384]; App *a=(App*)store5; Platform *hw; char t[64]; unsigned i;
    {   /* Month and day names: CP437 bytes. */
        Date d={2026,2,14}; long_date_text(d,LANG_FR,t,sizeof(t)); assert(strcmp(t,"Samedi 14 F\202vrier 2026 (semaine 7)")==0);
        d.month=8; d.day=15; long_date_text(d,LANG_FR,t,sizeof(t)); assert(strcmp(t,"Samedi 15 Ao\226t 2026 (semaine 33)")==0);
        d.month=12; d.day=25; long_date_text(d,LANG_FR,t,sizeof(t)); assert(strcmp(t,"Vendredi 25 D\202cembre 2026 (semaine 52)")==0);
        assert(strcmp(ACTIVITIES_FR[1],"R\202gime")==0 && strcmp(ACTIVITIES_FR[9],"Cong\202")==0);
        assert(strcmp("Activités","Activit\202s")==0 && strcmp("Échap","\220chap")==0 && strcmp("À FAIRE","A FAIRE")==0);
    }
    {   /* ICS: same bytes as the PicoCalc's exportIcs, UTF-8 (main.cpp, v1.12). */
        static const char want[]=
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//LPS Companion//EN\r\nCALSCALE:GREGORIAN\r\nBEGIN:VEVENT\r\n"
            "UID:lps-companion-20260917@picocalc\r\nDTSTART;VALUE=DATE:20260917\r\nDTEND;VALUE=DATE:20260918\r\n"
            "SUMMARY:LPS Companion\r\nDESCRIPTION:Activit\303\251s: Marche, R\303\251gime, Cong\303\251\\NXP:"
            "\\NPoids attendu: 110.500 kg\\NPoids effectif: 111.250 kg\\NNote: Legacy\\, ascii\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
        static char got[2048]; static Day d; static State cat; char dir[256], path[300]; FILE *f; size_t n;
        state_init(&cat);
        memset(&d,0,sizeof(d)); d.date.year=2026; d.date.month=9; d.date.day=17; d.number=3;
        d.flags=(1u<<0)|(1u<<1)|(1u<<9); d.expected=110500; d.actual=111250; strcpy(d.note,"Legacy, ascii");
        assert(build_ics(&d,&cat,0,0,LANG_FR,got,sizeof(got)) && strcmp(got,want)==0);
        rm_tree(tmpdir); mkdir(tmpdir,0755); sprintf(dir,"%s",tmpdir);
        assert(write_ics_file(dir,&d,&cat,0,0,LANG_FR));
        sprintf(path,"%s\\20260917.ICS",tmpdir);
        f=fopen(path,"rb"); assert(f); n=fread(got,1,sizeof(got),f); fclose(f);
        assert(n==strlen(want) && memcmp(got,want,n)==0);
        for(i=0;i<n;i++) assert((unsigned char)got[i]!=0x82); /* no CP437 byte in the file */
    }
    /* Every French screen: CP437 text only, no folded word. */
    rm_tree(tmpdir); mkdir(tmpdir,0755); lps_set_data_dir(tmpdir); lps_ensure_dirs();
    hw=plat_host_create(0); app_init(a,hw); app_start(a); rosary_set_path(ROSARY_DAT_NAME);
    app_key(a,KEY_TAB); app_key(a,'1'); app_key(a,'2'); app_key(a,'4');
    assert(app_state(a)->language==LANG_FR && app_screen(a)==SCR_HOME);
    french_screen_ok(hw,"home");
    assert(any_row(hw,"1  Activit\202s") && any_row(hw,"4  Rosaire chr\202tien") && any_row(hw,"5  Revue journali\212re"));
    app_key(a,'1'); french_screen_ok(hw,"activities"); assert(any_row(hw,"ACTIVIT\220S"));
    app_key(a,'1'); app_key(a,'2'); assert(any_row(hw,"R\202gime")); app_key(a,KEY_ESCAPE);
    app_key(a,'2'); french_screen_ok(hw,"weight"); app_key(a,KEY_ESCAPE);
    app_key(a,'3'); french_screen_ok(hw,"kanban"); app_key(a,'n'); french_screen_ok(hw,"task edit"); app_key(a,KEY_ESCAPE); app_key(a,KEY_ESCAPE);
    app_key(a,'5'); french_screen_ok(hw,"daily review"); app_key(a,KEY_ESCAPE);
    app_key(a,KEY_TAB); french_screen_ok(hw,"config"); app_key(a,KEY_ESCAPE);
    app_key(a,'6'); french_screen_ok(hw,"reminders"); app_key(a,KEY_ESCAPE);
    app_key(a,'7'); french_screen_ok(hw,"timer"); app_key(a,KEY_ESCAPE);
    assert(app_screen(a)==SCR_HOME);
    app_key(a,'4'); assert(app_screen(a)==SCR_ROSARY_MODE); french_screen_ok(hw,"rosary mode");
    app_key(a,KEY_ENTER); french_screen_ok(hw,"rosary set"); app_key(a,KEY_ENTER); french_screen_ok(hw,"rosary decade");
    app_key(a,KEY_ENTER);
    for(i=0;i<6 && !any_row(hw,"Notre P\212re, qui es aux cieux");i++){ french_screen_ok(hw,"rosary prayer"); app_key(a,KEY_RIGHT); }
    assert(app_screen(a)==SCR_ROSARY_PRAYER && any_row(hw,"Notre P\212re, qui es aux cieux"));
    french_screen_ok(hw,"rosary prayer");
    app_key(a,KEY_ESCAPE); assert(app_screen(a)==SCR_HOME);
    app_key(a,'8');
    if(app_screen(a)==SCR_FINISH){ french_screen_ok(hw,"close the day"); app_key(a,KEY_ESCAPE); }
    else french_screen_ok(hw,"close the day refused");
}

/* LPS-DOS-115-CHG03c: word wrap at spaces, CP437 one byte per cell. */
static void wrap_expect(const char *text, unsigned width, const char *const *want){
    const char *p=text; unsigned len, n=0;
    while(*p){
        const char *next=wrap_line(p,width,&len);
        assert(want[n] && len==strlen(want[n]) && memcmp(p,want[n],len)==0);
        assert(next>p); p=next; n++;
    }
    assert(!want[n] && wrap_count(text,width)==n);
}
static void check_wrapped(const char *text, unsigned width){
    const char *p=text; unsigned len;
    while(*p){
        const char *next=wrap_line(p,width,&len);
        assert(len<=width && len>0);
        assert(p[0]!=' ' && p[len-1]!=' ');             /* no edge spaces */
        assert(p[len]==' ' || p[len]==0);                /* breaks only at a space: no word split */
        if(*next){ /* the next word did not fit on this line */
            const char *w=next; unsigned wl=0; while(w[wl] && w[wl]!=' ') wl++;
            assert(len+1+wl>width);
        }
        p=next;
    }
}
static void check_wrap(const char *tmpdir){
    static const char *const a[]={"aaa bbb","ccc",0};
    static const char *const b[]={"abcd","efgh","ij",0};
    static const char *const c[]={"one","two",0};
    static const char *const d[]={"\202t\202","\212re",0};
    static const char *const e[]={"ab","cdef","gh i",0};
    static const char *const f[]={"exactly8","next",0};
    static const char *const g[]={"x",0};
    unsigned id; int lang;
    wrap_expect("aaa bbb ccc",7,a);
    wrap_expect("abcdefghij",4,b);          /* longer than the line: hard break */
    wrap_expect("one   two",4,c);           /* spaces at the break are dropped */
    wrap_expect("été ère",4,d);             /* CP437: one byte per cell */
    wrap_expect("ab cdefgh i",4,e);
    wrap_expect("exactly8 next",8,f);       /* a line can fill the width exactly */
    wrap_expect("x",80,g);
    assert(wrap_count("",80)==0);
    /* Every rosary text, EN and FR: no line over 80, no word split. */
    for(id=0;id<ROS_T_COUNT;id++) for(lang=0;lang<2;lang++) check_wrapped(rosary_source_text(id,lang),COLUMNS);
    {   /* On screen: the French Hail Mary, rows 6.., joined with single spaces, is the text. */
        static unsigned char store6[16384]; App *ap=(App*)store6; Platform *hw; char joined[1024]; int r; unsigned i;
        const char *want;
        rm_tree(tmpdir); mkdir(tmpdir,0755); lps_set_data_dir(tmpdir); lps_ensure_dirs();
        hw=plat_host_create(0); app_init(ap,hw); app_start(ap); rosary_set_path(ROSARY_DAT_NAME);
        app_key(ap,KEY_TAB); app_key(ap,'1'); app_key(ap,'2'); app_key(ap,'4');
        app_key(ap,'4'); app_key(ap,KEY_ENTER); app_key(ap,KEY_ENTER); app_key(ap,KEY_ENTER);
        for(i=0;i<3;i++) app_key(ap,KEY_RIGHT);
        want=rosary_source_text(ROS_T_HAIL,LANG_FR);
        assert(app_screen(ap)==SCR_ROSARY_PRAYER && any_row(hw,"JE VOUS SALUE MARIE"));
        joined[0]=0;
        for(r=6;r<22;r++){
            char row[COLUMNS+1]; int n; strcpy(row,plat_host_row(hw,r));
            n=(int)strlen(row); while(n>0 && row[n-1]==' ') row[--n]=0;
            if(!n) continue;
            if(joined[0]) strcat(joined," ");
            strcat(joined,row);
        }
        assert(strcmp(joined,want)==0);
        assert(wrap_count(want,COLUMNS)>=3);
        app_key(ap,KEY_ESCAPE);
    }
}

/* ---- LPS-DOS-115-CHG04: JSON saves SAVE_A/B.JSN (PicoCalc v1.15 schema 2) ---- */
static char jbuf[65536], jbuf2[65536];
static size_t slurp(const char *path, char *buf, size_t cap){
    FILE *f=fopen(path,"rb"); size_t n; assert(f); n=fread(buf,1,cap-1,f); fclose(f); buf[n]=0; return n;
}
static void spit(const char *path, const char *buf, size_t n){
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(buf,1,n,f)==n); assert(fclose(f)==0);
}
static int exists(const char *path){ FILE *f=fopen(path,"rb"); if(f) fclose(f); return f!=NULL; }
static void jpath(char *out, const char *dir, const char *name){ sprintf(out,"%s/%s",dir,name); }
static void copy_file(const char *from, const char *to){ size_t n=slurp(from,jbuf2,sizeof(jbuf2)); spit(to,jbuf2,n); }
/* "crc32" again over every byte before the key, as the PicoCalc tests do. */
static void resign(char *buf){
    char *at=NULL, *p=buf, hex[9];
    while((p=strstr(p,"\"crc32\":\""))!=NULL){ at=p; p++; }
    assert(at);
    sprintf(hex,"%08lx",(unsigned long)crc32((const uint8_t*)buf,(size_t)(at-buf)));
    memcpy(at+9,hex,8);
}
/* Insert what right after the first occurrence of after. */
static void insert_after(char *buf, const char *after, const char *what){
    char *p=strstr(buf,after); size_t w=strlen(what);
    assert(p); p+=strlen(after);
    memmove(p+w,p,strlen(p)+1); memcpy(p,what,w);
}
static void remove_span(char *buf, const char *from, const char *to){ /* from..to (to included) */
    char *a=strstr(buf,from), *b; assert(a); b=strstr(a,to); assert(b); b+=strlen(to);
    memmove(a,b,strlen(b)+1);
}
static const Day *ring(const State *s, unsigned i){ return &s->history[(s->next+HISTORY_LENGTH-s->count+i)%HISTORY_LENGTH]; }
static int same_day(const Day *a, const Day *b){
    return a->date.year==b->date.year && a->date.month==b->date.month && a->date.day==b->date.day && a->number==b->number
        && a->flags==b->flags && a->expected==b->expected && a->actual==b->actual && !strcmp(a->note,b->note);
}
static int same_state(const State *a, const State *b){
    unsigned i;
    if(a->language!=b->language||a->palette!=b->palette||a->xp!=b->xp||a->count!=b->count||!same_day(&a->today,&b->today)) return 0;
    for(i=0;i<a->count;i++) if(!same_day(ring(a,i),ring(b,i))) return 0;
    if(a->taskCount!=b->taskCount||a->reminderCount!=b->reminderCount) return 0;
    if(a->activityCount!=b->activityCount||a->nextActivityId!=b->nextActivityId) return 0;
    for(i=0;i<a->activityCount;i++) if(a->activities[i].id!=b->activities[i].id
        ||strcmp(a->activities[i].fr,b->activities[i].fr)||strcmp(a->activities[i].en,b->activities[i].en)) return 0;
    for(i=0;i<a->taskCount;i++) if(a->tasks[i].status!=b->tasks[i].status||strcmp(a->tasks[i].name,b->tasks[i].name)) return 0;
    for(i=0;i<a->reminderCount;i++){
        const Reminder *x=&a->reminders[i], *y=&b->reminders[i];
        if(x->from.year!=y->from.year||x->from.month!=y->from.month||x->from.day!=y->from.day||x->duration!=y->duration||strcmp(x->text,y->text)) return 0;
    }
    return 1;
}
static void write_bin(const char *dir, const char *name, const State *s, uint32_t generation){
    static Blob b; static uint8_t rec[RECORD_SIZE]; char path[256];
    encode_state(s,&b); assert(b.size==WIRE_SIZE);
    put32(rec,SAVE_MAGIC); put32(rec+4,generation); put32(rec+8,(uint32_t)b.size);
    memcpy(rec+12,b.bytes,b.size); put32(rec+RECORD_SIZE-4,crc32(rec,RECORD_SIZE-4));
    jpath(path,dir,name); spit(path,(const char*)rec,RECORD_SIZE);
}
static void fresh_dir(const char *dir){ rm_tree(dir); mkdir(dir,0755); lps_set_data_dir(dir); lps_ensure_dirs(); }
static void sample_state(State *s, unsigned days){
    unsigned i;
    state_init(s); s->language=LANG_FR; s->palette=PAL_ORANGE; s->xp=990;
    for(i=0;i<days;i++){
        Day *d=&s->history[s->next]; d->date.year=2026; d->date.month=(uint8_t)(9+i/28); d->date.day=(uint8_t)(1+i%28);
        d->number=1+i; d->flags=(uint16_t)(0x1111u<<(i%4)); d->expected=i%2?-1:(int32_t)(80000+i); d->actual=81000;
        sprintf(d->note,"Jour %u : \"bien\" \\ fin",i);
        s->next=(uint8_t)((s->next+1)%HISTORY_LENGTH); if(s->count<HISTORY_LENGTH) s->count++;
    }
    s->today.number=1+days; s->today.flags=0x8001; strcpy(s->today.note,"Caf\xc3\xa9 \xc3\xa0 l'\xc3\xa9glise, tr\xc3\xa8s t\xc3\xb4t");
    s->taskCount=2; s->tasks[0].status=TASK_DOING; strcpy(s->tasks[0].name,"R\xc3\xa9parer le v\xc3\xa9lo");
    s->tasks[1].status=TASK_DONE; strcpy(s->tasks[1].name,"Plan \\ \"B\"");
    s->reminderCount=1; s->reminders[0].from=DEFAULT_DATE; s->reminders[0].duration=365; strcpy(s->reminders[0].text,"F\xc3\xaate de No\xc3\xabl");
}
static void check_json_saves(const char *dir){
    static State s, s2, out; static unsigned char store[16384];
    char pa[256], pb[256], pba[256], pbb[256], path[256]; uint32_t gen=0, check=0; int active=-1; size_t n, n2;
    App *app=(App*)store; Platform *hw;
    jpath(pa,dir,"SAVE_A.JSN"); jpath(pb,dir,"SAVE_B.JSN"); jpath(pba,dir,"SAVE_A.BIN"); jpath(pbb,dir,"SAVE_B.BIN");

    /* 1. Round trip, A/B alternation, generations, UTF-8 text in the file. */
    fresh_dir(dir); sample_state(&s,31+5); /* the ring wrapped */
    assert(save_load(dir,&out,&gen,&active)==LOAD_MISSING);
    gen=0; active=-1;
    assert(save_store(dir,&s,&gen,&active) && gen==1 && active==0 && exists(pa) && !exists(pb));
    assert(save_store(dir,&s,&gen,&active) && gen==2 && active==1 && exists(pb));
    assert(save_store(dir,&s,&gen,&active) && gen==3 && active==0);
    assert(!exists(pba) && !exists(pbb));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==3 && active==0 && same_state(&s,&out) && out.count==31);
    n=slurp(pa,jbuf,sizeof(jbuf));
    assert(strstr(jbuf,"{\"format\":\"lps-companion\",\"version\":2,\"generation\":3,\n\"state\":{\"language\":\"fr\",\"palette\":\"orange\",\"xp\":990,\n\"activities\":[\n{\"id\":1,\"fr\":\"Marche\",\"en\":\"Walk\"},")==jbuf);
    assert(strstr(jbuf,"{\"id\":22,\"fr\":\"M\xc3\xa9" "ditation\",\"en\":\"Meditation\"}],\n\"today\":{"));
    assert(strstr(jbuf,"\"activities\":[\"Marche\",\"Shopping\"],\"expected\":null,\"actual\":null,\"note\":\"Caf\xc3\xa9 \xc3\xa0 l'\xc3\xa9glise, tr\xc3\xa8s t\xc3\xb4t\"}"));
    assert(strstr(jbuf,"{\"status\":\"doing\",\"name\":\"R\xc3\xa9parer le v\xc3\xa9lo\"},\n{\"status\":\"done\",\"name\":\"Plan \\\\ \\\"B\\\"\"}],"));
    assert(strstr(jbuf,"\"note\":\"Jour 35 : \\\"bien\\\" \\\\ fin\"}],\n\"kanban\":["));
    assert(strstr(jbuf,"],\n\"meals\":{\"played\":[]},\n\"homeHidden\":[]},\n\"crc32\":\"") && jbuf[n-1]=='\n' && jbuf[n-2]=='}');

    /* 2. PicoCalc fixtures (tools/picocalc_fixture.cpp, the PicoCalc v1.15 writer):
     * DOS reads them and writes back the very same bytes. */
    fresh_dir(dir); copy_file("tests/fixtures/picocalc_custom.jsn",pa);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==7 && active==0);
    assert(out.language==LANG_FR && out.palette==PAL_BLUE && out.xp==4321);
    /* LPS-DOS-115-CHG05: flags follow the PicoCalc catalog (Sport 0, Messe 3,
     * Projet perso 5, Marche 16, Vélo 22, Chess 23). */
    assert(out.activityCount==24 && out.nextActivityId==25 && out.activities[0].id==17 && !strcmp(out.activities[22].fr,"V\xc3\xa9lo"));
    assert(out.today.number==40 && out.today.flags==((1ul<<0)|(1ul<<3)|(1ul<<5)|(1ul<<22)|(1ul<<23)));
    assert(!strcmp(out.today.note,"No\xc3\xabl en famille, g\xc3\xa2teau \xc3\xa0 l'orange") && out.today.expected==82500 && out.today.actual==-1);
    assert(out.count==5 && out.next==5);
    assert(ring(&out,1)->number==36 && ring(&out,1)->flags==((1ul<<0)|(1ul<<16)|(1ul<<22)) && !strcmp(ring(&out,1)->note,"S\xc3\xa9" "ance \xc3\xa0 la piscine, tr\xc3\xa8s bien"));
    assert(day_orphan_count(&out,2)==1 && !strcmp(day_orphan(&out,2,0),"Tai-chi") && day_names(2)->count==4);
    assert(ring(&out,2)->flags==((1ul<<2)|(1ul<<16)|(1ul<<23))); /* the frozen "Projet" is no catalog entry now */
    assert(day_orphan_count(&out,3)==1 && !strcmp(day_orphan(&out,3,0),"Projet"));
    assert(ring(&out,3)->flags==((1ul<<0)|(1ul<<9)|(1ul<<16)|(1ul<<21)) && day_orphan_count(&out,4)==0);
    assert(out.taskCount==3 && out.tasks[0].status==TASK_DOING && !strcmp(out.tasks[0].name,"R\xc3\xa9parer le v\xc3\xa9lo"));
    assert(!strcmp(out.tasks[1].name,"\xc3\x89" "crire \xc3\xa0 H\xc3\xa9l\xc3\xa8ne") && !strcmp(out.tasks[2].name,"Plan \\ \"B\""));
    assert(out.reminderCount==2 && !strcmp(out.reminders[0].text,"Anniversaire de Fran\xc3\xa7ois") && out.reminders[0].duration==30);
    jpath(path,dir,"OUT.JSN");
    assert(jsave_write(path,&out,7,&check));
    n=slurp("tests/fixtures/picocalc_custom.jsn",jbuf,sizeof(jbuf)); n2=slurp(path,jbuf2,sizeof(jbuf2));
    assert(n==n2 && !memcmp(jbuf,jbuf2,n));
    /* Through the app: tick Marche, close the day; the next slot keeps the
     * PicoCalc data, names in catalog order ("Marche" is 17th there). */
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    if(app_screen(app)==SCR_REMINDER_POPUP) app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_HOME);
    /* 24 entries: 3 columns of 8; Marche (16th) heads the third column. */
    app_key(app,'1'); app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); app_key(app,'1'); app_key(app,KEY_ESCAPE); /* tick Marche */
    /* Kanban and Rosary are hidden in this save: Close the day is 6 (LPS-DOS-115-CHG11). */
    app_key(app,'6'); assert(app_screen(app)==SCR_FINISH); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD);
    n=slurp(pb,jbuf,sizeof(jbuf)); /* the tick, saved when leaving Activities */
    assert(strstr(jbuf,"\"generation\":8,") && strstr(jbuf,"\"meals\":{\"played\":[3,0,15]},"));
    n=slurp(pa,jbuf,sizeof(jbuf)); /* the closed day */
    assert(strstr(jbuf,"\"generation\":9,"));
    assert(strstr(jbuf,"{\"id\":24,\"fr\":\"\",\"en\":\"Chess\"}],"));
    assert(strstr(jbuf,"{\"date\":\"2026-11-14\",\"number\":40,\"activities\":[\"Sport\",\"Messe\",\"Projet perso\",\"Marche\",\"V\xc3\xa9lo\",\"Chess\"],"));
    assert(strstr(jbuf,"\"number\":36,\"activities\":[\"Sport\",\"Marche\",\"V\xc3\xa9lo\",\"Tai-chi\"],"));
    assert(strstr(jbuf,"\"today\":{\"date\":\"2026-11-15\",\"number\":41,\"activities\":[],"));
    assert(strstr(jbuf,"\"meals\":{\"played\":[]},\n\"homeHidden\":[\"kanban\",\"rosary\"]},")); /* new day, new deck */
    /* Full PicoCalc state (all 22 activities, 31 days, 24 tasks, quotes and backslashes). */
    fresh_dir(dir); copy_file("tests/fixtures/picocalc_full.jsn",pb);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==41 && active==1 && out.count==31 && out.taskCount==24 && out.reminderCount==3);
    assert(out.today.flags==0x3ffffful && out.today.number==100 && !strcmp(out.today.note,"Note 100: \"quoted\" back\\slash y"));
    jpath(path,dir,"OUT.JSN");
    assert(jsave_write(path,&out,41,&check));
    n=slurp("tests/fixtures/picocalc_full.jsn",jbuf,sizeof(jbuf)); n2=slurp(path,jbuf2,sizeof(jbuf2));
    assert(n==n2 && !memcmp(jbuf,jbuf2,n));
    assert(save_store(dir,&out,&gen,&active) && gen==42 && active==0); /* B active: the next save writes A */

    /* 3. Migration from SAVE_A/B.BIN: the newest BIN loads, the app writes
     * SAVE_A.JSN at once, the BIN files stay byte-for-byte untouched. */
    fresh_dir(dir); sample_state(&s,3); strcpy(s.today.note,"ASCII only in BIN");
    strcpy(s.tasks[0].name,"Fix the bike"); strcpy(s.reminders[0].text,"Christmas party");
    s2=s; s2.xp=1000; s2.today.flags=0x0042;
    write_bin(dir,"SAVE_A.BIN",&s,5); write_bin(dir,"SAVE_B.BIN",&s2,6);
    n=slurp(pba,jbuf2,sizeof(jbuf2));
    assert(save_load(dir,&out,&gen,&active)==LOAD_MIGRATED && gen==6 && active==-1 && same_state(&out,&s2));
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    assert(exists(pa) && !exists(pb) && !app_is_dirty(app));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==7 && active==0 && same_state(&out,&s2));
    assert(slurp(pba,jbuf,sizeof(jbuf))==n && !memcmp(jbuf,jbuf2,n));
    write_bin(dir,"SAVE_C.BIN",&s2,6); jpath(path,dir,"SAVE_C.BIN"); n=slurp(path,jbuf2,sizeof(jbuf2)); /* B as written */
    assert(slurp(pbb,jbuf,sizeof(jbuf))==n && !memcmp(jbuf,jbuf2,n));
    /* Older BIN slot newer than the damaged other one: A wins. */
    fresh_dir(dir); write_bin(dir,"SAVE_A.BIN",&s,9); spit(pbb,"junk",4);
    assert(save_load(dir,&out,&gen,&active)==LOAD_MIGRATED && gen==9 && same_state(&out,&s));
    /* A first JSON write that was cut short: still migrates from BIN. */
    spit(pa,"{\"format\":\"lps-comp",19);
    assert(save_load(dir,&out,&gen,&active)==LOAD_MIGRATED && gen==9);
    /* Both JSON slots damaged: the BIN files are older data, so nothing loads. */
    spit(pb,"{}",2);
    assert(save_load(dir,&out,&gen,&active)==LOAD_ERROR);
    /* A damaged JSON slot and no BIN at all: protected, not a fresh start. */
    fresh_dir(dir); spit(pa,"{",1);
    assert(save_load(dir,&out,&gen,&active)==LOAD_ERROR);

    /* 4. A damaged slot falls back to the other one. */
    fresh_dir(dir); sample_state(&s,2); s2=s; s2.xp=5;
    gen=0; active=-1;
    assert(save_store(dir,&s2,&gen,&active) && save_store(dir,&s,&gen,&active) && save_store(dir,&s2,&gen,&active)); /* A:3 B:2 */
    n=slurp(pa,jbuf,sizeof(jbuf)); jbuf[n/2]^=0x20; spit(pa,jbuf,n); /* corrupt the newest, A */
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==2 && active==1 && same_state(&out,&s));
    assert(save_store(dir,&out,&gen,&active) && gen==3 && active==0); /* rewrites the damaged A */
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==3 && active==0);
    n=slurp(pb,jbuf,sizeof(jbuf)); spit(pb,jbuf,n-30); /* truncated B, A still fine */
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==3 && active==0);
    /* Files from another writer (first line in another order, spaces) are
     * read in full; the newer valid slot still wins, a tie goes to A. */
    assert(save_store(dir,&s,&gen,&active) && gen==4 && active==1); /* a sound B again */
    n=slurp(pa,jbuf,sizeof(jbuf));
    { char *nl=strchr(jbuf,'\n'); char head[96]; assert(nl);
      sprintf(head,"{ \"generation\" : 7 , \"version\":2,\"format\":\"lps-companion\",");
      memmove(jbuf+strlen(head),nl+1,strlen(nl+1)+1); memcpy(jbuf,head,strlen(head)); resign(jbuf); spit(pa,jbuf,strlen(jbuf)); }
    assert(jsave_peek(pa,&check,&gen)==SAVE_DAMAGED && jsave_read(pa,&out,&gen,&check)==SAVE_VALID && gen==7);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==7 && active==0);
    n=slurp(pb,jbuf,sizeof(jbuf)); { char *g=strstr(jbuf,"\"generation\":"); assert(g); memcpy(g+13,"8,",2); } resign(jbuf); spit(pb,jbuf,strlen(jbuf));
    assert(strstr(jbuf,"\"generation\":8,"));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==8 && active==1);
    n=slurp(pb,jbuf,sizeof(jbuf)); { char *g=strstr(jbuf,"\"generation\":"); memcpy(g+13,"7,",2); } resign(jbuf); spit(pb,jbuf,strlen(jbuf));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==7 && active==0); /* tie: A */
    copy_file(pb,pa); assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==7 && active==0); /* tie, both short form: A */
    /* App on a protected save: writes are blocked. */
    spit(pa,"x",1); spit(pb,"{",1); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    assert(any_row(hw,"Read error: save protected") || any_row(hw,"Erreur lecture"));
    app_key(app,'1'); app_key(app,'1'); app_key(app,KEY_ESCAPE); app_flush_save(app);
    assert(slurp(pa,jbuf,sizeof(jbuf))==1 && slurp(pb,jbuf,sizeof(jbuf))==1); /* nothing written */

    /* 5. Unknown keys are skipped (top level, state, days, tasks, reminders,
     * catalog, meals); crc32 must be right and last; a newer schema is refused. */
    fresh_dir(dir); sample_state(&s,2);
    gen=0; active=-1; assert(save_store(dir,&s,&gen,&active));
    n=slurp(pa,jbuf,sizeof(jbuf));
    insert_after(jbuf,"\"generation\":1,","\"device\":{\"name\":\"PicoCalc\",\"list\":[1,2.5e3,-4,true,false,null,{\"a\":[[]]}]},");
    insert_after(jbuf,"\"xp\":990,","\"theme\":\"dark\",\"aVeryLongUnknownKeyNameIndeed\":{\"x\":\"\\u00e9\\ud83d\\ude00\"},");
    insert_after(jbuf,"\"today\":{","\"mood\":[\"ok\"],");
    insert_after(jbuf,"{\"status\":\"doing\",","\"due\":null,");
    insert_after(jbuf,"\"days\":365,","\"snooze\":3,");
    insert_after(jbuf,"{\"id\":5,","\"icon\":\"work\",");
    insert_after(jbuf,"\"meals\":{","\"date\":\"2026-09-17\",");
    resign(jbuf); spit(pa,jbuf,strlen(jbuf));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==1 && same_state(&out,&s));
    jpath(path,dir,"OUT.JSN"); assert(jsave_write(path,&out,1,&check)); /* unknown keys are not written back */
    n2=slurp(path,jbuf2,sizeof(jbuf2)); assert(!strstr(jbuf2,"device") && !strstr(jbuf2,"mood") && !strstr(jbuf2,"icon"));
    { char *c=strstr(jbuf,"\"crc32\""); c[10]^=1; spit(pa,jbuf,strlen(jbuf)); } /* wrong CRC */
    assert(jsave_read(pa,&out,&gen,&check)==SAVE_DAMAGED);
    n=slurp(path,jbuf,sizeof(jbuf)); /* nothing may follow crc32 */
    { char *end=strrchr(jbuf,'}'); strcpy(end,",\"late\":1}\n"); resign(jbuf); spit(pa,jbuf,strlen(jbuf)); }
    assert(jsave_read(pa,&out,&gen,&check)==SAVE_DAMAGED);
    n=slurp(path,jbuf,sizeof(jbuf)); { char *v=strstr(jbuf,"\"version\":2"); v[10]='3'; }
    resign(jbuf); spit(pa,jbuf,strlen(jbuf)); remove(pb);
    assert(jsave_read(pa,&out,&gen,&check)==SAVE_UNSUPPORTED);
    assert(save_load(dir,&out,&gen,&active)==LOAD_ERROR);
    /* Schema 1 (no catalog) loads with the default list of 22, written back. */
    n=slurp(path,jbuf,sizeof(jbuf)); { char *v=strstr(jbuf,"\"version\":2"); v[10]='1'; }
    remove_span(jbuf,"\"activities\":[\n{","}],\n");
    resign(jbuf); spit(pa,jbuf,strlen(jbuf));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && same_state(&out,&s));
    assert(jsave_write(path,&out,1,&check)); slurp(path,jbuf,sizeof(jbuf));
    assert(strstr(jbuf,"{\"id\":17,\"fr\":\"Sport\",\"en\":\"Sport\"}") && strstr(jbuf,"\"version\":2,"));
    /* Text is kept byte for byte, even what CP437 lacks (CHG06d); \u escapes are decoded to UTF-8. */
    n=slurp(path,jbuf,sizeof(jbuf));
    { char *note=strstr(jbuf,"\"note\":\"Caf"); assert(note);
      insert_after(note,"\"note\":\"","\\u00e9t\xc3\xa9 \xe2\x80\x93 \xe2\x82\xac \xc5\x93 \\ud83d\\ude00 "); }
    resign(jbuf); spit(pa,jbuf,strlen(jbuf));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK);
    { static const char want[]="\xc3\xa9t\xc3\xa9 \xe2\x80\x93 \xe2\x82\xac \xc5\x93 \xf0\x9f\x98\x80 Caf\xc3\xa9";
      assert(!strncmp(out.today.note,want,strlen(want))); }
    /* Field limits as on the PicoCalc: a 33-byte task name is damage. */
    n=slurp(path,jbuf,sizeof(jbuf));
    { char *name=strstr(jbuf,"\"name\":\"R"); assert(name); insert_after(name,"\"name\":\"","0123456789abcdef0123"); }
    resign(jbuf); spit(pa,jbuf,strlen(jbuf));
    assert(jsave_read(pa,&out,&gen,&check)==SAVE_DAMAGED);
    jsave_reset();
}

/* LPS-DOS-115-CHG05: the activity catalog, the 3-column Activities screen. */
static int cell_is(Platform *hw, int row, int col, const char *text){ return !memcmp(plat_host_row(hw,row)+col,text,strlen(text)); }
static void read_ics(const char *dir, const char *ymd, char *got, size_t cap){
    char path[300]; FILE *f; size_t n;
    sprintf(path,"%s/EXPORT\\%s.ICS",dir,ymd);
    f=fopen(path,"rb"); assert(f); n=fread(got,1,cap-1,f); fclose(f); got[n]=0;
}
static void check_activities(const char *dir){
    static State s, out; static unsigned char store[16384]; static char got[2048];
    App *app=(App*)store; Platform *hw; uint32_t gen=0, check=0; int active=-1; unsigned i; char pa[256];
    static const char *const fr[6]={"Sport","Cuisine","M\xc3\xa9nage","Famille","Amis","M\xc3\xa9" "ditation"};
    static const char *const en[6]={"Sport","Cooking","Chores","Family","Friends","Meditation"};
    jpath(pa,dir,"SAVE_A.JSN");

    /* Default catalog: the PicoCalc's 22, ids 1-22, UTF-8 names. */
    state_init(&s);
    assert(s.activityCount==22 && s.nextActivityId==23);
    for(i=0;i<22;i++) assert(s.activities[i].id==i+1);
    assert(!strcmp(s.activities[0].fr,"Marche") && !strcmp(s.activities[1].fr,"R\xc3\xa9gime") && !strcmp(s.activities[9].en,"Day off"));
    for(i=0;i<6;i++) assert(!strcmp(s.activities[16+i].fr,fr[i]) && !strcmp(s.activities[16+i].en,en[i]));

    /* Fresh start: 22 entries in three columns of 8, 8 and 6. */
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    assert(app_screen(app)==SCR_HOME && row_has(hw,5,"0/22 activities"));
    app_key(app,'1'); assert(app_screen(app)==SCR_ACTIVITIES && app_selection(app)==0);
    assert(cell_is(hw,5,0,"1 [ ] Walk") && cell_is(hw,5,27,"  [ ] Illness") && cell_is(hw,5,54,"  [ ] Sport"));
    assert(cell_is(hw,12,0,"8 [ ] Gurumed") && cell_is(hw,12,27,"  [ ] Shopping") && cell_is(hw,10,54,"  [ ] Meditation"));
    assert(cell_is(hw,11,54,"      ") && cell_is(hw,13,0,"          ") && plat_host_rev(hw,5,0) && plat_host_rev(hw,5,25) && !plat_host_rev(hw,5,27));
    assert(row_has(hw,22,"1-9,0: tick in this column"));
    /* Up/Down wrap in the column; Left/Right wrap over the columns, keeping the row. */
    app_key(app,KEY_UP); assert(app_selection(app)==7 && plat_host_rev(hw,12,0));
    app_key(app,KEY_DOWN); assert(app_selection(app)==0);
    app_key(app,KEY_RIGHT); assert(app_selection(app)==8 && cell_is(hw,5,27,"1 [ ] Illness") && cell_is(hw,5,0,"  [ ] Walk"));
    app_key(app,KEY_RIGHT); assert(app_selection(app)==16 && cell_is(hw,10,54,"6 [ ] Meditation"));
    app_key(app,KEY_RIGHT); assert(app_selection(app)==0);
    app_key(app,KEY_LEFT); assert(app_selection(app)==16);
    app_key(app,KEY_RIGHT); app_key(app,KEY_UP); assert(app_selection(app)==7);
    app_key(app,KEY_RIGHT); assert(app_selection(app)==15);
    app_key(app,KEY_RIGHT); assert(app_selection(app)==21); /* row 8 does not exist there: the last row */
    /* Keys 1-9, 0 act on the cursor's column; past its end they do nothing. */
    app_key(app,'7'); app_key(app,'0'); assert(app_state(app)->today.flags==0 && app_selection(app)==21);
    app_key(app,'6'); assert(app_state(app)->today.flags==(1ul<<21) && cell_is(hw,10,54,"6 [x] Meditation"));
    app_key(app,KEY_LEFT); assert(app_selection(app)==13);
    app_key(app,'3'); assert(app_selection(app)==10 && app_state(app)->today.flags==((1ul<<21)|(1ul<<10)));
    app_key(app,KEY_ENTER); assert(app_state(app)->today.flags==(1ul<<21));
    /* LPS-DOS-115-CHG09: SPACE opens the planner and comes back to the same cursor. */
    app_key(app,' '); assert(app_screen(app)==SCR_MEAL_PLANNER);
    app_key(app,' '); assert(app_screen(app)==SCR_ACTIVITIES && app_selection(app)==10 && app_state(app)->today.flags==(1ul<<21));
    app_key(app,KEY_ENTER); assert(app_state(app)->today.flags==((1ul<<21)|(1ul<<10)) && cell_is(hw,7,27,"3 [x] Weekend"));
    app_key(app,'0'); assert(app_state(app)->today.flags==((1ul<<21)|(1ul<<10))); /* 8 rows: no row 10 */
    app_key(app,KEY_LEFT); app_key(app,plat_host_key(1,'&')); assert(app_state(app)->today.flags==((1ul<<21)|(1ul<<10)|1ul)); /* AZERTY 1 */
    app_key(app,'1'); app_key(app,KEY_ESCAPE);
    assert(app_screen(app)==SCR_HOME && row_has(hw,5,"2/22 activities"));
    app_key(app,'5'); assert(any_row(hw,"ACTIVITIES (2)") && any_row(hw,"  Weekend") && any_row(hw,"  Meditation")); app_key(app,KEY_ESCAPE);
    /* Closing the day: the ICS lists the ticks in catalog order, as the PicoCalc. */
    app_key(app,'8'); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD); app_key(app,KEY_ENTER);
    read_ics(dir,"20260917",got,sizeof(got));
    assert(strstr(got,"DESCRIPTION:Activities: Weekend, Meditation\\NXP:\r\n"));
    assert(day_names(1)->count==2 && !strcmp(day_names(1)->names[0],"Weekend") && !strcmp(day_names(1)->names[1],"M\xc3\xa9" "ditation") && day_names(0)->count==0);
    app_key(app,KEY_LEFT); assert(row_has(hw,5,"2/22 activities")); app_key(app,KEY_RIGHT);
    /* French: names with accents on screen (CP437), UTF-8 in the ICS. */
    app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,'2'); app_key(app,'4');
    app_key(app,'1'); assert(cell_is(hw,5,54,"  [ ] Sport") && cell_is(hw,10,54,"  [ ] M\202ditation") && cell_is(hw,6,27,"  [ ] Cong\202"));
    app_key(app,KEY_RIGHT); app_key(app,'2'); app_key(app,KEY_RIGHT); app_key(app,'1'); app_key(app,'6'); app_key(app,'3'); app_key(app,KEY_ESCAPE);
    assert(row_has(hw,5,"4/22 activit\202s"));
    app_key(app,'8'); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD);
    read_ics(dir,"20260918",got,sizeof(got));
    assert(strstr(got,"DESCRIPTION:Activit\xc3\xa9s: Cong\xc3\xa9, Sport, M\xc3\xa9nage, M\xc3\xa9" "ditation\\NXP:\r\n"));

    /* A PicoCalc catalog: renamed, moved, custom and English-only entries
     * show by their names; an archived day lists its frozen extras, ticked. */
    fresh_dir(dir); copy_file("tests/fixtures/picocalc_custom.jsn",pa);
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    if(app_screen(app)==SCR_REMINDER_POPUP) app_key(app,KEY_ENTER);
    assert(row_has(hw,5,"5/24 activit\202s"));
    app_key(app,'1'); /* 24 entries: 8 rows */
    assert(cell_is(hw,5,0,"1 [x] Sport") && cell_is(hw,10,0,"6 [x] Projet perso") && cell_is(hw,5,54,"  [ ] Marche"));
    assert(cell_is(hw,11,54,"  [x] V\202lo") && cell_is(hw,12,54,"  [x] Chess"));
    app_key(app,KEY_ESCAPE);
    app_key(app,KEY_LEFT); app_key(app,KEY_LEFT); app_key(app,KEY_LEFT); /* day 37 */
    assert(row_has(hw,1,"Mercredi 11 Novembre 2026") && row_has(hw,5,"4/24 activit\202s"));
    app_key(app,'1'); /* 24 + 1 extra: 9 rows */
    assert(app_screen(app)==SCR_ACTIVITIES && cell_is(hw,7,0,"3 [x] Bible") && cell_is(hw,12,27,"  [x] Marche"));
    assert(cell_is(hw,10,54,"  [x] Chess") && cell_is(hw,11,54,"  [x] Projet") && cell_is(hw,12,54,"   "));
    assert(row_has(hw,22,"<- -> colonnes   \220chap : menu du jour"));
    app_key(app,'3'); assert(row_has(hw,23,"Jour archiv\202 : lecture seule") && day_orphan_count(app_state(app),3)==1);
    app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN);
    app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN);
    assert(app_selection(app)==24 && plat_host_rev(hw,11,54)); /* the extra can be selected */
    app_key(app,KEY_ENTER); assert(row_has(hw,23,"Jour archiv\202 : lecture seule"));
    app_key(app,KEY_ESCAPE); app_key(app,KEY_LEFT); assert(row_has(hw,5,"4/24 activit\202s")); /* day 36, Tai-chi */
    app_key(app,KEY_RIGHT);
    /* ICS of a custom catalog: the export language's name, the other one when empty. */
    app_key(app,'1'); app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); app_key(app,'1'); app_key(app,KEY_ESCAPE);
    app_key(app,'6'); /* Kanban, Rosary hidden: Close the day is 6 */ app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD);
    read_ics(dir,"20261114",got,sizeof(got));
    assert(strstr(got,"DESCRIPTION:Activit\xc3\xa9s: Sport, Messe, Projet perso, Marche, V\xc3\xa9lo, Chess\\NXP:"));

    /* 30 entries: 10 rows, key 0 is row 10. A long name is cut in its cell and
     * shown whole under the grid. 30 entries plus 30 extras: two pages. */
    state_init(&s); s.today.number=2;
    for(i=22;i<30;i++){ s.activities[i].id=(uint16_t)(i+1); sprintf(s.activities[i].fr,"Nouveau %u",i+1); sprintf(s.activities[i].en,"New %u",i+1); }
    strcpy(s.activities[29].fr,"Une activite tres longue"); strcpy(s.activities[29].en,"A very long activity nam"); /* 24 bytes, the limit */
    s.activityCount=30; s.nextActivityId=31;
    s.history[0].number=1; s.history[0].date.year=2026; s.history[0].date.month=9; s.history[0].date.day=16; s.next=1; s.count=1;
    fresh_dir(dir); day_names_clear();
    day_names(1)->count=30;
    for(i=0;i<30;i++) sprintf(day_names(1)->names[i],"Extra %02u",i+1);
    assert(jsave_write(pa,&s,1,&check));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.activityCount==30 && out.history[0].flags==0 && day_orphan_count(&out,1)==30);
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    assert(row_has(hw,5,"0/30 activities"));
    app_key(app,'1');
    assert(cell_is(hw,14,0,"0 [ ] Day off") && cell_is(hw,14,54,"  [ ] A very long activity") && !strstr(plat_host_row(hw,14),"A very long activity n"));
    app_key(app,'0'); assert(app_state(app)->today.flags==(1ul<<9) && app_selection(app)==9);
    app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); assert(app_selection(app)==29);
    assert(row_has(hw,16,"A very long activity nam") && cell_is(hw,14,54,"0 [ ] A very long activity"));
    app_key(app,'0'); assert(app_state(app)->today.flags==((1ul<<9)|(1ul<<29)));
    app_key(app,KEY_ESCAPE); assert(row_has(hw,5,"2/30 activities"));
    app_key(app,KEY_LEFT); assert(row_has(hw,5,"30/30 activities"));
    app_key(app,'1'); /* 60 entries: 14 rows, 5 columns, 2 pages */
    assert(row_has(hw,3,"ACTIVITIES  1/2") && cell_is(hw,5,0,"1 [ ] Walk") && cell_is(hw,18,54,"  [x] Extra 12"));
    app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT);
    assert(app_selection(app)==42 && row_has(hw,3,"ACTIVITIES  2/2") && cell_is(hw,5,0,"1 [x] Extra 13") && cell_is(hw,5,27,"  [x] Extra 27"));
    assert(cell_is(hw,8,27,"  [x] Extra 30") && cell_is(hw,9,27,"   ") && cell_is(hw,5,54,"   "));
    app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN);
    app_key(app,KEY_RIGHT); assert(app_selection(app)==59); /* row 6 of a 4-row column: its last */
    app_key(app,KEY_RIGHT); assert(app_selection(app)==3 && row_has(hw,3,"ACTIVITIES  1/2"));
    app_key(app,KEY_LEFT); assert(app_selection(app)==59);
    app_key(app,KEY_ESCAPE); app_key(app,KEY_RIGHT);
    /* The save keeps the 30 entries, the 30 extras and both new ticks. */
    app_flush_save(app);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.activityCount==30 && out.today.flags==((1ul<<9)|(1ul<<29)) && day_orphan_count(&out,1)==30);
    /* An empty catalog says so instead of drawing a grid. */
    state_init(&s); s.activityCount=0; fresh_dir(dir); day_names_clear(); assert(jsave_write(pa,&s,1,&check));
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    assert(row_has(hw,5,"0/0 activities")); app_key(app,'1'); assert(row_has(hw,5,"N: add an activity"));
    app_key(app,'1'); app_key(app,KEY_ENTER); app_key(app,KEY_DOWN); app_key(app,KEY_RIGHT); assert(app_state(app)->today.flags==0);
}

/* The 8088 fast path (history flags renumbered in place) must equal the
 * PicoCalc's full rematch for every edit made with unique names. */
static unsigned long lcg_state=12345ul;
static unsigned lcg(unsigned n){ lcg_state=lcg_state*1103515245ul+12345ul; return (unsigned)((lcg_state>>16)&0x7fff)%n; }
/* A name some history day stored that the catalog lacks, if any. */
static int pick_orphan(const State *s, char *name){
    unsigned start=lcg(HISTORY_LENGTH), k;
    for(k=0;k<HISTORY_LENGTH;k++){
        unsigned slot=1+(start+k)%HISTORY_LENGTH, n=day_orphan_count(s,slot);
        if(n){ strcpy(name,day_orphan(s,slot,lcg(n))); return 1; }
    }
    return 0;
}
static unsigned orphan_hits;
static void check_fast_rematch(const char *fixture){
    static State base, fast, full; static DayNames names[DAY_SLOTS], saved[DAY_SLOTS];
    uint32_t g=0, c=0; unsigned round, d, tested=0;
    assert(jsave_read(fixture,&base,&g,&c)==SAVE_VALID);
    for(round=0;round<400;round++){
        unsigned op=lcg(5), n=base.activityCount, a=n?lcg(n):0, b=n?lcg(n):0; int ok=1;
        char name[ACTIVITY_NAME_LENGTH+1];
        if(!activity_names_unique(&base)) break;
        memcpy(saved,lps_day_names,sizeof(saved));
        fast=base; activity_pin_history(&fast);
        if(op==0) activity_swap(&fast,a,b);
        else if(op==1) activity_rotate(&fast,(int)lcg(2));
        else if(op==2 && n>1) activity_delete(&fast,a);
        else if(op==3 && n<ACTIVITY_CAPACITY){ /* add, sometimes under an orphan name */
            if(pick_orphan(&fast,name) && lcg(3)) {} else sprintf(name,"New %u",round);
            b=fast.activityCount++; memset(&fast.activities[b],0,sizeof(Activity)); fast.activities[b].id=fast.nextActivityId++;
            strcpy(fast.activities[b].fr,name);
        }else if(op==4 && n){ /* rename in either language, sometimes to an orphan */
            if(pick_orphan(&fast,name) && lcg(3)) {} else sprintf(name,"Renamed %u",round);
            strcpy(lcg(2)?fast.activities[a].fr:fast.activities[a].en,name); b=a;
        }else ok=0;
        if(!ok || !activity_names_unique(&fast)) continue;
        full=fast; memcpy(names,lps_day_names,sizeof(names));
        if(op>=3) activity_rematch_entry(&fast,b);
        activity_refresh_today(&fast);
        memcpy(lps_day_names,names,sizeof(names));
        activity_refresh(&full);
        if(op>=3 && activity_index(&full,name)<full.activityCount && full.count){ unsigned h; for(h=0;h<HISTORY_LENGTH;h++) if(FLAG_BIT(full.history[h].flags,b)) orphan_hits++; }
        assert(fast.today.flags==full.today.flags);
        for(d=0;d<HISTORY_LENGTH;d++) assert(fast.history[d].flags==full.history[d].flags);
        base=full; tested++;
        (void)saved;
    }
    assert(tested>100);
    day_names_clear();
}

/* LPS-DOS-115-CHG06: user-defined activities, as PicoCalc LPS-113-CHG07/07b. */
static void type_text(App *app, const char *text){ while(*text) app_key(app,(unsigned char)*text++); }
static void erase(App *app, unsigned n){ while(n--) app_key(app,KEY_BACKSPACE); }
static void choose_language(App *app, int fr){ app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,fr?'2':'1'); app_key(app,'4'); }
static void check_custom_activities(const char *dir){
    static State s, out; static unsigned char store[16384], store2[16384];
    App *app=(App*)store, *again=(App*)store2; Platform *hw; uint32_t gen=0; int active=-1; unsigned i;

    /* Rename in English only; French keeps its name; empty cancels; trim; duplicates refused. */
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    app_key(app,'1'); app_key(app,'e');
    assert(app_screen(app)==SCR_ACTIVITY_EDIT && row_has(hw,3,"RENAME ACTIVITY") && row_has(hw,6,"Walk") && row_has(hw,9,"4/24 characters"));
    erase(app,4); type_text(app,"Stroll"); app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_ACTIVITIES && app_selection(app)==0);
    assert(!strcmp(app_state(app)->activities[0].en,"Stroll") && !strcmp(app_state(app)->activities[0].fr,"Marche"));
    assert(cell_is(hw,5,0,"1 [ ] Stroll") && row_has(hw,21,"E: rename  N: new  Del: delete  F4/F5: move"));
    app_key(app,KEY_ESCAPE); choose_language(app,1);
    app_key(app,'1'); assert(cell_is(hw,5,0,"1 [ ] Marche") && !any_row(hw,"Stroll") && row_has(hw,21,"E : renommer"));
    app_key(app,KEY_ESCAPE); choose_language(app,0); app_key(app,'1'); assert(cell_is(hw,5,0,"1 [ ] Stroll"));
    app_key(app,'e'); erase(app,10); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_ACTIVITIES && !strcmp(app_state(app)->activities[0].en,"Stroll"));
    app_key(app,KEY_DOWN); app_key(app,'e'); erase(app,8); type_text(app,"  stroll  "); app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_ACTIVITY_EDIT && any_row(hw,"That name already exists") && !strcmp(app_state(app)->activities[1].en,"Diet"));
    app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_ACTIVITIES && app_selection(app)==1);
    app_key(app,'e'); erase(app,8); type_text(app,"  Solo  "); app_key(app,KEY_ENTER);
    assert(!strcmp(app_state(app)->activities[1].en,"Solo") && cell_is(hw,6,0,"2 [ ] Solo"));
    /* Too long: 24 characters at most, and 24 UTF-8 bytes (an accent counts twice). */
    app_key(app,'e'); erase(app,4); for(i=0;i<30;i++) app_key(app,'a');
    assert(row_has(hw,9,"24/24 characters")); erase(app,24);
    /* CHG06d: the count is UTF-8 bytes and typing stops at 24, as for tasks. */
    for(i=0;i<13;i++) app_key(app,plat_host_key(1,130));
    assert(row_has(hw,9,"24/24 characters") && row_has(hw,6,"\202\202\202\202\202\202\202\202\202\202\202\202") && !row_has(hw,6,"\202\202\202\202\202\202\202\202\202\202\202\202\202"));
    app_key(app,'a'); assert(row_has(hw,9,"24/24")); erase(app,1); assert(row_has(hw,9,"22/24"));
    app_key(app,KEY_ESCAPE);
    /* Create in French only, typing an accent: English shows the same name. */
    app_key(app,KEY_ESCAPE); choose_language(app,1);
    app_key(app,'1'); app_key(app,'n'); assert(app_screen(app)==SCR_ACTIVITY_EDIT && row_has(hw,3,"NOUVELLE ACTIVIT\220"));
    app_key(app,'R'); app_key(app,plat_host_key(1,130)); type_text(app,"veil"); assert(row_has(hw,6,"R\202veil")); app_key(app,KEY_ENTER);
    assert(app_state(app)->activityCount==23 && app_state(app)->activities[22].id==23 && app_selection(app)==22);
    assert(!strcmp(app_state(app)->activities[22].fr,"R\xc3\xa9veil") && !app_state(app)->activities[22].en[0]);
    assert(cell_is(hw,11,54,"7 [ ] R\202veil"));
    app_key(app,plat_host_key(1,130)); /* é outside a name is the AZERTY 2: ticks row 2 of the column */
    assert(app_state(app)->today.flags==(1ul<<17));
    app_key(app,plat_host_key(1,130)); assert(app_state(app)->today.flags==0);
    app_key(app,KEY_ESCAPE); choose_language(app,0);
    app_key(app,'1'); assert(cell_is(hw,11,54,"  [ ] R\202veil"));
    /* The custom list goes through the JSON save. */
    app_key(app,KEY_ESCAPE); app_flush_save(app);
    hw=plat_host_create(0); app_init(again,hw); app_start(again);
    assert(app_state(again)->activityCount==23 && !strcmp(app_state(again)->activities[0].en,"Stroll") && !strcmp(app_state(again)->activities[0].fr,"Marche"));
    assert(!strcmp(app_state(again)->activities[22].fr,"R\xc3\xa9veil") && app_state(again)->nextActivityId==24);

    /* Today's tick follows a rename and a move; Del asks first. */
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    app_key(app,'1'); app_key(app,KEY_ENTER); assert(app_state(app)->today.flags==1);
    app_key(app,'e'); erase(app,4); type_text(app,"Stroll"); app_key(app,KEY_ENTER);
    assert(app_state(app)->today.flags==1 && cell_is(hw,5,0,"1 [x] Stroll"));
    app_key(app,KEY_F5);
    assert(!strcmp(app_state(app)->activities[1].en,"Stroll") && app_state(app)->today.flags==2 && app_selection(app)==1 && cell_is(hw,6,0,"2 [x] Stroll"));
    app_key(app,KEY_F4); assert(!strcmp(app_state(app)->activities[0].en,"Stroll") && app_state(app)->today.flags==1);
    app_key(app,KEY_DELETE); assert(row_has(hw,21,"Delete Stroll?") && row_has(hw,22,"Enter: yes   Esc: no"));
    app_key(app,KEY_DOWN); app_key(app,'2'); assert(app_selection(app)==0 && app_state(app)->today.flags==1); /* other keys wait */
    app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_ACTIVITIES && app_state(app)->activityCount==22 && app_state(app)->today.flags==1);
    app_key(app,KEY_DELETE); app_key(app,KEY_ENTER);
    assert(app_state(app)->activityCount==21 && app_state(app)->today.flags==0 && !strcmp(app_state(app)->activities[0].en,"Diet"));
    assert(day_names(0)->count==0 && cell_is(hw,5,0,"1 [ ] Diet"));

    /* F4/F5 wrap; the tick and the cursor follow. */
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    app_key(app,'1'); app_key(app,KEY_ENTER);
    app_key(app,KEY_F4);
    assert(!strcmp(app_state(app)->activities[21].en,"Walk") && !strcmp(app_state(app)->activities[0].en,"Diet") && !strcmp(app_state(app)->activities[20].en,"Meditation"));
    assert(app_state(app)->today.flags==(1ul<<21) && app_selection(app)==21 && cell_is(hw,10,54,"6 [x] Walk") && cell_is(hw,5,0,"  [ ] Diet"));
    app_key(app,KEY_F5);
    assert(!strcmp(app_state(app)->activities[0].en,"Walk") && !strcmp(app_state(app)->activities[21].en,"Meditation") && app_state(app)->today.flags==1 && app_selection(app)==0);
    app_key(app,KEY_LEFT); app_key(app,KEY_UP); assert(app_selection(app)==21);
    app_key(app,KEY_F5);
    assert(!strcmp(app_state(app)->activities[0].en,"Meditation") && !strcmp(app_state(app)->activities[1].en,"Walk") && app_state(app)->today.flags==2 && app_selection(app)==0);
    app_key(app,KEY_F4);
    assert(!strcmp(app_state(app)->activities[0].en,"Walk") && !strcmp(app_state(app)->activities[21].en,"Meditation") && app_state(app)->today.flags==1 && app_selection(app)==21);

    /* History keeps its frozen names after a French rename and a delete;
     * the archive lists them read-only after the catalog. */
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    app_key(app,'0'); erase(app,10); type_text(app,"2026-11-09"); app_key(app,KEY_ENTER);
    app_key(app,'1'); app_key(app,KEY_ENTER); app_key(app,KEY_LEFT); app_key(app,KEY_ENTER); /* Walk, Sport */
    assert(app_state(app)->today.flags==((1ul<<0)|(1ul<<16)));
    app_key(app,KEY_ESCAPE); app_key(app,'8'); app_key(app,KEY_ENTER); app_key(app,KEY_ENTER);
    assert(app_state(app)->count==1 && app_state(app)->today.flags==0);
    assert(day_names(1)->count==2 && !strcmp(day_names(1)->names[0],"Marche") && !strcmp(day_names(1)->names[1],"Sport"));
    while(app_screen(app)!=SCR_HOME) app_key(app,KEY_ESCAPE);
    app_key(app,KEY_LEFT); assert(row_has(hw,5,"2/22 activities"));
    app_key(app,'1'); assert(cell_is(hw,5,0,"1 [x] Walk"));
    app_key(app,KEY_ESCAPE); app_key(app,KEY_RIGHT);
    choose_language(app,1);
    app_key(app,'1'); app_key(app,'e'); erase(app,10); type_text(app,"Promenade"); app_key(app,KEY_ENTER);
    assert(!strcmp(app_state(app)->activities[0].fr,"Promenade") && !strcmp(app_state(app)->activities[0].en,"Walk"));
    app_key(app,KEY_LEFT); app_key(app,KEY_DELETE); assert(row_has(hw,21,"Supprimer Sport ?")); app_key(app,KEY_ENTER);
    assert(app_state(app)->activityCount==21 && app_selection(app)==16);
    app_key(app,KEY_ESCAPE); app_key(app,KEY_LEFT); assert(row_has(hw,5,"2/21 activit\202s"));
    app_key(app,'1');
    assert(cell_is(hw,5,0,"1 [ ] Promenade") && cell_is(hw,10,54,"  [x] Marche") && cell_is(hw,11,54,"  [x] Sport"));
    app_key(app,'e'); app_key(app,'n'); app_key(app,KEY_DELETE); app_key(app,KEY_F4); app_key(app,KEY_F5); app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_ACTIVITIES && any_row(hw,"lecture seule") && app_state(app)->activityCount==21 && day_names(1)->count==2);
    assert(!strcmp(app_state(app)->activities[0].fr,"Promenade"));
    app_key(app,KEY_ESCAPE); app_key(app,KEY_RIGHT); app_flush_save(app);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.activityCount==21 && day_orphan_count(&out,1)==2 && out.history[0].flags==0);

    /* Limits: 30 entries, then delete them all; ids go on from the highest. */
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app); app_key(app,'1');
    for(i=0;i<8;i++){ char name[8]; sprintf(name,"Extra%u",i); app_key(app,'n'); type_text(app,name); app_key(app,KEY_ENTER); }
    assert(app_state(app)->activityCount==30 && app_state(app)->activities[29].id==30 && cell_is(hw,14,54,"0 [ ] Extra7"));
    app_key(app,'n'); assert(app_screen(app)==SCR_ACTIVITIES && any_row(hw,"Limit: 30 activities"));
    while(app_state(app)->activityCount){ app_key(app,KEY_DELETE); app_key(app,KEY_ENTER); }
    assert(row_has(hw,5,"N: add an activity") && app_selection(app)==0);
    app_key(app,KEY_ESCAPE); assert(row_has(hw,5,"0/0 activities"));
    app_key(app,'1'); app_key(app,'n'); type_text(app,"Solo"); app_key(app,KEY_ENTER);
    assert(app_state(app)->activityCount==1 && app_state(app)->activities[0].id==31 && cell_is(hw,5,0,"1 [ ] Solo"));

    /* A history day without stored names (BIN migration in this session) is
     * pinned to its names before the list moves, as a reload would do. */
    state_init(&s); day_names_clear(); s.count=1; s.next=1; s.history[0].flags=1;
    activity_pin_history(&s);
    assert(day_names(1)->count==1 && !strcmp(day_names(1)->names[0],"Marche"));
    activity_rotate(&s,1); activity_refresh(&s); assert(s.history[0].flags==(1ul<<21));
    assert(activity_names_equal("M\xc3\xa9nage","m\xc3\x89NAGE") && !activity_names_equal("Sport","Sports"));
    day_names_clear();
}

/* LPS-DOS-115-CHG06b: top-row keys by BIOS scan code, any layout, no Shift. */
static void fr_type(App *app, const char *cp437){ while(*cp437) app_key(app,plat_host_key(1,(unsigned char)*cp437++)); }
static void check_bios_keys(const char *dir){
    static unsigned char store[16384]; App *app=(App*)store; Platform *hw; static State out; uint32_t gen=0; int active=-1;
    /* The mapping itself. */
    assert(app_bios_key('1',0x02)==KEY_TOPROW('1','1') && app_bios_key('0',0x0B)==KEY_TOPROW('0','0'));
    assert(app_bios_key(130,0x03)==KEY_TOPROW(130,'2') && KEY_CHAR(app_bios_key(130,0x03))==130 && KEY_TOPROW_DIGIT(app_bios_key(130,0x03))=='2');
    assert(app_bios_key('&',0x08)==KEY_TOPROW('&','7'));              /* US Shift+7 */
    assert(app_bios_key('&',0x02)==KEY_TOPROW('&','1'));              /* AZERTY 1 */
    assert(app_bios_key('7',0x47)=='7' && app_bios_key('0',0x52)=='0'); /* keypad with NumLock */
    assert(app_bios_key(0,0x48)==KEY_UP && app_bios_key(0xE0,0x53)==KEY_DELETE && app_bios_key(0,0x3E)==KEY_F4);
    assert(app_bios_key(13,0x1C)==KEY_ENTER && app_bios_key(27,0x01)==KEY_ESCAPE && app_bios_key(8,0x0E)==KEY_BACKSPACE);
    assert(app_bios_key('q',0x1E)=='q' && app_bios_key(1,0x1E)==-1 && app_bios_key(0,0x47)==-1);
    assert(plat_host_key(1,'-')==KEY_TOPROW('-','6') && plat_host_key(0,'-')=='-' && plat_host_key(1,',')==',');

    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    /* Home: AZERTY & is 1; US Shift+7 (&) is 7 (Timer), not 1; a bare & is nothing. */
    app_key(app,plat_host_key(1,'&')); assert(app_screen(app)==SCR_ACTIVITIES); app_key(app,KEY_ESCAPE);
    app_key(app,plat_host_key(0,'&')); assert(app_screen(app)==SCR_TIMER_SELECT); app_key(app,KEY_ESCAPE);
    app_key(app,'&'); assert(app_screen(app)==SCR_HOME);
    app_key(app,app_bios_key('3',0x51)); assert(app_screen(app)==SCR_KANBAN); app_key(app,KEY_ESCAPE); /* keypad 3 */
    /* Config (TAB) with the AZERTY top row: & Language, é French, ' (4) Save. */
    app_key(app,KEY_TAB); fr_type(app,"&\202'"); assert(app_screen(app)==SCR_HOME && app_state(app)->language==LANG_FR);
    /* Timer: è (7) then & starts 5 minutes. */
    fr_type(app,"\212&"); assert(app_screen(app)==SCR_TIMER_RUNNING); app_key(app,KEY_ESCAPE); app_key(app,KEY_ESCAPE);
    assert(app_screen(app)==SCR_HOME);
    /* Date: à (0); unshifted 6 is '-' as AZERTY prints it, Shift+6 is 6. */
    fr_type(app,"\205"); assert(app_screen(app)==SCR_DATE); erase(app,10);
    fr_type(app,"\202\205\2026-&&-&6"); assert(row_has(hw,6,"2026-11-16")); app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_HOME && app_state(app)->today.date.year==2026 && app_state(app)->today.date.month==11 && app_state(app)->today.date.day==16);
    /* Weight: é (2), then &&à,5 = 110,5. */
    fr_type(app,"\202"); assert(app_screen(app)==SCR_WEIGHT); erase(app,8); fr_type(app,"&&\205,5"); app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_HOME && app_state(app)->today.expected==110500);
    /* Activities: AZERTY é ticks row 2; US Shift+7 ticks row 7 (Nap), never row 1. */
    fr_type(app,"&"); fr_type(app,"\202"); assert(app_state(app)->today.flags==(1ul<<1));
    app_key(app,plat_host_key(0,'&')); assert(app_state(app)->today.flags==((1ul<<1)|(1ul<<6)));
    app_key(app,'&'); assert(app_state(app)->today.flags==((1ul<<1)|(1ul<<6)));
    /* Text entry keeps the characters: activity name, task, reminder text. */
    app_key(app,'n'); fr_type(app,"\202t\202 & \205"); assert(row_has(hw,6,"\202t\202 & \205")); app_key(app,KEY_ENTER);
    assert(!strcmp(app_state(app)->activities[22].fr,"\xc3\xa9t\xc3\xa9 & \xc3\xa0"));
    app_key(app,KEY_ESCAPE);
    fr_type(app,"\""); assert(app_screen(app)==SCR_KANBAN); app_key(app,'n'); fr_type(app,"a&(_'\"-\202"); app_key(app,KEY_ENTER); /* é is a letter here too (CHG06c) */
    assert(app_state(app)->taskCount==1 && !strcmp(app_state(app)->tasks[0].name,"a&(_'\"-\xc3\xa9") && app_screen(app)==SCR_KANBAN);
    app_key(app,KEY_ESCAPE);
    fr_type(app,"-"); assert(app_screen(app)==SCR_REMINDERS); /* - is 6 (LPS-DOS-115-CHG10 numbering) */ app_key(app,'n'); assert(app_screen(app)==SCR_REMINDER_EDIT);
    app_key(app,KEY_DOWN); fr_type(app,"&"); /* days field: & is 1 */
    app_key(app,KEY_DOWN); fr_type(app,"&\"'("); assert(row_has(hw,11,"&\"'("));
    app_key(app,KEY_ENTER); assert(app_state(app)->reminderCount==1 && !strcmp(app_state(app)->reminders[0].text,"&\"'(") && app_state(app)->reminders[0].duration==11);
    app_key(app,KEY_ESCAPE); app_flush_save(app);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.language==LANG_FR && out.taskCount==1 && out.activityCount==23);
}

/* LPS-DOS-115-CHG06c: accented task names and reminder texts. CP437 on screen
 * and in the State, UTF-8 in the save, within the PicoCalc's 32 UTF-8 bytes. */
static size_t both_saves(const char *dir, char *buf, size_t cap){
    char p[256]; size_t n; jpath(p,dir,"SAVE_A.JSN"); n=exists(p)?slurp(p,buf,cap):0;
    jpath(p,dir,"SAVE_B.JSN"); if(exists(p)) n+=slurp(p,buf+n,cap-n); buf[n]=0; return n;
}
static void check_accented_text(const char *dir){
    static unsigned char store[16384]; static State s, out; App *app=(App*)store; Platform *hw;
    uint32_t gen=0, check=0; int active=-1; unsigned i; char path[256], name[40];
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    choose_language(app,1);
    app_key(app,'0'); erase(app,10); type_text(app,"2026-11-16"); app_key(app,KEY_ENTER);
    /* Kanban: KEYB FR keys (é è ç à from the top row, ê ô î ï û from dead keys).
     * 26 letters, 33 UTF-8 bytes: the final s does not fit. */
    app_key(app,'3'); app_key(app,'n'); fr_type(app,"\220crire \205 H\202l\212ne, c\223t\202 ma\213s");
    assert(row_has(hw,6,"\220crire \205 H\202l\212ne, c\223t\202 ma\213") && !row_has(hw,6,"ma\213s") && row_has(hw,9,"32/32 caract\212res"));
    app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_KANBAN && any_row(hw,"\220crire \205 H\202l\212ne")); /* the column cuts long names */
    assert(!strcmp(app_state(app)->tasks[0].name,"\xc3\x89" "crire \xc3\xa0 H\xc3\xa9l\xc3\xa8ne, c\xc3\xb4t\xc3\xa9 ma\xc3\xaf")); /* UTF-8 in memory (CHG06d) */
    /* 16 é = 32 bytes; ₧ and ƒ are refused (the PicoCalc cannot show them, CHG06d); a 2-byte letter does not fit in 1 byte. */
    app_key(app,'n'); for(i=0;i<17;i++) app_key(app,plat_host_key(1,130));
    assert(strlen(plat_host_row(hw,6))>=16 && row_has(hw,9,"32/32") && !row_has(hw,6,"\202\202\202\202\202\202\202\202\202\202\202\202\202\202\202\202\202"));
    app_key(app,'a'); assert(row_has(hw,9,"32/32")); app_key(app,KEY_ENTER);
    app_key(app,'n'); app_key(app,0x9E); app_key(app,0x9F); type_text(app,"ab"); app_key(app,0x9E); assert(row_has(hw,9,"2/32") && row_has(hw,6,"ab")); app_key(app,KEY_ENTER);
    app_key(app,'n'); for(i=0;i<30;i++) app_key(app,'x'); app_key(app,0x97); app_key(app,0x88); app_key(app,'y'); assert(row_has(hw,9,"32/32")); app_key(app,KEY_ENTER);
    app_key(app,'n'); fr_type(app,"\226\203\214\210\211\201\230 \227\207\205\212 \237"); assert(row_has(hw,9,"24/32")); app_key(app,KEY_ENTER);
    assert(app_state(app)->taskCount==5);
    for(i=0;i<16;i++){ name[2*i]='\xc3'; name[2*i+1]='\xa9'; } name[32]=0;
    assert(!strcmp(app_state(app)->tasks[1].name,name));
    assert(!strcmp(app_state(app)->tasks[2].name,"ab"));
    /* Editing an accented name: Backspace takes one letter. */
    app_key(app,KEY_UP); app_key(app,KEY_UP); app_key(app,KEY_UP); app_key(app,KEY_UP); app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_TASK_EDIT && row_has(hw,9,"32/32")); erase(app,1); assert(row_has(hw,9,"30/32")); /* ï */
    fr_type(app,"s"); app_key(app,KEY_ENTER); assert(!strcmp(app_state(app)->tasks[0].name,"\xc3\x89" "crire \xc3\xa0 H\xc3\xa9l\xc3\xa8ne, c\xc3\xb4t\xc3\xa9 mas"));
    app_key(app,KEY_ESCAPE);
    /* Reminder text, same 32-byte limit. */
    app_key(app,'6'); app_key(app,'n'); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN);
    fr_type(app,"F\210te : cr\212me br\226l\202e \205 20h, d\202j\205 !");
    assert(row_has(hw,11,"F\210te : cr\212me br\226l\202e \205 20h, ") && !row_has(hw,11,", d")); /* 32 bytes: the rest does not fit */
    app_key(app,KEY_ENTER); assert(app_state(app)->reminderCount==1 && !strcmp(app_state(app)->reminders[0].text,"F\xc3\xaate : cr\xc3\xa8me br\xc3\xbbl\xc3\xa9" "e \xc3\xa0 20h, "));
    assert(any_row(hw,"F\210te : cr\212me br\226l\202e"));
    app_key(app,KEY_ESCAPE); app_flush_save(app);
    /* The save holds UTF-8, every byte of it (no cut at 32 bytes). */
    both_saves(dir,jbuf,sizeof(jbuf));
    assert(strstr(jbuf,"\"name\":\"\xc3\x89" "crire \xc3\xa0 H\xc3\xa9l\xc3\xa8ne, c\xc3\xb4t\xc3\xa9 mas\"}"));
    assert(strstr(jbuf,"\"name\":\"\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\"}"));
    assert(strstr(jbuf,"\"name\":\"ab\"}"));
    assert(strstr(jbuf,"xxxxxxx\xc3\xb9\"}") && strstr(jbuf,"\"name\":\"\xc3\xbb\xc3\xa2\xc3\xae\xc3\xaa\xc3\xab\xc3\xbc\xc3\xbf \xc3\xb9\xc3\xa7\xc3\xa0\xc3\xa8 \"}"));
    assert(strstr(jbuf,"\"text\":\"F\xc3\xaate : cr\xc3\xa8me br\xc3\xbbl\xc3\xa9" "e \xc3\xa0 20h, \"}"));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.taskCount==5 && !strcmp(out.tasks[1].name,app_state(app)->tasks[1].name)
        && !strcmp(out.reminders[0].text,app_state(app)->reminders[0].text));
    /* The PicoCalc re-saves this one byte for byte (tests/fixtures/picocalc_accents.jsn,
     * a DOS save of this kind passed through the PicoCalc writer): DOS loads
     * it, shows the letters, and writes the same bytes. */
    fresh_dir(dir); jpath(path,dir,"SAVE_A.JSN"); copy_file("tests/fixtures/picocalc_accents.jsn",path);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.taskCount==5 && out.reminderCount==1);
    assert(!strcmp(out.tasks[0].name,"\xc3\x89" "crire \xc3\xa0 H\xc3\xa9l\xc3\xa8ne, c\xc3\xb4t\xc3\xa9 mas") && !strcmp(out.tasks[1].name,name));
    assert(!strcmp(out.tasks[2].name,"\xe2\x82\xa7\xe2\x82\xa7\xe2\x82\xa7\xe2\x82\xa7\xe2\x82\xa7\xe2\x82\xa7\xe2\x82\xa7\xe2\x82\xa7\xe2\x82\xa7\xe2\x82\xa7" "ab")); /* PicoCalc text: kept even if typing refuses it now */
    jpath(path,dir,"OUT.JSN"); assert(jsave_write(path,&out,gen,&check));
    { size_t n=slurp("tests/fixtures/picocalc_accents.jsn",jbuf,sizeof(jbuf)), n2=slurp(path,jbuf2,sizeof(jbuf2)); assert(n==n2 && !memcmp(jbuf,jbuf2,n)); }
    /* ICS: as on the PicoCalc, tasks and reminders are not exported; the
     * note and activity names are UTF-8. */
    { static char ics[2048]; strcpy(out.today.note,"No\xc3\xabl \xc3\xa0 la for\xc3\xaat");
      assert(build_ics(&out.today,&out,0,0,LANG_FR,ics,sizeof(ics)));
      assert(strstr(ics,"\\NNote: No\xc3\xabl \xc3\xa0 la for\xc3\xaat\r\n") && !strstr(ics,"crire") && !strstr(ics,"te : cr")); }
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    if(app_screen(app)==SCR_REMINDER_POPUP) app_key(app,KEY_ENTER);
    app_key(app,'3'); assert(any_row(hw,"\220crire \205 H\202l\212ne") && any_row(hw,"\202\202\202\202\202\202\202\202"));
    app_key(app,KEY_ESCAPE); app_key(app,'6'); assert(any_row(hw,"F\210te : cr\212me br\226l\202e"));
    /* BIN migration: the old binary saves only ever held ASCII (both writers
     * were ASCII-only, and both readers refuse other bytes); accents typed
     * after the migration go to the JSON save. */
    fresh_dir(dir); sample_state(&s,1); strcpy(s.today.note,"ASCII"); strcpy(s.history[0].note,"ASCII"); s.tasks[0].status=TASK_TODO; strcpy(s.tasks[0].name,"Fix the bike"); strcpy(s.reminders[0].text,"Christmas");
    write_bin(dir,"SAVE_A.BIN",&s,3);
    assert(save_load(dir,&out,&gen,&active)==LOAD_MIGRATED);
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    if(app_screen(app)==SCR_REMINDER_POPUP) app_key(app,KEY_ENTER);
    app_key(app,'3'); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_TASK_EDIT); type_text(app," \207a"); app_key(app,KEY_ENTER);
    app_key(app,KEY_ESCAPE); app_flush_save(app); both_saves(dir,jbuf,sizeof(jbuf));
    assert(strstr(jbuf,"\"name\":\"Fix the bike \xc3\xa7" "a\"}"));
    fresh_dir(dir); strcpy(s.tasks[0].name,"Fix \202"); write_bin(dir,"SAVE_A.BIN",&s,3);
    assert(save_load(dir,&out,&gen,&active)!=LOAD_MIGRATED);
}

/* LPS-DOS-115-CHG06d: files are the same bytes on the PicoCalc, DOS and the
 * console. tests/fixtures/picocalc_intl.* come from the PicoCalc code
 * (tools/picocalc_fixture.cpp intl / close): text CP437 lacks (œ, À, €,
 * emoji) in catalog names, a frozen day name, notes, tasks and reminders. */
static int same_file(const char *a, const char *b){
    size_t n=slurp(a,jbuf,sizeof(jbuf)), n2=slurp(b,jbuf2,sizeof(jbuf2)); return n==n2 && !memcmp(jbuf,jbuf2,n);
}
static void check_intl_interop(const char *dir){
    static unsigned char store[16384]; static State out; App *app=(App*)store; Platform *hw;
    uint32_t gen=0, check=0; int active=-1; char path[256], pa[256], pb[256];
    /* JSON: every byte kept in memory, written back the same. */
    fresh_dir(dir); jpath(pa,dir,"SAVE_A.JSN"); jpath(pb,dir,"SAVE_B.JSN"); copy_file("tests/fixtures/picocalc_intl.jsn",pa);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen==12 && active==0);
    assert(!strcmp(out.activities[0].fr,"Marche \xe2\x82\xac \xf0\x9f\x99\x82") && !strcmp(out.activities[1].en,"\xc5\x92uvre \xc3\x80 faire"));
    assert(!strcmp(out.activities[22].fr,"\xc5\x92ufs; c\xc5\x93ur, \xc3\x80 \xf0\x9f\x8d\xb3") && !strcmp(out.activities[22].en,"Eggs \xf0\x9f\x8d\xb3"));
    assert(day_orphan_count(&out,2)==1 && !strcmp(day_orphan(&out,2,0),"P\xc3\xa2tes \xf0\x9f\x8d\x9d \xc3\x80 l'\xc5\x93il"));
    assert(!strcmp(out.today.note,"\xc5\x92uvre du jour, \xc3\x80 10 \xe2\x82\xac \xf0\x9f\x98\x80; \xc2\xab bien \xc2\xbb\\N fin"));
    assert(!strcmp(ring(&out,1)->note,"C\xc5\x93ur: \xc3\x80 5 \xe2\x82\xac \xf0\x9f\x99\x82, \"ok\" \\ fin; oui"));
    assert(out.taskCount==3 && !strcmp(out.tasks[0].name,"Acheter \xc5\x93ufs \xc3\x80 3 \xe2\x82\xac \xf0\x9f\x99\x82") && !strcmp(out.tasks[1].name,"\xc5\x92uvre, \xf0\x9f\x98\x80 \"x\" \\ y"));
    assert(out.reminderCount==2 && !strcmp(out.reminders[0].text,"F\xc3\xaate \xf0\x9f\x8e\x89 \xc3\x80 20h, 15 \xe2\x82\xac") && !strcmp(out.reminders[1].text,"C\xc5\x93ur \xc5\x93uvre \xc5\x92\xc3\x80\xe2\x82\xac"));
    jpath(path,dir,"OUT.JSN"); assert(jsave_write(path,&out,gen,&check) && same_file("tests/fixtures/picocalc_intl.jsn",path));
    /* Close the day on DOS: the weight CSV, the ICS and the next save are the
     * PicoCalc's bytes (its file names differ: EXPORT\YYYYMMDD.ICS, CHG02c). */
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    if(app_screen(app)==SCR_REMINDER_POPUP){ assert(any_row(hw,"F\210te ? ? 20h, 15 ?")); app_key(app,KEY_ENTER); }
    assert(app_screen(app)==SCR_HOME && row_has(hw,12,"F\210te ? ? 20h, 15 ?") && row_has(hw,13,"C?ur ?uvre ???"));
    app_key(app,'8'); assert(app_screen(app)==SCR_FINISH); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD);
    jpath(path,dir,"EXPORT\\20261115.ICS"); /* write_ics_file joins with a DOS backslash */ assert(same_file("tests/fixtures/picocalc_intl.ics",path));
    jpath(path,dir,"WEIGHTS\\2026.csv"); assert(same_file("tests/fixtures/picocalc_intl.csv",path));
    assert(same_file("tests/fixtures/picocalc_intl_closed.jsn",pb));
    /* On screen CP437 shows '?' for what it lacks; the bytes stay. */
    app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_HOME);
    app_key(app,'1'); assert(cell_is(hw,5,0,"1 [ ] Marche ? ?") && row_has(hw,11,"  [ ] ?ufs; c?ur, ? ?"));
    app_key(app,KEY_ESCAPE); app_key(app,'3'); assert(any_row(hw,"Acheter ?ufs ? 3 ? ?"));
    /* Edits that change nothing leave the bytes alone: open and save a task,
     * a reminder and an activity name. */
    app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_TASK_EDIT && row_has(hw,6,"Acheter ?ufs ? 3 ? ?") && row_has(hw,9,"27/32"));
    app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE);
    app_key(app,'6'); app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REMINDER_EDIT && row_has(hw,11,"C?ur ?uvre ???"));
    app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REMINDERS); app_key(app,KEY_ESCAPE);
    app_key(app,'1'); app_key(app,'e'); assert(row_has(hw,6,"Marche ? ?") && row_has(hw,9,"15/24")); app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE);
    app_flush_save(app);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && gen>13);
    jpath(path,dir,"OUT.JSN"); assert(jsave_write(path,&out,13,&check) && same_file("tests/fixtures/picocalc_intl_closed.jsn",path)); /* nothing moved */
    assert(!strcmp(out.tasks[0].name,"Acheter \xc5\x93ufs \xc3\x80 3 \xe2\x82\xac \xf0\x9f\x99\x82") && !strcmp(out.reminders[1].text,"C\xc5\x93ur \xc5\x93uvre \xc5\x92\xc3\x80\xe2\x82\xac"));
    assert(!strcmp(out.activities[0].fr,"Marche \xe2\x82\xac \xf0\x9f\x99\x82"));
    /* Backspace takes a whole UTF-8 character: the emoji (4 bytes), then €. */
    app_key(app,'3'); app_key(app,KEY_ENTER); erase(app,1); assert(row_has(hw,9,"23/32") && row_has(hw,6,"Acheter ?ufs ? 3 ? "));
    erase(app,2); assert(row_has(hw,9,"19/32")); app_key(app,0x80); app_key(app,KEY_ENTER); /* + Ç */
    assert(!strcmp(app_state(app)->tasks[0].name,"Acheter \xc5\x93ufs \xc3\x80 3 \xc3\x87"));
    app_key(app,KEY_ESCAPE); app_flush_save(app);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && !strcmp(out.tasks[0].name,"Acheter \xc5\x93ufs \xc3\x80 3 \xc3\x87"));
    /* The PicoCalc reads what DOS wrote: checked with tools/picocalc_fixture check (README). */
}

/* ---- Meal Planner (LPS-DOS-115-CHG07) ------------------------------------ */
static void put_file(const char *path, const unsigned char *buf, size_t n){ write_file(path,buf,n); }
static int open_variant(const char *path){ meals_close(); meals_set_path(path); return meals_open(); }
static void check_meals_dat(const char *tmpdir){
    static unsigned char dat[MEALS_DAT_SIZE+1], bad[MEALS_DAT_SIZE+1];
    char path[256], cwd[512]; long n; unsigned i, k; const uint8_t *pl, *first, *again, *pal;
    const size_t pictures=MEALS_DAT_HEADER+(size_t)MEAL_PICTURE_COUNT*MEALS_TABLE_ENTRY;
    n=read_file(MEALS_DAT_NAME,dat,sizeof(dat));
    if(n<0){ fprintf(stderr,"missing MEALS.DAT (run `make meals` in lps-companion)\n"); assert(0); }
    assert(n==MEALS_DAT_SIZE && n==199964 && !memcmp(dat,MEALS_DAT_MAGIC,8));
    { static const uint8_t w[4]={1,0,2,0}; assert(meals_checksum(w,4)==0x00040003ul && meals_checksum(w,0)==0); }
    assert(open_variant(MEALS_DAT_NAME)==MEALS_OK);
    for(i=0;i<MEAL_PICTURE_COUNT;i++){
        pal=meals_palette(i);
        assert(pal && pal[0]==0 && pal[1]==0 && pal[MEAL_INK_BODY]==0x3F && pal[MEAL_INK_TEXT]==0x00);
        for(k=2;k<16;k++) assert(pal[k]<64);
        assert(meals_picture(i,&pl)==MEALS_OK && pl);
        if(!i) first=pl;
        /* Every pixel is a card colour (2-15): index bits 1-3 are never all 0. */
        for(k=0;k<MEALS_PLANE_BYTES;k++) assert((pl[MEALS_PLANE_BYTES+k]|pl[2*MEALS_PLANE_BYTES+k]|pl[3*MEALS_PLANE_BYTES+k])==0xFF);
        /* The top-left corner is the card body (2) in every picture. */
        assert((pl[0]&0x80)==0 && (pl[MEALS_PLANE_BYTES]&0x80) && !(pl[2*MEALS_PLANE_BYTES]&0x80) && !(pl[3*MEALS_PLANE_BYTES]&0x80));
        assert(meals_checksum(pl,MEALS_PICTURE_BYTES)==((uint32_t)pal[16]|((uint32_t)pal[17]<<8)|((uint32_t)pal[18]<<16)|((uint32_t)pal[19]<<24)));
    }
    assert(meals_picture(0,&again)==MEALS_OK && again==first); /* kept until closed */
    assert(meals_picture(MEAL_PICTURE_COUNT,&pl)==MEALS_CORRUPT && !pl);
    meals_close(); assert(!meals_palette(0) && meals_picture(0,&pl)==MEALS_CORRUPT);
    /* Damaged copies are refused when the planner opens. */
    sprintf(path,"%s/MEALS.DAT",tmpdir);
    {   static const size_t at[]={0,8,10,12,14,16,20,MEALS_DAT_HEADER+5,MEALS_DAT_HEADER+MEALS_TABLE_ENTRY+17};
        for(i=0;i<sizeof(at)/sizeof(at[0]);i++){
            memcpy(bad,dat,(size_t)n); bad[at[i]]^=0x01;
            put_file(path,bad,(size_t)n); assert(open_variant(path)==MEALS_CORRUPT && !meals_palette(0));
        }
    }
    put_file(path,dat,(size_t)n-1); assert(open_variant(path)==MEALS_CORRUPT);           /* truncated */
    memcpy(bad,dat,(size_t)n); bad[n]=0; put_file(path,bad,(size_t)n+1); assert(open_variant(path)==MEALS_CORRUPT); /* longer */
    /* A palette value past 63, the table CRC made right: still refused. */
    memcpy(bad,dat,(size_t)n); bad[MEALS_DAT_HEADER+3*MEALS_TABLE_ENTRY+9]=64;
    put32(bad+20,crc32(bad+MEALS_DAT_HEADER,(size_t)MEAL_PICTURE_COUNT*MEALS_TABLE_ENTRY));
    put_file(path,bad,(size_t)n); assert(open_variant(path)==MEALS_CORRUPT);
    /* A damaged picture: the planner opens, that picture is refused, the
     * others still load. */
    memcpy(bad,dat,(size_t)n); bad[pictures+5u*MEALS_PICTURE_BYTES+1234]^=0x10;
    put_file(path,bad,(size_t)n); assert(open_variant(path)==MEALS_OK);
    assert(meals_picture(5,&pl)==MEALS_CORRUPT && !pl && meals_picture(4,&pl)==MEALS_OK && meals_picture(6,&pl)==MEALS_OK);
    meals_close(); remove(path);
    /* Not next to the EXE: the current directory's copy; nowhere: missing. */
    assert(open_variant(path)==MEALS_OK && meals_picture(MP_SOUP,&pl)==MEALS_OK && pl); meals_close();
    assert(getcwd(cwd,sizeof(cwd)) && chdir(tmpdir)==0);
    assert(open_variant(path)==MEALS_MISSING && !meals_palette(0));
    assert(chdir(cwd)==0);
    meals_set_path(MEALS_DAT_NAME);
}
/* LPS-DOS-115-CHG14: MEALSCGA.DAT, the CGA pictures. */
static int open_cga_variant(const char *path){ mealscga_close(); mealscga_set_path(path); return mealscga_open(); }
static unsigned cga_pixel(const uint8_t *p, unsigned x, unsigned y){ return (p[y*MEALSCGA_ROW_BYTES+x/4u]>>(6u-2u*(x%4u)))&3u; }
static void check_mealscga_dat(const char *tmpdir){
    static unsigned char dat[MEALSCGA_DAT_SIZE+1], bad[MEALSCGA_DAT_SIZE+1];
    char path[256], cwd[512]; long n; unsigned i, x, y; const uint8_t *pl, *first, *again, *ega;
    const size_t pictures=MEALS_DAT_HEADER+(size_t)MEAL_PICTURE_COUNT*MEALSCGA_TABLE_ENTRY;
    unsigned count[MEAL_PICTURE_COUNT][4];
    n=read_file(MEALSCGA_DAT_NAME,dat,sizeof(dat));
    if(n<0){ fprintf(stderr,"missing MEALSCGA.DAT (run `make mealscga` in lps-companion)\n"); assert(0); }
    assert(n==MEALSCGA_DAT_SIZE && n==49996 && !memcmp(dat,MEALSCGA_DAT_MAGIC,8) && MEALSCGA_PICTURE_BYTES==3840);
    assert(open_cga_variant(MEALSCGA_DAT_NAME)==MEALS_OK);
    for(i=0;i<MEAL_PICTURE_COUNT;i++){
        const uint8_t *t=dat+MEALS_DAT_HEADER+i*MEALSCGA_TABLE_ENTRY;
        assert(mealscga_picture(i,&pl)==MEALS_OK && pl);
        if(!i) first=pl;
        assert(meals_checksum(pl,MEALSCGA_PICTURE_BYTES)==((uint32_t)t[0]|((uint32_t)t[1]<<8)|((uint32_t)t[2]<<16)|((uint32_t)t[3]<<24)));
        /* The corners are the card background (colour 0, never dithered). */
        assert(!cga_pixel(pl,0,0) && !cga_pixel(pl,159,0) && !cga_pixel(pl,0,95) && !cga_pixel(pl,159,95));
        memset(count[i],0,sizeof(count[i]));
        for(y=0;y<MEALSCGA_HEIGHT;y++) for(x=0;x<MEALSCGA_WIDTH;x++) count[i][cga_pixel(pl,x,y)]++;
        assert(count[i][0]<MEALSCGA_WIDTH*MEALSCGA_HEIGHT*3u/4u && count[i][3]>0); /* a drawing, with yellow */
    }
    /* Colours land where the sheets have them: the apple is red, mint tea and
     * salad have green, latte and black coffee none, the cheese mostly yellow. */
    assert(count[MP_APPLE][2]>count[MP_APPLE][3] && count[MP_APPLE][2]>count[MP_APPLE][1]);
    assert(count[MP_MINT_TEA][1]>500 && count[MP_SALAD][1]>500 && !count[MP_LATTE][1] && !count[MP_BLACK][1]);
    assert(count[MP_CHEESE][3]>3u*count[MP_CHEESE][2]);
    assert(mealscga_picture(0,&again)==MEALS_OK && again==first);
    assert(mealscga_picture(MEAL_PICTURE_COUNT,&pl)==MEALS_CORRUPT && !pl);
    /* EGA and CGA files are independent. */
    assert(open_variant(MEALS_DAT_NAME)==MEALS_OK && meals_picture(0,&ega)==MEALS_OK);
    meals_close(); assert(mealscga_picture(0,&again)==MEALS_OK && again==first);
    mealscga_close(); assert(mealscga_picture(0,&pl)==MEALS_CORRUPT && !pl);
    /* Damaged copies are refused when the planner opens. */
    sprintf(path,"%s/MEALSCGA.DAT",tmpdir);
    {   static const size_t at[]={0,7,8,10,12,14,16,20,MEALS_DAT_HEADER+1,MEALS_DAT_HEADER+4*MEALSCGA_TABLE_ENTRY+2};
        for(i=0;i<sizeof(at)/sizeof(at[0]);i++){
            memcpy(bad,dat,(size_t)n); bad[at[i]]^=0x01;
            put_file(path,bad,(size_t)n); assert(open_cga_variant(path)==MEALS_CORRUPT);
        }
    }
    put_file(path,dat,(size_t)n-1); assert(open_cga_variant(path)==MEALS_CORRUPT);
    memcpy(bad,dat,(size_t)n); bad[n]=0; put_file(path,bad,(size_t)n+1); assert(open_cga_variant(path)==MEALS_CORRUPT);
    /* The EGA file is not a CGA file. */
    put_file(path,dat,(size_t)n); memcpy(bad,dat,8); memcpy(bad,MEALS_DAT_MAGIC,8); put_file(path,bad,(size_t)n);
    assert(open_cga_variant(path)==MEALS_CORRUPT);
    /* A damaged picture: refused, the others still load. */
    memcpy(bad,dat,(size_t)n); bad[pictures+7u*MEALSCGA_PICTURE_BYTES+999]^=0x40;
    put_file(path,bad,(size_t)n); assert(open_cga_variant(path)==MEALS_OK);
    assert(mealscga_picture(7,&pl)==MEALS_CORRUPT && !pl && mealscga_picture(6,&pl)==MEALS_OK && mealscga_picture(8,&pl)==MEALS_OK);
    mealscga_close(); remove(path);
    /* Not next to the EXE: the current directory's copy; nowhere: missing. */
    assert(open_cga_variant(path)==MEALS_OK && mealscga_picture(MP_SOUP,&pl)==MEALS_OK && pl); mealscga_close();
    assert(getcwd(cwd,sizeof(cwd)) && chdir(tmpdir)==0);
    assert(open_cga_variant(path)==MEALS_MISSING);
    assert(chdir(cwd)==0);
    mealscga_set_path(MEALSCGA_DAT_NAME);
}
static int fake_opens, fake_closes, fake_cards, fake_open_result, fake_card_result;
static MealView fake_view;
static int fake_meal_open(Platform *p){ (void)p; fake_opens++; return fake_open_result; }
static int fake_meal_card(Platform *p, const MealView *v){ (void)p; fake_cards++; fake_view=*v; return fake_card_result; }
static void fake_meal_close(Platform *p){ (void)p; fake_closes++; }
static int text_card_shows(Platform *hw, const char *name, const char *portion){
    char b[48]; sprintf(b,"[ %s ]",portion);
    return row_has(hw,12,name) && row_has(hw,14,b);
}
static void check_meal_planner(const char *dir){
    static unsigned char store[16384]; static State out; App *app=(App*)store; Platform *hw;
    uint32_t gen=0; int active=-1; char pa[256], pb[256]; size_t n;
    fresh_dir(dir); jpath(pa,dir,"SAVE_A.JSN"); jpath(pb,dir,"SAVE_B.JSN");
    hw=plat_host_create(0); app_init(app,hw); app_start(app); assert(app_screen(app)==SCR_HOME);
    /* SPACE from Home; no graphics on the host: the text card. */
    app_key(app,' '); assert(app_screen(app)==SCR_MEAL_PLANNER);
    assert(row_has(hw,3,"MEAL PLANNER") && row_has(hw,4,"Drinks 1/6") && row_has(hw,4,"16/16 left"));
    assert(row_has(hw,7,"Drinks") && plat_host_rev(hw,7,38) && !plat_host_rev(hw,7,20) && text_card_shows(hw,"Coffee (Latte)","1 mug"));
    assert(row_has(hw,21,"No EGA/CGA colour screen: Meal Planner in text mode") && row_has(hw,22,"<- -> Card   Up/Down Row"));
    assert(row_has(hw,24,"Enter:Play  Space/Esc:Back") && !row_has(hw,3,"PLAYED"));
    /* Arrows wrap in the row; Up/Down keep the column or take the row's last. */
    app_key(app,KEY_LEFT); assert(row_has(hw,4,"Drinks 6/6") && text_card_shows(hw,"Light Juice","1 glass"));
    app_key(app,KEY_RIGHT); assert(row_has(hw,4,"Drinks 1/6"));
    app_key(app,KEY_DOWN); assert(row_has(hw,4,"Food 1/8") && text_card_shows(hw,"Light Cheese Wrap","1 wrap"));
    app_key(app,KEY_LEFT); assert(row_has(hw,4,"Food 8/8") && text_card_shows(hw,"Soup","1 bowl"));
    app_key(app,KEY_DOWN); assert(row_has(hw,4,"Blank 2/2") && row_has(hw,13,"~  Blank  ~") && !row_has(hw,14,"["));
    app_key(app,KEY_DOWN); assert(row_has(hw,4,"Drinks 2/6") && text_card_shows(hw,"Coffee (Black)","1 cup"));
    app_key(app,KEY_UP); assert(row_has(hw,4,"Blank 2/2"));
    app_key(app,KEY_UP); assert(row_has(hw,4,"Food 2/8"));
    app_key(app,KEY_UP); assert(row_has(hw,4,"Drinks 2/6"));
    {   unsigned c, k; static const int keys[4]={KEY_LEFT,KEY_RIGHT,KEY_UP,KEY_DOWN};
        for(c=0;c<MEAL_CARD_COUNT;c++) for(k=0;k<4;k++){ unsigned to=meal_move(c,keys[k]); assert(to<MEAL_CARD_COUNT); }
        assert(meal_move(5,KEY_RIGHT)==0 && meal_move(6,KEY_LEFT)==13 && meal_move(13,KEY_DOWN)==15 && meal_move(15,KEY_DOWN)==1 && meal_move(0,KEY_UP)==14 && meal_move(12,KEY_UP)==5);
    }
    /* Enter plays (in order), Enter again puts back; later plays move up. */
    app_key(app,KEY_ENTER); assert(app_state(app)->mealCount==1 && app_state(app)->meals[0]==1);
    assert(row_has(hw,3,"PLAYED #1") && row_has(hw,17,"PLAYED #1") && row_has(hw,4,"15/16 left") && row_has(hw,24,"Enter:Put back  Space/Esc:Back"));
    app_key(app,KEY_RIGHT); app_key(app,KEY_ENTER); assert(row_has(hw,3,"PLAYED #2"));
    app_key(app,KEY_LEFT); app_key(app,KEY_ENTER); assert(!row_has(hw,3,"PLAYED") && row_has(hw,4,"15/16 left"));
    assert(app_state(app)->mealCount==1 && app_state(app)->meals[0]==2 && app_state(app)->meals[1]==0);
    assert(!exists(pa)); /* saved when the planner closes */
    app_key(app,' '); assert(app_screen(app)==SCR_HOME);
    n=slurp(pa,jbuf,sizeof(jbuf)); assert(n && strstr(jbuf,"\"meals\":{\"played\":[2]},"));
    /* It opens on the first card still in the deck; Esc saves too. */
    app_key(app,' '); assert(row_has(hw,4,"Drinks 1/6")); app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE);
    assert(app_screen(app)==SCR_HOME); slurp(pb,jbuf,sizeof(jbuf)); assert(strstr(jbuf,"\"meals\":{\"played\":[2,0]},"));
    app_key(app,' '); assert(row_has(hw,4,"Drinks 2/6")); app_key(app,KEY_ESCAPE);
    {   /* A full deck opens on card 0; a 17th card is ignored. */
        unsigned c; static State s; state_init(&s);
        for(c=0;c<MEAL_CARD_COUNT;c++) meal_toggle(&s,c);
        assert(s.mealCount==MEAL_CARD_COUNT && meal_first_unplayed(&s)==0 && meal_play_position(&s,15)==16);
        meal_toggle(&s,MEAL_CARD_COUNT); assert(s.mealCount==MEAL_CARD_COUNT);
        meal_toggle(&s,0); assert(s.mealCount==15 && s.meals[0]==1 && s.meals[14]==15 && meal_first_unplayed(&s)==0);
        meal_toggle(&s,0); assert(meal_play_position(&s,0)==16 && meal_first_unplayed(&s)==0);
    }
    /* Closing the day deals a new deck. */
    app_key(app,'0'); type_text(app,"\b\b\b\b\b\b\b\b\b\b2026-11-16"); app_key(app,KEY_ENTER);
    app_key(app,'8'); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD && app_state(app)->mealCount==0);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.mealCount==0);
    slurp(active?pb:pa,jbuf,sizeof(jbuf)); assert(strstr(jbuf,"\"meals\":{\"played\":[]},"));
    app_key(app,KEY_ENTER);
    /* An archived day is read-only: SPACE does not open the planner. */
    app_key(app,KEY_LEFT); app_key(app,' ');
    assert(app_screen(app)==SCR_HOME && row_has(hw,23,"Archived day: read-only"));
    app_key(app,KEY_RIGHT);
    /* With graphics: the platform gets the card instead of the text card. */
    hw->meal_open=fake_meal_open; hw->meal_card=fake_meal_card; hw->meal_close=fake_meal_close;
    fake_open_result=MEALS_OK; fake_card_result=MEALS_OK;
    app_key(app,' '); assert(fake_opens==1 && fake_cards>=1 && fake_closes==0);
    assert(fake_view.card==0 && fake_view.picture==MP_LATTE && fake_view.row==MEAL_DRINKS && fake_view.position==0 && fake_view.language==LANG_EN);
    assert(!strcmp(fake_view.rowName,"Drinks") && !strcmp(fake_view.name,"Coffee (Latte)") && !strcmp(fake_view.portion,"1 mug"));
    assert(!row_has(hw,12,"Coffee") && !row_has(hw,21,"EGA") && row_has(hw,4,"Drinks 1/6"));
    app_key(app,KEY_ENTER); assert(fake_view.position==1 && row_has(hw,3,"PLAYED #1"));
    app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); assert(fake_view.card==14 && fake_view.picture==MP_BLANK && fake_view.row==MEAL_BLANK && !fake_view.name[0] && !fake_view.portion[0]);
    fake_card_result=MEALS_CORRUPT; app_key(app,KEY_RIGHT);
    assert(fake_view.card==15 && row_has(hw,23,"MEALS.DAT is damaged: copy it again"));
    fake_card_result=MEALS_NO_MEMORY; app_key(app,KEY_LEFT); assert(row_has(hw,23,"Not enough memory for the pictures"));
    fake_card_result=MEALS_OK; app_key(app,KEY_LEFT); assert(!row_has(hw,23,"MEALS"));
    app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_HOME && fake_closes==1);
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.mealCount==1 && out.meals[0]==0);
    slurp(active?pb:pa,jbuf,sizeof(jbuf)); assert(strstr(jbuf,"\"meals\":{\"played\":[0]},"));
    fake_open_result=MEALS_MISSING; n=(size_t)fake_cards;
    app_key(app,' '); assert(fake_opens==2 && (size_t)fake_cards==n && text_card_shows(hw,"Coffee (Black)","1 cup"));
    assert(row_has(hw,21,"MEALS.DAT not found: copy it next to LPSCOMP.EXE"));
    app_key(app,' '); assert(app_screen(app)==SCR_HOME && fake_closes==2);
    /* French: names, portions and labels in CP437. */
    choose_language(app,1); app_key(app,KEY_ESCAPE); assert(app_state(app)->language==LANG_FR);
    fake_open_result=MEALS_NO_EGA; app_key(app,' ');
    assert(row_has(hw,3,"PLANIFICATEUR DE REPAS") && row_has(hw,4,"Boissons 2/6") && row_has(hw,4,"15/16 restantes"));
    assert(text_card_shows(hw,"Caf\202 (Noir)","1 tasse") && row_has(hw,21,"Pas d'\202cran couleur EGA/CGA : planificateur en mode texte"));
    assert(row_has(hw,24,"Entr\202e:Jouer  Espace/\220chap:Retour") && row_has(hw,22,"<- -> Carte   Haut/Bas Rang\202e"));
    app_key(app,KEY_DOWN); assert(text_card_shows(hw,"Wrap fromage l\202ger","1 wrap"));
    app_key(app,KEY_RIGHT); assert(text_card_shows(hw,"Salade de tomates","1 bol"));
    app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); assert(text_card_shows(hw,"Pomme","1 moyenne"));
    app_key(app,KEY_UP); app_key(app,KEY_LEFT); assert(text_card_shows(hw,"Th\202 \205 la menthe","1 tasse"));
    app_key(app,KEY_ENTER); assert(row_has(hw,3,"JOU\220E #2") && row_has(hw,17,"JOU\220E #2") && row_has(hw,24,"Entr\202e:Remettre  Espace/\220chap:Retour"));
    hw->meal_open=fake_meal_open; fake_open_result=MEALS_OK; app_key(app,KEY_ESCAPE); app_key(app,' ');
    assert(!strcmp(fake_view.rowName,"Boissons") && !strcmp(fake_view.name,"Caf\202 (Noir)") && fake_view.language==LANG_FR);
    app_key(app,KEY_ESCAPE);
    hw->meal_open=0; hw->meal_card=0; hw->meal_close=0;
    /* The PicoCalc reads the deck DOS wrote; DOS keeps a PicoCalc deck. */
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.mealCount==2 && out.meals[0]==0 && out.meals[1]==4);
    /* Byte for byte with the PicoCalc: from its custom save ([3,0,15]), play
     * 2, put back 0, play 9 and 14, put back 15, play 13 - through the keys.
     * tools/picocalc_fixture meals did the same with the PicoCalc's
     * toggleMealCard and writer (one save, generation 8: DOS saves when the
     * planner closes). */
    fresh_dir(dir); copy_file("tests/fixtures/picocalc_custom.jsn",pb);
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    if(app_screen(app)==SCR_REMINDER_POPUP) app_key(app,KEY_ENTER);
    app_key(app,' '); assert(row_has(hw,4,"Boissons 2/6") && row_has(hw,4,"13/16 restantes")); /* card 0 is played */
    app_key(app,KEY_RIGHT); app_key(app,KEY_ENTER);                         /* play 2 */
    app_key(app,KEY_LEFT); app_key(app,KEY_LEFT); assert(row_has(hw,3,"JOU\220E #2")); app_key(app,KEY_ENTER); /* put back 0 */
    app_key(app,KEY_DOWN); app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT);
    assert(text_card_shows(hw,"Biscuit","1 portion")); app_key(app,KEY_ENTER);  /* play 9 */
    app_key(app,KEY_DOWN); app_key(app,KEY_LEFT); assert(row_has(hw,4,"Blank 1/2")); app_key(app,KEY_ENTER); /* play 14 */
    app_key(app,KEY_RIGHT); assert(row_has(hw,3,"JOU\220E #2")); app_key(app,KEY_ENTER); /* put back 15 */
    app_key(app,KEY_UP); app_key(app,KEY_LEFT); app_key(app,KEY_LEFT); assert(text_card_shows(hw,"Soupe","1 bol")); app_key(app,KEY_ENTER); /* play 13 */
    assert(row_has(hw,3,"JOU\220E #5") && row_has(hw,4,"11/16 restantes"));
    app_key(app,' '); assert(app_screen(app)==SCR_HOME && same_file("tests/fixtures/picocalc_meals.jsn",pa));
    /* LPS-DOS-115-CHG08: and the CSV written on leaving is the PicoCalc's. */
    {   char csv[256]; sprintf(csv,"%s/EXPORT/MP261114.CSV",dir);
        assert(same_file("tests/fixtures/mealcsv/MP261114.CSV",csv)); }
}

/* ---- LPS-DOS-115-CHG08: Meal Planner CSV, byte for byte the PicoCalc's.
 * tests/fixtures/mealcsv/ holds files written by the PicoCalc's own
 * mealCsvName/writeMealCsv and main.cpp exportMealCsv rules
 * (tools/picocalc_fixture mealcsv); the rules below are its
 * meal_planner_test and storage_test cases. */
#define MCSV_HEAD "order,category,card,portion\r\n"
static int file_is(const char *path, const char *text){
    size_t n; if(!exists(path)) return 0;
    n=slurp(path,jbuf2,sizeof(jbuf2)); return n==strlen(text) && memcmp(jbuf2,text,n)==0;
}
static int field_is(const char *value, const char *want){
    char b[64]; size_t u=0; return meal_csv_field(b,sizeof(b),&u,value) && u==strlen(want) && !strcmp(b,want);
}
static int csv_is(const State *st, const char *want){
    char b[1024]; size_t n=meal_csv_text(st,b,sizeof(b)); return n && n==strlen(want) && !strcmp(b,want);
}
/* LPS-DOS-115-CHG13: 80-cell rows folded to the CGA's 40 cells. */
static void fold_text(const char *row80, const unsigned char *rev, char *out41, unsigned char *orev){
    lps_fold_row(row80,rev,40,out41,orev); out41[40]=0;
}
static void fold_host_row(Platform *hw, int row, char *out41, unsigned char *orev){
    unsigned char rev[COLUMNS]; char cells[COLUMNS+1]; int c; const char *t=plat_host_row(hw,row);
    for(c=0;c<COLUMNS;c++){ cells[c]=t[c]?t[c]:' '; rev[c]=(unsigned char)plat_host_rev(hw,row,c); if(!t[c]) break; }
    for(;c<COLUMNS;c++){ cells[c]=' '; rev[c]=(unsigned char)plat_host_rev(hw,row,c); }
    cells[COLUMNS]=0; fold_text(cells,rev,out41,orev);
}
static void row80(char *b, unsigned char *rev, int revAll){ memset(b,' ',COLUMNS); b[COLUMNS]=0; memset(rev,revAll,COLUMNS); }
static void check_cga_fold(const char *dir){
    static unsigned char store[16384]; App *app=(App*)store; Platform *hw;
    char b[COLUMNS+1], o[41]; unsigned char rev[COLUMNS], orev[40]; int i;
    /* Centred title + date at the right edge, reversed end to end. */
    row80(b,rev,1); memcpy(b+30,"LPS COMPANION v1.15",19); memcpy(b+70,"2026-11-16",10);
    fold_text(b,rev,o,orev);
    assert(!strcmp(o,"          LPS COMPANION v1.15 2026-11-16"));
    for(i=0;i<40;i++) assert(orev[i]==1);
    /* Label on the left (indent halved), counter at the right edge, not reversed. */
    row80(b,rev,0); memcpy(b+1,"MEAL PLANNER",12); memcpy(b+79-9,"PLAYED #1",9);
    fold_text(b,rev,o,orev); assert(!strcmp(o,"MEAL PLANNER                   PLAYED #1") && !orev[0] && !orev[39]);
    /* One block with inner runs of up to 5 spaces stays whole and centred. */
    row80(b,rev,0); memcpy(b+28,"<- -> Card   Up/Down Row",24);
    fold_text(b,rev,o,orev); assert(!strncmp(o+8,"<- -> Card   Up/Down Row",24) && o[7]==' ' && o[32]==' ');
    /* Too wide: the left block gives way, then the right one is cut. */
    row80(b,rev,0); memcpy(b+1,"PLANIFICATEUR DE REPAS",22); memcpy(b+50,"15/16 restantes",15); memcpy(b+79-8,"JOU\220E #2",8);
    fold_text(b,rev,o,orev); assert(!strcmp(o,"PLANIFICATEUR DE REPAS 15/16 restantes J"));
    /* A reversed word inside a plain row keeps its own flag. */
    row80(b,rev,0); memcpy(b+4,"Date",4); rev[4]=rev[5]=rev[6]=rev[7]=1; memcpy(b+12,"Weight",6);
    fold_text(b,rev,o,orev); assert(!strncmp(o+2,"Date    Weight",14) && orev[2] && orev[5] && !orev[6] && !orev[10]);
    /* Empty row: 40 spaces. */
    row80(b,rev,0); fold_text(b,rev,o,orev); for(i=0;i<40;i++) assert(o[i]==' ' && !orev[i]);
    /* The planner's own rows in graphics, as the CGA draws them (EN, then FR played). */
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    hw->meal_open=fake_meal_open; hw->meal_card=fake_meal_card; hw->meal_close=fake_meal_close;
    fake_open_result=MEALS_OK; fake_card_result=MEALS_OK;
    app_key(app,' '); assert(app_screen(app)==SCR_MEAL_PLANNER);
    fold_host_row(hw,0,o,orev); assert(!strncmp(o+10,"LPS COMPANION v1.15",19) && orev[0] && orev[39]);
    fold_host_row(hw,3,o,orev); assert(!strcmp(o,"MEAL PLANNER                            "));
    fold_host_row(hw,4,o,orev); assert(!strcmp(o,"Drinks 1/6                    16/16 left"));
    fold_host_row(hw,22,o,orev); assert(!strcmp(o,"        <- -> Card   Up/Down Row        "));
    fold_host_row(hw,24,o,orev); assert(!strcmp(o,"       Enter:Play  Space/Esc:Back       ") && orev[0] && orev[39]);
    app_key(app,KEY_ENTER); fold_host_row(hw,3,o,orev); assert(!strcmp(o,"MEAL PLANNER                   PLAYED #1"));
    app_key(app,' '); choose_language(app,1); app_key(app,KEY_ESCAPE); assert(app_state(app)->language==LANG_FR);
    app_key(app,' '); app_key(app,KEY_LEFT); /* opens on the first unplayed card */
    fold_host_row(hw,3,o,orev); assert(!strcmp(o,"PLANIFICATEUR DE REPAS          JOU\220E #1"));
    fold_host_row(hw,4,o,orev); assert(!strcmp(o,"Boissons 1/6             15/16 restantes"));
    fold_host_row(hw,24,o,orev); assert(!strcmp(o,"  Entr\202e:Remettre  Espace/\220chap:Retour  "));
    app_key(app,' '); assert(app_screen(app)==SCR_HOME);
    /* LPS-DOS-115-CHG14: no MEALSCGA.DAT on CGA: graphics card without picture,
     * a message short enough for the 40-column row (FR here, then EN). */
    fake_open_result=MEALS_OK; fake_card_result=MEALS_CGA_MISSING; app_key(app,' ');
    assert(row_has(hw,23,"MEALSCGA.DAT absent : pas d'images"));
    fold_host_row(hw,23,o,orev); assert(strstr(o,"MEALSCGA.DAT absent : pas d'images"));
    fake_card_result=MEALS_CGA_CORRUPT; app_key(app,KEY_RIGHT);
    fold_host_row(hw,23,o,orev); assert(strstr(o,"MEALSCGA.DAT ab\214m\202 : pas d'images"));
    app_key(app,' '); choose_language(app,0); app_key(app,KEY_ESCAPE);
    fake_card_result=MEALS_CGA_MISSING; app_key(app,' ');
    fold_host_row(hw,23,o,orev); assert(strstr(o,"MEALSCGA.DAT missing: no pictures"));
    fake_card_result=MEALS_CGA_CORRUPT; app_key(app,KEY_RIGHT);
    fold_host_row(hw,23,o,orev); assert(strstr(o,"MEALSCGA.DAT damaged: no pictures"));
    fake_card_result=MEALS_OK; app_key(app,' '); assert(app_screen(app)==SCR_HOME);
}
/* LPS-DOS-115-CHG15: the CGA played card. A card-sized patch of mode 04h
 * memory (2 bits a pixel, leftmost pixel in bits 7-6) greyed out word by
 * word as cga.c does, and the PLAYED #n label sizes. */
#define GREY_W 176
#define GREY_H 136
static unsigned grey_pixel(const unsigned char *rows, int x, int y){ return (rows[y*(GREY_W/4)+x/4]>>(6-2*(x&3)))&3u; }
static void grey_patch(unsigned char *rows, int y0){
    int y, i;
    for(y=0;y<GREY_H;y++){
        unsigned short m=lps_cga_grey_mask(y0+y);
        for(i=0;i<GREY_W/8;i++){
            unsigned char *p=rows+y*(GREY_W/4)+i*2;
            unsigned short w=(unsigned short)(p[0]|(p[1]<<8)); /* little-endian, as the 8088 reads it */
            w&=m; p[0]=(unsigned char)(w&0xFF); p[1]=(unsigned char)(w>>8);
        }
    }
}
static void check_cga_grey(void){
    static unsigned char before[GREY_W/4*GREY_H], after[GREY_W/4*GREY_H];
    char b[24]; unsigned n, pos, seed=12345u, kept=0; int x, y, y0;
    /* Masks: fixed per line parity, equal bytes, and complementary. */
    assert(lps_cga_grey_mask(0)==0xCCCCu && lps_cga_grey_mask(1)==0x3333u && lps_cga_grey_mask(40)==0xCCCCu && lps_cga_grey_mask(175)==0x3333u);
    assert((lps_cga_grey_mask(0)|lps_cga_grey_mask(1))==0xFFFFu && !(lps_cga_grey_mask(0)&lps_cga_grey_mask(1)));
    for(y0=40;y0<=41;y0++){ /* the card starts on an even line; check an odd start too */
        for(x=0;x<(int)sizeof(before);x++){ seed=seed*1103515245u+12345u; before[x]=(unsigned char)(seed>>16); }
        memset(before,0xFF,GREY_W/4*10); /* a solid yellow band like the card's border */
        memcpy(after,before,sizeof(before)); grey_patch(after,y0);
        for(y=0;y<GREY_H;y++) for(x=0;x<GREY_W;x++){
            unsigned p=grey_pixel(before,x,y), q=grey_pixel(after,x,y);
            if(((x+y+y0)&1)==0){ assert(q==p); kept++; } else assert(q==0); /* checkerboard of colour 0 */
        }
        /* Yellow turns into yellow / colour 0 every other pixel: half stays. */
        for(x=0;x<GREY_W;x++) assert(grey_pixel(after,x,0)==(((x+y0)&1)?0u:3u));
        /* Idempotent: greying a greyed card changes nothing. */
        memcpy(before,after,sizeof(after)); grey_patch(after,y0); assert(!memcmp(before,after,sizeof(after)));
    }
    assert(kept==GREY_W*GREY_H); /* half of the pixels, on each of the two runs */
    /* Colour 0 stays colour 0. */
    memset(after,0,sizeof(after)); grey_patch(after,40); for(x=0;x<(int)sizeof(after);x++) assert(!after[x]);
    /* Labels: CP437, and the framed box ((n + 2) cells) fits the 160-pixel picture. */
    assert(lps_played_label(b,0,1)==9 && !strcmp(b,"PLAYED #1"));
    assert(lps_played_label(b,1,1)==8 && !strcmp(b,"JOU\220E #1"));
    for(pos=1;pos<=MEAL_CARD_COUNT;pos++){
        n=lps_played_label(b,0,pos); assert(n==strlen(b) && (n+2)*8<=160);
        n=lps_played_label(b,1,pos); assert(n==strlen(b) && (n+2)*8<=160);
    }
    assert(lps_played_label(b,0,16)==10 && !strcmp(b,"PLAYED #16") && lps_played_label(b,1,16)==9);
}
static void check_meal_csv(const char *dir){
    static unsigned char store[16384]; static State st; App *app=(App*)store; Platform *hw;
    char pa[300], ex[300], csv[600], next[600], name[16]; Date d, saved; uint32_t gen=0; int active=-1; size_t n;
    static const unsigned plays[5]={4,0,14,7,2}; unsigned i;
    /* File names: the PicoCalc's MPYYMMDD.CSV is already 8.3. */
    d.year=2026; d.month=11; d.day=14; meal_csv_name(&d,name); assert(!strcmp(name,"MP261114.CSV"));
    d.year=2000; d.month=1; d.day=5; meal_csv_name(&d,name); assert(!strcmp(name,"MP000105.CSV"));
    d.year=2099; d.month=12; d.day=31; meal_csv_name(&d,name); assert(!strcmp(name,"MP991231.CSV"));
    /* Content in order of play; quoting; UTF-8 passes through. */
    state_init(&st); assert(csv_is(&st,MCSV_HEAD));
    for(i=0;i<5;i++) meal_toggle(&st,plays[i]);
    assert(csv_is(&st,MCSV_HEAD "1,Drinks,Mint Tea,1 cup\r\n2,Drinks,Coffee (Latte),1 mug\r\n3,Blank,Blank,\r\n4,Food,Light Cheese Wrap,1 wrap\r\n5,Drinks,Coffee (Black),1 cup\r\n"));
    meal_toggle(&st,0); meal_toggle(&st,0);
    assert(csv_is(&st,MCSV_HEAD "1,Drinks,Mint Tea,1 cup\r\n2,Blank,Blank,\r\n3,Food,Light Cheese Wrap,1 wrap\r\n4,Drinks,Coffee (Black),1 cup\r\n5,Drinks,Coffee (Latte),1 mug\r\n"));
    assert(field_is("Coffee (Latte)","Coffee (Latte)") && field_is("",""));
    assert(field_is("a,b","\"a,b\"") && field_is("say \"hi\"","\"say \"\"hi\"\"\"") && field_is("two\nlines","\"two\nlines\""));
    assert(field_is("Th\303\251 \303\240 la menthe","Th\303\251 \303\240 la menthe"));
    {   char b[40]; size_t u=0; assert(!meal_csv_field(b,4,&u,"abcd") && meal_csv_field(b,5,&u,"abcd") && u==4);
        state_init(&st); for(i=0;i<MEAL_CARD_COUNT;i++) meal_toggle(&st,i);
        assert(st.mealCount==16 && meal_csv_text(&st,b,sizeof(b))==0 && meal_csv_text(&st,jbuf,449)==448 && meal_csv_text(&st,jbuf,448)==0); }
    for(i=0;i<MEAL_CARD_COUNT;i++){ const char *p; for(p=MEAL_DECK[i].en;*p;p++) assert((unsigned char)*p<0x80); }
    /* All 16 cards, from a PicoCalc save: the same bytes as the PicoCalc's file. */
    fresh_dir(dir); jpath(pa,dir,"SAVE_A.JSN"); jpath(ex,dir,"EXPORT");
    copy_file("tests/fixtures/picocalc_meals_all.jsn",pa);
    assert(save_load(dir,&st,&gen,&active)==LOAD_OK && st.mealCount==16);
    assert(meal_csv_export(ex,&st)); jpath(csv,ex,"MP261115.CSV");
    assert(same_file("tests/fixtures/mealcsv/MP261115.CSV",csv));
    st.today.date.month=2; st.today.date.day=30; assert(!meal_csv_export(ex,&st)); /* invalid date */
    /* Through the app: entering writes nothing, leaving (Space or Esc) rewrites. */
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    meal_csv_name(&app_state(app)->today.date,name); jpath(csv,ex,name);
    app_key(app,' '); app_key(app,KEY_ESCAPE); assert(!exists(csv)); /* nothing played: no file */
    app_key(app,' '); app_key(app,KEY_ENTER); assert(!exists(csv));
    app_key(app,KEY_DOWN); app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); app_key(app,KEY_ENTER);
    app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); app_key(app,' ');
    assert(app_screen(app)==SCR_HOME && !row_has(hw,23,"export"));
    assert(file_is(csv,MCSV_HEAD "1,Drinks,Coffee (Latte),1 mug\r\n2,Food,Tomato Salad,1 bowl\r\n3,Blank,Blank,\r\n"));
    app_key(app,' '); app_key(app,KEY_LEFT); app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE);
    assert(file_is(csv,MCSV_HEAD "1,Food,Tomato Salad,1 bowl\r\n2,Blank,Blank,\r\n"));
    /* A failed export is shown on Home; the deck stays saved; the next exit retries. */
    rm_tree(ex); write_file(ex,(const unsigned char*)"x",1);
    app_key(app,' '); app_key(app,KEY_ESCAPE);
    assert(app_screen(app)==SCR_HOME && row_has(hw,23,"Meal CSV export failed") && app_state(app)->mealCount==2);
    assert(save_load(dir,&st,&gen,&active)==LOAD_OK && st.mealCount==2);
    ((State*)app_state(app))->language=LANG_FR; app_key(app,' '); app_key(app,' ');
    assert(row_has(hw,23,"Export CSV repas \202chou\202"));
    ((State*)app_state(app))->language=LANG_EN;
    remove(ex); lps_ensure_dirs(); app_key(app,' '); app_key(app,' ');
    assert(!row_has(hw,23,"export") && file_is(csv,MCSV_HEAD "1,Food,Tomato Salad,1 bowl\r\n2,Blank,Blank,\r\n"));
    /* Everything put back: the existing file is reset to its header. */
    app_key(app,' '); app_key(app,KEY_DOWN); app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); app_key(app,KEY_ENTER);
    app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE);
    assert(app_state(app)->mealCount==0 && file_is(csv,MCSV_HEAD));
    /* An undated day: nothing written, and the planner says why. */
    saved=app_state(app)->today.date; ((State*)app_state(app))->today.date.year=0;
    app_key(app,' '); app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE);
    assert(row_has(hw,23,"Set Date to export meals") && file_is(csv,MCSV_HEAD));
    ((State*)app_state(app))->language=LANG_FR; app_key(app,' '); app_key(app,' ');
    assert(row_has(hw,23,"Renseignez Date pour exporter les repas"));
    ((State*)app_state(app))->language=LANG_EN; ((State*)app_state(app))->today.date=saved;
    app_key(app,' '); app_key(app,KEY_ESCAPE);
    assert(file_is(csv,MCSV_HEAD "1,Drinks,Coffee (Latte),1 mug\r\n"));
    /* Closing the day writes no CSV: the closed day's file stays final, and
     * the next day starts a new deck (and no file until a card is played). */
    n=slurp(csv,jbuf,sizeof(jbuf));
    app_key(app,'8'); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD && app_state(app)->mealCount==0);
    app_key(app,KEY_ENTER);
    assert(slurp(csv,jbuf2,sizeof(jbuf2))==n && memcmp(jbuf,jbuf2,n)==0);
    meal_csv_name(&app_state(app)->today.date,name); jpath(next,ex,name); assert(strcmp(next,csv)!=0);
    app_key(app,' '); app_key(app,KEY_ESCAPE); assert(!exists(next) && file_is(csv,MCSV_HEAD "1,Drinks,Coffee (Latte),1 mug\r\n"));
    app_key(app,' '); app_key(app,KEY_RIGHT); app_key(app,KEY_ENTER); app_key(app,' ');
    assert(file_is(next,MCSV_HEAD "1,Drinks,Coffee (Black),1 cup\r\n"));
}

/* ---- LPS-DOS-115-CHG09: TAB/SPACE from any screen, as PicoCalc
 * LPS-115-CHG02/02b (its tests/overlay_keys_test.cpp, section by section). */
static unsigned ov_exports, ov_saves;
static int (*ov_real_export)(Platform*, const State*);
static int (*ov_real_save)(Platform*, const State*);
static int ov_export(Platform *p, const State *s){ ov_exports++; return ov_real_export(p,s); }
static int ov_save(Platform *p, const State *s){ ov_saves++; return ov_real_save(p,s); }
#define OV_FRAME (ROWS*COLUMNS*2+ROWS)
static void frame_of(Platform *hw, char *out){
    int r, c; char *o=out;
    for(r=0;r<ROWS;r++){
        const char *t=plat_host_row(hw,r);
        for(c=0;c<COLUMNS;c++){ *o++=t[c]; *o++=(char)(plat_host_rev(hw,r,c)?1:0); }
        *o++='\n';
    }
}
static int frame_is(Platform *hw, const char *before){ static char now[OV_FRAME]; frame_of(hw,now); return memcmp(now,before,OV_FRAME)==0; }
/* TAB and SPACE each open their overlay and close back to the identical
 * frame (same screen, cursor, column, scroll), by the same key or by Esc. */
static void round_trips(App *app, Platform *hw, enum Screen s){
    static char before[OV_FRAME]; unsigned exports=ov_exports, saves=ov_saves;
    assert(app_screen(app)==s); frame_of(hw,before);
    app_key(app,KEY_TAB); assert(app_screen(app)==SCR_CONFIG && row_has(hw,3,"CONFIGURATION"));
    app_key(app,KEY_TAB); assert(app_screen(app)==s && frame_is(hw,before));
    app_key(app,KEY_TAB); app_key(app,KEY_ESCAPE); assert(app_screen(app)==s && frame_is(hw,before));
    app_key(app,KEY_TAB); app_key(app,'1'); assert(app_screen(app)==SCR_LANGUAGE);
    app_key(app,KEY_TAB); assert(app_screen(app)==s && frame_is(hw,before)); /* from a sub-screen, TAB closes Config entirely */
    app_key(app,' '); assert(app_screen(app)==SCR_MEAL_PLANNER && row_has(hw,3,"MEAL PLANNER"));
    app_key(app,' '); assert(app_screen(app)==s && frame_is(hw,before));
    app_key(app,' '); app_key(app,KEY_ESCAPE); assert(app_screen(app)==s && frame_is(hw,before));
    assert(ov_exports==exports+2 && ov_saves==saves); /* every exit writes the CSV; nothing to save */
}
static Platform *ov_start(App *app, const char *dir){
    Platform *hw; State *st; unsigned i; static const char *const names[4]={"alpha","beta","gamma","delta"};
    fresh_dir(dir); hw=plat_host_create(0); app_init(app,hw); app_start(app);
    ov_real_export=hw->export_meal_csv; hw->export_meal_csv=ov_export;
    ov_real_save=hw->save; hw->save=ov_save; ov_exports=ov_saves=0;
    st=(State*)app_state(app);
    for(i=0;i<4;i++){ st->tasks[i].status=(uint8_t)(i<2?TASK_TODO:TASK_DOING); strcpy(st->tasks[i].name,names[i]); }
    st->taskCount=4;
    st->reminders[0].from=st->today.date; st->reminders[0].duration=30; strcpy(st->reminders[0].text,"Water plants");
    st->reminders[1].from=st->today.date; st->reminders[1].duration=60; strcpy(st->reminders[1].text,"Call bank");
    st->reminderCount=2;
    app_render(app); assert(app_screen(app)==SCR_HOME);
    return hw;
}
static void check_overlay_keys(const char *dir){
    static unsigned char store[16384]; static char typed[OV_FRAME], kanban[OV_FRAME], planner[OV_FRAME];
    App *app=(App*)store; Platform *hw; int i; char ex[300], csv[600], name[16];
    /* 1. Menus, lists and views: identical return. */
    hw=ov_start(app,dir);
    app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); round_trips(app,hw,SCR_HOME);
    assert(plat_host_rev(hw,10,2) && row_has(hw,10,"3  Kanban"));
    app_key(app,KEY_ENTER); app_key(app,KEY_RIGHT); app_key(app,KEY_DOWN); assert(any_row(hw,"delta")); round_trips(app,hw,SCR_KANBAN);
    app_key(app,KEY_ESCAPE); app_key(app,'1'); app_key(app,KEY_RIGHT); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN);
    assert(app_selection(app)==10); round_trips(app,hw,SCR_ACTIVITIES);
    app_key(app,'3'); assert(app_state(app)->today.flags==(1ul<<10)); /* keys still act on that column afterwards */
    app_key(app,KEY_ESCAPE); app_key(app,'5'); app_key(app,KEY_DOWN); round_trips(app,hw,SCR_DAILY_REVIEW);
    app_key(app,KEY_ESCAPE); app_key(app,'6'); app_key(app,KEY_DOWN); round_trips(app,hw,SCR_REMINDERS);
    app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REMINDER_EDIT && any_row(hw,"Call bank")); app_key(app,KEY_ESCAPE); app_key(app,KEY_ESCAPE);
    app_key(app,'7'); app_key(app,KEY_DOWN); round_trips(app,hw,SCR_TIMER_SELECT);
    app_key(app,KEY_ESCAPE); app_key(app,'4'); app_key(app,KEY_DOWN); round_trips(app,hw,SCR_ROSARY_MODE); app_key(app,KEY_UP); app_key(app,KEY_ENTER);
    app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); round_trips(app,hw,SCR_ROSARY_SET); app_key(app,KEY_ENTER);
    app_key(app,KEY_DOWN); round_trips(app,hw,SCR_ROSARY_DECADE); app_key(app,KEY_ENTER);
    app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); app_key(app,KEY_RIGHT); round_trips(app,hw,SCR_ROSARY_PRAYER);
    /* DOS: the prayer texts stay loaded under the overlays (no reload). */
    app_key(app,' '); assert(rosary_loaded()); app_key(app,KEY_TAB); assert(rosary_loaded()); app_key(app,KEY_TAB); app_key(app,' ');
    for(i=0;i<300 && app_screen(app)==SCR_ROSARY_PRAYER;i++) app_key(app,KEY_RIGHT);
    round_trips(app,hw,SCR_ROSARY_COMPLETE); app_key(app,KEY_ENTER); assert(!rosary_loaded());
    app_key(app,' '); assert(!rosary_loaded()); app_key(app,' ');
    app_key(app,'8'); app_key(app,KEY_ENTER); round_trips(app,hw,SCR_REWARD); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_HOME);
    /* 2. Text entry: SPACE stays a character, TAB does not leave the editor. */
    hw=ov_start(app,dir);
    app_key(app,'3'); app_key(app,'n'); app_key(app,'a'); app_key(app,' '); app_key(app,KEY_TAB); app_key(app,'b');
    assert(app_screen(app)==SCR_TASK_EDIT); app_key(app,KEY_ENTER); assert(!strcmp(app_state(app)->tasks[4].name,"a b"));
    app_key(app,KEY_ESCAPE); app_key(app,'1'); app_key(app,'n'); app_key(app,'x'); app_key(app,' '); app_key(app,KEY_TAB); app_key(app,'y');
    assert(app_screen(app)==SCR_ACTIVITY_EDIT); app_key(app,KEY_ENTER); assert(!strcmp(app_state(app)->activities[22].en,"x y"));
    app_key(app,KEY_ESCAPE); app_key(app,'6'); app_key(app,'n'); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN);
    app_key(app,'p'); app_key(app,' '); app_key(app,KEY_TAB); app_key(app,'q'); assert(app_screen(app)==SCR_REMINDER_EDIT && any_row(hw,"p q"));
    app_key(app,KEY_ESCAPE); app_key(app,KEY_ESCAPE);
    app_key(app,'0'); app_key(app,' '); app_key(app,KEY_TAB); assert(app_screen(app)==SCR_DATE); app_key(app,KEY_ESCAPE);
    /* Weight (numeric entry): TAB opens Config and returns with the field and
     * the digits typed so far; Up/Down switch the field; SPACE is ignored. */
    app_key(app,'2'); app_key(app,'1'); app_key(app,KEY_DOWN); app_key(app,'2'); app_key(app,'.');
    assert(plat_host_rev(hw,9,0) && row_has(hw,9,"Actual   : 2."));
    frame_of(hw,typed);
    app_key(app,' '); assert(app_screen(app)==SCR_WEIGHT && frame_is(hw,typed));
    app_key(app,KEY_TAB); assert(app_screen(app)==SCR_CONFIG); app_key(app,KEY_TAB); assert(app_screen(app)==SCR_WEIGHT && frame_is(hw,typed));
    app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,KEY_ESCAPE); app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_WEIGHT && frame_is(hw,typed));
    app_key(app,KEY_TAB); app_key(app,'2'); app_key(app,KEY_TAB); assert(frame_is(hw,typed));
    app_key(app,'5'); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_HOME && app_state(app)->today.expected==1000 && app_state(app)->today.actual==2500);
    /* Saving a language from Weight keeps the drafts too. */
    app_key(app,'2'); for(i=0;i<8;i++) app_key(app,KEY_BACKSPACE); app_key(app,'9');
    app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,'2'); app_key(app,'4');
    assert(app_screen(app)==SCR_WEIGHT && row_has(hw,6,"Attendu  : 9") && plat_host_rev(hw,6,0) && row_has(hw,23,"Configuration enregistr\202e"));
    app_key(app,KEY_ENTER); assert(app_state(app)->today.expected==9000 && app_state(app)->today.actual==2500);
    app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,'1'); app_key(app,'4'); assert(app_state(app)->language==LANG_EN);
    assert(ov_exports==0);
    /* 3. Confirmations: TAB/SPACE change nothing; the reminder popup is dismissed by any key. */
    app_key(app,KEY_ESCAPE); app_key(app,'1'); app_key(app,KEY_DELETE); assert(row_has(hw,21,"Delete"));
    app_key(app,KEY_TAB); app_key(app,' '); assert(app_screen(app)==SCR_ACTIVITIES && row_has(hw,21,"Delete"));
    app_key(app,KEY_ESCAPE); assert(!row_has(hw,21,"Delete") && app_state(app)->activityCount==23); app_key(app,KEY_ESCAPE);
    app_key(app,'8'); app_key(app,KEY_TAB); app_key(app,' '); assert(app_screen(app)==SCR_FINISH); app_key(app,KEY_ESCAPE);
    app_init(app,hw); app_start(app); assert(app_screen(app)==SCR_REMINDER_POPUP); app_key(app,KEY_TAB); assert(app_screen(app)==SCR_HOME);
    app_init(app,hw); app_start(app); app_key(app,' '); assert(app_screen(app)==SCR_HOME);
    assert(ov_exports==0);
    /* 4. Cross-open: TAB from the planner opens Config, which closes back to
     * the planner (card kept, pictures kept); SPACE is ignored in Config;
     * Esc goes up one level. */
    hw=ov_start(app,dir);
    hw->meal_open=fake_meal_open; hw->meal_card=fake_meal_card; hw->meal_close=fake_meal_close;
    fake_open_result=MEALS_NO_EGA; fake_card_result=MEALS_OK; fake_opens=fake_closes=0;
    app_key(app,'3'); app_key(app,KEY_RIGHT); app_key(app,KEY_DOWN); frame_of(hw,kanban);
    app_key(app,' '); app_key(app,KEY_DOWN); app_key(app,KEY_RIGHT); frame_of(hw,planner); assert(row_has(hw,4,"Food 2/8") && fake_opens==1);
    app_key(app,KEY_TAB); assert(app_screen(app)==SCR_CONFIG && fake_closes==0);
    app_key(app,' '); assert(app_screen(app)==SCR_CONFIG); app_key(app,'2'); assert(app_screen(app)==SCR_COLORS);
    app_key(app,' '); assert(app_screen(app)==SCR_COLORS);
    app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_CONFIG); app_key(app,KEY_ESCAPE);
    assert(app_screen(app)==SCR_MEAL_PLANNER && frame_is(hw,planner) && fake_opens==1 && fake_closes==0);
    app_key(app,KEY_TAB); app_key(app,KEY_TAB); assert(frame_is(hw,planner));
    assert(ov_exports==0); /* Config inside the planner does not leave it */
    app_key(app,KEY_ENTER); assert(app_state(app)->mealCount==1);
    app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_KANBAN && frame_is(hw,kanban) && fake_closes==1);
    assert(ov_exports==1 && ov_saves==1);
    hw->meal_open=0; hw->meal_card=0; hw->meal_close=0;
    /* Saving Config returns to the screen it was opened from, in the new language. */
    app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,'2'); app_key(app,'4');
    assert(app_screen(app)==SCR_KANBAN && app_state(app)->language==LANG_FR && row_has(hw,23,"Configuration enregistr\202e") && any_row(hw,"delta"));
    /* A CSV failure is reported on the screen the planner returns to. */
    jpath(ex,dir,"EXPORT"); rm_tree(ex); write_file(ex,(const unsigned char*)"x",1);
    app_key(app,' '); app_key(app,' '); assert(app_screen(app)==SCR_KANBAN && row_has(hw,23,"Export CSV repas \202chou\202") && any_row(hw,"delta"));
    remove(ex); lps_ensure_dirs();
    /* A Rosary page past the end of the new language's text is clamped on return. */
    app_key(app,KEY_ESCAPE); app_key(app,'4'); app_key(app,KEY_ENTER); app_key(app,KEY_ENTER); app_key(app,KEY_ENTER);
    for(i=0;i<6;i++) app_key(app,KEY_RIGHT);
    app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,'1'); app_key(app,'4'); assert(app_screen(app)==SCR_ROSARY_PRAYER && app_state(app)->language==LANG_EN);
    app_key(app,KEY_LEFT); app_key(app,KEY_RIGHT); assert(app_screen(app)==SCR_ROSARY_PRAYER);
    app_key(app,KEY_ESCAPE);
    /* 5. Archived days keep the read-only message. */
    app_key(app,'8'); app_key(app,KEY_ENTER); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_HOME);
    app_key(app,KEY_LEFT); app_key(app,KEY_TAB); assert(app_screen(app)==SCR_HOME && row_has(hw,23,"Archived day: read-only"));
    app_key(app,KEY_DOWN); app_key(app,' '); assert(app_screen(app)==SCR_HOME && row_has(hw,23,"Archived day: read-only"));
    app_key(app,'1'); assert(app_screen(app)==SCR_ACTIVITIES); app_key(app,KEY_TAB); assert(app_screen(app)==SCR_ACTIVITIES && row_has(hw,23,"Archived day: read-only"));
    app_key(app,KEY_ESCAPE); app_key(app,'2'); assert(app_screen(app)==SCR_WEIGHT); app_key(app,KEY_TAB); assert(app_screen(app)==SCR_WEIGHT && row_has(hw,23,"Archived day: read-only"));
    i=(int)ov_exports; app_key(app,KEY_ESCAPE); app_key(app,'1'); app_key(app,' '); assert(app_screen(app)==SCR_ACTIVITIES && ov_exports==(unsigned)i);
    app_key(app,KEY_ESCAPE); app_key(app,KEY_RIGHT);
    /* 6. The timer keeps counting behind Config and the planner, and its alert takes over. */
    hw=ov_start(app,dir); meal_csv_name(&app_state(app)->today.date,name); jpath(ex,dir,"EXPORT"); jpath(csv,ex,name);
    hw->meal_open=fake_meal_open; hw->meal_card=fake_meal_card; hw->meal_close=fake_meal_close; fake_opens=fake_closes=0;
    app_tick(app,1000); app_key(app,'7'); app_key(app,'1'); assert(any_row(hw,"05:00"));
    app_key(app,KEY_TAB); app_tick(app,61000); assert(app_screen(app)==SCR_CONFIG);
    app_key(app,KEY_TAB); assert(app_screen(app)==SCR_TIMER_RUNNING && any_row(hw,"04:00"));
    app_key(app,' '); app_key(app,KEY_TAB); app_tick(app,121000); app_key(app,KEY_ESCAPE); app_tick(app,122000); app_key(app,' ');
    assert(app_screen(app)==SCR_TIMER_RUNNING && any_row(hw,"02:59"));
    app_key(app,KEY_ENTER); app_key(app,KEY_TAB); app_tick(app,500000); app_key(app,KEY_TAB); assert(any_row(hw,"02:59")); /* paused stays paused */
    app_key(app,KEY_ENTER); app_key(app,' '); app_key(app,KEY_ENTER); app_tick(app,678999); assert(app_screen(app)==SCR_MEAL_PLANNER);
    i=fake_closes; app_tick(app,679000);
    assert(app_screen(app)==SCR_TIMER_COMPLETE && any_row(hw,"TIMER FINISHED!") && ov_exports==2 && fake_closes==i+1);
    {   static State out; uint32_t gen=0; int active=-1; /* DOS: the deck was saved before the alert */
        assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.mealCount==1 && file_is(csv,MCSV_HEAD "1,Drinks,Coffee (Latte),1 mug\r\n")); }
    /* Expiry inside Config drops the pending choice; inside Config over the planner it also writes the CSV. */
    i=(int)ov_saves; app_key(app,'1'); app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,'2'); app_tick(app,679000+300000);
    assert(app_screen(app)==SCR_TIMER_COMPLETE && app_state(app)->language==LANG_EN && ov_saves==(unsigned)i);
    app_key(app,KEY_TAB); assert(app_screen(app)==SCR_CONFIG && row_has(hw,6,"English")); app_key(app,KEY_TAB);
    i=fake_closes; app_key(app,'1'); app_key(app,' '); app_key(app,KEY_TAB); app_tick(app,979000+300000);
    assert(app_screen(app)==SCR_TIMER_COMPLETE && ov_exports==3 && fake_closes==i+1);
    app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_TIMER_SELECT);
    /* Esc on the running timer still cancels it: no alert later. */
    app_key(app,'1'); app_key(app,KEY_ESCAPE); app_tick(app,9999999); assert(app_screen(app)==SCR_HOME);
    hw->meal_open=0; hw->meal_card=0; hw->meal_close=0;
}

/* LPS-DOS-115-CHG10 (PicoCalc v1.15 LPS-115-CHG01/03/05): Config 1 Language,
 * 2 Colors, 3 Activities (home entries, Date and Close the day locked), 4 Save;
 * "homeHidden" in the save, byte for byte as the PicoCalc writes it. The home
 * menu itself is not filtered yet (CHG11). */
static int he_fail; static unsigned he_saves;
static int (*he_real_save)(Platform*, const State*);
static int he_save(Platform *p, const State *s){ if(he_fail) return 0; he_saves++; return he_real_save(p,s); }
/* The newest of SAVE_A.JSN / SAVE_B.JSN, into jbuf. */
static size_t newest_save(const char *dir){
    char pa[256], pb[256]; size_t na, nb; long ga=-1, gb=-1; const char *g;
    sprintf(pa,"%s/SAVE_A.JSN",dir); sprintf(pb,"%s/SAVE_B.JSN",dir);
    if(exists(pb)){ nb=slurp(pb,jbuf,sizeof(jbuf)); g=strstr(jbuf,"\"generation\":"); if(g) gb=atol(g+13); (void)nb; }
    if(exists(pa)){ na=slurp(pa,jbuf,sizeof(jbuf)); g=strstr(jbuf,"\"generation\":"); if(g) ga=atol(g+13); (void)na; }
    return slurp(ga>=gb?pa:pb,jbuf,sizeof(jbuf));
}
static Platform *he_start(App *app, const char *dir, int fresh){
    Platform *hw;
    if(fresh) fresh_dir(dir); else lps_set_data_dir(dir);
    hw=plat_host_create(0); app_init(app,hw); app_start(app);
    he_real_save=hw->save; hw->save=he_save; he_fail=0; he_saves=0;
    return hw;
}
/* A Home menu row (columns 1-37) holds exactly text. */
static int menu_is(Platform *hw, int row, const char *text){
    const char *r=plat_host_row(hw,row); size_t n=strlen(text), i;
    if(strncmp(r+1,text,n)) return 0;
    for(i=1+n;i<38;i++) if(r[i]!=' ') return 0;
    return 1;
}
static void he_hide(App *app, const unsigned *entries, unsigned count){ /* toggle entries and save */
    unsigned e, i;
    app_key(app,KEY_TAB); app_key(app,'3');
    for(e=0;e<count;e++){ for(i=0;i<entries[e];i++) app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); for(i=0;i<entries[e];i++) app_key(app,KEY_UP); }
    app_key(app,KEY_ESCAPE); app_key(app,'4');
}
/* LPS-DOS-115-CHG11 (PicoCalc v1.15 LPS-115-CHG04): today's home menu lists
 * only the checked entries, renumbered 0..n-1; digits, arrows and Enter work
 * on positions; return points keep the entry. */
static void check_home_filtered(const char *dir){
    static unsigned char store[16384]; App *app=(App*)store; Platform *hw; int r;
    static const char *const expected[7]={"0  Date","1  Weight tracker","2  Kanban","3  Christian Rosary","4  Reminders","5  Timer","6  Close the day"};
    static const unsigned act_daily[2]={1,5}, kanban[1]={3}, timer[1]={7}, all5[5]={1,2,3,5,7}, act_weight[2]={1,2};
    hw=he_start(app,dir,1);
    app_key(app,'1'); app_key(app,'1'); app_key(app,KEY_ESCAPE); /* tick an activity first: hidden entries keep their data */
    he_hide(app,act_daily,2); /* Activities and Daily review */
    assert(app_state(app)->hiddenHome==0x022);
    for(r=0;r<7;r++) assert(menu_is(hw,7+r,expected[r]));
    assert(menu_is(hw,14,"") && row_has(hw,18,"<- Previous day") && row_has(hw,24,"Tab:Config") && row_has(hw,5,"1/22 activities") && plat_host_rev(hw,7,1));
    /* Digits map to positions; keys past the end do nothing. */
    app_key(app,'1'); assert(app_screen(app)==SCR_WEIGHT); app_key(app,KEY_ESCAPE);
    app_key(app,'5'); assert(app_screen(app)==SCR_TIMER_SELECT); app_key(app,KEY_ESCAPE);
    app_key(app,'6'); assert(app_screen(app)==SCR_FINISH); app_key(app,KEY_ESCAPE);
    app_key(app,'7'); app_key(app,'8'); app_key(app,'9'); assert(app_screen(app)==SCR_HOME && plat_host_rev(hw,7,1));
    /* The AZERTY top row by scan code (CHG06b) maps to positions too: é is 2, ( is 5. */
    app_key(app,plat_host_key(1,'\202')); assert(app_screen(app)==SCR_KANBAN); app_key(app,KEY_ESCAPE);
    app_key(app,plat_host_key(1,'(')); assert(app_screen(app)==SCR_TIMER_SELECT); app_key(app,KEY_ESCAPE);
    app_key(app,plat_host_key(1,'\212')); assert(app_screen(app)==SCR_HOME); /* è is 7: past the end */
    /* Up/Down move over visible entries only and wrap. */
    app_key(app,KEY_UP); assert(plat_host_rev(hw,13,1)); app_key(app,KEY_DOWN); assert(plat_host_rev(hw,7,1));
    app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_WEIGHT); app_key(app,KEY_ESCAPE);
    /* Cursor kept on the same entry after a Config save... */
    app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); assert(plat_host_rev(hw,9,1) && menu_is(hw,9,"2  Kanban"));
    app_key(app,KEY_TAB); app_key(app,'3'); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE); app_key(app,'4'); /* hide Weight tracker */
    assert(app_screen(app)==SCR_HOME && plat_host_rev(hw,8,1) && menu_is(hw,8,"1  Kanban") && menu_is(hw,12,"5  Close the day"));
    /* ...and after Config closed from a sub-screen (TAB) or by Esc. */
    app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,KEY_TAB); assert(app_screen(app)==SCR_HOME && plat_host_rev(hw,8,1));
    app_key(app,KEY_TAB); app_key(app,'2'); app_key(app,KEY_ESCAPE); app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_HOME && plat_host_rev(hw,8,1));
    /* ...or moved to the next visible entry when its own entry is hidden. */
    he_hide(app,kanban,1); assert(plat_host_rev(hw,8,1) && menu_is(hw,8,"1  Christian Rosary"));
    app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_ROSARY_MODE); app_key(app,KEY_ESCAPE); /* Esc from a screen returns to Home on Date */
    assert(plat_host_rev(hw,7,1));
    app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); assert(menu_is(hw,10,"3  Timer") && plat_host_rev(hw,10,1));
    he_hide(app,timer,1); assert(menu_is(hw,10,"3  Close the day") && plat_host_rev(hw,10,1)); /* hiding the last hideable entry lands on Close the day */
    he_hide(app,all5,5); assert(app_state(app)->hiddenHome==0 && plat_host_rev(hw,15,1) && menu_is(hw,14,"7  Timer") && menu_is(hw,15,"8  Close the day")); /* all shown, cursor kept on Close the day */
    /* Meal Planner return point: the entry hidden while in Config over the planner. */
    app_key(app,KEY_UP); app_key(app,KEY_UP); assert(menu_is(hw,13,"6  Reminders") && plat_host_rev(hw,13,1));
    app_key(app,' '); app_key(app,KEY_TAB); app_key(app,'3'); for(r=0;r<6;r++) app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE); app_key(app,'4');
    assert(app_screen(app)==SCR_MEAL_PLANNER); app_key(app,' ');
    assert(app_screen(app)==SCR_HOME && plat_host_rev(hw,13,1) && menu_is(hw,13,"6  Timer"));
    /* Archive day menu unchanged; hidden entries keep working behind the scenes. */
    he_hide(app,act_weight,2); assert(menu_is(hw,7,"0  Date") && menu_is(hw,8,"1  Kanban"));
    app_key(app,'0'); app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE); /* keep the date */
    app_key(app,KEY_UP); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_FINISH); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD); app_key(app,KEY_ENTER);
    app_key(app,KEY_LEFT); assert(menu_is(hw,7,"1  Activities") && menu_is(hw,8,"2  Weight tracker") && menu_is(hw,9,""));
    app_key(app,'3'); assert(row_has(hw,23,"Archived day: read-only"));
    app_key(app,'1'); assert(app_screen(app)==SCR_ACTIVITIES && any_row(hw,"[x]")); app_key(app,KEY_ESCAPE); app_key(app,KEY_RIGHT);
    assert(menu_is(hw,8,"1  Kanban"));
    /* The timer keeps running while its entry is hidden. */
    assert(menu_is(hw,11,"4  Timer")); app_tick(app,1000); app_key(app,'4'); assert(app_screen(app)==SCR_TIMER_SELECT); app_key(app,'1');
    he_hide(app,timer,1); assert(app_screen(app)==SCR_TIMER_RUNNING); app_tick(app,1000+300000); assert(app_screen(app)==SCR_TIMER_COMPLETE);
    app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_HOME && menu_is(hw,11,"4  Close the day") && !any_row(hw,"Timer"));
    /* French, Timer hidden: 7 is nothing, 6 is Close the day; the save holds the PicoCalc ids. */
    app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,'2'); app_key(app,'4');
    assert(menu_is(hw,10,"3  Revue journali\212re") && menu_is(hw,11,"4  Terminer le jour"));
    newest_save(dir); assert(strstr(jbuf,"\"homeHidden\":[\"activities\",\"weight\",\"reminders\",\"timer\"]},\n\"crc32\":\""));
    /* Restart keeps the filtered menu. */
    hw=he_start(app,dir,0); assert(app_state(app)->hiddenHome==0x0c6 && menu_is(hw,8,"1  Kanban") && menu_is(hw,11,"4  Terminer le jour") && menu_is(hw,12,""));
    app_key(app,'5'); assert(app_screen(app)==SCR_HOME);
}
static void check_home_entries(const char *dir){
    static unsigned char store[16384]; static char home[OV_FRAME], kanban[OV_FRAME], file[65536];
    App *app=(App*)store; Platform *hw; State out; uint32_t gen; int active, r; size_t n;
    assert(HOME_LOCKED==0x101 && HOME_ALL==0x1ff && HOME_ENTRY_COUNT==9);
    hw=he_start(app,dir,1);
    frame_of(hw,home);
    app_key(app,KEY_TAB); assert(app_screen(app)==SCR_CONFIG);
    assert(row_has(hw,6,"1  Language: English") && row_has(hw,7,"2  Colors: PAL") && row_has(hw,8,"3  Activities: 9/9") && row_has(hw,9,"4  Save"));
    assert(row_has(hw,11,"Language, Activities: then Save.") && row_has(hw,12,"Esc: discard changes") && row_has(hw,13,"Tab: close Config"));
    app_key(app,'3'); assert(app_screen(app)==SCR_HOME_ENTRIES && plat_host_rev(hw,6,0));
    assert(row_has(hw,3,"ACTIVITIES") && row_has(hw,4,"Home menu entries shown"));
    for(r=6;r<15;r++) assert(row_has(hw,r,"[x] "));
    /* Same cells as the PicoCalc: the tag ends in column 39. */
    assert(row_has(hw,6,"[x] Date                       (locked)") && row_has(hw,14,"[x] Close the day              (locked)"));
    assert(row_has(hw,7,"[x] Activities") && !row_has(hw,7,"locked") && row_has(hw,13,"[x] Timer"));
    assert(row_has(hw,15,"Enter: show/hide") && row_has(hw,16,"Then Save in Configuration."));
    /* Locked entries: Enter only explains; Up wraps to Close the day. */
    app_key(app,KEY_ENTER); assert(row_has(hw,23,"Always shown") && row_has(hw,6,"[x] Date"));
    app_key(app,KEY_UP); assert(plat_host_rev(hw,14,0)); app_key(app,KEY_ENTER); assert(row_has(hw,23,"Always shown") && row_has(hw,14,"[x] Close the day"));
    /* Toggling is pending until Save, like Language. */
    app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); assert(plat_host_rev(hw,7,0)); app_key(app,KEY_ENTER);
    assert(row_has(hw,7,"[ ] Activities") && !row_has(hw,23,"Always"));
    app_key(app,KEY_DOWN); app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); app_key(app,KEY_ENTER); assert(row_has(hw,9,"[x] Kanban")); /* twice = unchanged */
    app_key(app,' '); assert(app_screen(app)==SCR_HOME_ENTRIES); /* SPACE does nothing in Config */
    app_key(app,'2'); assert(app_screen(app)==SCR_HOME_ENTRIES && row_has(hw,7,"[ ] Activities")); /* no digits here */
    assert(app_state(app)->hiddenHome==0 && he_saves==0);
    app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_CONFIG && row_has(hw,8,"3  Activities: 8/9"));
    app_key(app,'4'); assert(app_screen(app)==SCR_HOME && row_has(hw,23,"Configuration saved") && he_saves==1);
    assert(app_state(app)->hiddenHome==0x002);
    n=newest_save(dir); assert(strstr(jbuf,"],\n\"meals\":{\"played\":[]},\n\"homeHidden\":[\"activities\"]},\n\"crc32\":\"") && jbuf[n-1]=='\n');
    /* LPS-DOS-115-CHG11: the home menu shows the checked entries only. */
    assert(menu_is(hw,8,"1  Weight tracker") && menu_is(hw,14,"7  Close the day") && menu_is(hw,15,""));
    /* Reopening starts from the saved choice; Tab drops a pending change, from the sub-screen too. */
    app_key(app,'2'); assert(app_screen(app)==SCR_KANBAN); frame_of(hw,kanban);
    app_key(app,KEY_TAB); app_key(app,'3'); assert(row_has(hw,7,"[ ] Activities")); app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); assert(row_has(hw,7,"[x] Activities"));
    app_key(app,KEY_TAB); assert(app_screen(app)==SCR_KANBAN && frame_is(hw,kanban) && app_state(app)->hiddenHome==0x002);
    app_key(app,KEY_TAB); app_key(app,'3'); assert(row_has(hw,7,"[ ] Activities")); app_key(app,KEY_ESCAPE); app_key(app,KEY_ESCAPE); assert(app_screen(app)==SCR_KANBAN);
    /* Hide everything that can be hidden; save together with a language change. */
    app_key(app,KEY_TAB); app_key(app,'3'); for(r=0;r<9;r++){ app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); }
    assert(row_has(hw,7,"[x] Activities") && row_has(hw,8,"[ ] Weight tracker") && row_has(hw,13,"[ ] Timer") && row_has(hw,6,"[x] Date") && row_has(hw,14,"[x] Close the day"));
    app_key(app,KEY_ESCAPE); app_key(app,'1'); app_key(app,'2'); assert(row_has(hw,8,"3  Activities: 3/9")); app_key(app,'4');
    assert(app_state(app)->language==LANG_FR && app_state(app)->hiddenHome==0x0fc && app_screen(app)==SCR_KANBAN && he_saves==2);
    n=newest_save(dir); assert(strstr(jbuf,"\"homeHidden\":[\"weight\",\"kanban\",\"rosary\",\"daily_review\",\"reminders\",\"timer\"]},\n\"crc32\":\""));
    assert(strstr(jbuf,"\"state\":{\"language\":\"fr\","));
    app_key(app,KEY_TAB); app_key(app,'3'); assert(row_has(hw,3,"ACTIVIT\220S") && row_has(hw,4,"Entr\202es affich\202es dans le menu"));
    assert(row_has(hw,6,"[x] Date" "                   " "(verrouill\202)") && row_has(hw,14,"[x] Terminer le jour" "       " "(verrouill\202)"));
    assert(row_has(hw,15,"Entr\202e : afficher/masquer"));
    app_key(app,KEY_DOWN); for(r=0;r<5;r++){ app_key(app,KEY_DOWN); app_key(app,KEY_ENTER); }
    app_key(app,KEY_ESCAPE); assert(row_has(hw,8,"3  Activit\202s: 8/9") && row_has(hw,11,"Langue, Activit\202s : puis enregistrer.") && row_has(hw,13,"Tab : fermer la configuration"));
    /* A failed write keeps the pending choice, the old state and the old file. */
    newest_save(dir); memcpy(file,jbuf,sizeof(file));
    he_fail=1; app_key(app,'4');
    assert(app_screen(app)==SCR_CONFIG && app_state(app)->hiddenHome==0x0fc && app_state(app)->language==LANG_FR && row_has(hw,8,"8/9") && row_has(hw,23,"chec sauvegarde"));
    newest_save(dir); assert(!strcmp(jbuf,file));
    he_fail=0; app_key(app,'4'); assert(app_state(app)->hiddenHome==0x080 && app_screen(app)==SCR_KANBAN); /* only Timer hidden */
    n=newest_save(dir); assert(strstr(jbuf,"\"homeHidden\":[\"timer\"]},\n\"crc32\":\""));
    /* Colors: a failed write keeps the old palette too. */
    app_key(app,KEY_TAB); app_key(app,'2'); app_key(app,'5'); he_fail=1; app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_COLORS && app_state(app)->palette!=4);
    he_fail=0; app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_KANBAN && app_state(app)->palette==4 && app_state(app)->hiddenHome==0x080);
    /* Restart: the choice comes back from the file. */
    app_key(app,KEY_ESCAPE);
    hw=he_start(app,dir,0); assert(app_state(app)->hiddenHome==0x080 && app_state(app)->language==LANG_FR);
    app_key(app,KEY_TAB); assert(row_has(hw,8,"3  Activit\202s: 8/9")); app_key(app,'3'); assert(row_has(hw,13,"[ ] Minuteur"));
    /* Show everything again; save. */
    app_key(app,KEY_UP); app_key(app,KEY_UP); app_key(app,KEY_ENTER); app_key(app,KEY_ESCAPE); app_key(app,'4');
    assert(app_state(app)->hiddenHome==0 && app_screen(app)==SCR_HOME);
    n=newest_save(dir); assert(strstr(jbuf,"\"meals\":{\"played\":[]},\n\"homeHidden\":[]},\n\"crc32\":\""));
    /* The timer finishing over the sub-screen drops the pending choice. */
    app_tick(app,1000); app_key(app,'7'); app_key(app,'1'); app_key(app,KEY_TAB); app_key(app,'3'); app_key(app,KEY_DOWN); app_key(app,KEY_ENTER);
    app_tick(app,1000+300000); assert(app_screen(app)==SCR_TIMER_COMPLETE && app_state(app)->hiddenHome==0);
    app_key(app,KEY_TAB); assert(row_has(hw,8,"9/9")); app_key(app,KEY_TAB);
    /* A locked bit in RAM is never written; the ids read back give 0x0fe. */
    { static State s; state_init(&s); s.hiddenHome=0x1ff; gen=0; active=-1;
      fresh_dir(dir); assert(save_store(dir,&s,&gen,&active));
      newest_save(dir); assert(strstr(jbuf,"\"homeHidden\":[\"activities\",\"weight\",\"kanban\",\"rosary\",\"daily_review\",\"reminders\",\"timer\"]},\n\"crc32\":\""));
      assert(save_load(dir,&out,&gen,&active)==LOAD_OK && out.hiddenHome==0x0fe); }
    /* A save written by the PicoCalc's code (Kanban and Rosary hidden) shows its
     * choice in Config; saving it unchanged gives the PicoCalc's homeHidden bytes. */
    { char pa[256]; sprintf(pa,"%s/SAVE_A.JSN",dir); fresh_dir(dir); copy_file("tests/fixtures/picocalc_custom.jsn",pa); }
    hw=he_start(app,dir,0); if(app_screen(app)==SCR_REMINDER_POPUP) app_key(app,KEY_ENTER);
    assert(app_state(app)->hiddenHome==0x018);
    app_key(app,KEY_TAB); assert(row_has(hw,8,": 7/9")); app_key(app,'3');
    assert(row_has(hw,9,"[ ] Kanban") && row_has(hw,10,"[ ] Ros") && row_has(hw,8,"[x] ") && row_has(hw,11,"[x] "));
    app_key(app,KEY_ESCAPE); app_key(app,'4'); assert(he_saves==1);
    newest_save(dir); assert(strstr(jbuf,"\"homeHidden\":[\"kanban\",\"rosary\"]},\n\"crc32\":\""));
    (void)n;
}
int main(void){
    const char *tmpdir="/tmp/lps-msdos-test";
    State s, out; Blob b; int32_t g=0; Date calendar; Day day;
    char icsbuf[2048]; char export_dir[200], weights_dir[200];
    uint32_t gen=0; int active=-1;
    Platform *hw; App *app; static unsigned char store[16384];
    assert(lps_buffers_init() && lps_buffers_init()); /* idempotent */
    rm_tree(tmpdir); mkdir(tmpdir,0755);
    lps_set_data_dir(tmpdir); lps_ensure_dirs();

    assert(WIRE_SIZE==4633);
    assert(parse_weight("110,500",&g)&&g==110500);
    assert(parse_weight("0.001",&g)&&g==1);
    assert(parse_weight("",&g)&&g==-1);
    assert(!parse_weight("0",&g));
    assert(parse_date("2028-02-29",&calendar)&&next_date(&calendar)&&calendar.month==3&&calendar.day==1);
    assert(!parse_date("2027-02-29",&calendar));

    state_init(&s);
    s.xp=70; s.today.flags=0x1ff; s.today.expected=110500; s.today.actual=111200;
    strcpy(s.today.note,"OK");
    s.language=LANG_EN; s.palette=PAL_RED;
    s.taskCount=1; s.tasks[0].status=TASK_TODO; strcpy(s.tasks[0].name,"Write docs");
    s.reminderCount=1; s.reminders[0].from=s.today.date; s.reminders[0].duration=7; strcpy(s.reminders[0].text,"Call dentist");
    encode_state(&s,&b);
    assert(b.size==WIRE_SIZE);
    assert(decode_state(b.bytes,b.size,&out));
    assert(out.xp==70 && out.today.flags==0x1ff && out.today.expected==110500);
    assert(strcmp(out.today.note,"OK")==0);
    assert(out.taskCount==1 && strcmp(out.tasks[0].name,"Write docs")==0);
    assert(out.reminderCount==1 && strcmp(out.reminders[0].text,"Call dentist")==0);
    assert(out.palette==PAL_RED);

    /* A/B write/load (JSON, LPS-DOS-115-CHG04; more in check_json_saves) */
    gen=0; active=-1;
    assert(save_store(tmpdir,&s,&gen,&active) && gen==1 && active==0);
    assert(save_load(tmpdir,&out,&gen,&active)==LOAD_OK && gen==1 && active==0);
    assert(out.xp==70 && out.today.flags==0x1ff && strcmp(out.tasks[0].name,"Write docs")==0);
    assert(save_store(tmpdir,&s,&gen,&active) && gen==2 && active==1);

    day=s.today;
    assert(build_ics(&day, &s, 110, 40, LANG_EN, icsbuf, sizeof(icsbuf)));
    assert(strstr(icsbuf,"BEGIN:VCALENDAR"));
    assert(strstr(icsbuf,"UID:lps-companion-20260917@picocalc"));
    assert(strstr(icsbuf,"Activities: "));
    /* LPS-DOS-115-CHG01: the XP line stays, empty, exactly like PicoCalc v1.12+. */
    assert(strstr(icsbuf,"\\NXP:\\NExpected weight: 110.500 kg\\NActual weight: 111.200 kg\\NNote: OK\r\n"));
    assert(!strstr(icsbuf,"XP: +") && !strstr(icsbuf,"total"));
    lps_path_join(export_dir,sizeof(export_dir),tmpdir,"EXPORT");
    assert(write_ics_file(export_dir,&day,&s,110,40,LANG_EN));
    lps_path_join(weights_dir,sizeof(weights_dir),tmpdir,"WEIGHTS");
    assert(write_weight_csv(weights_dir,&day));
    assert(write_weight_csv(weights_dir,&day)); /* idempotent */

    /* App smoke on a clean data dir (avoid reminder popup from prior SAVE). */
    rm_tree(tmpdir); mkdir(tmpdir,0755);
    lps_set_data_dir(tmpdir); lps_ensure_dirs();
    hw=plat_host_create(0);
    assert(app_bytes()<=sizeof(store));
    app=(App*)store;
    app_init(app,hw);
    app_start(app);
    if(app_screen(app)==SCR_REMINDER_POPUP) app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_HOME);
    assert(row_has(hw,1,"Thursday 17 September 2026 (week 38)") && !any_xp(hw));
    assert(row_has(hw,5,"0/22 activities"));
    app_key(app,'1'); /* activities */
    app_key(app,'1'); app_key(app,'2'); app_key(app,'3');
    assert(points(&app_state(app)->today)==40);
    app_key(app,KEY_ESCAPE);
    app_key(app,KEY_UP); /* Close the day */
    app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_FINISH);
    assert(row_has(hw,6,"Activities: 3") && !any_xp(hw));
    assert(row_has(hw,1,"Thursday 17 September 2026 (week 38)"));
    app_key(app,KEY_ENTER);
    assert(app_state(app)->xp==40); /* still banked silently */
    assert(app_state(app)->today.number==2);
    assert(app_screen(app)==SCR_REWARD && row_has(hw,3,"DAY SAVED") && !any_xp(hw));
    assert(!strstr(plat_host_row(hw,8),"KEEPSAKE") && !strstr(plat_host_row(hw,9),"Pocket notebook"));
    app_key(app,KEY_ENTER); /* reward -> home */
    assert(app_screen(app)==SCR_HOME && row_has(hw,1,"Friday 18 September 2026 (week 38)") && !any_xp(hw));
    app_key(app,KEY_LEFT); /* archive: previous day */
    assert(row_has(hw,1,"Thursday 17 September 2026 (week 38)") && row_has(hw,2,"ARCHIVED DAY") && !any_xp(hw));
    assert(row_has(hw,5,"3/22 activities"));
    app_key(app,KEY_RIGHT);
    app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,'2'); app_key(app,'4'); /* Config: French, save */
    assert(app_state(app)->language==LANG_FR && app_screen(app)==SCR_HOME);
    assert(row_has(hw,1,"Vendredi 18 Septembre 2026 (semaine 38)") && row_has(hw,5,"0/22 activit\202s") && !any_xp(hw));
    { /* the exported ICS file on disk carries the empty XP line */
        char path[256], got[2048]; FILE *f; size_t n;
        /* write_ics_file joins with a DOS backslash, so on the host it is part of the name. */
        sprintf(path,"%s/EXPORT\\20260917.ICS", tmpdir);
        f=fopen(path,"rb"); assert(f); n=fread(got,1,sizeof(got)-1,f); fclose(f); got[n]=0;
        assert(strstr(got,"DESCRIPTION:Activities: Walk, Diet, Bible\\NXP:\r\nEND:VEVENT"));
    }

    /* Authentic PicoCalc v1.2 payload, wrapped as SAVE_A.BIN (not a machine-local path). */
    rm_tree(tmpdir); mkdir(tmpdir,0755);
    assert(write_v12_fixture(tmpdir));
    {
        enum LoadResult r=save_load(tmpdir,&out,&gen,&active);
        assert(r==LOAD_MIGRATED && gen==50 && active==-1);
        assert(out.xp==90 && out.language==LANG_EN);
        assert(out.today.number==3 && out.today.flags==1025);
        assert(out.today.expected==110500 && out.today.actual==111250);
        assert(strcmp(out.today.note,"Keep this original note")==0);
        assert(out.count==2 && out.next==2);
        assert(out.history[1].flags==512 && out.history[1].actual==111500);
        assert(strcmp(out.history[1].note,"Yesterday from v1.2")==0);
        printf("fixture v1.2 SAVE_A.BIN migrated xp=%lu tasks=%u reminders=%u gen=%lu\n",
            (unsigned long)out.xp,(unsigned)out.taskCount,(unsigned)out.reminderCount,(unsigned long)gen);
    }

    /* mystery weekday: 2026-10-01 Thursday -> Luminous */
    { Date d={2026,10,1}; assert(weekday(d)==4); }

    /* LPS-DOS-115-CHG01: long date line with ISO week (PicoCalc LPS-112-CHG03 wording). */
    {
        char t[64]; Date d;
        d.year=2026; d.month=11; d.day=8;
        long_date_text(d,LANG_FR,t,sizeof(t)); assert(strcmp(t,"Dimanche 8 Novembre 2026 (semaine 45)")==0);
        long_date_text(d,LANG_EN,t,sizeof(t)); assert(strcmp(t,"Sunday 8 November 2026 (week 45)")==0);
        d.year=2026; d.month=2; d.day=14;
        long_date_text(d,LANG_FR,t,sizeof(t)); assert(strcmp(t,"Samedi 14 Février 2026 (semaine 7)")==0);
        d.year=2021; d.month=1; d.day=3; assert(iso_week(d)==53);
        d.year=2020; d.month=12; d.day=31; assert(iso_week(d)==53);
        d.year=2024; d.month=12; d.day=30; assert(iso_week(d)==1);
        d.year=2000; d.month=1; d.day=1; assert(iso_week(d)==52);
        d.year=2099; d.month=12; d.day=31; assert(iso_week(d)==53);
        check_iso_weeks();
    }

    check_rosary_dat("/tmp");
    {   /* The app: Rosary opens with ROSARY.DAT, and says why when it cannot. */
        static unsigned char store2[16384]; App *ra=(App*)store2; Platform *rh;
        rm_tree(tmpdir); mkdir(tmpdir,0755); lps_set_data_dir(tmpdir); lps_ensure_dirs();
        rh=plat_host_create(0); app_init(ra,rh); app_start(ra);
        rosary_set_path(ROSARY_DAT_NAME);
        app_key(ra,'4'); assert(app_screen(ra)==SCR_ROSARY_MODE && rosary_loaded());
        app_key(ra,KEY_ENTER); app_key(ra,KEY_ENTER); /* decade mode, today's set */
        app_key(ra,KEY_ENTER); app_key(ra,KEY_RIGHT); /* first decade, mystery step */
        assert(app_screen(ra)==SCR_ROSARY_PRAYER);
        app_key(ra,KEY_ESCAPE); assert(app_screen(ra)==SCR_HOME && !rosary_loaded()); /* freed */
        {   /* Missing: a directory where neither path nor fallback exists. */
            char cwd[512]; assert(getcwd(cwd,sizeof(cwd)));
            assert(chdir(tmpdir)==0);
            rosary_set_path("/tmp/lps-msdos-test/none/ROSARY.DAT");
            app_key(ra,'4');
            assert(app_screen(ra)==SCR_HOME && !rosary_loaded());
            assert(strstr(plat_host_row(rh,23),"ROSARY.DAT not found: copy it next to LPSCOMP.EXE"));
            {   unsigned char junk[32]; memset(junk,'z',sizeof(junk));
                write_file("ROSARY.DAT",junk,sizeof(junk)); app_key(ra,'4');
                assert(app_screen(ra)==SCR_HOME && strstr(plat_host_row(rh,23),"ROSARY.DAT is damaged: copy it again"));
                remove("ROSARY.DAT"); }
            assert(chdir(cwd)==0);
            rosary_set_path(ROSARY_DAT_NAME);
            app_key(ra,'4'); assert(app_screen(ra)==SCR_ROSARY_MODE); app_key(ra,KEY_ESCAPE);
        }
    }

    check_menu_and_legacy_note(tmpdir);
    check_timer(tmpdir);
    check_french_cp437(tmpdir);
    check_wrap(tmpdir);
    check_json_saves(tmpdir);
    check_activities(tmpdir);
    check_custom_activities(tmpdir);
    check_fast_rematch("tests/fixtures/picocalc_full.jsn");
    check_fast_rematch("tests/fixtures/picocalc_custom.jsn");
    check_bios_keys(tmpdir);
    check_accented_text(tmpdir);
    check_intl_interop(tmpdir);
    check_meals_dat(tmpdir);
    check_mealscga_dat(tmpdir);
    check_meal_planner(tmpdir);
    check_meal_csv(tmpdir);
    check_cga_fold(tmpdir);
    check_cga_grey();
    check_overlay_keys(tmpdir);
    check_home_entries(tmpdir);
    check_home_filtered(tmpdir);
    assert(orphan_hits>0); /* some edit took a name history had stored */

    printf("%s", "PASS: storage, ICS (empty XP line), CSV, A/B, app close-day without XP, long date + ISO week 2000-2099, ROSARY.DAT load/missing/damaged, title v1.15, menu 0-8 without notepad or Config, archive menu 1-2, legacy note in ICS, timer 5/30/60 on keys 1-3, ICS 8.3 name YYYYMMDD.ICS, French CP437 screens + UTF-8 ICS, word wrap, JSON saves (round trip, PicoCalc fixtures byte-identical, BIN migration, damaged slot fallback,");
    printf("%s", " unknown keys, newer schema), activities (22 defaults, catalog from the save, 3 columns, keys per column, archive extras, 30 entries, pages), custom activities (rename per language, new, Del with confirm, F4/F5 wrap, frozen history, limits), top-row keys by BIOS scan code (AZERTY/US, keypad, text entry), accented tasks and reminders (32 UTF-8 bytes, PicoCalc fixture),");
    printf(" UTF-8 kept byte for byte (PicoCalc JSON/ICS/CSV with oe, A grave, euro, emoji), MEALS.DAT (13 pictures, checksums, damaged/missing), MEALSCGA.DAT (13 CGA pictures, colours, checksums, damaged/missing, EGA/CGA independent), Meal Planner (SPACE, wraps, play/put back, saves, archive read-only, new deck at close, text card, graphics hooks, FR, PicoCalc deck byte-identical),");
    printf(" Meal Planner CSV (MPYYMMDD.CSV byte-identical to PicoCalc, rules, failures, close day), TAB/SPACE overlays (identical return, text entry, Weight, confirmations, cross-open, archive, timer behind overlays + CSV), CGA 40-column fold (title/date, label/counter, blocks, overflow, reverse, planner rows EN/FR, MEALSCGA.DAT messages), CGA played card (checkerboard grey-out masks, PLAYED/JOUEE label sizes), fixture decode (%u captures)\n", 0);
    rm_tree(tmpdir);
    return 0;
}
