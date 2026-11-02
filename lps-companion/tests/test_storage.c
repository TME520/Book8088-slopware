/* Host unit tests: encode/decode, ICS, CSV, A/B SAVE round-trip. */
#include "../src/lps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

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

int main(void){
    const char *tmpdir="/tmp/lps-msdos-test";
    State s, out; Blob b; int32_t g=0; Date calendar; Day day;
    char icsbuf[2048]; char export_dir[200], weights_dir[200];
    uint8_t payload[BLOB_CAPACITY]; size_t psz=0; uint32_t gen=0; int active=0;
    Platform *hw; App *app; static unsigned char store[8192];
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
    assert(strstr(icsbuf,"\\NXP: +40 (total 110)"));
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
    app_key(app,'1'); /* activities */
    app_key(app,'1'); app_key(app,'2'); app_key(app,'3');
    assert(points(&app_state(app)->today)==40);
    app_key(app,KEY_ESCAPE);
    app_key(app,KEY_UP); /* Close the day */
    app_key(app,KEY_ENTER);
    assert(app_screen(app)==SCR_FINISH);
    app_key(app,KEY_ENTER);
    assert(app_state(app)->xp==40);
    assert(app_state(app)->today.number==2);
    app_key(app,KEY_ENTER); /* reward -> home */

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

    printf("PASS: storage, ICS, CSV, A/B, app close-day, fixture decode (%u captures)\n", 0);
    rm_tree(tmpdir);
    return 0;
}
