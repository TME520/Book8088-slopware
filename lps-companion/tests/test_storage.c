/* Host unit tests: encode/decode, ICS, CSV, A/B SAVE round-trip, ROSARY.DAT. */
#define _POSIX_C_SOURCE 200112L
#include "../src/lps.h"
#include "../src/rosary.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

Platform *plat_host_create(int capture);
const char *plat_host_row(Platform *p, int row);

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
    rosary_unload(); rosary_set_path(ROSARY_DAT_NAME);
}

/* LPS-DOS-115-CHG02 (PicoCalc LPS-112-CHG05): no notepad, menu 0-9, archive
 * menu 1-2, legacy notes kept in the save and exported to the ICS. */
static int any_row(Platform *hw, const char *text){
    int r; for(r=0;r<ROWS;r++) if(strstr(plat_host_row(hw,r),text)) return 1; return 0;
}
static void check_menu_and_legacy_note(const char *tmpdir){
    static unsigned char store3[8192]; App *a=(App*)store3; Platform *hw; State s; Blob b;
    uint32_t gen=0; int active=0; unsigned i; char path[256], got[2048]; FILE *f; size_t n;
    static const char *labels[10]={"0  Date","1  Activities","2  Weight tracker","3  Kanban","4  Christian Rosary",
        "5  Daily review","6  Config","7  Reminders","8  Timer","9  Close the day"};
    static const int screens[10]={SCR_DATE,SCR_ACTIVITIES,SCR_WEIGHT,SCR_KANBAN,SCR_ROSARY_MODE,
        SCR_DAILY_REVIEW,SCR_CONFIG,SCR_REMINDERS,SCR_TIMER_SELECT,SCR_FINISH};
    rm_tree(tmpdir); mkdir(tmpdir,0755); lps_set_data_dir(tmpdir); lps_ensure_dirs();
    /* A save written before v1.12 with a note on today. */
    state_init(&s); s.today.flags=3; strcpy(s.today.note,"Legacy note, kept; really");
    encode_state(&s,&b); assert(save_write_ab(tmpdir,b.bytes,b.size,&gen,&active));
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
    sprintf(path,"%s/EXPORT\\2026-09-17_LPS-Companion.ics",tmpdir);
    f=fopen(path,"rb"); assert(f); n=fread(got,1,sizeof(got)-1,f); fclose(f); got[n]=0;
    assert(strstr(got,"DESCRIPTION:Activities: Walk, Diet\\NXP:\\NNote: Legacy note\\, kept\\; really\r\nEND:VEVENT"));
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

int main(void){
    const char *tmpdir="/tmp/lps-msdos-test";
    State s, out; Blob b; int32_t g=0; Date calendar; Day day;
    char icsbuf[2048]; char export_dir[200], weights_dir[200];
    uint8_t payload[BLOB_CAPACITY]; size_t psz=0; uint32_t gen=0; int active=0;
    Platform *hw; App *app; static unsigned char store[8192];
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

    /* A/B write/load */
    gen=0; active=0;
    assert(save_write_ab(tmpdir,b.bytes,b.size,&gen,&active));
    assert(gen==1);
    assert(save_load_ab(tmpdir,payload,sizeof(payload),&psz,&gen,&active)==LOAD_OK);
    assert(psz==WIRE_SIZE && decode_state(payload,psz,&out));

    /* Round-trip again to other slot */
    assert(save_write_ab(tmpdir,b.bytes,b.size,&gen,&active));
    assert(gen==2);

    day=s.today;
    assert(build_ics(&day, 110, 40, LANG_EN, icsbuf, sizeof(icsbuf)));
    assert(strstr(icsbuf,"BEGIN:VCALENDAR"));
    assert(strstr(icsbuf,"UID:lps-companion-20260917@picocalc"));
    assert(strstr(icsbuf,"Activities: "));
    /* LPS-DOS-115-CHG01: the XP line stays, empty, exactly like PicoCalc v1.12+. */
    assert(strstr(icsbuf,"\\NXP:\\NExpected weight: 110.500 kg\\NActual weight: 111.200 kg\\NNote: OK\r\n"));
    assert(!strstr(icsbuf,"XP: +") && !strstr(icsbuf,"total"));
    lps_path_join(export_dir,sizeof(export_dir),tmpdir,"EXPORT");
    assert(write_ics_file(export_dir,&day,110,40,LANG_EN));
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
    assert(row_has(hw,5,"0/16 activities"));
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
    assert(row_has(hw,5,"3/16 activities"));
    app_key(app,KEY_RIGHT);
    app_key(app,'6'); app_key(app,'1'); app_key(app,'2'); app_key(app,'3'); /* Config: French, save */
    assert(app_state(app)->language==LANG_FR && app_screen(app)==SCR_HOME);
    assert(row_has(hw,1,"Vendredi 18 Septembre 2026 (semaine 38)") && row_has(hw,5,"0/16 activites") && !any_xp(hw));
    { /* the exported ICS file on disk carries the empty XP line */
        char path[256], got[2048]; FILE *f; size_t n;
        /* write_ics_file joins with a DOS backslash, so on the host it is part of the name. */
        sprintf(path,"%s/EXPORT\\2026-09-17_LPS-Companion.ics", tmpdir);
        f=fopen(path,"rb"); assert(f); n=fread(got,1,sizeof(got)-1,f); fclose(f); got[n]=0;
        assert(strstr(got,"DESCRIPTION:Activities: Walk, Diet, Bible\\NXP:\r\nEND:VEVENT"));
    }

    /* Authentic PicoCalc v1.2 payload, wrapped as SAVE_A.BIN (not a machine-local path). */
    rm_tree(tmpdir); mkdir(tmpdir,0755);
    assert(write_v12_fixture(tmpdir));
    {
        enum LoadResult r=save_load_ab(tmpdir,payload,sizeof(payload),&psz,&gen,&active);
        assert(r==LOAD_OK);
        assert(decode_state(payload,psz,&out));
        assert(out.xp==90 && out.language==LANG_EN);
        assert(out.today.number==3 && out.today.flags==1025);
        assert(out.today.expected==110500 && out.today.actual==111250);
        assert(strcmp(out.today.note,"Keep this original note")==0);
        assert(out.count==2 && out.next==2);
        assert(out.history[1].flags==512 && out.history[1].actual==111500);
        assert(strcmp(out.history[1].note,"Yesterday from v1.2")==0);
        printf("fixture SAVE ok version decoded xp=%lu tasks=%u reminders=%u gen=%lu slot=%c\n",
            (unsigned long)out.xp,(unsigned)out.taskCount,(unsigned)out.reminderCount,(unsigned long)gen, active?'B':'A');
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
        long_date_text(d,LANG_FR,t,sizeof(t)); assert(strcmp(t,"Samedi 14 Fevrier 2026 (semaine 7)")==0);
        d.year=2021; d.month=1; d.day=3; assert(iso_week(d)==53);
        d.year=2020; d.month=12; d.day=31; assert(iso_week(d)==53);
        d.year=2024; d.month=12; d.day=30; assert(iso_week(d)==1);
        d.year=2000; d.month=1; d.day=1; assert(iso_week(d)==52);
        d.year=2099; d.month=12; d.day=31; assert(iso_week(d)==53);
        check_iso_weeks();
    }

    check_rosary_dat("/tmp");
    {   /* The app: Rosary opens with ROSARY.DAT, and says why when it cannot. */
        static unsigned char store2[8192]; App *ra=(App*)store2; Platform *rh;
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

    printf("PASS: storage, ICS (empty XP line), CSV, A/B, app close-day without XP, long date + ISO week 2000-2099, ROSARY.DAT load/missing/damaged, menu 0-9 without notepad, archive menu 1-2, legacy note in ICS, fixture decode (%u captures)\n", 0);
    rm_tree(tmpdir);
    return 0;
}
