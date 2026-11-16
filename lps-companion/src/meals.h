/* meals.h - Meal Planner deck and MEALS.DAT pictures (LPS-DOS-115-CHG07).
 * The deck, its order and its names are the PicoCalc's (v1.14 CHG01,
 * src/lps_companion.cpp MealDeck); the pictures are the user's card sheets,
 * converted by tools/mkmeals.py. */
#ifndef MEALS_H
#define MEALS_H
#include "lps.h"

enum MealRow { MEAL_DRINKS = 0, MEAL_FOOD = 1, MEAL_BLANK = 2, MEAL_ROW_COUNT = 3 };
/* Pictures in MEALS.DAT, the order of tools/mkmeals.py SPRITES. */
enum MealPicture { MP_LATTE, MP_BLACK, MP_TEA, MP_MINT_TEA, MP_JUICE, MP_WRAP, MP_SALAD,
    MP_BISCUIT, MP_CHEESE, MP_APPLE, MP_ORANGE, MP_SOUP, MP_BLANK, MEAL_PICTURE_COUNT };
typedef struct { uint8_t row, picture; const char *en, *fr, *enPortion, *frPortion; } MealCard;
extern const MealCard MEAL_DECK[MEAL_CARD_COUNT];
extern const uint8_t MEAL_ROW_FIRST[MEAL_ROW_COUNT + 1]; /* first card of each row, then the end */
const char *meal_row_name(unsigned row, int language);
/* 1-based place in the order of play, 0 when the card is still in the deck. */
unsigned meal_play_position(const State *s, unsigned card);
/* Plays a card, or puts a played card back (later plays move up one place). */
void meal_toggle(State *s, unsigned card);
/* The planner opens on the first card still in the deck (card 0 if none). */
unsigned meal_first_unplayed(const State *s);
/* Left/Right wrap within the row; Up/Down wrap between rows and keep the
 * column when the new row is long enough (else its last card). */
unsigned meal_move(unsigned card, int key);
void meal_view(const State *s, unsigned card, MealView *v);

/* MEALS.DAT, little-endian (tools/mkmeals.py writes it):
 *   0  8 bytes  magic "LPSMEALS"
 *   8  u16      version (1)
 *  10  u16      picture count (MEAL_PICTURE_COUNT)
 *  12  u16      width (MEALS_WIDTH), u16 height (MEALS_HEIGHT)
 *  16  u32      bytes per picture (MEALS_PICTURE_BYTES)
 *  20  u32      CRC-32 of the table
 *  24  table: per picture 16 bytes of EGA palette (colour values 0-63 for
 *      palette indices 0-15; 2-15 are the card's) and the u32 checksum of
 *      its pixels (meals_checksum)
 *  then the pictures: 4 bit planes each (plane 0 = bit 0 of the palette
 *  index), MEALS_HEIGHT rows of MEALS_WIDTH/8 bytes, leftmost pixel in the
 *  high bit. */
#define MEALS_DAT_NAME "MEALS.DAT"
#define MEALS_DAT_MAGIC "LPSMEALS"
#define MEALS_DAT_VERSION 1
#define MEALS_DAT_HEADER 24
#define MEALS_TABLE_ENTRY 20
#define MEALS_WIDTH 240
#define MEALS_HEIGHT 128
#define MEALS_ROW_BYTES (MEALS_WIDTH / 8)
#define MEALS_PLANE_BYTES (MEALS_ROW_BYTES * MEALS_HEIGHT)
#define MEALS_PICTURE_BYTES (MEALS_PLANE_BYTES * 4)
#define MEALS_DAT_SIZE (MEALS_DAT_HEADER + MEAL_PICTURE_COUNT * (MEALS_TABLE_ENTRY + (long)MEALS_PICTURE_BYTES))
/* Palette indices of a card (the screen keeps 0 = background, 1 = text). */
#define MEAL_INK_BODY 2
#define MEAL_INK_TEXT 3
#define MEAL_INK_EDGE 4
#define MEAL_INK_RIBBON 5
/* A picture's checksum: two 16-bit running sums over its little-endian
 * words, s1 += word, s2 += s1, as s2 << 16 | s1. A CRC-32 of 15 KB takes
 * 1.2 s at 8088 speed, this about 0.1 s; the table has a CRC-32. */
uint32_t meals_checksum(const uint8_t *p, unsigned n);
/* Full path of MEALS.DAT (main sets it next to LPSCOMP.EXE); if that file
 * is absent, the current directory is tried. */
void meals_set_path(const char *path);
/* Reads and checks the header and the table (284 bytes); enum MealsLoad. */
int meals_open(void);
/* A picture's 16 palette registers (meals_open first), NULL if not open. */
const uint8_t *meals_palette(unsigned picture);
/* A picture's planes, read on first use and kept in far memory until
 * meals_close; its checksum is checked on that first read. enum MealsLoad. */
int meals_picture(unsigned picture, const uint8_t **planes);
void meals_close(void);
#endif
