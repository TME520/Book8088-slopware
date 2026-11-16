/* app.c - LPS Companion UI state machine (v1.11 feature parity). C89. */
#include "lps.h"
#include "rosary.h"
#include "meals.h"
#include <stdio.h>
#include <string.h>

/* State (~4.6K) must NOT live on the tiny DOS stack, nor in DGROUP: the edit
 * candidate is in the far-heap buffers (lps_buffers_init). */
#define g_scratch (lps_buf->scratch)

struct App {
    Platform *hw;
    State state;
    int screen;
    unsigned selection, page, field, daysBack;
    int blocked;
    int pendingLanguage;
    int pendingPalette;
    char dateDraft[11];
    char taskDraft[TASK_NAME_LENGTH+1];
    char weights[2][8];
    char message[100];
    int missingStorage;
    int kanbanColumn;
    unsigned reviewOffset;
    unsigned editingReminder;
    unsigned reminderField;
    char reminderDate[11];
    char reminderDuration[4];
    char reminderText[REMINDER_TEXT_LENGTH+1];
    unsigned editedTask;
    int rosaryMode;
    int rosarySet;
    unsigned rosaryDecade, rosaryStep, rosaryPage;
    lps_tick_t timerNow, timerLast, timerRemaining;
    int timerPaused;
    int dirty; /* RAM changed; disk SAVE pending */
    /* Catalog names as shown (CP437, display language), rebuilt when the
     * language or the catalog changes (labelLanguage -1 = stale). */
    int labelLanguage;
    char label[ACTIVITY_CAPACITY][ACTIVITY_NAME_LENGTH+1];
    /* LPS-DOS-115-CHG06: the name being typed (CP437, up to 24 characters),
     * the entry it renames (ACTIVITY_CAPACITY = a new one), and the pending
     * Del confirmation on the Activities screen. */
    unsigned editingActivity;
    char activityDraft[ACTIVITY_NAME_LENGTH+1];
    int confirmingDelete;
    /* LPS-DOS-115-CHG07: the card shown in the Meal Planner, and whether the
     * platform draws it in graphics (MEALS_OK) or why not (text card). */
    unsigned mealCursor;
    int mealGraphics;
    /* LPS-DOS-115-CHG09 (PicoCalc LPS-115-CHG02): TAB opens Config and SPACE
     * the Meal Planner from any screen of today that is not a text entry or a
     * confirmation. Each overlay keeps one return point (screen and cursor),
     * no stack; Config may open over the planner, never the other way round.
     * The timer counts on behind them. mealOpen: the planner's pictures are
     * held (from opening the planner until it is left, Config over it
     * included). */
    ReturnPoint configReturn, mealReturn;
    int timerActive;
    int mealOpen;
};

static int in_config(int s){ return s==SCR_CONFIG||s==SCR_LANGUAGE||s==SCR_COLORS; }
static int is_overlay(int s){ return in_config(s)||s==SCR_MEAL_PLANNER; }
static const char *tr(const App *a, const char *en, const char *fr){
    return a->state.language==LANG_FR?fr:en;
}
static void say(App *a, const char *text){ sprintf(a->message, "%s", text); }
static void read_only(App *a){ say(a, tr(a,"Archived day: read-only","Jour archivé : lecture seule")); }
static unsigned timer_seconds(const App *a){ return (unsigned)((a->timerRemaining+999)/1000); }
/* LPS-DOS-115-CHG02b (PicoCalc LPS-112-CHG01/CHG02): 5, 30 or 60 minutes,
 * started directly with keys 1-3. Timing still comes from the BIOS tick. */
static void go(App *a, int s);
static void start_timer(App *a, unsigned option){
    static const unsigned minutes[3]={5,30,60};
    a->timerRemaining=(lps_tick_t)minutes[option]*60000ul;
    a->timerLast=a->timerNow; a->timerPaused=0; a->timerActive=1;
    go(a,SCR_TIMER_RUNNING);
}
static int displayed_palette(const App *a){ return a->screen==SCR_COLORS?a->pendingPalette:a->state.palette; }

static void line(App *a, int row, const char *text, int reverse){
    a->hw->text(a->hw, row, 0, text, reverse);
}
/* Write text in a fixed-width field. Reverse covers the field, not the whole row. */
static void field(App *a, int row, int col, int width, const char *text, int reverse){
    char b[COLUMNS+1]; int n,i;
    if(width<0) width=0;
    if(col<0) col=0;
    if(col>=COLUMNS) return;
    if(col+width>COLUMNS) width=COLUMNS-col;
    for(i=0;i<width;i++) b[i]=' ';
    b[width]=0;
    n=(int)strlen(text);
    if(n>width) n=width;
    for(i=0;i<n;i++) b[i]=text[i];
    a->hw->text(a->hw, row, col, b, reverse);
}
static void center_line(App *a, int row, const char *text, int reverse){
    char b[COLUMNS+1]; int n=(int)strlen(text); int col,i;
    if(reverse){
        for(i=0;i<COLUMNS;i++) b[i]=' ';
        b[COLUMNS]=0;
        if(n>COLUMNS) n=COLUMNS;
        col=(COLUMNS-n)/2;
        memcpy(b+col, text, (size_t)n);
        a->hw->text(a->hw, row, 0, b, 1);
    }else{
        if(n>COLUMNS){ a->hw->text(a->hw, row, 0, text, 0); return; }
        col=(COLUMNS-n)/2;
        a->hw->text(a->hw, row, col, text, 0);
    }
}
#define REVIEW_PAGE 15
#define ROSARY_LINES 16
/* Rosary pages: ROSARY_LINES word-wrapped lines each (LPS-DOS-115-CHG03c). */
static unsigned rosary_pages(const char *text){
    unsigned pages=(wrap_count(text,COLUMNS)+ROSARY_LINES-1)/ROSARY_LINES;
    return pages?pages:1;
}

/* Apply edit in RAM only — disk writes go through flush_save (slow on 8088 HDD). */
static int commit(App *a, const State *candidate){
    if(a->daysBack){ read_only(a); return 0; }
    if(a->blocked){ say(a, tr(a,"Unreadable save: writes blocked","Sauvegarde illisible : écriture bloquée")); return 0; }
    a->state=*candidate;
    a->dirty=1;
    return 1;
}

/* Persist dirty state: one A/B slot, with visible status. */
static int flush_save(App *a){
    if(!a->dirty) return 1;
    if(a->blocked){ say(a, tr(a,"Unreadable save: writes blocked","Sauvegarde illisible : écriture bloquée")); return 0; }
    if(a->missingStorage){ say(a, tr(a,"No storage","Pas de stockage")); return 0; }
    say(a, tr(a,"Saving...","Sauvegarde..."));
    app_render(a);
    if(!a->hw->save(a->hw, &a->state)){
        say(a, tr(a,"Save failed: restart device","Échec sauvegarde : redémarrer"));
        app_render(a);
        return 0;
    }
    a->dirty=0;
    say(a, tr(a,"Saved","Enregistré"));
    return 1;
}

/* ---- Activities (LPS-DOS-115-CHG05, PicoCalc LPS-113-CHG07/08/08b/08c) ----
 * The catalog from the save, up to 30 entries, shown on one page in three
 * columns of up to 10 rows (22 entries: 8, 8, 6), filled column by column.
 * An archived day adds the names it stored that the catalog lacks (frozen
 * extras), ticked and read-only; past 3 x GRID_MAX_ROWS the columns go on
 * to a second page, reached with Left/Right. */
#define GRID_COLUMNS 3
#define GRID_MAX_ROWS 14
#define GRID_TOP 5
#define GRID_CELL 26  /* 26 + 1 + 26 + 1 + 26 = 80 columns */
#define GRID_NAME 20  /* "1 [x] " then the name */
static void refresh_labels(App *a){
    unsigned i;
    if(a->labelLanguage==(int)a->state.language) return;
    for(i=0;i<a->state.activityCount;i++){
        const char *n=activity_display_name(&a->state.activities[i],a->state.language);
        utf8_to_cp437(n,strlen(n),a->label[i]);
    }
    a->labelLanguage=(int)a->state.language;
}
static unsigned viewed_slot(const App *a){
    return a->daysBack ? 1u+(a->state.next+HISTORY_LENGTH-a->daysBack)%HISTORY_LENGTH : 0u;
}
static unsigned grid_items(const App *a){
    return a->state.activityCount + (a->daysBack ? day_orphan_count(&a->state,viewed_slot(a)) : 0u);
}
static unsigned grid_rows(unsigned n){
    unsigned r=(n+GRID_COLUMNS-1)/GRID_COLUMNS;
    if(r>GRID_MAX_ROWS) r=GRID_MAX_ROWS;
    return r?r:1u;
}
/* Entries in column col (columns counted over every page). */
static unsigned grid_column_length(unsigned n, unsigned rows, unsigned col){
    unsigned first=col*rows;
    return first>=n ? 0u : (n-first<rows ? n-first : rows);
}
/* Label of grid entry i (CP437): a catalog name, or a frozen extra. */
static const char *grid_label(App *a, unsigned i, char *tmp){
    const char *n;
    if(i<a->state.activityCount) return a->label[i];
    n=day_orphan(&a->state,viewed_slot(a),i-a->state.activityCount);
    utf8_to_cp437(n,strlen(n),tmp);
    return tmp;
}
/* ---- Catalog edits (LPS-DOS-115-CHG06, PicoCalc LPS-113-CHG07/07b) ----
 * E renames the entry under the cursor in the current language only, N adds
 * one at the end (30 at most), Del deletes it after Enter confirms, F4/F5 move
 * it up or down and wrap. Edits change RAM and set the dirty flag; the save is
 * written on Esc back to Home, as for every other DOS edit. */
static int catalog_editable(App *a){
    if(a->daysBack){ read_only(a); return 0; }
    if(a->blocked){ say(a, tr(a,"Unreadable save: writes blocked","Sauvegarde illisible : écriture bloquée")); return 0; }
    return 1;
}
/* unique: activity_names_unique before the edit. With unique names before
 * and after, only today and the changed entry are matched again (~0.1 s at
 * 8088 speed instead of ~0.7 s with 31 closed days); otherwise everything is
 * matched again, exactly as the PicoCalc's refreshActivityFlags. entry: the
 * new or renamed entry, or ACTIVITY_CAPACITY after a move or a delete. */
