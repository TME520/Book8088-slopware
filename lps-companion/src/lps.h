/* LPS Companion MS-DOS port - shared types (Companion v1.11 parity). C89. */
#ifndef LPS_H
#define LPS_H

#include <stddef.h>

#if defined(__WATCOMC__)
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long  uint32_t;
typedef signed long    int32_t;
typedef unsigned long  lps_tick_t;
#else
#include <stdint.h>
typedef uint64_t lps_tick_t;
#endif

#define LPS_VERSION_STR "1.11"
#define NOTE_LENGTH 96 /* legacy notes: kept in SAVE and ICS, no editor since LPS-DOS-115-CHG02 */
#define HISTORY_LENGTH 31
#define TASK_NAME_LENGTH 32
#define TASK_CAPACITY 24
#define REMINDER_CAPACITY 3
#define REMINDER_TEXT_LENGTH 32
#define BLOB_CAPACITY 4700
#define ACTIVITY_COUNT 16
#define PAGE_SIZE 10
#define PAGE_COUNT 2
#define ACTIVITY_MASK 0xFFFFu
/* Book 8088 text mode is 80x25. One width drives every screen. */
#define COLUMNS 80
#define ROWS 25
#define ACT_PER_COL 8

#define KEY_ENTER 13
#define KEY_BACKSPACE 8
#define KEY_ESCAPE 27
#define KEY_TAB 9
#define KEY_UP 1000
#define KEY_DOWN 1001
#define KEY_LEFT 1002
#define KEY_RIGHT 1003
#define KEY_DELETE 1004
#define KEY_F1 1005
#define KEY_F2 1006
#define KEY_F3 1007
#define KEY_F4 1008
#define KEY_F5 1009

#define WIRE_V1 3568
#define WIRE_V2 (WIRE_V1 + 1)
#define WIRE_V3 (WIRE_V2 + 32 * 4)
#define WIRE_V4 (WIRE_V3 + 1)
#define WIRE_V5 (WIRE_V4 + 1 + TASK_CAPACITY * (1 + TASK_NAME_LENGTH + 1))
#define WIRE_SIZE (WIRE_V5 + 1 + REMINDER_CAPACITY * (2 + 1 + 1 + 2 + REMINDER_TEXT_LENGTH + 1))
#define RECORD_OVERHEAD 16
#define RECORD_SIZE (WIRE_SIZE + RECORD_OVERHEAD)
#define SAVE_MAGIC 0x3152504Cul /* 'LPR1' */
#define WIRE_MAGIC 0x3153504Cul /* 'LPS1' */

enum Language { LANG_EN = 0, LANG_FR = 1 };
enum Palette { PAL_RED = 0, PAL_GREEN = 1, PAL_BLUE = 2, PAL_ORANGE = 3, PAL_PINK = 4 };
enum TaskStatus { TASK_TODO = 0, TASK_DOING = 1, TASK_DONE = 2 };
enum RosaryMode { ROS_DECADE = 0, ROS_FULL = 1, ROS_GUIDED = 2, ROS_CONTEMP = 3 };
enum MysterySet { MYS_JOYFUL = 0, MYS_LUMINOUS = 1, MYS_SORROWFUL = 2, MYS_GLORIOUS = 3 };
/* LOAD_MIGRATED: loaded from the old SAVE_A/B.BIN; the app writes a JSON save at once. */
enum LoadResult { LOAD_MISSING = 0, LOAD_OK = 1, LOAD_NO_STORAGE = 2, LOAD_ERROR = 3, LOAD_MIGRATED = 4 };
enum SaveCheck { SAVE_MISSING = 0, SAVE_VALID, SAVE_DAMAGED, SAVE_UNSUPPORTED, SAVE_IOERROR };
enum Screen {
    SCR_TIMER_SELECT = 0, SCR_TIMER_RUNNING, SCR_TIMER_COMPLETE,
    SCR_HOME, SCR_REMINDER_POPUP, SCR_REMINDERS, SCR_REMINDER_EDIT,
    SCR_DATE, SCR_ACTIVITIES, SCR_WEIGHT, SCR_KANBAN, SCR_TASK_EDIT,
    SCR_DAILY_REVIEW, SCR_FINISH, SCR_REWARD, SCR_CONFIG, SCR_LANGUAGE, SCR_COLORS,
    SCR_ROSARY_MODE, SCR_ROSARY_SET, SCR_ROSARY_DECADE, SCR_ROSARY_PRAYER, SCR_ROSARY_COMPLETE
};

typedef struct { uint16_t year; uint8_t month, day; } Date;
typedef struct {
    Date date;
    uint32_t number;
    uint16_t flags;
    int32_t expected, actual; /* grams; -1 missing */
    char note[NOTE_LENGTH + 1];
} Day;
typedef struct { uint8_t status; char name[TASK_NAME_LENGTH + 1]; } Task;
typedef struct { Date from; uint16_t duration; char text[REMINDER_TEXT_LENGTH + 1]; } Reminder;
typedef struct {
    uint8_t language;
    uint8_t palette;
    uint32_t xp; /* banked in the save for compatibility; never shown since LPS-DOS-115-CHG01 */
    Day today;
    Day history[HISTORY_LENGTH];
    uint8_t next, count;
    uint8_t taskCount;
    Task tasks[TASK_CAPACITY];
    uint8_t reminderCount;
    Reminder reminders[REMINDER_CAPACITY];
} State;
typedef struct { uint8_t bytes[BLOB_CAPACITY]; size_t size; } Blob;

