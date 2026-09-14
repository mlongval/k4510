/* test/renumtest.c -- demo/renum.h on the host: VI's :renum and EDIT's
 * Ctrl-R share it, so it is tested once, here, in the three dialects. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static unsigned tab[40000];
static void tput(unsigned i, unsigned v) { tab[i] = v; }
static unsigned tget(unsigned i) { return tab[i]; }
#define RN_TAB_PUT tput
#define RN_TAB_GET tget
#include "../demo/renum.h"

static int fails;

/* run name's lines through the VI flow; expect either the lines or a refusal */
static void check(const char *what, const char *name, const char *const *in, int n,
                  unsigned start, unsigned step, const char *const *want, const char *wantmsg)
{
    uint8_t l[256], o[256]; int i; const char *m;
    rn_begin();
    for (i = 0; i < n; i++) { l[0] = (uint8_t) strlen(in[i]); memcpy(l + 1, in[i], l[0]); rn_scan(l); }
    m = rn_check(rn_lang_of(name), start, step);
    if (m || wantmsg) {
        if (!m || !wantmsg || strcmp(m, wantmsg)) { printf("FAIL %s: refusal \"%s\", wanted \"%s\"\n", what, m ? m : "(none)", wantmsg ? wantmsg : "(none)"); fails++; }
        else printf("ok   %s: %s\n", what, m);
        return;
    }
    for (i = 0; i < n; i++) {
        l[0] = (uint8_t) strlen(in[i]); memcpy(l + 1, in[i], l[0]);
        if (!rn_line(l, o)) { printf("FAIL %s: line %d too long\n", what, i); fails++; return; }
        o[o[0] + 1] = 0;
        if (strcmp((char *) o + 1, want[i])) { printf("FAIL %s: line %d\n  got  %s\n  want %s\n", what, i, (char *) o + 1, want[i]); fails++; return; }
    }
    printf("ok   %s: %s\n", what, rn_report());
}

int main(void)
{
    static const char *const ms[] = {
        "5 PRINT \"GOTO 5\"", "7 IF A THEN 20", "20 ON X GOTO 5, 7,20:GOSUB 30", "30 REM GOTO 5",
        "35 RESTORE 20:DATA 5,7", "40 GOTO 99", "50 ELSE 5", "60 if a then 7" };
    static const char *const msw[] = {
        "100 PRINT \"GOTO 5\"", "110 IF A THEN 120", "120 ON X GOTO 100, 110,120:GOSUB 130", "130 REM GOTO 5",
        "140 RESTORE 120:DATA 5,7", "150 GOTO 99", "160 ELSE 5", "170 if a then 110" };
    static const char *const bbc[] = {
        "   10 IF x THEN 30 ELSE 20", "   20 goto=5:PRINT goto", "   30 GOTO 10", "   40 PRINT \"THEN 10" };
    static const char *const bbcw[] = {
        "   100 IF x THEN 110 ELSE 105", "   105 goto=5:PRINT goto", "   110 GOTO 100", "   115 PRINT \"THEN 10" };
    static const char *const order[] = { "20 A", "10 B" };
    static const char *const two[] = { "10 A", "20 GOTO 10" };
    static const char *const twow[] = { "65270 A", "65279 GOTO 65270" };
    static const char *const none[] = { "PRINT 1" };

    check("EhBASIC / MS BASIC", "PROG.BAS", ms, 8, 100, 10, msw, NULL);
    check("BBC BASIC", "EDITTMP.BBC", bbc, 4, 100, 5, bbcw, NULL);
    check("out of order", "X.BAS", order, 2, 10, 10, NULL, "renum: line numbers out of order");
    check("past the top", "X.BAS", two, 2, 63990, 10, NULL, "renum: past line 63999");
    check("BBC's top", "X.BBC", two, 2, 65270, 9, twow, NULL);
    check("LOGO", "SQUARE.LGO", two, 2, 10, 10, NULL, "renum: LOGO has no line numbers");
    check("Pascal", "HELLO.PAS", two, 2, 10, 10, NULL, "renum: not a BASIC file (.BAS, .BBC)");
    check("no numbers", "X.BAS", none, 1, 10, 10, NULL, "renum: no numbered lines");
    printf(fails ? "renumtest: %d FAILED\n" : "renumtest: all passed\n", fails);
    return fails != 0;
}
