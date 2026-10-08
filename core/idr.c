/* Integer display resolutions.  See idr.h and docs/design-video-foundations.md. */
#include "idr.h"

const idr_limits idr_default_limits = { 320, 200, 1920L * 1080L, 1920, 1200, 0 };

void idr_canvas(int pw, int ph, int base, int *cw, int *ch)
{
    if (pw < 4 || ph < 3) { *cw = *ch = 0; return; }
    if (base == IDR_BASE_FULL) { *cw = pw; *ch = ph; return; }
    /* the largest 4:3 rectangle: whole panel height when the panel is wider
     * than 4:3, whole width otherwise; the other side floored */
    if ((long) pw * 3 >= (long) ph * 4) { *ch = ph; *cw = (int)((long) ph * 4 / 3); }
    else                                { *cw = pw; *ch = (int)((long) pw * 3 / 4); }
}

int idr_list(int pw, int ph, int base, const idr_limits *lim, idr_t *out, int max)
{
    int cw, ch, n = 0;
    if (!lim) lim = &idr_default_limits;
    idr_canvas(pw, ph, base, &cw, &ch);
    for (int s = 1; n < max; s++) {
        int w = cw / s, h = ch / s;
        if (w < lim->min_w || h < lim->min_h) break;          /* smaller from here on */
        if (lim->pow2 && (s & (s - 1))) continue;
        if ((long) w * h > lim->max_pixels) continue;           /* too big: the next scale may fit */
        if ((lim->max_w && w > lim->max_w) || (lim->max_h && h > lim->max_h)) continue;
        out[n].w = w; out[n].h = h; out[n].scale = s; n++;
    }
    return n;
}

void idr_grid(int w, int h, int cell_w, int cell_h, int *cols, int *rows)
{
    *cols = cell_w > 0 ? w / cell_w : 0;
    *rows = cell_h > 0 ? h / cell_h : 0;
}

int idr_hd_text(const idr_t *r, const idr_limits *lim)
{
    if (!lim) lim = &idr_default_limits;
    int k = r->scale >= 2 && !(r->scale & 1) ? 2 : r->scale >= 3 && r->scale % 3 == 0 ? 3 : 0;   /* the frame drawn 2x, or 3x (2026-10-07: 480x360) */
    if (!k) return 0;
    if ((long) r->w * r->h * k * k > lim->max_pixels) return 0;
    if ((lim->max_w && k * r->w > lim->max_w) || (lim->max_h && k * r->h > lim->max_h)) return 0;
    return k;
}
