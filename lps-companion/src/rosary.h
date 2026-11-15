#ifndef ROSARY_H
#define ROSARY_H
#include "lps.h"
#define RP_SIGN 0
#define RP_CREED 1
#define RP_OPEN_OF 2
#define RP_OPEN_HAIL 3
#define RP_GLORY 4
#define RP_MYSTERY 5
#define RP_SCRIPTURE 6
#define RP_MEDITATION 7
#define RP_SILENCE 8
#define RP_OUR_FATHER 9
#define RP_HAIL 10
#define RP_FATIMA 11
#define RP_HHQ 12
#define RP_CLOSING 13
#define RP_END_SIGN 14
/* ROSARY.DAT text ids (LPS-DOS-115-CHG03b). Each id has an English and a
 * French string, stored at index id*2+lang. tools/mkrosary writes the file
 * from src/rosary_text.c, the single source of the texts. */
#define ROS_T_SIGN 0
#define ROS_T_CREED 1
#define ROS_T_OUR_FATHER 2
#define ROS_T_HAIL 3
#define ROS_T_GLORY 4
#define ROS_T_SILENCE 5
#define ROS_T_FATIMA 6
#define ROS_T_HHQ 7
#define ROS_T_CLOSING 8
#define ROS_T_PRAYER_COUNT 9
#define ROS_T_MYSTERY 9     /* + set*5 + decade */
#define ROS_T_SCRIPTURE 29  /* + set*5 + decade */
#define ROS_T_MEDITATION 49 /* + set*5 + decade */
#define ROS_T_COUNT 69
#define ROSARY_DAT_STRINGS (ROS_T_COUNT*2)
#define ROSARY_DAT_NAME "ROSARY.DAT"
/* File layout, little-endian:
 *   0  8 bytes  magic "LPSROSRY"
 *   8  u16      version (ROSARY_DAT_VERSION)
 *  10  u16      string count (ROSARY_DAT_STRINGS)
 *  12  u32      payload size in bytes
 *  16  u32      CRC-32 of the payload (same CRC as the SAVE files)
 *  20  payload: count x u16 offsets into the string area, then the
 *      NUL-terminated strings (printable ASCII). */
#define ROSARY_DAT_MAGIC "LPSROSRY"
#define ROSARY_DAT_VERSION 1
#define ROSARY_DAT_HEADER 20
#define ROSARY_DAT_MAX_PAYLOAD 0xFF00u /* one far-heap block */
enum RosaryLoad { ROSARY_OK = 0, ROSARY_MISSING = 1, ROSARY_CORRUPT = 2, ROSARY_NO_MEMORY = 3 };
/* Full path of ROSARY.DAT (main sets it next to LPSCOMP.EXE). If that file is
 * absent, the current directory is tried. */
void rosary_set_path(const char *path);
const char *rosary_path(void);
/* Loads ROSARY.DAT into far memory once; returns enum RosaryLoad. */
int rosary_load(void);
void rosary_unload(void);
int rosary_loaded(void);
/* Build side only (tools/mkrosary, host tests): the source texts. */
const char *rosary_source_text(unsigned id, int lang);

const char *mystery_set_name(int lang, int set);
const char *mystery_name(int lang, int set, unsigned decade);
unsigned rosary_steps(int mode);
const char *rosary_text(int lang, int mode, int set, unsigned rosaryDecade, unsigned step);
const char *rosary_part_name(int lang, int mode, unsigned rosaryDecade, unsigned step);
int rosary_part(int mode, unsigned rosaryDecade, unsigned step);
unsigned rosary_view_decade(int mode, unsigned rosaryDecade, unsigned step);
unsigned rosary_view_count(int mode, unsigned rosaryDecade, unsigned step);
#endif
