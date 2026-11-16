/* cga.h - CGA 320 x 200, 4 colours (BIOS mode 04h), for the Meal Planner on
 * a colour CGA without EGA (LPS-DOS-115-CHG13). DOS only, except
 * lps_fold_row (wrap.c, host-tested).
 *
 * Palette 0 high intensity: 1 light green, 2 light red, 3 yellow; colour 0
 * is the background (black, or blue for the Blue colour theme). Text is the
 * 8 x 8 font of cgafont.c on a 40 x 25 grid: the 80-column rows are folded
 * to 40 columns by lps_fold_row. Until LPS-DOS-115-CHG14/15 bring the dithered
 * pictures (MEALSCGA.DAT), the card is a placeholder frame: header band,
 * an empty picture box, name, portion ribbon, and PLAYED #n when played. */
#ifndef CGA_H
#define CGA_H
#include "lps.h"

#define CGA_COLUMNS 40
extern const unsigned char cga_font8[2048];

/* Mode 04h, palette 0 high intensity, background colour (0-15). text: the
 * colour (1-3) of the screen's text. Forgets the card drawn before. */
void cga_enter(unsigned char text, unsigned char background);
/* One 80-cell text row (CP437) folded to 40 cells, at y = row * 8. */
void cga_text_row(int row, const char *cells, const unsigned char *rev);
/* The placeholder card in text rows 5-21 (y 40-175). Redrawn only when the
 * card, its language or played state changed since the last call. */
void cga_card(const MealView *v);
#endif