static void catalog_changed(App *a, int unique, unsigned entry){
    /* A move or a delete cannot make two names equal: no second check. */
    if(unique && (entry>=ACTIVITY_CAPACITY || activity_names_unique(&a->state))){
        if(entry<ACTIVITY_CAPACITY) activity_rematch_entry(&a->state,entry);
        activity_refresh_today(&a->state);
    }else activity_refresh(&a->state);
    a->labelLanguage=-1;
    a->dirty=1;
}
static void begin_activity_edit(App *a, unsigned index){
    a->editingActivity=index; memset(a->activityDraft,0,sizeof(a->activityDraft));
    if(index<a->state.activityCount){
        const char *n=activity_display_name(&a->state.activities[index],a->state.language);
        size_t len=strlen(n); memcpy(a->activityDraft,n,len<ACTIVITY_NAME_LENGTH?len:ACTIVITY_NAME_LENGTH); /* UTF-8, as stored */
    }
    a->screen=SCR_ACTIVITY_EDIT; a->message[0]=0;
}
static void trim_spaces(char *text){
    char *start=text; size_t n;
    while(*start==' ') start++;
    n=strlen(start); while(n && start[n-1]==' ') n--;
    start[n]=0; if(start!=text) memmove(text,start,n+1);
}
static void save_activity_edit(App *a){
    const char *utf8=a->activityDraft; unsigned destination=a->editingActivity; /* UTF-8 since CHG06d */
    trim_spaces(a->activityDraft);
    if(!a->activityDraft[0]){ a->screen=SCR_ACTIVITIES; memset(a->activityDraft,0,sizeof(a->activityDraft)); return; } /* empty = cancel */
    if(!activity_valid_name(utf8)) say(a,tr(a,"Name is too long","Nom trop long"));
    else if(activity_duplicate(&a->state,utf8,destination,a->state.language)) say(a,tr(a,"That name already exists","Ce nom existe déjà"));
    else if(catalog_editable(a)){
        Activity *e; int unique=activity_names_unique(&a->state);
        activity_pin_history(&a->state);
        if(destination>=ACTIVITY_CAPACITY){
            if(a->state.activityCount>=ACTIVITY_CAPACITY){ say(a,tr(a,"Limit: 30 activities","Limite : 30 activités")); return; }
            destination=a->state.activityCount++;
            memset(&a->state.activities[destination],0,sizeof(Activity));
            a->state.activities[destination].id=a->state.nextActivityId++;
        }
        e=&a->state.activities[destination];
        strcpy(a->state.language==LANG_FR?e->fr:e->en,utf8);
        catalog_changed(a,unique,destination);
        a->editingActivity=destination; a->selection=destination;
        a->screen=SCR_ACTIVITIES; memset(a->activityDraft,0,sizeof(a->activityDraft)); a->message[0]=0;
    }
}
/* Activities keys E, N, Del, F4, F5 (the caller has ruled out a pending Del). */
static void activity_edit_key(App *a, int k){
    unsigned count=a->state.activityCount, id=a->selection;
    if(a->daysBack){ read_only(a); return; }
    if(k=='n'||k=='N'){
        if(count>=ACTIVITY_CAPACITY) say(a,tr(a,"Limit: 30 activities","Limite : 30 activités"));
        else begin_activity_edit(a,ACTIVITY_CAPACITY);
    }else if(id>=count) return;
    else if(k=='e'||k=='E') begin_activity_edit(a,id);
    else if(k==KEY_DELETE) a->confirmingDelete=1;
    else if(count>=2 && catalog_editable(a)){ /* F4 up, F5 down, wrapping like the Kanban */
        unsigned other; int unique=activity_names_unique(&a->state);
        activity_pin_history(&a->state);
        if(k==KEY_F4 && id==0){ activity_rotate(&a->state,1); other=count-1; }
        else if(k==KEY_F5 && id+1==count){ activity_rotate(&a->state,0); other=0; }
        else { other=k==KEY_F4?id-1:id+1; activity_swap(&a->state,id,other); }
        catalog_changed(a,unique,ACTIVITY_CAPACITY);
        a->selection=other;
    }
}
static void confirm_activity_delete(App *a){
    unsigned id=a->selection, n; int unique;
    if(id>=a->state.activityCount || !catalog_editable(a)) return;
    unique=activity_names_unique(&a->state);
    activity_pin_history(&a->state);
    activity_delete(&a->state,id);
    catalog_changed(a,unique,ACTIVITY_CAPACITY);
    a->confirmingDelete=0;
    n=a->state.activityCount;
    if(a->selection>=n) a->selection=n?n-1:0;
}

/* Home count, as on the PicoCalc: an archived day counts the names it stored. */
static unsigned marked_activities(const App *a, const Day *day){
    if(a->daysBack && day_names(viewed_slot(a))->count) return day_names(viewed_slot(a))->count;
    return count_flags(day->flags);
}

static unsigned tasks_in(const App *a, int status){
    unsigned n=0,i; for(i=0;i<a->state.taskCount;i++) if(a->state.tasks[i].status==status) n++; return n;
}
static unsigned task_at(const App *a, int status, unsigned position){
    unsigned i; for(i=0;i<a->state.taskCount;i++) if(a->state.tasks[i].status==status && position--==0) return i;
    return TASK_CAPACITY;
}
static unsigned task_position(const App *a, int status, unsigned index){
    unsigned position=0,i; for(i=0;i<index && i<a->state.taskCount;i++) if(a->state.tasks[i].status==status) position++;
    return position;
}
static const char *task_status_name(const App *a, int status){
    static const char *en[]={"TODO","DOING","DONE"};
    static const char *fr[]={"À FAIRE","EN COURS","FINI"};
    return a->state.language==LANG_FR?fr[status]:en[status];
}
static unsigned review_rows(const App *a){
    return 3+count_flags(a->state.today.flags)+tasks_in(a,TASK_TODO)+tasks_in(a,TASK_DOING)
        +(count_flags(a->state.today.flags)==0)+(tasks_in(a,TASK_TODO)==0)+(tasks_in(a,TASK_DOING)==0);
}
static const char *shown(const char *utf8); /* CHG06d: UTF-8 user text to CP437 */
static void review_line(App *a, unsigned index, char *out, size_t size){
    unsigned activities=count_flags(a->state.today.flags); unsigned i; int status;
    if(index==0){ sprintf(out, tr(a,"ACTIVITIES (%u)","ACTIVITÉS (%u)"), activities); return; }
    --index;
    for(i=0;i<a->state.activityCount;i++) if(FLAG_BIT(a->state.today.flags,i)){
        if(index--==0){ sprintf(out,"  %s", a->label[i]); return; }
    }
    if(!activities){ if(index--==0){ sprintf(out,"  %s", tr(a,"None selected","Aucune sélectionnée")); return; } }
    for(status=TASK_TODO; status<=TASK_DOING; status++){
        unsigned n=tasks_in(a,status);
        if(index--==0){ sprintf(out,"%s (%u)", task_status_name(a,status), n); return; }
        for(i=0;i<a->state.taskCount;i++) if(a->state.tasks[i].status==status){
            if(index--==0){ sprintf(out,"  %s", shown(a->state.tasks[i].name)); return; }
        }
        if(!n && index--==0){ sprintf(out,"  %s", tr(a,"None","Aucune")); return; }
    }
    out[0]=0; (void)size;
}
static int rem_expired(const App *a, const Reminder *r){
    return valid_date(a->state.today.date) && day_index(a->state.today.date)>=day_index(r->from)+r->duration;
}
static int rem_active(const App *a, const Reminder *r){
    return valid_date(a->state.today.date) && day_index(a->state.today.date)>=day_index(r->from) && !rem_expired(a,r);
}
static unsigned active_reminders(const App *a){
    unsigned n=0,i; for(i=0;i<a->state.reminderCount;i++) if(rem_active(a,&a->state.reminders[i])) n++; return n;
}
static void prune_reminders(App *a){
    unsigned kept=0,i;
    g_scratch=a->state;
    for(i=0;i<a->state.reminderCount;i++) if(!rem_expired(a,&a->state.reminders[i])) g_scratch.reminders[kept++]=a->state.reminders[i];
    if(kept!=a->state.reminderCount){
        for(i=kept;i<REMINDER_CAPACITY;i++){ memset(&g_scratch.reminders[i],0,sizeof(Reminder)); }
        g_scratch.reminderCount=(uint8_t)kept; commit(a,&g_scratch);
    }
}
static int is_rosary_screen(int s){ return s>=SCR_ROSARY_MODE && s<=SCR_ROSARY_COMPLETE; }
/* Why the Meal Planner shows text instead of pictures (LPS-DOS-115-CHG07). */
static const char *meals_problem(const App *a, int r){
    if(r==MEALS_MISSING) return tr(a,"MEALS.DAT not found: copy it next to LPSCOMP.EXE","MEALS.DAT introuvable : copiez-le à côté de LPSCOMP.EXE");
    if(r==MEALS_CORRUPT) return tr(a,"MEALS.DAT is damaged: copy it again","MEALS.DAT endommagé : recopiez-le");
    if(r==MEALS_NO_MEMORY) return tr(a,"Not enough memory for the pictures","Mémoire insuffisante pour les images");
    return tr(a,"No EGA screen: Meal Planner in text mode","Pas d'écran EGA : planificateur en mode texte");
}
/* LPS-DOS-115-CHG08 (PicoCalc LPS-114-CHG02 / LPS-115-CHG02 exportMeals):
 * leaving the planner rewrites the day's EXPORT\MPYYMMDD.CSV; a failure is
 * shown on the screen it returns to, and the deck itself stays saved. */
static void export_meals(App *a){
    if(a->missingStorage) return; /* the missing-storage warning already explains it */
    if(!valid_date(a->state.today.date)){ say(a,tr(a,"Set Date to export meals","Renseignez Date pour exporter les repas")); return; }
    if(a->hw->export_meal_csv && !a->hw->export_meal_csv(a->hw,&a->state)) say(a,tr(a,"Meal CSV export failed","Export CSV repas échoué"));
}
static void meal_release(App *a){
    if(a->mealOpen && a->hw->meal_close) a->hw->meal_close(a->hw);
    a->mealOpen=0;
}
static void reset_pending(App *a){ a->pendingLanguage=a->state.language; a->pendingPalette=a->state.palette; }
/* LPS-DOS-115-CHG09 (PicoCalc returnTo): back to a screen without go(), so
 * its drafts, Kanban column, Activities cursor, review scroll and Rosary
 * position are untouched. A language saved meanwhile can shorten the Rosary
 * text or the review, so those positions are clamped. The rosary texts and
 * the planner pictures were kept loaded under the overlay (see go). */
static void return_to(App *a, ReturnPoint r){
    if(r.screen!=SCR_MEAL_PLANNER && !in_config(r.screen)) meal_release(a);
    a->screen=r.screen; a->selection=r.selection; a->message[0]=0; a->confirmingDelete=0;
    if(a->screen==SCR_ROSARY_PRAYER && rosary_loaded()){
        unsigned pages=rosary_pages(rosary_text(a->state.language,a->rosaryMode,a->rosarySet,a->rosaryDecade,a->rosaryStep));
        if(pages && a->rosaryPage>=pages) a->rosaryPage=pages-1;
    }
    if(a->screen==SCR_DAILY_REVIEW){
        unsigned rows=review_rows(a), maximum=rows>REVIEW_PAGE?rows-REVIEW_PAGE:0;
        if(a->reviewOffset>maximum) a->reviewOffset=maximum;
    }
}
static ReturnPoint here(const App *a){ ReturnPoint r; r.screen=a->screen; r.selection=a->selection; return r; }
static void open_config(App *a){ a->configReturn=here(a); reset_pending(a); go(a,SCR_CONFIG); }
/* Leaves Config and its sub-screens; pending, unsaved changes are dropped. */
static void close_config(App *a){ reset_pending(a); return_to(a,a->configReturn); }
static void open_meal_planner(App *a){ a->mealReturn=here(a); go(a,SCR_MEAL_PLANNER); }
/* Space or Esc: save the deck, back to where the planner was opened, then
 * the CSV (a failure is shown there). */
static void leave_meal_planner(App *a){ flush_save(a); return_to(a,a->mealReturn); export_meals(a); }
/* Screens where TAB/SPACE keep their own meaning: text entry (SPACE is a
 * character) and confirmations. Weight is numeric entry: TAB opens Config
 * there (PicoCalc LPS-115-CHG02b) and SPACE stays ignored. */
