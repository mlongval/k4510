/* demo/renum.h -- renumber a BASIC program that is being edited as text.
 * Shared by VI (:renum) and EDIT (Ctrl-R), and compiled on the host by
 * test/renumtest.c.  Doc, 2026-09-14: "*VI and *EDIT could use a nice renum
 * command for the programming language that launched it (context aware)".
 *
 * The language is the file's: .BBC is BBC BASIC (its *VI file is
 * EDITTMP.BBC), .BAS is EhBASIC or Microsoft BASIC, .LGO is LOGO, which has
 * no line numbers to renumber.  Two passes, over lines the includer walks:
 *   rn_scan(line)    each line's own number, into a table the includer keeps
 *                    (RN_TAB_PUT / RN_TAB_GET: far memory on the machine)
 *   rn_check(...)    the numbers in order, and the new ones inside the limit
 *   rn_line(in,out)  the line renumbered: its own number, and each target
 *                    after GOTO GOSUB THEN RESTORE (and ELSE, which only BBC
 *                    BASIC has), ON x GOTO lists too.  Strings, REM and DATA
 *                    are left alone; a target naming no line is kept, counted.
 * A line is a length byte and up to 255 characters, as VI keeps them. */

#define RN_NONE 0
#define RN_MS   1
#define RN_BBC  2
#define RN_LOGO 3

static unsigned rn_n, rn_i, rn_prev, rn_start, rn_step, rn_unres;
static uint8_t rn_lang, rn_sorted, rn_ovf;
static uint8_t *rn_o;
static char rn_msg[64];
static const char *const rn_refs[] = { "GOTO", "GOSUB", "THEN", "RESTORE", "ELSE" };

static uint8_t rn_up(uint8_t c) { return (uint8_t)((c >= 'a' && c <= 'z') ? c - 32 : c); }
static uint8_t rn_dig(uint8_t c) { return (uint8_t)(c >= '0' && c <= '9'); }

static uint8_t rn_lang_of(const char *name)
{
    const char *d = 0, *s;
    for (s = name; *s; s++) if (*s == '.') d = s;
    if (!d) return RN_NONE;
    if (rn_up(d[1]) == 'B' && rn_up(d[2]) == 'A' && rn_up(d[3]) == 'S' && !d[4]) return RN_MS;
    if (rn_up(d[1]) == 'B' && rn_up(d[2]) == 'B' && rn_up(d[3]) == 'C' && !d[4]) return RN_BBC;
    if (rn_up(d[1]) == 'L' && rn_up(d[2]) == 'G' && rn_up(d[3]) == 'O' && !d[4]) return RN_LOGO;
    return RN_NONE;
}

/* a number at l[*p] (1-based: l[0] is the length); 1 if there was one */
static uint8_t rn_getnum(const uint8_t *l, unsigned *p, unsigned *v)
{
    unsigned long n = 0;
    if (*p > l[0] || !rn_dig(l[*p])) return 0;
    while (*p <= l[0] && rn_dig(l[*p])) {
        n = n * 10 + (unsigned)(l[*p] - '0');
        if (n > 65535UL) n = 65535UL;
        (*p)++;
    }
    *v = (unsigned)n;
    return 1;
}

static void rn_begin(void) { rn_n = 0; rn_prev = 0; rn_sorted = 1; rn_unres = 0; }
static void rn_rewind(void) { rn_i = 0; rn_unres = 0; }

static void rn_scan(const uint8_t *l)
{
    unsigned p = 1, v;
    while (p <= l[0] && l[p] == ' ') p++;
    if (!rn_getnum(l, &p, &v)) return;
    if (rn_n && v <= rn_prev) rn_sorted = 0;
    rn_prev = v;
    RN_TAB_PUT(rn_n, v);
    rn_n++;
}

/* 0 when the renumbering can go ahead, else why not */
static const char *rn_check(uint8_t lang, unsigned start, unsigned step)
{
    unsigned long last, max = lang == RN_BBC ? 65279UL : 63999UL;
    if (lang == RN_LOGO) return "renum: LOGO has no line numbers";
    if (lang == RN_NONE) return "renum: not a BASIC file (.BAS, .BBC)";
    if (!rn_n) return "renum: no numbered lines";
    if (!rn_sorted) return "renum: line numbers out of order";
    if (!start || !step) return "renum: start and step from 1";
    last = start + (unsigned long)step * (rn_n - 1);
    if (last > max) return lang == RN_BBC ? "renum: past line 65279" : "renum: past line 63999";
    rn_lang = lang; rn_start = start; rn_step = step;
    rn_rewind();
    return 0;
}