typedef struct Platform Platform;
struct Platform {
    void (*clear)(Platform *self);
    void (*text)(Platform *self, int row, int col, const char *s, int reverse);
    void (*alert)(Platform *self, int row, int col, const char *s);
    void (*large_text)(Platform *self, const char *value, int red);
    void (*present)(Platform *self);
    void (*set_palette)(Platform *self, int palette);
    int  (*battery_low)(Platform *self);
    enum LoadResult (*load)(Platform *self, State *out); /* out is set only on LOAD_OK / LOAD_MIGRATED */
    int  (*save)(Platform *self, const State *s);
    int  (*export_ics)(Platform *self, const Day *day, uint32_t totalXp, uint32_t dayXp, int language);
    int  (*export_weight_csv)(Platform *self, const Day *day);
    void *user;
};

extern const char *ACTIVITIES_FR[ACTIVITY_COUNT];
extern const char *ACTIVITIES_EN[ACTIVITY_COUNT];
extern const Date FIRST_DATE, LAST_DATE, DEFAULT_DATE;

int leap(uint16_t year);
unsigned days_in_month(uint16_t year, uint8_t month);
int valid_date(Date d);
int next_date(Date *d);
void date_text(Date d, char *out, size_t n);
int parse_date(const char *text, Date *d);
unsigned count_flags(uint16_t flags);
uint32_t points(const Day *d);
int parse_weight(const char *s, int32_t *grams);
void weight_text(int32_t grams, char *out, size_t n);
uint32_t crc32(const uint8_t *p, size_t n);
uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n); /* running: start 0xffffffff, invert at the end */
/* cp437.c (LPS-DOS-115-CHG03): screen text is CP437, files are UTF-8. */
size_t cp437_to_utf8(unsigned char c, char *out); /* out: up to 3 bytes */
int unicode_to_cp437(unsigned long cp);            /* -1 if CP437 lacks it */
int cp437_text_byte(unsigned char c);              /* printable ASCII or CP437 letter */
/* wrap.c (LPS-DOS-115-CHG03c): word wrap. wrap_line sets *len to the cells of
 * the line starting at text (at most width) and returns where the next line
 * starts; wrap_count returns the number of lines. */
const char *wrap_line(const char *text, unsigned width, unsigned *len);
unsigned wrap_count(const char *text, unsigned width);
#ifndef LPS_DOS
void encode_state(const State *s, Blob *b); /* binary format 6: only the tests still write it */
#endif
int decode_state(const uint8_t *bytes, size_t size, State *out);
void state_init(State *s);
/* Large working buffers (LPS-DOS-115-CHG03b). One block allocated at start-up
 * from the far heap (large model), so they sit outside DGROUP and add no
 * zero-filled bytes to LPSCOMP.EXE. lps_buffers_init() is idempotent. */
typedef struct {
    State decode;                 /* SAVE parsing / validation scratch */
    State scratch;                /* app: edit candidate before commit */
    uint8_t rec[RECORD_SIZE];     /* one old SAVE_A/B.BIN record (migration) */
    char ics[2048];               /* one ICS export */
    char csv[8192];               /* one WEIGHTS\YYYY.csv (was on the stack) */
} LpsBuffers;
extern LpsBuffers *lps_buf;
int lps_buffers_init(void); /* 0 = out of memory */
#define lps_decode_scratch (lps_buf->decode)
uint32_t day_index(Date d);
unsigned weekday(Date d); /* 0=Sunday */
unsigned iso_week(Date d); /* ISO 8601, 1..53 */
void long_date_text(Date d, int language, char *out, size_t n);

/* ICS / CSV */
int ics_escape_append(char *dst, size_t cap, size_t *used, const char *text);
int build_ics(const Day *day, uint32_t totalXp, uint32_t dayXp, int language, char *out, size_t cap);
int write_ics_file(const char *export_dir, const Day *day, uint32_t totalXp, uint32_t dayXp, int language);
int write_weight_csv(const char *weights_dir, const Day *day);

/* JSON save file (jsave.c, LPS-DOS-115-CHG04): PicoCalc v1.15 schema 2. The
 * catalog, extra day names, Meal Planner deck and hidden home entries the DOS
 * port does not use are kept inside jsave.c and written back. */
int jsave_init(void);   /* far-heap tables; 0 = out of memory. Idempotent. */
void jsave_reset(void); /* forget kept data: default catalog, nothing else */
void jsave_new_day(void); /* closing the day deals a new Meal Planner deck */
enum SaveCheck jsave_read(const char *path, State *out, uint32_t *generation, uint32_t *checksum);
int jsave_write(const char *path, const State *s, uint32_t generation, uint32_t *checksum);
enum SaveCheck jsave_peek(const char *path, uint32_t *version, uint32_t *generation); /* first line only */
int jsave_verify(const char *path, uint32_t generation, uint32_t checksum); /* read-back after a write */

/* A/B SAVE (save.c): SAVE_A/B.JSN, migration from SAVE_A/B.BIN.
 * *active: 0=A, 1=B, -1 = none yet (the next save writes A). */
enum LoadResult save_load(const char *lps_dir, State *out, uint32_t *generation, int *active);
int save_store(const char *lps_dir, const State *s, uint32_t *generation, int *active);

/* App */
typedef struct App App;
size_t app_bytes(void);
void app_init(App *app, Platform *hw);
void app_start(App *app);
void app_key(App *app, int k);
void app_tick(App *app, lps_tick_t ms);
void app_render(App *app);
const State *app_state(const App *app);
enum Screen app_screen(const App *app);
const Day *app_viewed_day(const App *app);
int app_flush_save(App *app);
int app_is_dirty(const App *app);

/* paths helpers */
void lps_set_data_dir(const char *dir);
const char *lps_data_dir(void);
int lps_ensure_dirs(void);
void lps_path_join(char *out, size_t n, const char *a, const char *b);

#endif
