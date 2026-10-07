/* Integer display resolutions (IDRs): from the panel to the list of
 * machine resolutions that show on it with whole-number pixels.
 * docs/design-video-foundations.md is the design; nothing calls this yet.
 *
 *   panel  (pw x ph)   the display's native pixels, as the frontend finds them
 *   canvas (cw x ch)   the panel itself (IDR_BASE_FULL), or the largest 4:3
 *                      rectangle in it (IDR_BASE_43):
 *                        wider than 4:3  ch = ph, cw = floor(ph * 4 / 3)
 *                        otherwise       cw = pw, ch = floor(pw * 3 / 4)
 *   IDR n              floor(cw / n) x floor(ch / n), shown at n x n panel
 *                      pixels a machine pixel.  What the floors drop (under
 *                      n panel pixels an axis) joins the border.
 *
 * The list runs from the largest scale that fits the limits down to the
 * smallest picture the limits allow, largest picture first.  Pure C, no
 * state, no SDL: the frontend, the tests and a tool can all ask it. */
#ifndef K4510_IDR_H
#define K4510_IDR_H

enum { IDR_BASE_43, IDR_BASE_FULL };

#define IDR_MAX 24                    /* more scales than any panel offers inside the limits below */

typedef struct { int w, h, scale; } idr_t;

typedef struct {
    int  min_w, min_h;                /* the smallest picture offered (K/OS: 320x200, 40x25 in 8x8 cells) */
    long max_pixels;                  /* the most pixels VICKY draws a frame (the performance cap) */
    int  max_w, max_h;                /* VICKY's line buffers and frame buffer; 0 = no limit */
    int  pow2;                        /* 1: scales 1, 2, 4, 8 ... only; 0: every whole scale */
} idr_limits;

/* The proposal's defaults (design doc section 3): 320x200 at least, at most
 * 1920x1080's pixels (2,073,600), every whole scale. */
extern const idr_limits idr_default_limits;

/* The canvas for a panel.  A panel under 4x3 pixels gives 0x0. */
void idr_canvas(int pw, int ph, int base, int *cw, int *ch);

/* The IDRs for a panel, largest first, into out[0..max-1]; returns how many.
 * lim NULL means idr_default_limits. */
int  idr_list(int pw, int ph, int base, const idr_limits *lim, idr_t *out, int max);

/* A text grid: whole cells of cell_w x cell_h machine pixels in w x h. */
void idr_grid(int w, int h, int cell_w, int cell_h, int *cols, int *rows);

/* Can this IDR draw HD text -- 16-wide glyphs in the cells of its 8-wide
 * grid, VICKY drawing the frame at twice the IDR (as 720x540 does today)?
 * Only at an even scale, so a glyph pixel is scale/2 panel pixels, whole; and
 * only if the doubled frame is inside the limits.  lim NULL: the defaults. */
int  idr_hd_text(const idr_t *r, const idr_limits *lim);

#endif