static int overlay_keys_allowed(const App *a, int k){
    switch(a->screen){
    case SCR_WEIGHT: return k==KEY_TAB;
    case SCR_DATE: case SCR_ACTIVITY_EDIT: case SCR_TASK_EDIT: case SCR_REMINDER_EDIT:
    case SCR_FINISH: case SCR_REMINDER_POPUP: return 0;
    case SCR_ACTIVITIES: return !a->confirmingDelete;
    default: return 1;
    }
}
static void go(App *a, int s){
    const Day *vd;
    /* LPS-DOS-115-CHG07: the pictures stay in memory while the planner is
     * open; CHG09: Config over the planner keeps them too. */
    if(s!=SCR_MEAL_PLANNER && !in_config(s)) meal_release(a);
    /* LPS-DOS-115-CHG03b: the prayer texts live in ROSARY.DAT. Load them when
     * the Rosary opens; on failure stay put and say why. Free them on leaving. */
    if(is_rosary_screen(s)){
        int r=rosary_load();
        if(r!=ROSARY_OK){
            if(r==ROSARY_MISSING) say(a,tr(a,"ROSARY.DAT not found: copy it next to LPSCOMP.EXE","ROSARY.DAT introuvable : copiez-le à côté de LPSCOMP.EXE"));
            else if(r==ROSARY_NO_MEMORY) say(a,tr(a,"Not enough memory for the rosary","Mémoire insuffisante pour le rosaire"));
            else say(a,tr(a,"ROSARY.DAT is damaged: copy it again","ROSARY.DAT endommagé : recopiez-le"));
            return;
        }
    }else if(!is_overlay(s)) rosary_unload(); /* CHG09: kept under Config and the planner */
    a->screen=s; a->selection=0; a->message[0]=0; a->confirmingDelete=0;
    if(s==SCR_DAILY_REVIEW) a->reviewOffset=0;
    if(s==SCR_MEAL_PLANNER){ /* PicoCalc LPS-114-CHG01: open on the first card still in the deck */
        a->mealCursor=meal_first_unplayed(&a->state);
        a->mealGraphics=a->hw->meal_open?a->hw->meal_open(a->hw):MEALS_NO_EGA;
        a->mealOpen=1;
    }
    if(s==SCR_LANGUAGE) a->selection=(unsigned)a->pendingLanguage;
    if(s==SCR_COLORS) a->selection=(unsigned)a->pendingPalette;
    if(s==SCR_WEIGHT){
        int i; a->field=0; vd=app_viewed_day(a);
        for(i=0;i<2;i++){ int32_t v=i?vd->actual:vd->expected; memset(a->weights[i],0,8); if(v>=0) weight_text(v,a->weights[i],8); }
    }
    if(s==SCR_DATE){
        Date d; memset(a->dateDraft,0,sizeof(a->dateDraft));
        d=valid_date(a->state.today.date)?a->state.today.date:DEFAULT_DATE;
        date_text(d,a->dateDraft,sizeof(a->dateDraft));
    }
}
static void edit_buf(char *value, size_t cap, int key){
    size_t len=strlen(value);
    if(key==KEY_BACKSPACE||key==127){ if(len) value[len-1]=0; }
    else if(key>=32 && key<=126 && len+1<cap){ value[len]=(char)key; value[len+1]=0; }
}
/* LPS-DOS-115-CHG06d: user text (task names, reminders, notes, activity
 * names) stays in memory as the exact UTF-8 bytes of the files, so the
 * PicoCalc, DOS and console versions read and write the same bytes. The
 * screen gets a CP437 copy; a character CP437 lacks shows as '?' but is never
 * changed. Two buffers: a line may show two texts. */
#define SHOWN_BYTES 33 /* user texts on screen are at most 32 bytes */
static char g_shown[2][SHOWN_BYTES];
static const char *shown(const char *utf8){
    static unsigned which; char *o; size_t n; const unsigned char *p=(const unsigned char*)utf8;
    while(*p && *p<0x80) p++;
    if(!*p) return utf8; /* plain ASCII: shown as it is, no copy */
    o=g_shown[which^=1u]; n=(size_t)((const char*)p-utf8)+strlen((const char*)p);
    if(n>=SHOWN_BYTES) n=SHOWN_BYTES-1;
    utf8_to_cp437(utf8,n,o); return o;
}
/* A typed key into a UTF-8 field: printable ASCII and the CP437 letters,
 * converted at once, while the field's UTF-8 bytes stay within cap (the
 * PicoCalc limit); a letter that would not fit is not typed. ₧ and ƒ are
 * refused: the PicoCalc screen cannot show them. Backspace removes one whole
 * UTF-8 character. */
static void edit_text(char *value, unsigned cap, int key){
    size_t len=strlen(value), n; char u[3];
    if(key==KEY_BACKSPACE||key==127){
        if(len){ len--; while(len && ((unsigned char)value[len]&0xC0)==0x80) len--; value[len]=0; }
    }else if(key>=0 && key<=255 && cp437_text_byte((unsigned char)key) && key!=0x9E && key!=0x9F){
        n=cp437_to_utf8((unsigned char)key,u);
        if(len+n<=cap){ memcpy(value+len,u,n); value[len+n]=0; }
    }
}
static void begin_reminder_edit(App *a, unsigned index){
    Reminder r; memset(&r,0,sizeof(r));
    a->editingReminder=index; a->reminderField=0;
    memset(a->reminderDate,0,sizeof(a->reminderDate));
    memset(a->reminderDuration,0,sizeof(a->reminderDuration));
    memset(a->reminderText,0,sizeof(a->reminderText));
    if(index<a->state.reminderCount) r=a->state.reminders[index];
    else { r.from=a->state.today.date; r.duration=1; }
    if(valid_date(r.from)) date_text(r.from,a->reminderDate,sizeof(a->reminderDate));
    /* Field holds at most 3 digits ("365"); valid reminders are 1..365. */
    {
        char tmp[8];
        unsigned d=(unsigned)r.duration;
        if(d>999u) d=999u;
        memset(tmp,0,sizeof(tmp));
        sprintf(tmp,"%u",d);
        memcpy(a->reminderDuration,tmp,sizeof(a->reminderDuration));
    }
    memcpy(a->reminderText,r.text,REMINDER_TEXT_LENGTH+1);
    go(a,SCR_REMINDER_EDIT);
}
static void begin_task_edit(App *a, unsigned index){
    a->editedTask=index; memset(a->taskDraft,0,sizeof(a->taskDraft));
    if(index<TASK_CAPACITY) memcpy(a->taskDraft,a->state.tasks[index].name,TASK_NAME_LENGTH+1);
    go(a,SCR_TASK_EDIT);
}
static void move_task_to(App *a, int target){
    unsigned n=tasks_in(a,a->kanbanColumn), index=n?task_at(a,a->kanbanColumn,a->selection):TASK_CAPACITY;
    Task moved; unsigned i;
    if(index>=a->state.taskCount) return;
    if(a->state.tasks[index].status==target){ say(a,tr(a,"Task is already in this column","Tâche déjà dans cette colonne")); return; }
    g_scratch=a->state; moved=g_scratch.tasks[index]; moved.status=(uint8_t)target;
    for(i=index;i+1<g_scratch.taskCount;i++) g_scratch.tasks[i]=g_scratch.tasks[i+1];
    g_scratch.tasks[g_scratch.taskCount-1]=moved;
    if(commit(a,&g_scratch)){ a->kanbanColumn=target; a->selection=tasks_in(a,target)-1; say(a,tr(a,"Task moved","Tâche déplacée")); }
}
static void reorder_task(App *a, int downward){
    unsigned n=tasks_in(a,a->kanbanColumn); unsigned indices[TASK_CAPACITY]; unsigned i; unsigned destination; Task moved;
    if(n<2) return;
    for(i=0;i<n;i++) indices[i]=task_at(a,a->kanbanColumn,i);
    g_scratch=a->state; destination=a->selection;
    if(!downward && a->selection==0){
        moved=g_scratch.tasks[indices[0]]; for(i=0;i+1<n;i++) g_scratch.tasks[indices[i]]=g_scratch.tasks[indices[i+1]];
        g_scratch.tasks[indices[n-1]]=moved; destination=n-1;
    } else if(downward && a->selection==n-1){
        moved=g_scratch.tasks[indices[n-1]]; for(i=n-1;i>0;i--) g_scratch.tasks[indices[i]]=g_scratch.tasks[indices[i-1]];
        g_scratch.tasks[indices[0]]=moved; destination=0;
    } else {
        destination=downward?a->selection+1:a->selection-1;
        moved=g_scratch.tasks[indices[a->selection]];
        g_scratch.tasks[indices[a->selection]]=g_scratch.tasks[indices[destination]];
        g_scratch.tasks[indices[destination]]=moved;
    }
    if(commit(a,&g_scratch)) a->selection=destination;
}
static int suggested_mysteries(const App *a){
    if(!valid_date(a->state.today.date)) return MYS_JOYFUL;
    switch(weekday(a->state.today.date)){
    case 1: case 6: return MYS_JOYFUL;
    case 2: case 5: return MYS_SORROWFUL;
    case 4: return MYS_LUMINOUS;
    default: return MYS_GLORIOUS;
    }
}

size_t app_bytes(void){ return sizeof(struct App); }
void app_init(App *app, Platform *hw){
    lps_buffers_init(); /* idempotent; main checks it before app_init */
    memset(app,0,sizeof(*app));
    app->hw=hw;
    state_init(&app->state);
    app->screen=SCR_HOME;
    app->pendingLanguage=LANG_EN;
    app->pendingPalette=PAL_GREEN;
    app->editingReminder=REMINDER_CAPACITY;
    app->configReturn.screen=app->mealReturn.screen=SCR_HOME;
    app->editedTask=TASK_CAPACITY;
    app->kanbanColumn=TASK_TODO;
    app->dirty=0;
    app->labelLanguage=-1;
    app->editingActivity=ACTIVITY_CAPACITY;
}
const State *app_state(const App *app){ return &app->state; }
enum Screen app_screen(const App *app){ return (enum Screen)app->screen; }
unsigned app_selection(const App *app){ return app->selection; }
const Day *app_viewed_day(const App *app){
    if(app->daysBack)
        return &app->state.history[(app->state.next+HISTORY_LENGTH-app->daysBack)%HISTORY_LENGTH];
    return &app->state.today;
}

void app_start(App *app){
    enum LoadResult r;
    r=app->hw->load(app->hw,&app->state);
    app->labelLanguage=-1; /* the catalog came with the save */
    if(r==LOAD_NO_STORAGE){ app->blocked=1; app->missingStorage=1; say(app,"[!] Missing SD card!"); }
    else if(r==LOAD_ERROR){
        app->blocked=1; say(app, tr(app,"Read error: save protected","Erreur lecture : sauvegarde protégée"));
    }
    else if(r==LOAD_MIGRATED) app->dirty=1; /* LPS-DOS-115-CHG04: write SAVE_A.JSN now */
    if(!app->blocked){ prune_reminders(app); if(app->dirty) flush_save(app); if(active_reminders(app)) app->screen=SCR_REMINDER_POPUP; }
    app_render(app);
}

