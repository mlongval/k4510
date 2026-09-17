/* JIM's pictures: the Kitty graphics protocol, on VICKY.
 *
 * Doc, 2026-09-17: "I would like BOOK to display images inline..... can Jim be
 * upgraded to Kitty graphics????"  It can, and that is the right way to give
 * BOOK its pictures: BOOK already talks to JIM, VICKY's layers already stack,
 * and a terminal that speaks Kitty's protocol is one that `icat`, `chafa`, file
 * managers and Neovim's image plugins can draw on from a `!` shell as well.
 *
 * What JIM implements (https://sw.kovidgoyal.net/kitty/graphics-protocol/):
 *   ESC _ G key=value,... ; base64 payload ESC \
 *   a=q query   a=T transmit and show   a=t transmit   a=p show   a=d delete
 *   f=24 RGB, f=32 RGBA, f=100 PNG;  o=z zlib;  m=1 chunks
 *   t=d in the escape, t=f a file, t=t a temporary file (deleted after)
 *   i= image id;  c= r= columns and rows to fill;  x y w h a part of the image
 *   C=1 leave the cursor;  q=1/2 say less
 * Not: t=s shared memory, animation, z-order under the text, unicode
 * placeholders, relative placements.  A program that asks is told EINVAL.
 *
 * Where the pixels go.  VICKY's layer 3 -- the top one; layers 1 and 2 stay
 * the programs' -- as an 8-bit bitmap the size of the glass, in far memory at
 * JIMGFX_PLANE, where nothing else lives.  Colour 0 is transparent there, so
 * the text shows wherever no picture is.  VICKY has one palette of 256, so a
 * picture is dithered onto a fixed 6x6x6 cube in entries 40-255 (loaded when
 * the first picture is shown): several pictures share the screen without
 * fighting over colours, and the 16 the console uses are untouched.
 *
 * Pictures scroll with the text and go when the screen is cleared, as they do
 * in Kitty.  JIM asks the plane to follow (jimgfx_scroll, jimgfx_clear).
 */
#ifndef K4510_JIMGFX_H
#define K4510_JIMGFX_H
#include <stdint.h>
#include <stddef.h>

#define JIMGFX_PLANE 0x0F000000u     /* 1440x1080 bytes at most: $0F000000-$0F17BAFF, under the ROM at $0FFF0000 */
#define JIMGFX_LAYER 3

typedef struct {
    int cols, rows;                  /* the text window, in cells */
    int px0, py0;                    /* the glass pixel at the window's top left cell */
    int cell_w, cell_h;              /* 8, and 8 or 16 */
    int cx, cy;                      /* the cursor, in cells, in the window */
    int host;                        /* a `!` session: t=f paths are the Linux's; otherwise the machine's */
} jimgfx_geom_t;

/* what a command asks JIM to do once it has been understood */
typedef struct {
    int place;                       /* 1: show image `img` at the cursor, `cols` x `rows` cells */
    int img, cols, rows, keep_cursor;
    int sx, sy, sw, sh;              /* the part of the image, in its pixels */
    int pw, ph;                      /* how large to draw it, in glass pixels */
    char reply[96];                  /* "" or an APC for the reply FIFO */
} jimgfx_todo_t;

void jimgfx_apc(const uint8_t *buf, size_t len, const jimgfx_geom_t *g, jimgfx_todo_t *todo);   /* one whole APC, without ESC _ and ESC \ */
void jimgfx_draw(const jimgfx_todo_t *todo, const jimgfx_geom_t *g);      /* after JIM has scrolled to make room */
void jimgfx_scroll(int y0, int y1, int dy);      /* glass rows y0..y1-1 move by dy pixels (negative: up); the gap is cleared */
void jimgfx_clear_rows(int y0, int y1);
void jimgfx_clear(void);                         /* every picture off the glass; the images stay in memory, as Kitty's do */
void jimgfx_reset(void);                         /* and forgotten: RIS, a mode change */
int  jimgfx_active(void);
#endif
