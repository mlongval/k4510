/* K4510: WALL -- what was sent to this machine from outside, and the answer to it.
 *
 * Doc, 2026-09-18: "a WALL type program, but interactive, you can send me a text
 * notice on the command line, but can also ask for my reply.  Either multiple
 * choice or free text ... I can avoid having to switch between the k4510 and my
 * phone."
 *
 *   WALL          the messages waiting, oldest first, each answered as it asks
 *
 * A message is a file in /SYSTEM/WALL/INBOX, put there by tools/k4510-remote wall
 * (which also types WALL for you when the prompt is idle; otherwise it waits).
 * Its first line says what it wants and the rest is the text:
 *
 *   WALL N                 a notice: read it, a key goes on
 *   WALL T                 a line of free text is the answer
 *   WALL C one|two|three   one of these: its digit is the answer
 *
 * The answer goes to /SYSTEM/WALL/OUTBOX under the same name and the message is
 * deleted; the other end collects it.  Esc leaves a message where it is, for later.
 * Nothing here is in the ROM and nothing polls: the cost of WALL is WALL.  */
#include "k4510.h"

void __fastcall__ rom_chrout(unsigned char c);
unsigned char rom_getin(void);

#define FS        0xD300u
#define SHELL_RC  (*(volatile uint8_t *)0x03FF)

static const char inbox[] = "/SYSTEM/WALL/INBOX";
static char name[64];
static char out[96];                        /* /SYSTEM/WALL/OUTBOX/ + name */
static char msg[2048];
static char ans[200];
static char *opt[9];

static void fs_w32(uint8_t r, uint32_t v)
{
    REG(FS + r) = (uint8_t)v; REG(FS + r + 1) = (uint8_t)(v >> 8);
    REG(FS + r + 2) = (uint8_t)(v >> 16); REG(FS + r + 3) = (uint8_t)(v >> 24);
}
static uint8_t fs_cmd(uint8_t c) { REG(FS) = c; return REG(FS + 1); }
static uint8_t fs_name(uint8_t c, const char *n) { fs_w32(4, (uint16_t)n); return fs_cmd(c); }
static void say(const char *s) { while (*s) rom_chrout((uint8_t)*s++); }
static void sgr(const char *s) { rom_chrout(27); rom_chrout('['); say(s); rom_chrout('m'); }
static uint8_t key(void) { uint8_t k; do { k = rom_getin(); } while (!k); return k; }

static uint8_t is_txt(const char *n)
{
    uint8_t l = 0;
    while (n[l]) l++;
    return (uint8_t)(l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == 't' && (n[l - 2] | 32) == 'x' && (n[l - 1] | 32) == 't');
}
/* the oldest message's name into name[]: the listing is sorted and the names are stamps */
static uint8_t next_message(void)
{
    if (fs_cmd(6)) return 0;
    for (;;) {
        fs_w32(8, (uint16_t)name);
        if (fs_cmd(7)) return 0;
        if (is_txt(name)) return 1;
    }
}
static uint8_t read_line(void)              /* into ans[]; 0 = Esc */
{
    uint8_t n = 0, k;
    for (;;) {
        k = key();
        if (k == 27) return 0;
        if (k == 13) { if (n) break; continue; }
        if (k == 8) { if (n) { n--; rom_chrout(8); rom_chrout(' '); rom_chrout(8); } continue; }
        if ((k >= 0x20 && k < 0x7F) || (k >= 0x80 && !(REG(0xD101) & 0x40))) {   /* a character, not a KEY_* code (KBDST bit 6) */
            if (n < sizeof ans - 2) { ans[n++] = (char)k; rom_chrout(k); }
        }
    }
    ans[n++] = '\n'; ans[n] = 0;
    rom_chrout('\n');
    return n;
}

void main(void)
{
    uint8_t kind, nopt, i, k, n, shown = 0;
    uint16_t len; char *p, *body;

    if (fs_name(11, inbox)) { say("WALL: no messages\n"); return; }
    while (next_message()) {
        fs_w32(4, (uint16_t)name); fs_w32(8, (uint16_t)msg); fs_w32(12, sizeof msg - 1);
        k = fs_cmd(9);
        if (k && k != 6) break;                              /* 6: longer than the buffer -- what fitted is shown */
        len = (uint16_t)REG(FS + 12) | ((uint16_t)REG(FS + 13) << 8);
        if (len > sizeof msg - 1) len = sizeof msg - 1;
        msg[len] = 0;

        /* the first line: WALL N | WALL T | WALL C a|b|c.  Anything else is a notice, all of it */
        kind = 'N'; nopt = 0; body = msg;
        if (msg[0] == 'W' && msg[1] == 'A' && msg[2] == 'L' && msg[3] == 'L' && msg[4] == ' ') {
            kind = (uint8_t)msg[5];
            p = msg + 6;
            if (kind == 'C') {
                while (*p == ' ') p++;
                while (*p && *p != '\n' && nopt < 9) {
                    opt[nopt++] = p;
                    while (*p && *p != '\n' && *p != '|') p++;
                    if (*p == '|') *p++ = 0;
                }
            }
            while (*p && *p != '\n') p++;
            if (*p) *p++ = 0;
            body = p;
            if (kind == 'C' && !nopt) kind = 'N';
            if (kind != 'C' && kind != 'T') kind = 'N';
        }

        rom_chrout('\n'); sgr("7"); say(" WALL "); sgr("0"); rom_chrout(' '); say(name); rom_chrout('\n');
        say(body);
        if (len && msg[len - 1] != '\n') rom_chrout('\n');
        shown++;

        if (kind == 'C') {
            for (i = 0; i < nopt; i++) { say("  "); rom_chrout((uint8_t)('1' + i)); say("  "); say(opt[i]); rom_chrout('\n'); }
            say("1-"); rom_chrout((uint8_t)('0' + nopt)); say(", Esc later: ");
            do { k = key(); } while (k != 27 && !(k >= '1' && k < (uint8_t)('1' + nopt)));
            if (k == 27) { say("later\n"); break; }
            rom_chrout(k); rom_chrout('\n');
            n = 0; ans[n++] = (char)k; ans[n++] = ' ';
            for (p = opt[k - '1']; *p && n < sizeof ans - 2; p++) ans[n++] = *p;
            ans[n++] = '\n';
        } else if (kind == 'T') {
            say("(Esc later) > ");
            n = read_line();
            if (!n) { say("later\n"); break; }
        } else {
            say("-- a key (Esc later) --");
            k = key(); rom_chrout('\n');
            if (k == 27) break;
            ans[0] = 's'; ans[1] = 'e'; ans[2] = 'e'; ans[3] = 'n'; ans[4] = '\n'; n = 5;
        }

        /* the answer, then the message goes: in that order, so an answer is never lost to a crash between */
        p = out; { const char *d = "/SYSTEM/WALL/OUTBOX/"; while (*d) *p++ = *d++; }
        for (i = 0; name[i] && i < sizeof name - 1; i++) *p++ = name[i];
        *p = 0;
        fs_name(12, "/SYSTEM/WALL/OUTBOX");                   /* there already, usually: the status is not news */
        fs_w32(4, (uint16_t)out); fs_w32(8, (uint16_t)ans); fs_w32(12, n);
        if (fs_cmd(10)) { say("WALL: could not write the answer\n"); SHELL_RC = 1; break; }
        if (fs_name(13, name)) { say("WALL: could not remove the message\n"); SHELL_RC = 1; break; }
    }
    fs_cmd(22);                                               /* back to where we were */
    if (!shown) say("WALL: no messages\n");
}