void app_tick(App *app, lps_tick_t ms){
    unsigned before;
    app->timerNow=ms;
    if(!app->timerActive) return; /* CHG09: counts down behind Config and the planner too */
    before=timer_seconds(app);
    if(!app->timerPaused){
        lps_tick_t elapsed=app->timerNow-app->timerLast;
        app->timerRemaining = elapsed>=app->timerRemaining ? 0 : app->timerRemaining-elapsed;
    }
    app->timerLast=app->timerNow;
    if(!app->timerRemaining){
        /* The alert takes over: Config closes without saving, and a planner
         * left this way saves its deck and writes its CSV (PicoCalc tick). */
        int meals=app->screen==SCR_MEAL_PLANNER||(in_config(app->screen)&&app->configReturn.screen==SCR_MEAL_PLANNER);
        app->timerActive=0;
        if(in_config(app->screen)) reset_pending(app);
        if(meals) flush_save(app);
        go(app,SCR_TIMER_COMPLETE); if(meals) export_meals(app); app_render(app);
    }else if(app->screen==SCR_TIMER_RUNNING && before!=timer_seconds(app)) app_render(app);
}

/* --- key handler continues in app_key.c style: keep in this file --- */

/* LPS-DOS-115-CHG06b: keys from the BIOS. The top row (scan 02h-0Bh) is
 * tagged with the digit on the key, so it works without Shift on AZERTY and
 * any other layout; the numeric keypad sends plain digit characters. */
int app_bios_key(unsigned ascii, unsigned scan){
    ascii&=0xFF; scan&=0xFF;
    if(ascii==0 || ascii==0xE0){
        switch(scan){
        case 72: return KEY_UP;
        case 80: return KEY_DOWN;
        case 75: return KEY_LEFT;
        case 77: return KEY_RIGHT;
        case 83: return KEY_DELETE;
        case 14: return KEY_BACKSPACE; /* some BIOS */
        case 59: return KEY_F1;
        case 60: return KEY_F2;
        case 61: return KEY_F3;
        case 62: return KEY_F4;
        case 63: return KEY_F5;
        default: return -1;
        }
    }
    if(ascii==13) return KEY_ENTER;
    if(ascii==27) return KEY_ESCAPE;
    if(ascii==8 || ascii==127) return KEY_BACKSPACE;
    if(ascii==9) return KEY_TAB;
    if(!cp437_text_byte((unsigned char)ascii)) return -1;
    if(scan>=0x02 && scan<=0x0B) return KEY_TOPROW(ascii, scan==0x0B ? '0' : (int)('1'+scan-0x02));
    return (int)ascii;
}
/* Text fields keep the character; every other screen takes the top row's
 * digit. A '-' stays a hyphen where one is typed (dates, Kanban reorder):
 * it is AZERTY's unshifted 6, as before. */
