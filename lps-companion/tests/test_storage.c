/* Host unit tests: JSON A/B saves and BIN migration, ICS, CSV, ROSARY.DAT, screens. */
#define _POSIX_C_SOURCE 200112L
#include "lps.h"
#include "rosary.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

Platform *plat_host_create(int capture);
const char *plat_host_row(Platform *p, int row);
int plat_host_rev(Platform *p, int row, int col);

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

/* LPS-DOS-115-CHG02 (PicoCalc LPS-112-CHG05): no notepad, menu 0-9, archive
 * menu 1-2, legacy notes kept in the save and exported to the ICS. */
static int any_row(Platform *hw, const char *text){
    int r; for(r=0;r<ROWS;r++) if(strstr(plat_host_row(hw,r),text)) return 1; return 0;
}
static void check_menu_and_legacy_note(const char *tmpdir){
    static unsigned char store3[16384]; App *a=(App*)store3; Platform *hw; State s;
    uint32_t gen=0; int active=-1; unsigned i; char path[256], got[2048]; FILE *f; size_t n;
    static const char *labels[10]={"0  Date","1  Activities","2  Weight tracker","3  Kanban","4  Christian Rosary",
        "5  Daily review","6  Config","7  Reminders","8  Timer","9  Close the day"};
    static const int screens[10]={SCR_DATE,SCR_ACTIVITIES,SCR_WEIGHT,SCR_KANBAN,SCR_ROSARY_MODE,
        SCR_DAILY_REVIEW,SCR_CONFIG,SCR_REMINDERS,SCR_TIMER_SELECT,SCR_FINISH};
    rm_tree(tmpdir); mkdir(tmpdir,0755); lps_set_data_dir(tmpdir); lps_ensure_dirs();
    /* A save written before v1.12 with a note on today. */
    state_init(&s); s.today.flags=3; strcpy(s.today.note,"Legacy note, kept; really");
    assert(save_store(tmpdir,&s,&gen,&active));
    hw=plat_host_create(0); app_init(a,hw); app_start(a);
    assert(app_screen(a)==SCR_HOME);
    for(i=0;i<10;i++) assert(strstr(plat_host_row(hw,7+(int)i),labels[i]));
    assert(!any_row(hw,"Notepad") && !any_row(hw,"NOTEPAD") && !any_row(hw,"Legacy note") && !any_row(hw,"10 "));
    assert(strstr(plat_host_row(hw,7),"WEIGHT TRACKER") && strstr(plat_host_row(hw,11),"REMINDERS") && strstr(plat_host_row(hw,16),"KANBAN"));
    for(i=0;i<10;i++){
        rosary_set_path(ROSARY_DAT_NAME);
        app_key(a,(int)('0'+i));
        if((int)app_screen(a)!=screens[i]){ fprintf(stderr,"key %u -> screen %d\n",i,(int)app_screen(a)); assert(0); }
        app_key(a,KEY_ESCAPE); assert(app_screen(a)==SCR_HOME);
    }
    for(i=0;i<10;i++) app_key(a,KEY_DOWN); /* wraps over exactly 10 entries */
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
    app_key(a,'8'); assert(app_screen(a)==SCR_TIMER_SELECT);
    assert(strstr(plat_host_row(hw,8),"1  5 minutes") && strstr(plat_host_row(hw,10),"2  30 minutes") && strstr(plat_host_row(hw,12),"3  60 minutes"));
    assert(!any_row(hw,"15 minutes"));
    /* Each key starts its duration at once, without Enter. */
    app_key(a,'1'); assert(app_screen(a)==SCR_TIMER_RUNNING && strstr(plat_host_row(hw,12),"05:00")); app_key(a,KEY_ESCAPE);
    app_key(a,'8'); app_key(a,'2'); assert(app_screen(a)==SCR_TIMER_RUNNING && strstr(plat_host_row(hw,12),"30:00")); app_key(a,KEY_ESCAPE);
    app_key(a,'8'); app_key(a,'3'); assert(app_screen(a)==SCR_TIMER_RUNNING && strstr(plat_host_row(hw,12),"60:00"));
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
    app_key(a,'6'); app_key(a,'1'); app_key(a,'2'); app_key(a,'3');
    app_key(a,'8'); app_key(a,'1'); assert(app_state(a)->language==LANG_FR && any_row(hw,"MINUTEUR") && any_row(hw,"Enter: Pause   Esc: Annuler"));
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
    app_key(a,'6'); app_key(a,'1'); app_key(a,'2'); app_key(a,'3');
    assert(app_state(a)->language==LANG_FR && app_screen(a)==SCR_HOME);
    french_screen_ok(hw,"home");
    assert(any_row(hw,"1  Activit\202s") && any_row(hw,"4  Rosaire chr\202tien") && any_row(hw,"5  Revue journali\212re"));
    app_key(a,'1'); french_screen_ok(hw,"activities"); assert(any_row(hw,"ACTIVIT\220S"));
    app_key(a,'1'); app_key(a,'2'); assert(any_row(hw,"R\202gime")); app_key(a,KEY_ESCAPE);
    app_key(a,'2'); french_screen_ok(hw,"weight"); app_key(a,KEY_ESCAPE);
    app_key(a,'3'); french_screen_ok(hw,"kanban"); app_key(a,'n'); french_screen_ok(hw,"task edit"); app_key(a,KEY_ESCAPE); app_key(a,KEY_ESCAPE);
    app_key(a,'5'); french_screen_ok(hw,"daily review"); app_key(a,KEY_ESCAPE);
    app_key(a,'6'); french_screen_ok(hw,"config"); app_key(a,KEY_ESCAPE);
    app_key(a,'7'); french_screen_ok(hw,"reminders"); app_key(a,KEY_ESCAPE);
    app_key(a,'8'); french_screen_ok(hw,"timer"); app_key(a,KEY_ESCAPE);
    assert(app_screen(a)==SCR_HOME);
    app_key(a,'4'); assert(app_screen(a)==SCR_ROSARY_MODE); french_screen_ok(hw,"rosary mode");
    app_key(a,KEY_ENTER); french_screen_ok(hw,"rosary set"); app_key(a,KEY_ENTER); french_screen_ok(hw,"rosary decade");
    app_key(a,KEY_ENTER);
    for(i=0;i<6 && !any_row(hw,"Notre P\212re, qui es aux cieux");i++){ french_screen_ok(hw,"rosary prayer"); app_key(a,KEY_RIGHT); }
    assert(app_screen(a)==SCR_ROSARY_PRAYER && any_row(hw,"Notre P\212re, qui es aux cieux"));
    french_screen_ok(hw,"rosary prayer");
    app_key(a,KEY_ESCAPE); assert(app_screen(a)==SCR_HOME);
    app_key(a,'9');
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
        app_key(ap,'6'); app_key(ap,'1'); app_key(ap,'2'); app_key(ap,'3');
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
    s->today.number=1+days; s->today.flags=0x8001; strcpy(s->today.note,"Café à l'église, très tôt");
    s->taskCount=2; s->tasks[0].status=TASK_DOING; strcpy(s->tasks[0].name,"Réparer le vélo");
    s->tasks[1].status=TASK_DONE; strcpy(s->tasks[1].name,"Plan \\ \"B\"");
    s->reminderCount=1; s->reminders[0].from=DEFAULT_DATE; s->reminders[0].duration=365; strcpy(s->reminders[0].text,"Fête de Noël");
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
    assert(!strcmp(out.today.note,"Noël en famille, gâteau à l'orange") && out.today.expected==82500 && out.today.actual==-1);
    assert(out.count==5 && out.next==5);
    assert(ring(&out,1)->number==36 && ring(&out,1)->flags==((1ul<<0)|(1ul<<16)|(1ul<<22)) && !strcmp(ring(&out,1)->note,"Séance à la piscine, très bien"));
    assert(day_orphan_count(&out,2)==1 && !strcmp(day_orphan(&out,2,0),"Tai-chi") && day_names(2)->count==4);
    assert(ring(&out,2)->flags==((1ul<<2)|(1ul<<16)|(1ul<<23))); /* the frozen "Projet" is no catalog entry now */
    assert(day_orphan_count(&out,3)==1 && !strcmp(day_orphan(&out,3,0),"Projet"));
    assert(ring(&out,3)->flags==((1ul<<0)|(1ul<<9)|(1ul<<16)|(1ul<<21)) && day_orphan_count(&out,4)==0);
    assert(out.taskCount==3 && out.tasks[0].status==TASK_DOING && !strcmp(out.tasks[0].name,"Réparer le vélo"));
    assert(!strcmp(out.tasks[1].name,"Écrire à Hélène") && !strcmp(out.tasks[2].name,"Plan \\ \"B\""));
    assert(out.reminderCount==2 && !strcmp(out.reminders[0].text,"Anniversaire de François") && out.reminders[0].duration==30);
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
    app_key(app,'9'); assert(app_screen(app)==SCR_FINISH); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD);
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
    /* Text CP437 lacks becomes '?'; \u escapes are decoded. */
    n=slurp(path,jbuf,sizeof(jbuf));
    { char *note=strstr(jbuf,"\"note\":\"Caf"); assert(note);
      insert_after(note,"\"note\":\"","\\u00e9t\xc3\xa9 \xe2\x80\x93 \xe2\x82\xac \xc5\x93 \\ud83d\\ude00 "); }
    resign(jbuf); spit(pa,jbuf,strlen(jbuf));
    assert(save_load(dir,&out,&gen,&active)==LOAD_OK);
    assert(!strncmp(out.today.note,"été ? ? ? ? Café",strlen("été ? ? ? ? Café")));
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
    app_key(app,' '); assert(app_state(app)->today.flags==((1ul<<21)|(1ul<<10)) && cell_is(hw,7,27,"3 [x] Weekend"));
    app_key(app,'0'); assert(app_state(app)->today.flags==((1ul<<21)|(1ul<<10))); /* 8 rows: no row 10 */
    app_key(app,KEY_LEFT); app_key(app,'&'); assert(app_state(app)->today.flags==((1ul<<21)|(1ul<<10)|1ul)); /* AZERTY 1 */
    app_key(app,'1'); app_key(app,KEY_ESCAPE);
    assert(app_screen(app)==SCR_HOME && row_has(hw,5,"2/22 activities"));
    app_key(app,'5'); assert(any_row(hw,"ACTIVITIES (2)") && any_row(hw,"  Weekend") && any_row(hw,"  Meditation")); app_key(app,KEY_ESCAPE);
    /* Closing the day: the ICS lists the ticks in catalog order, as the PicoCalc. */
    app_key(app,'9'); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD); app_key(app,KEY_ENTER);
    read_ics(dir,"20260917",got,sizeof(got));
    assert(strstr(got,"DESCRIPTION:Activities: Weekend, Meditation\\NXP:\r\n"));
    assert(day_names(1)->count==2 && !strcmp(day_names(1)->names[0],"Weekend") && !strcmp(day_names(1)->names[1],"M\xc3\xa9" "ditation") && day_names(0)->count==0);
    app_key(app,KEY_LEFT); assert(row_has(hw,5,"2/22 activities")); app_key(app,KEY_RIGHT);
    /* French: names with accents on screen (CP437), UTF-8 in the ICS. */
    app_key(app,'6'); app_key(app,'1'); app_key(app,'2'); app_key(app,'3');
    app_key(app,'1'); assert(cell_is(hw,5,54,"  [ ] Sport") && cell_is(hw,10,54,"  [ ] M\202ditation") && cell_is(hw,6,27,"  [ ] Cong\202"));
    app_key(app,KEY_RIGHT); app_key(app,'2'); app_key(app,KEY_RIGHT); app_key(app,'1'); app_key(app,'6'); app_key(app,'3'); app_key(app,KEY_ESCAPE);
    assert(row_has(hw,5,"4/22 activit\202s"));
    app_key(app,'9'); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD);
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
    app_key(app,'9'); app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_REWARD);
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
static void choose_language(App *app, int fr){ app_key(app,'6'); app_key(app,'1'); app_key(app,fr?'2':'1'); app_key(app,'3'); }
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
    for(i=0;i<13;i++) app_key(app,130);
    app_key(app,KEY_ENTER); assert(app_screen(app)==SCR_ACTIVITY_EDIT && any_row(hw,"Name is too long"));
    app_key(app,KEY_ESCAPE);
    /* Create in French only, typing an accent: English shows the same name. */
    app_key(app,KEY_ESCAPE); choose_language(app,1);
    app_key(app,'1'); app_key(app,'n'); assert(app_screen(app)==SCR_ACTIVITY_EDIT && row_has(hw,3,"NOUVELLE ACTIVIT\220"));
    app_key(app,'R'); app_key(app,130); type_text(app,"veil"); assert(row_has(hw,6,"R\202veil")); app_key(app,KEY_ENTER);
    assert(app_state(app)->activityCount==23 && app_state(app)->activities[22].id==23 && app_selection(app)==22);
    assert(!strcmp(app_state(app)->activities[22].fr,"R\xc3\xa9veil") && !app_state(app)->activities[22].en[0]);
    assert(cell_is(hw,11,54,"7 [ ] R\202veil"));
    app_key(app,130); /* é outside a name is the AZERTY 2: ticks row 2 of the column */
    assert(app_state(app)->today.flags==(1ul<<17));
    app_key(app,130); assert(app_state(app)->today.flags==0);
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
    app_key(app,KEY_ESCAPE); app_key(app,'9'); app_key(app,KEY_ENTER); app_key(app,KEY_ENTER);
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
    app_key(app,'6'); app_key(app,'1'); app_key(app,'2'); app_key(app,'3'); /* Config: French, save */
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
    assert(orphan_hits>0); /* some edit took a name history had stored */

    printf("PASS: storage, ICS (empty XP line), CSV, A/B, app close-day without XP, long date + ISO week 2000-2099, ROSARY.DAT load/missing/damaged, menu 0-9 without notepad, archive menu 1-2, legacy note in ICS, timer 5/30/60 on keys 1-3, ICS 8.3 name YYYYMMDD.ICS, French CP437 screens + UTF-8 ICS, word wrap, JSON saves (round trip, PicoCalc fixtures byte-identical, BIN migration, damaged slot fallback, unknown keys, newer schema), activities (22 defaults, catalog from the save, 3 columns, keys per column, archive extras, 30 entries, pages), custom activities (rename per language, new, Del with confirm, F4/F5 wrap, frozen history, limits), fixture decode (%u captures)\n", 0);
    rm_tree(tmpdir);
    return 0;
}
