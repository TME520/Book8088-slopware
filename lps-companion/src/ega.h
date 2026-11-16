/* ega.h - EGA 640 x 350, 16 colours (BIOS mode 10h), for the Meal Planner
 * (LPS-DOS-115-CHG07). DOS only. */
#ifndef EGA_H
#define EGA_H
#include "lps.h"

/* Mode 10h and the screen's palette: 0 background (black), 1 text (the
 * user's colour, an EGA colour value 0-63). The card's palette comes with
 * ega_card. */
void ega_enter(unsigned char text_colour);
/* One row of 80 text cells (CP437, 8 x 14 font from the BIOS) at y = row*14;
 * rev[i] != 0 draws cell i in reverse (black on the text colour). */
void ega_text_row(int row, const char *cells, const unsigned char *rev);
/* The card in the middle of the screen. Draws only what changed since the
 * last call (ega_enter forgets it): a new picture or language redraws the
 * illustration, name and ribbon; a new row the header band; playing the
 * card or putting it back only changes the palette (faded greys).
 * planes NULL: no picture (MEALS.DAT unreadable). */
void ega_card(const MealView *v, const uint8_t *planes, const uint8_t *palette16);
#endif