static int text_entry(const App *a){
    return a->screen==SCR_ACTIVITY_EDIT || a->screen==SCR_TASK_EDIT
        || (a->screen==SCR_REMINDER_EDIT && a->reminderField==2);
}
static int resolve_key(const App *a, int k){
    int ch;
    if(k<0 || !KEY_IS_TOPROW(k)) return k;
    ch=KEY_CHAR(k);
    if(text_entry(a)) return ch;
    if(ch=='-' && (a->screen==SCR_DATE || a->screen==SCR_KANBAN || (a->screen==SCR_REMINDER_EDIT && a->reminderField==0))) return '-';
    return KEY_TOPROW_DIGIT(k);
}
void app_key(App *a, int k){
    k=resolve_key(a,k);
    if(k==KEY_ESCAPE && a->screen==SCR_ACTIVITIES && a->confirmingDelete){
        a->confirmingDelete=0; a->message[0]=0; app_render(a); return;
    }
    if(k==KEY_ESCAPE && a->screen==SCR_ACTIVITY_EDIT){
        a->screen=SCR_ACTIVITIES; memset(a->activityDraft,0,sizeof(a->activityDraft)); a->message[0]=0; app_render(a); return;
    }
    if(k==KEY_ESCAPE && a->screen==SCR_MEAL_PLANNER){ leave_meal_planner(a); app_render(a); return; }
    if(k==KEY_ESCAPE && a->screen==SCR_CONFIG){ close_config(a); app_render(a); return; } /* one level up */
    if(k==KEY_ESCAPE && a->screen==SCR_TIMER_RUNNING) a->timerActive=0; /* Esc still cancels the countdown */
    if(k==KEY_ESCAPE){
        int target=SCR_HOME;
        int from=a->screen;
        if(a->screen==SCR_LANGUAGE||a->screen==SCR_COLORS) target=SCR_CONFIG;
        else if(a->screen==SCR_REMINDER_EDIT) target=SCR_REMINDERS;
        else if(a->screen==SCR_TASK_EDIT) target=SCR_KANBAN;
        else if(a->screen==SCR_ROSARY_SET) target=SCR_ROSARY_MODE;
        else if(a->screen==SCR_ROSARY_DECADE) target=SCR_ROSARY_SET;
        /* Persist when leaving an editor back to Home (date/activities/weight/...). */
        if(target==SCR_HOME && a->dirty
           && (from==SCR_DATE||from==SCR_ACTIVITIES||from==SCR_WEIGHT
               ||from==SCR_KANBAN||from==SCR_REMINDERS||from==SCR_DAILY_REVIEW
               ||from==SCR_ROSARY_MODE||from==SCR_ROSARY_COMPLETE||from==SCR_FINISH
               ||from==SCR_REWARD||from==SCR_TIMER_SELECT)){
            flush_save(a);
        }
        go(a,target); app_render(a); return;
    }
    if(k==KEY_TAB||k==' '){
        /* TAB closes Config entirely, from any of its screens; SPACE does
         * nothing there. SPACE leaves the planner; TAB opens Config over it. */
        if(in_config(a->screen)){ if(k==KEY_TAB) close_config(a); app_render(a); return; }
        if(a->screen==SCR_MEAL_PLANNER && k==' '){ leave_meal_planner(a); app_render(a); return; }
        if(overlay_keys_allowed(a,k)){ a->message[0]=0;
            if(a->daysBack) read_only(a); else if(k==KEY_TAB) open_config(a); else open_meal_planner(a);
            app_render(a); return;
        }
    }
    a->message[0]=0;
    if(a->screen==SCR_REMINDER_POPUP){ go(a,SCR_HOME);
    }else if(a->screen==SCR_HOME){
        /* LPS-DOS-115-CHG02 (PicoCalc LPS-112-CHG05): no notepad; 0-9 today,
         * 1-2 on an archived day. */
        unsigned menuCount=a->daysBack?2u:10u;
        if(k==KEY_LEFT){
            if(a->daysBack<a->state.count){ a->daysBack++; a->selection=0; a->page=0; }
            else say(a, a->state.count?tr(a,"Oldest saved day","Plus ancien jour conservé"):tr(a,"No archived days","Aucun jour archivé"));
        }
        if(k==KEY_RIGHT){ a->daysBack=0; a->selection=0; a->page=0; }
        if(k==KEY_UP) a->selection=(a->selection+menuCount-1)%menuCount;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%menuCount;
        if(!a->daysBack && k=='0'){ a->selection=0; k=KEY_ENTER; }
        else if(a->daysBack && k>='1' && k<='2'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        else if(!a->daysBack && k>='1' && k<='9'){ a->selection=(unsigned)(k-'0'); k=KEY_ENTER; }
        if((k=='0'||(k>='3'&&k<='9')) && a->daysBack) read_only(a);
        if(k==KEY_ENTER){
            static const int currentScreens[]={SCR_DATE,SCR_ACTIVITIES,SCR_WEIGHT,SCR_KANBAN,SCR_ROSARY_MODE,SCR_DAILY_REVIEW,SCR_CONFIG,SCR_REMINDERS,SCR_TIMER_SELECT,SCR_FINISH};
            static const int archiveScreens[]={SCR_ACTIVITIES,SCR_WEIGHT};
            int target=a->daysBack?archiveScreens[a->selection]:currentScreens[a->selection];
            if(target==SCR_KANBAN) a->kanbanColumn=TASK_TODO;
            if(target==SCR_REMINDERS) prune_reminders(a);
            if(target==SCR_CONFIG) open_config(a); /* menu entry 6, as TAB */
            else go(a,target);
        }
    }else if(a->screen==SCR_TIMER_SELECT){
        if(k==KEY_UP) a->selection=(a->selection+2)%3;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%3;
        if(k>='1'&&k<='3'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER) start_timer(a,a->selection);
    }else if(a->screen==SCR_TIMER_RUNNING){
        if(k==KEY_ENTER){ a->timerPaused=!a->timerPaused; a->timerLast=a->timerNow; }
    }else if(a->screen==SCR_TIMER_COMPLETE){
        if(k>='1'&&k<='3') start_timer(a,(unsigned)(k-'1'));
        else if(k==KEY_ENTER) go(a,SCR_TIMER_SELECT);
    }else if(a->screen==SCR_REMINDERS){
        if(k==KEY_UP && a->state.reminderCount) a->selection=(a->selection+a->state.reminderCount-1)%a->state.reminderCount;
        else if(k==KEY_DOWN && a->state.reminderCount) a->selection=(a->selection+1)%a->state.reminderCount;
        else if(k=='n'||k=='N'){
            if(a->state.reminderCount>=REMINDER_CAPACITY) say(a,tr(a,"Limit: 3 reminders","Limite : 3 rappels"));
            else begin_reminder_edit(a,REMINDER_CAPACITY);
        }else if(k==KEY_ENTER && a->state.reminderCount) begin_reminder_edit(a,a->selection);
        else if(k==KEY_DELETE && a->state.reminderCount){
            unsigned i;
            g_scratch=a->state;
            for(i=a->selection;i+1<g_scratch.reminderCount;i++) g_scratch.reminders[i]=g_scratch.reminders[i+1];
            memset(&g_scratch.reminders[--g_scratch.reminderCount],0,sizeof(Reminder));
            if(commit(a,&g_scratch) && a->selection>=a->state.reminderCount && a->selection) a->selection--;
        }
    }else if(a->screen==SCR_REMINDER_EDIT){
        if(k==KEY_UP && a->reminderField) a->reminderField--;
        else if(k==KEY_DOWN && a->reminderField<2) a->reminderField++;
        else if(k==KEY_ENTER){
            Date from; unsigned duration=0; unsigned i;
            for(i=0;a->reminderDuration[i];i++){ char c=a->reminderDuration[i]; if(c<'0'||c>'9'){ duration=366; break; } duration=duration*10u+(unsigned)(c-'0'); }
            if(!parse_date(a->reminderDate,&from)||duration<1||duration>365||!a->reminderText[0]) say(a,tr(a,"Check date, 1-365 days and text","Vérifiez date, 1-365 jours et texte"));
            else if(day_index(from)+duration<=day_index(a->state.today.date)) say(a,tr(a,"Reminder already expired","Rappel déjà expiré"));
            else {
                unsigned index=a->editingReminder<REMINDER_CAPACITY?a->editingReminder:a->state.reminderCount;
                g_scratch=a->state;
                g_scratch.reminders[index].from=from; g_scratch.reminders[index].duration=(uint16_t)duration;
                memcpy(g_scratch.reminders[index].text,a->reminderText,REMINDER_TEXT_LENGTH+1);
                if(index==g_scratch.reminderCount) g_scratch.reminderCount++;
                if(commit(a,&g_scratch)) go(a,SCR_REMINDERS);
            }
        }else if(k==KEY_BACKSPACE||k==127||(k>=32&&k<=255)){ /* 128-255: CP437 letters in the text (CHG06c) */
            if(a->reminderField==0){ if((k>='0'&&k<='9')||k=='-'||k==KEY_BACKSPACE||k==127) edit_buf(a->reminderDate,sizeof(a->reminderDate),k); }
            else if(a->reminderField==1){ if((k>='0'&&k<='9')||k==KEY_BACKSPACE||k==127) edit_buf(a->reminderDuration,sizeof(a->reminderDuration),k); }
            else edit_text(a->reminderText,REMINDER_TEXT_LENGTH,k);
        }
    }else if(a->screen==SCR_DATE){
        if(k==KEY_ENTER){ Date d;
            g_scratch=a->state;
            if(!parse_date(a->dateDraft,&d)) say(a,tr(a,"Invalid date: YYYY-MM-DD, 2000-2099","Date invalide : AAAA-MM-JJ, 2000-2099"));
            else { g_scratch.today.date=d; if(commit(a,&g_scratch)){ flush_save(a); go(a,SCR_HOME); } }
        }else {
            if((k>='0'&&k<='9')||k=='-'||k==KEY_BACKSPACE||k==127) edit_buf(a->dateDraft,sizeof(a->dateDraft),k);
            /* other keys ignored — never freeze */
        }
    }else if(a->screen==SCR_ACTIVITIES){
        /* Up/Down move in the column and wrap; Left/Right go to the next or
         * previous column (and page) and wrap, keeping the row when it
         * exists. 1-9 and 0 tick rows 1-10 of the cursor's column, whose rows
         * carry those labels (PicoCalc LPS-113-CHG08b/08c: keys act on the
         * current page); Enter or Space ticks the cursor's entry. */
        unsigned n=grid_items(a), rows=grid_rows(n), col, row, len, cols;
        if(a->confirmingDelete){ if(k==KEY_ENTER) confirm_activity_delete(a); } /* Esc is handled above; other keys wait */
        else if(k=='e'||k=='E'||k=='n'||k=='N'||k==KEY_DELETE||k==KEY_F4||k==KEY_F5) activity_edit_key(a,k);
        else if(n==0){ if(k==KEY_ENTER||(k>='0'&&k<='9')) say(a,tr(a,"No activities in the list","Aucune activité dans la liste")); }
        else {
            if(a->selection>=n) a->selection=0;
            col=a->selection/rows; row=a->selection%rows;
            len=grid_column_length(n,rows,col); cols=(n+rows-1)/rows;
            if(k==KEY_UP) a->selection=col*rows+(row+len-1)%len;
            else if(k==KEY_DOWN) a->selection=col*rows+(row+1)%len;
            else if(k==KEY_LEFT||k==KEY_RIGHT){
                col=(col+(k==KEY_RIGHT?1u:cols-1u))%cols;
                len=grid_column_length(n,rows,col);
                a->selection=col*rows+(row<len?row:len-1);
            }
            else if(a->daysBack && (k==KEY_ENTER||(k>='0'&&k<='9'))) read_only(a);
            else if(k==KEY_ENTER||(k>='0'&&k<='9')){ /* CHG09: SPACE opens the planner */
                unsigned id=a->selection;
                if(k!=KEY_ENTER){
                    unsigned r=k=='0'?9u:(unsigned)(k-'1');
                    id=r<len ? col*rows+r : n; /* past the column's end: ignored */
                }
                if(id<a->state.activityCount){
                    g_scratch=a->state;
                    g_scratch.today.flags^=FLAG_MASK(id);
                    if(commit(a,&g_scratch)) a->selection=id;
                }
            }
        }
    }else if(a->screen==SCR_ACTIVITY_EDIT){
        if(k==KEY_ENTER) save_activity_edit(a);
        else edit_text(a->activityDraft,ACTIVITY_NAME_LENGTH,k); /* 24 UTF-8 bytes, as the PicoCalc */
    }else if(a->screen==SCR_WEIGHT){
        if(a->daysBack){ if(k==KEY_ENTER) go(a,SCR_HOME); else read_only(a); }
        else if(k==KEY_UP||k==KEY_DOWN) a->field=1-a->field; /* CHG09: TAB opens Config */
        else if(k==KEY_ENTER){ g_scratch=a->state;
            if(!parse_weight(a->weights[0],&g_scratch.today.expected)||!parse_weight(a->weights[1],&g_scratch.today.actual))
                say(a,tr(a,"Invalid weight (example: 110.500)","Poids invalide (exemple : 110,500)"));
            else if(commit(a,&g_scratch)){ flush_save(a); go(a,SCR_HOME); }
        }else if((k>='0'&&k<='9')||k=='.'||k==','||k==KEY_BACKSPACE||k==127) edit_buf(a->weights[a->field],8,k);
    }else if(a->screen==SCR_KANBAN){
        unsigned n=tasks_in(a,a->kanbanColumn); if(n && a->selection>=n) a->selection=n-1;
        if(k==KEY_LEFT||k==KEY_RIGHT){
            unsigned column=(unsigned)a->kanbanColumn; a->kanbanColumn=(int)((column+(k==KEY_RIGHT?1u:2u))%3u); a->selection=0;
        }else if(k==KEY_UP && n) a->selection=(a->selection+n-1)%n;
        else if(k==KEY_DOWN && n) a->selection=(a->selection+1)%n;
        else if(k=='n'||k=='N'){
            if(a->state.taskCount>=TASK_CAPACITY) say(a,tr(a,"Kanban is full (24 tasks)","Kanban complet (24 tâches)"));
            else begin_task_edit(a,TASK_CAPACITY);
        }else if(k==KEY_ENTER && n) begin_task_edit(a,task_at(a,a->kanbanColumn,a->selection));
        else if(k==KEY_DELETE && n){
            unsigned index=task_at(a,a->kanbanColumn,a->selection); unsigned i;
            g_scratch=a->state;
            for(i=index;i+1<g_scratch.taskCount;i++) g_scratch.tasks[i]=g_scratch.tasks[i+1];
            memset(&g_scratch.tasks[--g_scratch.taskCount],0,sizeof(Task));
            if(commit(a,&g_scratch) && a->selection>=tasks_in(a,a->kanbanColumn) && a->selection) a->selection--;
        }else if(k>=KEY_F1 && k<=KEY_F3 && n) move_task_to(a, k-KEY_F1);
        else if((k=='1'||k=='2'||k=='3') && n) move_task_to(a, k-'1');
        else if(k==KEY_F4 || k=='-'){ if(n) reorder_task(a,0); }
        else if(k==KEY_F5 || k=='+'){ if(n) reorder_task(a,1); }
    }else if(a->screen==SCR_TASK_EDIT){
        if(k==KEY_ENTER){
            if(!a->taskDraft[0]) say(a,tr(a,"Task name cannot be empty","Le nom ne peut pas être vide"));
            else {
                unsigned destination=a->editedTask;
                g_scratch=a->state;
                if(a->editedTask<TASK_CAPACITY) memcpy(g_scratch.tasks[a->editedTask].name,a->taskDraft,TASK_NAME_LENGTH+1);
                else if(g_scratch.taskCount>=TASK_CAPACITY){ say(a,tr(a,"Kanban is full (24 tasks)","Kanban complet (24 tâches)")); app_render(a); return; }
                else { destination=g_scratch.taskCount; g_scratch.tasks[g_scratch.taskCount].status=(uint8_t)a->kanbanColumn; memcpy(g_scratch.tasks[g_scratch.taskCount].name,a->taskDraft,TASK_NAME_LENGTH+1); g_scratch.taskCount++; }
                if(commit(a,&g_scratch)){ a->editedTask=destination; a->selection=task_position(a,a->kanbanColumn,a->editedTask); a->screen=SCR_KANBAN; a->message[0]=0; }
            }
        }else edit_text(a->taskDraft,TASK_NAME_LENGTH,k);
    }else if(a->screen==SCR_DAILY_REVIEW){
        unsigned maximum=review_rows(a)>REVIEW_PAGE?review_rows(a)-REVIEW_PAGE:0;
        if(k==KEY_DOWN && a->reviewOffset<maximum) a->reviewOffset++;
        else if(k==KEY_UP && a->reviewOffset) a->reviewOffset--;
        else if(k==KEY_RIGHT) a->reviewOffset=(a->reviewOffset+REVIEW_PAGE<maximum)?a->reviewOffset+REVIEW_PAGE:maximum;
        else if(k==KEY_LEFT) a->reviewOffset=a->reviewOffset>REVIEW_PAGE?a->reviewOffset-REVIEW_PAGE:0;
    }else if(a->screen==SCR_CONFIG){
        if(k==KEY_UP) a->selection=(a->selection+2)%3;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%3;
        if(k>='1'&&k<='3'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){
            if(a->selection==0) go(a,SCR_LANGUAGE);
            else if(a->selection==1) go(a,SCR_COLORS);
            else { g_scratch=a->state; g_scratch.language=(uint8_t)a->pendingLanguage;
                if(commit(a,&g_scratch)){ flush_save(a); close_config(a); say(a,tr(a,"Configuration saved","Configuration enregistrée")); }}
        }
    }else if(a->screen==SCR_LANGUAGE){
        if(k==KEY_UP||k==KEY_DOWN) a->selection=1-a->selection;
        if(k=='1'||k=='2'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){ a->pendingLanguage=(int)a->selection; go(a,SCR_CONFIG); }
    }else if(a->screen==SCR_COLORS){
        if(k==KEY_UP) a->selection=(a->selection+4)%5;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%5;
        if(k>='1'&&k<='5') a->selection=(unsigned)(k-'1');
        a->pendingPalette=(int)a->selection;
        if(k==KEY_ENTER){ g_scratch=a->state; g_scratch.palette=(uint8_t)a->pendingPalette;
            if(commit(a,&g_scratch)){ flush_save(a); close_config(a); say(a,tr(a,"Color saved","Couleur enregistrée")); }}
    }else if(a->screen==SCR_ROSARY_MODE){
        if(k==KEY_UP) a->selection=(a->selection+3)%4;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%4;
        if(k>='1'&&k<='4'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){ a->rosaryMode=(int)a->selection; go(a,SCR_ROSARY_SET); }
    }else if(a->screen==SCR_ROSARY_SET){
        if(k==KEY_UP) a->selection=(a->selection+4)%5;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%5;
        if(k>='1'&&k<='5'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){ a->rosarySet=a->selection? (int)(a->selection-1): suggested_mysteries(a);
            if(a->rosaryMode==ROS_DECADE) go(a,SCR_ROSARY_DECADE);
            else { a->rosaryStep=a->rosaryPage=0; go(a,SCR_ROSARY_PRAYER); }}
    }else if(a->screen==SCR_ROSARY_DECADE){
        if(k==KEY_UP) a->selection=(a->selection+4)%5;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%5;
        if(k>='1'&&k<='5'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){ a->rosaryDecade=a->selection; a->rosaryStep=a->rosaryPage=0; go(a,SCR_ROSARY_PRAYER); }
    }else if(a->screen==SCR_ROSARY_PRAYER){
        const char *text=rosary_text(a->state.language,a->rosaryMode,a->rosarySet,a->rosaryDecade,a->rosaryStep);
        unsigned pages=rosary_pages(text);
        if(k==KEY_LEFT){
            if(a->rosaryPage) a->rosaryPage--;
            else if(a->rosaryStep){ a->rosaryStep--; text=rosary_text(a->state.language,a->rosaryMode,a->rosarySet,a->rosaryDecade,a->rosaryStep); a->rosaryPage=rosary_pages(text)-1; }
        }else if(k==KEY_RIGHT||k==KEY_ENTER){ /* CHG09: SPACE opens the planner */
            if(a->rosaryPage+1<pages) a->rosaryPage++;
            else if(a->rosaryStep+1<rosary_steps(a->rosaryMode)){ a->rosaryStep++; a->rosaryPage=0; }
            else go(a,SCR_ROSARY_COMPLETE);
        }
    }else if(a->screen==SCR_ROSARY_COMPLETE){
        if(k==KEY_ENTER) go(a,SCR_HOME);
    }else if(a->screen==SCR_FINISH){
        if(k==KEY_ENTER){
            uint32_t gain=points(&a->state.today);
            if(a->state.today.number==0xfffffffful || a->state.xp>0xfffffffful-gain) say(a,tr(a,"Counter limit reached","Limite du compteur atteinte"));
            else if(!valid_date(a->state.today.date)) say(a,tr(a,"Set Date before closing the day","Renseignez Date avant de terminer le jour"));
            else {
                Date tomorrow; unsigned slot=a->state.next;
                g_scratch=a->state;
                g_scratch.history[g_scratch.next]=g_scratch.today;
                g_scratch.next=(uint8_t)((g_scratch.next+1)%HISTORY_LENGTH);
                if(g_scratch.count<HISTORY_LENGTH) g_scratch.count++;
                tomorrow=a->state.today.date;
                if(!next_date(&tomorrow)){ say(a,tr(a,"Calendar ends on 2099-12-31","Le calendrier s'arrête au 2099-12-31")); app_render(a); return; }
                if(!a->hw->export_weight_csv(a->hw,&a->state.today)){ say(a,tr(a,"Weight CSV export failed; day remains open","Export CSV poids échoué ; journée reste ouverte")); app_render(a); return; }
                if(!a->hw->export_ics(a->hw,&a->state.today,&a->state,a->state.xp+gain,gain,a->state.language)){ say(a,tr(a,"ICS export failed; day remains open","Export ICS échoué ; journée reste ouverte")); app_render(a); return; }
                g_scratch.xp+=gain; memset(&g_scratch.today,0,sizeof(Day)); g_scratch.today.number=a->state.today.number+1; g_scratch.today.date=tomorrow; g_scratch.today.expected=-1; g_scratch.today.actual=-1;
                /* LPS-DOS-115-CHG01: XP is still banked in the save for compatibility, never shown. */
                /* LPS-DOS-115-CHG05: the closed day keeps its ticked names, frozen
                 * in catalog order (PicoCalc freezeDayNames); today starts empty. */
                /* Closing the day deals a new Meal Planner deck, as on the PicoCalc. */
                g_scratch.mealCount=0; memset(g_scratch.meals,0,sizeof(g_scratch.meals));
                if(commit(a,&g_scratch)){
                    day_names_freeze(&a->state,1u+slot,a->state.history[slot].flags);
                    day_names(0)->count=0;
                    if(flush_save(a)) go(a,SCR_REWARD);
                }
            }
        }
    }else if(a->screen==SCR_REWARD && k==KEY_ENTER) go(a,SCR_HOME);
    else if(a->screen==SCR_MEAL_PLANNER){
        /* LPS-DOS-115-CHG07 (PicoCalc LPS-114-CHG01): arrows wrap; Enter plays
         * the card or puts it back, saved when the planner closes (SPACE or
         * Esc), as the other screens do. */
        if(k==KEY_LEFT||k==KEY_RIGHT||k==KEY_UP||k==KEY_DOWN) a->mealCursor=meal_move(a->mealCursor,k);
        else if(k==KEY_ENTER){ g_scratch=a->state; meal_toggle(&g_scratch,a->mealCursor); commit(a,&g_scratch); }
    }
    app_render(a);
}

static void right_text(App *a, int row, const char *text){
    int n=(int)strlen(text); if(n>COLUMNS-1) n=COLUMNS-1;
    a->hw->text(a->hw,row,COLUMNS-1-n,text,0);
}
/* The card in text mode (no EGA, no MEALS.DAT, host tests): a framed card
 * with the row on its header band, the name and the portion. */
#define CARD_LEFT 20
#define CARD_WIDTH 40
static void card_line(App *a, int row, const char *text, int reverse){
    char b[CARD_WIDTH+1]; int n=(int)strlen(text), i, col;
    if(n>CARD_WIDTH-2) n=CARD_WIDTH-2;
    for(i=0;i<CARD_WIDTH;i++) b[i]=' ';
    b[0]=b[CARD_WIDTH-1]='\xb3'; b[CARD_WIDTH]=0; /* CP437 vertical line */
    col=(CARD_WIDTH-n)/2; memcpy(b+col,text,(size_t)n);
    a->hw->text(a->hw,row,CARD_LEFT,b,0);
    if(reverse){ b[CARD_WIDTH-1]=0; a->hw->text(a->hw,row,CARD_LEFT+1,b+1,1); }
}
static void card_rule(App *a, int row, char left, char right){
    char b[CARD_WIDTH+1]; int i;
    for(i=0;i<CARD_WIDTH;i++) b[i]='\xc4'; /* CP437 horizontal line */
    b[0]=left; b[CARD_WIDTH-1]=right; b[CARD_WIDTH]=0;
    a->hw->text(a->hw,row,CARD_LEFT,b,0);
}
static void text_card(App *a, const MealView *v){
    char b[40]; int r;
    card_rule(a,6,'\xda','\xbf');
    card_line(a,7,v->rowName,1);
    card_rule(a,8,'\xc3','\xb4');
    for(r=9;r<19;r++) card_line(a,r,"",0);
    if(v->row==MEAL_BLANK) card_line(a,13,"~  Blank  ~",0);
    else {
        card_line(a,12,v->name,0);
        sprintf(b,"[ %s ]",v->portion); card_line(a,14,b,0);
    }
    if(v->position){ sprintf(b,tr(a,"PLAYED #%u","JOUÉE #%u"),(unsigned)v->position); card_line(a,17,b,0); }
    card_rule(a,19,'\xc0','\xd9');
}

void app_render(App *a){
    char b[128]; const Day *day=app_viewed_day(a); char date[16]; int statusRow; unsigned i;
    refresh_labels(a);
    a->hw->set_palette(a->hw, displayed_palette(a));
    a->hw->clear(a->hw);
    if(valid_date(day->date)) date_text(day->date,date,sizeof(date));
    else sprintf(date,"%s%lu", tr(a,"D","J"), (unsigned long)day->number);
    /* Title bar across all 80 columns, name centered, date kept on the right. */
    {
        char bar[COLUMNS+1]; int n,col,dn,k;
        sprintf(b,"LPS COMPANION v%s", LPS_VERSION_STR);
        for(k=0;k<COLUMNS;k++) bar[k]=' ';
        bar[COLUMNS]=0;
        n=(int)strlen(b); if(n>COLUMNS) n=COLUMNS;
        col=(COLUMNS-n)/2; memcpy(bar+col,b,(size_t)n);
        dn=(int)strlen(date);
        if(dn>0 && dn<COLUMNS-2) memcpy(bar+COLUMNS-dn, date, (size_t)dn);
        a->hw->text(a->hw, 0, 0, bar, 1);
    }
    /* LPS-DOS-115-CHG01: row 1 of every screen shows the long date and ISO
     * week of the viewed day (blank for an undated day). No XP anywhere. */
    if(valid_date(day->date)){ long_date_text(day->date,a->state.language,b,sizeof(b)); center_line(a,1,b,0); }
    if(a->daysBack) center_line(a,2,tr(a,"ARCHIVED DAY - READ ONLY","JOUR ARCHIVÉ - LECTURE SEULE"),0);

    if(a->screen==SCR_HOME){
        const char *menu[10];
        unsigned first=a->daysBack?1u:0u, total=a->daysBack?2u:10u;
        char wexp[16], wact[16];
        int gutter;
        menu[0]="0  Date";
        menu[1]=tr(a,"1  Activities","1  Activités");
        menu[2]=tr(a,"2  Weight tracker","2  Suivi du poids");
        menu[3]="3  Kanban";
        menu[4]=tr(a,"4  Christian Rosary","4  Rosaire chrétien");
        menu[5]=tr(a,"5  Daily review","5  Revue journalière");
        menu[6]="6  Config";
        menu[7]=tr(a,"7  Reminders","7  Rappels");
        menu[8]=tr(a,"8  Timer","8  Minuteur");
        menu[9]=tr(a,"9  Close the day","9  Terminer le jour");
        field(a,4,1,37, a->daysBack?tr(a,"CLOSED DAY","JOUR TERMINÉ"):tr(a,"TODAY","AUJOURD'HUI"), 0);
        sprintf(b,tr(a,"%u/%u activities","%u/%u activités"), marked_activities(a,day), (unsigned)a->state.activityCount);
        field(a,5,1,37,b,0);
        for(i=0;i<total;i++) field(a,7+(int)i,1,37, menu[first+i], i==a->selection);
        field(a,18,1,37,tr(a,"<- Previous day   -> Current day","<- Jour précédent   -> Jour actuel"),0);
        /* Right pane, tightened after the notepad left (LPS-DOS-115-CHG02):
         * blocks start on the menu's rows 4, 7, 11 and 16. */
        for(gutter=3; gutter<=18; gutter++) a->hw->text(a->hw, gutter, 39, "|", 0);
        field(a,4,41,38, "DATE", 0);
        field(a,5,41,38, date, 0);
        field(a,7,41,38, tr(a,"WEIGHT TRACKER (kg)","SUIVI DU POIDS (kg)"), 0);
        weight_text(day->expected, wexp, sizeof(wexp));
        weight_text(day->actual, wact, sizeof(wact));
        sprintf(b,tr(a,"Expected : %s","Attendu  : %s"), wexp); field(a,8,41,38,b,0);
        sprintf(b,tr(a,"Actual   : %s","Effectif : %s"), wact); field(a,9,41,38,b,0);
        field(a,11,41,38, tr(a,"REMINDERS","RAPPELS"), 0);
        if(!a->state.reminderCount) field(a,12,41,38, tr(a,"No reminders","Aucun rappel"), 0);
        else for(i=0;i<a->state.reminderCount && i<3;i++) field(a,12+(int)i,41,38, shown(a->state.reminders[i].text), 0);
        sprintf(b,"KANBAN  %s %u  %s %u  %s %u",
            task_status_name(a,TASK_TODO), tasks_in(a,TASK_TODO),
            task_status_name(a,TASK_DOING), tasks_in(a,TASK_DOING),
            task_status_name(a,TASK_DONE), tasks_in(a,TASK_DONE));
        field(a,16,41,38, b, 0);
    }else if(a->screen==SCR_TIMER_SELECT){
        const char *options[]={"1  5 minutes","2  30 minutes","3  60 minutes"};
        center_line(a,3,tr(a,"TIMER","MINUTEUR"),0);
        for(i=0;i<3;i++) field(a,8+(int)i*2, 25, 30, options[i], a->selection==i);
    }else if(a->screen==SCR_TIMER_RUNNING||a->screen==SCR_TIMER_COMPLETE){
        unsigned seconds=timer_seconds(a);
        center_line(a,3,tr(a,"TIMER","MINUTEUR"),0);
        sprintf(b,"%02u:%02u",seconds/60,seconds%60);
        a->hw->large_text(a->hw,b,seconds<60);
        if(a->screen==SCR_TIMER_COMPLETE) center_line(a,16,tr(a,"TIMER FINISHED!","MINUTEUR TERMINÉ !"),0);
        else if(a->timerPaused) center_line(a,16,tr(a,"PAUSED","EN PAUSE"),0);
        center_line(a,22, a->screen==SCR_TIMER_COMPLETE?tr(a,"1-3: Restart  Enter: Menu  Esc: Back","1-3: Relancer  Enter: Menu  Esc: Retour"):
            (a->timerPaused?tr(a,"Enter: Resume   Esc: Cancel","Enter: Reprendre   Esc: Annuler"):tr(a,"Enter: Pause   Esc: Cancel","Enter: Pause   Esc: Annuler")),0);
    }else if(a->screen==SCR_REMINDER_POPUP){
        unsigned row=6;
        center_line(a,3,tr(a,"REMINDERS","RAPPELS"),0);
        for(i=0;i<a->state.reminderCount;i++) if(rem_active(a,&a->state.reminders[i])){
            sprintf(b,"%u. %s", row-5, shown(a->state.reminders[i].text)); line(a,(int)row,b,0); row+=2;
        }
        line(a,15,tr(a,"Press any key to continue","Appuyez sur une touche"),0);
    }else if(a->screen==SCR_REMINDERS){
        center_line(a,3,tr(a,"REMINDERS (max 3)","RAPPELS (max 3)"),0);
        if(!a->state.reminderCount) line(a,6,tr(a,"No reminders","Aucun rappel"),0);
        for(i=0;i<a->state.reminderCount;i++){ char from[11]; date_text(a->state.reminders[i].from,from,sizeof(from));
            sprintf(b,"%s  %u %s",from,(unsigned)a->state.reminders[i].duration,tr(a,"days","jours"));
            line(a,6+(int)i*3,b,i==a->selection); line(a,7+(int)i*3,shown(a->state.reminders[i].text),0);
        }
        line(a,16,tr(a,"N New  Enter Edit  Del Delete","N Nouveau Entrée Modifier Suppr Effacer"),0);
    }else if(a->screen==SCR_REMINDER_EDIT){
        center_line(a,3,tr(a,"EDIT REMINDER","MODIFIER LE RAPPEL"),0);
        sprintf(b,tr(a,"From: %s","Début : %s"),a->reminderDate); line(a,6,b,a->reminderField==0);
        sprintf(b,tr(a,"Days: %s","Jours : %s"),a->reminderDuration); line(a,8,b,a->reminderField==1);
        line(a,10,tr(a,"Text:","Texte :"),0); line(a,11,shown(a->reminderText),a->reminderField==2);
        line(a,15,tr(a,"Up/Down fields  Enter save","Haut/Bas champs  Entrée valider"),0);
    }else if(a->screen==SCR_DATE){
        center_line(a,3,"DATE",0); line(a,6,a->dateDraft,1);
        line(a,9,tr(a,"Calendar: 2000-01-01 to 2099-12-31","Calendrier : 2000-01-01 au 2099-12-31"),0);
        line(a,12,tr(a,"Enter: save / Esc: cancel","Entrée : valider / Échap : annuler"),0);
        line(a,14,tr(a,"Next day is suggested after closing.","Jour suivant proposé après clôture."),0);
    }else if(a->screen==SCR_ACTIVITIES){
        unsigned n=grid_items(a), rows=grid_rows(n), sel=a->selection<n?a->selection:0u;
        unsigned col=sel/rows, first=col/GRID_COLUMNS*GRID_COLUMNS, cols=n?(n+rows-1)/rows:1u, c, r;
        char cell[GRID_CELL+1], tmp[ACTIVITY_NAME_LENGTH+1];
        if(cols>GRID_COLUMNS){
            sprintf(b,tr(a,"ACTIVITIES  %u/%u","ACTIVITÉS  %u/%u"),first/GRID_COLUMNS+1,(cols+GRID_COLUMNS-1)/GRID_COLUMNS);
            center_line(a,3,b,0);
        }else center_line(a,3,tr(a,"ACTIVITIES","ACTIVITÉS"),0);
        if(!n) line(a,GRID_TOP,tr(a,"N: add an activity","N : ajouter une activité"),0);
        /* Cells are built by hand: sprintf is slow on the 8088. */
        for(c=0;c<GRID_COLUMNS;c++){
            unsigned gc=first+c, len=grid_column_length(n,rows,gc);
            for(r=0;r<len;r++){
                unsigned id=gc*rows+r, k; const char *name=grid_label(a,id,tmp);
                int ticked=id<a->state.activityCount ? (int)FLAG_BIT(day->flags,id) : 1;
                for(k=0;k<GRID_CELL;k++) cell[k]=' ';
                cell[GRID_CELL]=0;
                if(gc==col && r<10) cell[0]=(char)(r==9?'0':'1'+r); /* keys label the cursor's column */
                cell[2]='['; cell[3]=(char)(ticked?'x':' '); cell[4]=']';
                for(k=0;name[k]&&k<GRID_NAME;k++) cell[6+k]=name[k];
                a->hw->text(a->hw, GRID_TOP+(int)r, (int)c*(GRID_CELL+1), cell, id==sel);
            }
        }
        /* A name longer than its cell is shown whole under the grid. */
        if(n){
            const char *name=grid_label(a,sel,tmp);
            if(strlen(name)>GRID_NAME) line(a,GRID_TOP+(int)rows+1,name,0);
        }
        if(a->confirmingDelete && sel<a->state.activityCount){
            sprintf(b,tr(a,"Delete %s?","Supprimer %s ?"),a->label[sel]); line(a,21,b,0);
            line(a,22,tr(a,"Enter: yes   Esc: no","Entrée : oui   Échap : non"),0);
        }else if(a->daysBack) line(a,22,tr(a,"<- -> columns   Esc: day menu","<- -> colonnes   Échap : menu du jour"),0);
        else {
            line(a,21,tr(a,"E: rename  N: new  Del: delete  F4/F5: move up/down","E : renommer  N : nouvelle  Suppr : supprimer  F4/F5 : monter/descendre"),0);
            line(a,22,tr(a,"1-9,0: tick in this column  Enter: tick  <- ->: columns","1-9,0 : cocher dans la colonne  Entrée : cocher  <- -> : colonnes"),0);
        }
    }else if(a->screen==SCR_ACTIVITY_EDIT){
        center_line(a,3, a->editingActivity<ACTIVITY_CAPACITY?tr(a,"RENAME ACTIVITY","RENOMMER L'ACTIVITÉ"):tr(a,"NEW ACTIVITY","NOUVELLE ACTIVITÉ"),0);
        line(a,6,shown(a->activityDraft),1);
        sprintf(b,tr(a,"%u/24 characters","%u/24 caractères"),(unsigned)strlen(a->activityDraft)); line(a,9,b,0); /* UTF-8 bytes (CHG06d) */
        line(a,11,tr(a,"Saved in English; the French name, if any, stays.","Enregistré en français ; le nom anglais, s'il existe, reste."),0);
        line(a,13,tr(a,"Enter: save  (empty cancels)","Entrée : enregistrer  (vide = annuler)"),0);
        line(a,15,tr(a,"Backspace: erase  Esc: cancel","Retour : effacer  Échap : annuler"),0);
    }else if(a->screen==SCR_WEIGHT){
        int32_t expg=0,actg=0; int valid;
        center_line(a,3,tr(a,"WEIGHT TRACKER (kg)","SUIVI DU POIDS (kg)"),0);
        sprintf(b,tr(a,"Expected : %s","Attendu  : %s"),a->weights[0]); line(a,6,b,a->field==0);
        sprintf(b,tr(a,"Actual   : %s","Effectif : %s"),a->weights[1]); line(a,9,b,a->field==1);
        valid=parse_weight(a->weights[0],&expg)&&parse_weight(a->weights[1],&actg)&&expg>=0&&actg>=0;
        if(valid){ int32_t d=actg-expg, absv=d<0?-d:d; sprintf(b,tr(a,"Difference: %c%ld.%03ld kg","Écart : %c%ld.%03ld kg"), d<0?'-':'+', (long)(absv/1000),(long)(absv%1000)); }
        else sprintf(b,"%s",tr(a,"Difference: --","Écart : --"));
        line(a,12,b,0);
        line(a,14,tr(a,"Blank = not recorded","Vide = non renseigné"),0);
        line(a,15, a->daysBack?tr(a,"Enter / Esc: day menu","Entrée / Échap : menu du jour"):tr(a,"Up/Down: field - Enter: save","Haut/Bas : champ - Entrée : valider"),0);
    }else if(a->screen==SCR_KANBAN){
        /* Three equal 26-column bands, one blank column between them (26+1+26+1+26). */
        int col, starts[3];
        starts[0]=0; starts[1]=27; starts[2]=54;
        center_line(a,3,"KANBAN",0);
        for(col=0; col<3; col++){
            unsigned n=tasks_in(a,col);
            int active=col==a->kanbanColumn;
            char hdr[27]; int hn, pad, hi;
            if(active && n && a->selection>=n) a->selection=n-1;
            sprintf(b,"%s (%u)", task_status_name(a,col), n);
            hn=(int)strlen(b); if(hn>26) hn=26;
            for(hi=0;hi<26;hi++) hdr[hi]=' ';
            hdr[26]=0;
            pad=(26-hn)/2;
            memcpy(hdr+pad, b, (size_t)hn);
            field(a,5,starts[col],26,hdr,active);
            if(!n){
                if(active && tasks_in(a,TASK_TODO)+tasks_in(a,TASK_DOING)+tasks_in(a,TASK_DONE)==0)
                    line(a,7,tr(a,"No tasks. Press N to create one.","Aucune tâche. Appuyez sur N."),0);
                else if(active) field(a,7,starts[col],26,tr(a,"No tasks. Press N to create one.","Aucune tâche. Appuyez sur N."),0);
            }else{
                unsigned visible=12, first, end, position;
                unsigned sel=active?a->selection:0;
                if(!active) sel=0;
                first=sel>=visible?sel-visible+1:0;
                if(!active){ first=0; }
                end=first+visible<n?first+visible:n;
                for(position=first; position<end; position++){
                    unsigned index=task_at(a,col,position);
                    sprintf(b,"%2u %-22s", position+1, shown(a->state.tasks[index].name));
                    field(a,7+(int)(position-first), starts[col], 26, b, active && position==a->selection);
                }
            }
        }
        line(a,20,tr(a,"N New  Enter Edit  Del Delete","N Nouveau Entrée Modifier Suppr Effacer"),0);
        line(a,21,tr(a,"F1 TODO  F2 DOING  F3 DONE","F1 À FAIRE F2 EN COURS F3 FINI"),0);
        line(a,22,tr(a,"F4 Up F5 Down  <- -> Columns","F4 Monter F5 Descendre <- -> Colonnes"),0);
    }else if(a->screen==SCR_TASK_EDIT){
        center_line(a,3, a->editedTask<TASK_CAPACITY?tr(a,"EDIT TASK","MODIFIER LA TÂCHE"):tr(a,"NEW TASK","NOUVELLE TÂCHE"),0);
        line(a,6,shown(a->taskDraft),1);
        /* As the PicoCalc: the count is the name's bytes in UTF-8 (CHG06c). */
        sprintf(b,tr(a,"%u/32 characters","%u/32 caractères"),(unsigned)strlen(a->taskDraft)); line(a,9,b,0);
        line(a,13,tr(a,"Enter: save task","Entrée : enregistrer la tâche"),0);
        line(a,15,tr(a,"Backspace: erase  Esc: cancel","Retour: effacer  Échap: annuler"),0);
    }else if(a->screen==SCR_DAILY_REVIEW){
        char weight[20]; unsigned total=review_rows(a);
        sprintf(b,"%s  %s", tr(a,"DAILY REVIEW","REVUE JOURNALIÈRE"), date); center_line(a,3,b,0);
        weight_text(a->state.today.actual,weight,sizeof(weight));
        sprintf(b,tr(a,"Actual weight: %s kg","Poids effectif : %s kg"), weight); line(a,4,b,0);
        for(i=0;i<REVIEW_PAGE && a->reviewOffset+i<total;i++){ review_line(a,a->reviewOffset+i,b,sizeof(b)); line(a,6+(int)i,b,0); }
        sprintf(b,tr(a,"Up/Down scroll  %u-%u/%u  Esc back","Haut/Bas défiler  %u-%u/%u  Échap retour"),
            a->reviewOffset+1, (a->reviewOffset+REVIEW_PAGE<total?a->reviewOffset+REVIEW_PAGE:total), total); line(a,22,b,0);
    }else if(a->screen==SCR_CONFIG){
        center_line(a,3,tr(a,"CONFIGURATION","CONFIGURATION"),0);
        sprintf(b,"%s: %s", tr(a,"1  Language","1  Langue"), a->pendingLanguage==LANG_EN?"English":"Français"); line(a,6,b,a->selection==0);
        sprintf(b,"%s: PAL%u", tr(a,"2  Colors","2  Couleurs"), (unsigned)a->state.palette+1); line(a,7,b,a->selection==1);
        line(a,8,tr(a,"3  Save","3  Enregistrer"),a->selection==2);
        line(a,11,tr(a,"Language: select, then Save.","Langue : choisir, puis enregistrer."),0);
        line(a,12,tr(a,"Esc: discard changes","Échap : annuler les modifications"),0);
    }else if(a->screen==SCR_LANGUAGE){
        center_line(a,3,tr(a,"LANGUAGE","LANGUE"),0);
        line(a,6,"1  English",a->selection==0); line(a,7,"2  Français",a->selection==1);
        line(a,10,tr(a,"Enter: select / Esc: back","Entrée : choisir / Échap : retour"),0);
        line(a,11,tr(a,"Then Save in Configuration.","Puis enregistrer dans Configuration."),0);
    }else if(a->screen==SCR_COLORS){
        center_line(a,3,tr(a,"COLORS","COULEURS"),0);
        line(a,6,tr(a,"1  PAL1  red","1  PAL1  rouge"),a->selection==0);
        line(a,7,tr(a,"2  PAL2  green","2  PAL2  vert"),a->selection==1);
        line(a,8,tr(a,"3  PAL3  blue","3  PAL3  bleu"),a->selection==2);
        line(a,9,"4  PAL4  Orange",a->selection==3);
        line(a,10,tr(a,"5  PAL5  Pink","5  PAL5  Rose"),a->selection==4);
        line(a,13,tr(a,"Enter: save color","Entrée : enregistrer couleur"),0);
    }else if(a->screen==SCR_ROSARY_MODE){
        center_line(a,3,tr(a,"CHOOSE ROSARY MODE","CHOISIR LE MODE DU ROSAIRE"),0);
        line(a,6,tr(a,"1  One decade             3-5 min","1  Une dizaine            3-5 min"),a->selection==0);
        line(a,8,tr(a,"2  Full Rosary          15-20 min","2  Rosaire complet      15-20 min"),a->selection==1);
        line(a,10,tr(a,"3  Scripture + guidance 20-30 min","3  Écriture + méditation 20-30 min"),a->selection==2);
        line(a,12,tr(a,"4  Slow contemplation   30-40 min","4  Contemplation lente  30-40 min"),a->selection==3);
        line(a,15,tr(a,"Choose before every new session.","Choix demandé à chaque nouvelle séance."),0);
    }else if(a->screen==SCR_ROSARY_SET){
        center_line(a,3,tr(a,"CHOOSE THE MYSTERIES","CHOISIR LES MYSTÈRES"),0);
        sprintf(b,tr(a,"1  Today: %s","1  Aujourd'hui : %s"), mystery_set_name(a->state.language,suggested_mysteries(a))); line(a,6,b,a->selection==0);
        for(i=0;i<4;i++){ sprintf(b,"%u  %s",i+2, mystery_set_name(a->state.language,(int)i)); line(a,7+(int)i,b,a->selection==i+1); }
        line(a,14,tr(a,"Today's cycle is selected by date.","Cycle du jour proposé automatiquement."),0);
    }else if(a->screen==SCR_ROSARY_DECADE){
        sprintf(b,tr(a,"ONE DECADE - %s MYSTERIES","UNE DIZAINE - MYSTÈRES %s"), mystery_set_name(a->state.language,a->rosarySet)); center_line(a,3,b,0);
        for(i=0;i<5;i++){ sprintf(b,"%u  %s",i+1, mystery_name(a->state.language,a->rosarySet,i)); line(a,6+(int)i,b,a->selection==i); }
        line(a,14,tr(a,"Choose the mystery to pray.","Choisissez le mystère à prier."),0);
    }else if(a->screen==SCR_ROSARY_PRAYER){
        int part=rosary_part(a->rosaryMode,a->rosaryDecade,a->rosaryStep);
        unsigned decade=rosary_view_decade(a->rosaryMode,a->rosaryDecade,a->rosaryStep);
        unsigned count=rosary_view_count(a->rosaryMode,a->rosaryDecade,a->rosaryStep);
        const char *text=rosary_text(a->state.language,a->rosaryMode,a->rosarySet,a->rosaryDecade,a->rosaryStep);
        unsigned pages=rosary_pages(text);
        sprintf(b,"%s  %u/%u", rosary_part_name(a->state.language,a->rosaryMode,a->rosaryDecade,a->rosaryStep), a->rosaryStep+1, rosary_steps(a->rosaryMode));
        center_line(a,3,b,1);
        if(part==RP_MYSTERY||part==RP_SCRIPTURE||part==RP_MEDITATION||part==RP_SILENCE){
            sprintf(b,tr(a,"Mystery %u/%u - %s","Mystère %u/%u - %s"), a->rosaryMode==ROS_DECADE?1u:decade+1, a->rosaryMode==ROS_DECADE?1u:5u, mystery_set_name(a->state.language,a->rosarySet));
            line(a,4,b,0);
        }else if(part==RP_HAIL){
            char beads[12];
            sprintf(b,tr(a,"Decade %u/%u    Hail Mary %u/10","Dizaine %u/%u    Ave Maria %u/10"), a->rosaryMode==ROS_DECADE?1u:decade+1, a->rosaryMode==ROS_DECADE?1u:5u, count);
            line(a,4,b,0);
            for(i=0;i<10;i++) beads[i]=i<count?'*':'o';
            beads[10]=0;
            line(a,5,beads,0);
        }else if(part==RP_OPEN_HAIL){ sprintf(b,tr(a,"Opening Hail Mary %u/3","Ave Maria d'ouverture %u/3"), count); line(a,4,b,0); }
        {   const char *p=text; unsigned len, n;
            for(n=0; *p && n<a->rosaryPage*ROSARY_LINES; n++) p=wrap_line(p,COLUMNS,&len);
            for(i=0;i<ROSARY_LINES && *p;i++){
                char slice[COLUMNS+1]; const char *next=wrap_line(p,COLUMNS,&len);
                memcpy(slice,p,len); slice[len]=0;
                line(a,6+(int)i,slice,0);
                p=next;
            }
        }
        if(pages>1){ sprintf(b,tr(a,"Page %u/%u  Enter: continue","Page %u/%u  Entrée : continuer"), a->rosaryPage+1, pages); line(a,22,b,0); }
        else line(a,22,tr(a,"Enter: next prayer   <- previous","Entrée : suite       <- précédent"),0);
        line(a,23,tr(a,"Esc: leave session","Échap : quitter la séance"),0);
    }else if(a->screen==SCR_ROSARY_COMPLETE){
        center_line(a,3,tr(a,"ROSARY COMPLETED","ROSAIRE TERMINÉ"),1);
        line(a,6,tr(a,"The prayer session is complete.","La séance de prière est terminée."),0);
        line(a,9,tr(a,"Remain for a moment in God's peace.","Demeurez un instant dans la paix de Dieu."),0);
        line(a,13,tr(a,"Enter: return to LPS Companion","Entrée : retour à LPS Companion"),0);
    }else if(a->screen==SCR_FINISH){
        center_line(a,3,tr(a,"CLOSE THE DAY?","TERMINER LE JOUR ?"),0);
        sprintf(b,tr(a,"Activities: %u","Activités : %u"), count_flags(a->state.today.flags)); line(a,6,b,0);
        line(a,13,tr(a,"Enter: save and advance","Entrée : enregistrer et avancer"),0);
        line(a,15,tr(a,"An empty day has no penalty.","Une journée vide ne coûte rien."),0);
    }else if(a->screen==SCR_MEAL_PLANNER){
        MealView v; char right[32];
        meal_view(&a->state,a->mealCursor,&v);
        field(a,3,1,40,tr(a,"MEAL PLANNER","PLANIFICATEUR DE REPAS"),0);
        if(v.position){ sprintf(right,tr(a,"PLAYED #%u","JOUÉE #%u"),(unsigned)v.position); right_text(a,3,right); }
        sprintf(b,"%s %u/%u",v.rowName,a->mealCursor-MEAL_ROW_FIRST[v.row]+1u,(unsigned)(MEAL_ROW_FIRST[v.row+1]-MEAL_ROW_FIRST[v.row]));
        field(a,4,1,40,b,0);
        sprintf(right,tr(a,"%u/%u left","%u/%u restantes"),(unsigned)(MEAL_CARD_COUNT-a->state.mealCount),(unsigned)MEAL_CARD_COUNT);
        right_text(a,4,right);
        if(a->mealGraphics==MEALS_OK){
            int r=a->hw->meal_card(a->hw,&v);
            if(r!=MEALS_OK && !a->message[0]) say(a,meals_problem(a,r));
        }else{
            text_card(a,&v);
            center_line(a,21,meals_problem(a,a->mealGraphics),0);
        }
        center_line(a,22,tr(a,"<- -> Card   Up/Down Row","<- -> Carte   Haut/Bas Rangée"),0);
    }else{
        center_line(a,3,tr(a,"DAY SAVED","JOUR ENREGISTRÉ"),0);
        line(a,15,tr(a,"Enter: start the new day","Entrée : commencer le nouveau jour"),0);
    }

    statusRow = (a->screen==SCR_TIMER_RUNNING||a->screen==SCR_TIMER_COMPLETE)?21:23;
    if(a->missingStorage) a->hw->alert(a->hw,statusRow,0,a->message);
    else if(a->message[0]) line(a,statusRow,a->message,0);
    if(a->hw->battery_low && a->hw->battery_low(a->hw))
        a->hw->alert(a->hw,22,0,tr(a,"[!] Low battery!","[!] Batterie faible !"));
    if(a->screen==SCR_MEAL_PLANNER)
        center_line(a,24,meal_play_position(&a->state,a->mealCursor)?tr(a,"Enter:Put back  Space/Esc:Back","Entrée:Remettre  Espace/Échap:Retour")
                                                              :tr(a,"Enter:Play  Space/Esc:Back","Entrée:Jouer  Espace/Échap:Retour"),1);
    else if(a->screen!=SCR_TIMER_RUNNING && a->screen!=SCR_TIMER_COMPLETE)
        center_line(a,24,tr(a,"Up/Down  Enter:OK  Esc:Back","Haut/Bas  Entrée:OK  Échap:Retour"),1);
    a->hw->present(a->hw);
}

int app_flush_save(App *app){ return flush_save(app); }
int app_is_dirty(const App *app){ return app->dirty; }
