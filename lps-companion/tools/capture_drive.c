#include "lps.h"
#include <stdio.h>
#include <string.h>

Platform *plat_host_create(int capture);
void plat_host_dump(Platform *p, FILE *out);
const char *plat_host_row(Platform *p, int row);

static void snap(Platform *hw, const char *name){
    char path[128]; FILE *f; int r;
    sprintf(path,"captures/%s.txt", name);
    f=fopen(path,"w"); if(!f) return;
    for(r=0;r<ROWS;r++) fprintf(f,"%s\n", plat_host_row(hw,r));
    fclose(f);
    printf("wrote %s\n", path);
}

int main(void){
    Platform *hw; App *app; static unsigned char store[16384];
    lps_set_data_dir("/tmp/lps-msdos-cap");
    lps_ensure_dirs();
    hw=plat_host_create(0);
    app=(App*)store; app_init(app,hw); app_start(app);
    snap(hw,"01-home-en");
    app_key(app,'1'); snap(hw,"02-activities");
    app_key(app,KEY_ESCAPE); app_key(app,'3');
    app_key(app,'n');
    { const char *s="Focus card"; while(*s) app_key(app,(unsigned char)*s++); }
    app_key(app,KEY_ENTER);
    app_key(app,'f');
    snap(hw,"03-kanban");
    app_key(app,KEY_ESCAPE); app_key(app,'4'); snap(hw,"04-rosary-mode");
    app_key(app,KEY_ESCAPE); app_key(app,'7'); snap(hw,"05-timer");
    app_key(app,KEY_ESCAPE); app_key(app,'5'); snap(hw,"12-daily-review");
    /* Config (TAB, LPS-DOS-115-CHG10): 1 Language, 2 Francais, 4 Save */
    app_key(app,KEY_ESCAPE); app_key(app,KEY_TAB); app_key(app,'1'); app_key(app,'2');
    app_key(app,'4'); /* save FR */ snap(hw,"06-home-fr");
    /* Close the day so the archive menu and archived review can be snapped. */
    app_key(app,'8'); app_key(app,KEY_ENTER); app_key(app,KEY_ENTER);
    app_key(app,KEY_LEFT); snap(hw,"13-archive-menu");
    app_key(app,'3'); snap(hw,"14-archive-review");
    return 0;
}