static int rn_find(unsigned v)                   /* binary: rn_check made sure they ascend */
{
    unsigned lo = 0, hi = rn_n, mid, t;
    while (lo < hi) {
        mid = (lo + hi) >> 1;
        t = RN_TAB_GET(mid);
        if (t == v) return (int)mid;
        if (t < v) lo = mid + 1; else hi = mid;
    }
    return -1;
}

static void rn_put(uint8_t c) { if (rn_o[0] == 255) { rn_ovf = 1; return; } rn_o[0]++; rn_o[rn_o[0]] = c; }
static void rn_putnum(unsigned v)
{
    char b[6]; uint8_t k = 0;
    do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) rn_put((uint8_t)b[--k]);
}

/* the length of keyword kw at l[p], or 0.  BBC BASIC's keywords are upper
 * case and never inside a name (goto is a fine variable there); the
 * Microsoft dialects find them anywhere, as their tokenisers do */
static uint8_t rn_kw(const uint8_t *l, unsigned p, const char *kw)
{
    unsigned i = 0; uint8_t c;
    if (rn_lang == RN_BBC && p > 1) {
        c = l[p - 1];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') return 0;
    }
    while (kw[i]) {
        if (p + i > l[0]) return 0;
        c = rn_lang == RN_BBC ? l[p + i] : rn_up(l[p + i]);
        if (c != (uint8_t)kw[i]) return 0;
        i++;
    }
    return (uint8_t)i;
}

/* in renumbered into out; 0 if it would pass 255 characters */
static uint8_t rn_line(const uint8_t *in, uint8_t *out)
{
    unsigned p = 1, q, v; uint8_t k, n, kmax = (uint8_t)(rn_lang == RN_BBC ? 5 : 4), list; int ix;
    rn_o = out; out[0] = 0; rn_ovf = 0;
    while (p <= in[0] && in[p] == ' ') rn_put(in[p++]);
    if (rn_getnum(in, &p, &v)) { rn_putnum(rn_start + rn_step * rn_i); rn_i++; }
    while (p <= in[0]) {
        if (in[p] == '"') {                              /* a string, to its closing quote */
            do rn_put(in[p++]); while (p <= in[0] && in[p] != '"');
            if (p <= in[0]) rn_put(in[p++]);
            continue;
        }
        if (rn_kw(in, p, "REM") || rn_kw(in, p, "DATA")) {
            while (p <= in[0]) rn_put(in[p++]);
            break;
        }
        n = 0;
        for (k = 0; k < kmax; k++) if ((n = rn_kw(in, p, rn_refs[k])) != 0) break;
        if (!n) { rn_put(in[p++]); continue; }
        list = (uint8_t)(k < 2);                         /* GOTO and GOSUB carry ON's lists */
        while (n--) rn_put(in[p++]);
        for (;;) {
            while (p <= in[0] && in[p] == ' ') rn_put(in[p++]);
            q = p;
            if (!rn_getnum(in, &p, &v)) break;
            ix = rn_find(v);
            if (ix >= 0) rn_putnum(rn_start + rn_step * (unsigned)ix);
            else { while (q < p) rn_put(in[q++]); rn_unres++; }
            if (!list) break;
            q = p;
            while (q <= in[0] && in[q] == ' ') q++;
            if (q > in[0] || in[q] != ',') break;
            while (p <= q) rn_put(in[p++]);              /* the spaces and the comma */
        }
    }
    return (uint8_t)!rn_ovf;
}

static void rn_cat(const char *s) { char *m = rn_msg; while (*m) m++; while (*s && m < rn_msg + sizeof rn_msg - 1) *m++ = *s++; *m = 0; }
static void rn_catn(unsigned v) { char b[6]; uint8_t k = 0; do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v); b[k] = 0;
                                  { uint8_t i; char t; for (i = 0; i < k / 2; i++) { t = b[i]; b[i] = b[k - 1 - i]; b[k - 1 - i] = t; } } rn_cat(b); }
static const char *rn_report(void)
{
    rn_msg[0] = 0;
    rn_cat("renumbered "); rn_catn(rn_n); rn_cat(rn_n == 1 ? " line" : " lines");
    if (rn_unres) { rn_cat(", "); rn_catn(rn_unres); rn_cat(" GOTO to no line kept"); }
    return rn_msg;
}
