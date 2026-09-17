#define _GNU_SOURCE                              /* strcasestr */
/* sdl/sidebars/navidrome.c -- a radio beside the machine: the sidebar that plays
 * music from a Navidrome server.
 *
 * Doc, 2026-09-17: "If you look at the apps running on the server you will find
 * a Navidrome server, I would like a side bar that can connect to that server
 * and play music on the k4510."
 *
 * It is the first sidebar that does something rather than only showing
 * something, so it is in two halves.
 *
 * THE PLAYER is a thread that lives while the sidebar is on the glass.  It
 * speaks Subsonic, which is Navidrome's API: asks for songs (a playlist by
 * name, or random ones), asks for each as an MP3 -- the SERVER transcodes, so
 * a FLAC library costs this machine nothing -- reads it from `curl`, which
 * every K4510 Linux has because the storage device's URLs use it, decodes with
 * minimp3 (one public-domain header: sdl/thirdparty), and resamples to the
 * machine's 48 kHz into a ring.  The frontend adds that ring to the machine's
 * own sound as it is made (navi_mix, beside the volume), so the F12 volume and
 * the volume keys govern it, and a paused machine pauses the music.
 *
 * It does NOT go through MELODY or the DigiMAX, and that is deliberate.  A
 * sidebar is not part of the machine -- it is drawn by the frontend in the
 * space beside the picture, and no program can see it -- so this is a radio
 * standing next to the computer, not a peripheral of it.  Eight bits at 11 kHz
 * would be the honest price of pretending otherwise.
 *
 * THE SCENE shows what is playing in the machine's own letters, how far
 * through it is, and a spectrum taken from the samples actually being heard.
 * With nothing to play -- no options yet, or the tests -- it idles, animated,
 * and says what it is waiting for.
 *
 * Options (/SYSTEM/SIDEBARS/NAVIDROME/OPTIONS.CFG; F12 -> Video -> Edit
 * options): server, user, password, play.  The password is sent as Subsonic's
 * `p=enc:` form, so the server should be reached by https, as Doc's is; it
 * reaches curl on its standard input, never on a command line (curl_open).
 * Make the server a user of its own for this.
 */
#include "canvas.h"
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "../thirdparty/minimp3.h"

/* ---- shared between the player thread and the frontend ------------------- */
#define RING_N (1u << 17)                         /* 2.7 s at 48 kHz: the player runs ahead, the network stutters, nobody hears */
static int16_t ring[RING_N];
static volatile unsigned ring_w, ring_r;          /* one producer (the player), one consumer (navi_mix) */

static pthread_mutex_t meta_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { char title[96], artist[96], album[96], note[96]; int duration; } meta;   /* under meta_mu */
static volatile unsigned played;                  /* samples of this song heard so far */
static volatile int want_on, skip_req, running;
static pthread_t th;
static char o_server[128], o_user[64], o_pass[64], o_play[64] = "random";               /* under meta_mu */
static volatile int opt_gen;                      /* bumped when an option changes: the player starts over */

/* the spectrum: eight one-pole low-passes in a row; a band is the difference
 * between two neighbours.  Cheap enough to run on every sample. */
#define BANDS 8
static float lp[BANDS + 1];
static volatile int level[BANDS];                 /* 0..255, decaying */

static void set_note(const char *s) { pthread_mutex_lock(&meta_mu); snprintf(meta.note, sizeof meta.note, "%s", s); pthread_mutex_unlock(&meta_mu); }

/* ---- the frontend's side -------------------------------------------------- */
void navi_mix(int16_t *out, int n)
{
    static const float coef[BANDS + 1] = { 0.90f, 0.55f, 0.30f, 0.16f, 0.085f, 0.045f, 0.024f, 0.012f, 0.006f };
    static float acc[BANDS]; static int cnt;
    if (!running) return;
    if (skip_req) ring_r = ring_w;                 /* a skip is heard NOW: whatever the player is still pushing of the old song is dropped here until it has let go of it */
    for (int i = 0; i < n; i++) {
        unsigned r = ring_r;
        int s = 0;
        if (r != ring_w) { s = ring[r & (RING_N - 1)]; ring_r = r + 1; played++; }
        { int v = out[i] + s * 3 / 4; out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }   /* under the machine's own sound, not over it */
        { float x = (float) s, prev = x;
          for (int k = 0; k <= BANDS; k++) { lp[k] += coef[k] * (x - lp[k]); if (k) { float d = prev - lp[k]; acc[k - 1] += d < 0 ? -d : d; } prev = lp[k]; } }
        if (++cnt >= 1024) {
            for (int k = 0; k < BANDS; k++) {
                int v = (int)(acc[k] / 1024.0f / 7.0f * (float)(k + 2)); int old = level[k] * 7 / 8;   /* the top bands are quiet: lean on them */
                if (v > 255) v = 255;
                level[k] = v > old ? v : old; acc[k] = 0;
            }
            cnt = 0;
        }
    }
}
void navi_next(void) { skip_req = 1; }

void navi_option(const char *key, const char *value)
{
    char *dst = NULL; size_t max = 0;
    if (!key) return;
    if (!value) value = "";
    if (!strcmp(key, "server")) { dst = o_server; max = sizeof o_server; }
    else if (!strcmp(key, "user")) { dst = o_user; max = sizeof o_user; }
    else if (!strcmp(key, "password")) { dst = o_pass; max = sizeof o_pass; }
    else if (!strcmp(key, "play")) { dst = o_play; max = sizeof o_play; if (!*value) value = "random"; }
    if (!dst) return;
    pthread_mutex_lock(&meta_mu);
    if (strcmp(dst, value)) { snprintf(dst, max, "%s", value); opt_gen++; }
    pthread_mutex_unlock(&meta_mu);
}

/* ---- the player ----------------------------------------------------------- */
static void hexenc(const char *s, char *out, size_t max) { size_t n = 0; for (; *s && n + 3 < max; s++) n += (size_t) snprintf(out + n, max - n, "%02x", (unsigned char) *s); out[n] = 0; }
static void urlenc(const char *s, char *out, size_t max)
{
    size_t n = 0;
    for (; *s && n + 4 < max; s++) {
        unsigned char ch = (unsigned char) *s;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.') out[n++] = (char) ch;
        else n += (size_t) snprintf(out + n, max - n, "%%%02X", ch);
    }
    out[n] = 0;
}
/* "https://server/rest/WHAT?auth&EXTRA" */
static void api_url(char *out, size_t max, const char *what, const char *extra)
{
    char u[200], p[140], srv[128]; size_t l;
    pthread_mutex_lock(&meta_mu);
    snprintf(srv, sizeof srv, "%s", o_server); urlenc(o_user, u, sizeof u); hexenc(o_pass, p, sizeof p);
    pthread_mutex_unlock(&meta_mu);
    l = strlen(srv); while (l && srv[l - 1] == '/') srv[--l] = 0;
    snprintf(out, max, "%s/rest/%s?u=%s&p=enc:%s&v=1.16.1&c=k4510&f=json%s%s", srv, what, u, p, extra && *extra ? "&" : "", extra ? extra : "");
}
/* The URL carries the password, so it must not be on curl's command line,
 * where `ps` shows it to anybody.  curl takes it as a CONFIG on its standard
 * input instead (-K -), which the shell feeds from a here-document; the
 * environment variable that carries it that far is gone again the moment the
 * shell has started, so a `!` shell or a Tube program begun later never
 * inherits it. */
static FILE *curl_open(const char *url, int seconds)
{
    char cmd[256]; FILE *f;
    setenv("K4510_NAVI_URL", url, 1);
    snprintf(cmd, sizeof cmd, "curl -sL --fail --max-time %d -K - 2>/dev/null <<K4510EOF\nurl = \"$K4510_NAVI_URL\"\nK4510EOF", seconds);
    f = popen(cmd, "r");
    unsetenv("K4510_NAVI_URL");
    return f;
}
static char *fetch_text(const char *url, size_t cap)
{
    FILE *f = curl_open(url, 20); char *b; size_t n = 0, k;
    if (!f) return NULL;
    if (!(b = malloc(cap + 1))) { pclose(f); return NULL; }
    while (n < cap && (k = fread(b + n, 1, cap - n, f)) > 0) n += k;
    b[n] = 0; pclose(f);
    if (!n) { free(b); return NULL; }
    return b;
}
/* "key":"value" inside the object that starts at p; the value un-escaped enough to read */
static int json_str(const char *p, const char *end, const char *key, char *out, size_t max)
{
    char pat[48]; const char *q; size_t n = 0;
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    out[0] = 0;
    if (!(q = strstr(p, pat)) || q >= end) return 0;
    for (q += strlen(pat); *q && *q != '"' && q < end && n + 1 < max; q++) {
        if (*q == '\\' && q[1]) { q++; if (*q == 'u' && q[1] && q[2] && q[3] && q[4]) { out[n++] = '?'; q += 4; continue; } }
        out[n++] = ((unsigned char) *q < 0x80) ? *q : '?';           /* the panel's font is CP437: a likeness, not a lookup */
        if ((unsigned char) *q >= 0xC0) while (((unsigned char) q[1] & 0xC0) == 0x80) q++;
    }
    out[n] = 0;
    return 1;
}
static int json_int(const char *p, const char *end, const char *key)
{
    char pat[48]; const char *q;
    snprintf(pat, sizeof pat, "\"%s\":", key);
    return (q = strstr(p, pat)) && q < end ? atoi(q + strlen(pat)) : 0;
}

typedef struct { char id[48], title[96], artist[96], album[96]; int duration; } song_t;
#define QMAX 40
static song_t queue[QMAX]; static int qn, qi;

static int parse_songs(const char *json, const char *arraykey)
{
    char pat[32]; const char *p;
    qn = qi = 0;
    snprintf(pat, sizeof pat, "\"%s\":[", arraykey);
    if (!(p = strstr(json, pat))) return 0;
    for (p += strlen(pat); qn < QMAX && (p = strchr(p, '{')); ) {
        const char *e = p; int depth = 0, instr = 0;
        for (; *e; e++) {                                         /* the end of this object, minding strings */
            if (instr) { if (*e == '\\' && e[1]) e++; else if (*e == '"') instr = 0; continue; }
            if (*e == '"') instr = 1; else if (*e == '{') depth++; else if (*e == '}' && --depth == 0) break;
        }
        if (!*e) break;
        if (json_str(p, e, "id", queue[qn].id, sizeof queue[qn].id)) {
            json_str(p, e, "title", queue[qn].title, sizeof queue[qn].title);
            json_str(p, e, "artist", queue[qn].artist, sizeof queue[qn].artist);
            json_str(p, e, "album", queue[qn].album, sizeof queue[qn].album);
            queue[qn].duration = json_int(p, e, "duration");
            qn++;
        }
        p = e + 1;
        if (*p == ']') break;
    }
    return qn;
}
static int api_failed(const char *json, char *why, size_t max)
{
    if (!strstr(json, "\"status\":\"failed\"")) return 0;
    if (!json_str(json, json + strlen(json), "message", why, max)) snprintf(why, max, "the server refused");
    return 1;
}
/* fill the queue: a playlist by name, or random songs */
static int fill_queue(void)
{
    char url[700], play[64], why[96], *js; int n = 0;
    pthread_mutex_lock(&meta_mu); snprintf(play, sizeof play, "%s", o_play); pthread_mutex_unlock(&meta_mu);
    if (strcasecmp(play, "random")) {
        api_url(url, sizeof url, "getPlaylists.view", "");
        if ((js = fetch_text(url, 256 * 1024))) {
            char pat[96], id[48] = ""; const char *p = js;
            if (api_failed(js, why, sizeof why)) { set_note(why); free(js); return 0; }
            snprintf(pat, sizeof pat, "\"name\":\"%s\"", play);
            while ((p = strchr(p, '{'))) {                           /* each playlist object: is it the one? */
                const char *e = strchr(p + 1, '}'); if (!e) break;
                { const char *hit = strcasestr(p, pat); if (hit && hit < e) { json_str(p, e, "id", id, sizeof id); break; } }
                p = e + 1;
            }
            free(js);
            if (id[0]) {
                char ex[80]; snprintf(ex, sizeof ex, "id=%s", id);
                api_url(url, sizeof url, "getPlaylist.view", ex);
                if ((js = fetch_text(url, 1024 * 1024))) { n = parse_songs(js, "entry"); free(js); }
            } else { char m[96]; snprintf(m, sizeof m, "no playlist called %.40s", play); set_note(m); return 0; }
        }
    } else {
        api_url(url, sizeof url, "getRandomSongs.view", "size=30");
        if ((js = fetch_text(url, 512 * 1024))) {
            if (api_failed(js, why, sizeof why)) { set_note(why); free(js); return 0; }
            n = parse_songs(js, "song"); free(js);
        }
    }
    if (!n) set_note("no answer from the server");
    return n;
}
static int stop_now(int gen) { return !want_on || skip_req || gen != opt_gen; }

static void play_song(const song_t *s, int gen)
{
    static mp3dec_t dec; static uint8_t in[32 * 1024]; static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    char url[700], ex[120]; FILE *f; size_t have = 0; int eof = 0; double pos = 0; int got_any = 0;
    snprintf(ex, sizeof ex, "id=%s&format=mp3&maxBitRate=192", s->id);
    api_url(url, sizeof url, "stream.view", ex);
    if (!(f = curl_open(url, 3600))) { set_note("cannot start curl"); return; }
    pthread_mutex_lock(&meta_mu);
    snprintf(meta.title, sizeof meta.title, "%s", s->title); snprintf(meta.artist, sizeof meta.artist, "%s", s->artist);
    snprintf(meta.album, sizeof meta.album, "%s", s->album); meta.duration = s->duration; meta.note[0] = 0;
    pthread_mutex_unlock(&meta_mu);
    played = 0;
    mp3dec_init(&dec);
    while (!stop_now(gen)) {
        mp3dec_frame_info_t info; int n;
        if (!eof && have < sizeof in / 2) { size_t k = fread(in + have, 1, sizeof in - have, f); if (!k) eof = 1; have += k; }
        if (!have) break;
        n = mp3dec_decode_frame(&dec, in, (int) have, pcm, &info);
        if (info.frame_bytes <= 0) { if (eof) break; if (have == sizeof in) have = 0; continue; }   /* no frame in sight: more, or give up on a bufferful of noise */
        memmove(in, in + info.frame_bytes, have - (size_t) info.frame_bytes); have -= (size_t) info.frame_bytes;
        if (n <= 0 || info.hz <= 0) continue;
        got_any = 1;
        { double step = (double) info.hz / 48000.0;                  /* to the machine's rate: a straight line between neighbours */
          while (pos < n - 1) {
              int i0 = (int) pos; double fr = pos - i0; int a, b;
              if (info.channels > 1) { a = (pcm[i0 * 2] + pcm[i0 * 2 + 1]) / 2; b = (pcm[i0 * 2 + 2] + pcm[i0 * 2 + 3]) / 2; } else { a = pcm[i0]; b = pcm[i0 + 1]; }
              while (ring_w - ring_r >= RING_N - 1) { if (stop_now(gen)) goto out; usleep(5000); }   /* full: this wait is what paces the download to the playing */
              ring[ring_w & (RING_N - 1)] = (int16_t)(a + (b - a) * fr);
              __sync_synchronize(); ring_w = ring_w + 1;
              pos += step;
          }
          pos -= n - 1; if (pos < 0) pos = 0; }
    }
out:
    pclose(f);                                                       /* curl sees the pipe close and goes */
    if (!got_any && !stop_now(gen)) { set_note("the server sent no music for that one"); sleep(2); }
    skip_req = 0;                                                    /* navi_mix has been emptying the ring since the key: the next song starts clean */
}
static void *player(void *unused)
{
    (void) unused;
    while (want_on) {
        int gen = opt_gen, ready;
        pthread_mutex_lock(&meta_mu); ready = o_server[0] && o_user[0] && o_pass[0]; pthread_mutex_unlock(&meta_mu);
        if (!ready) { set_note("set server, user and password: F12, Video, Edit options"); sleep(1); continue; }
        if (qi >= qn) { set_note("asking the server..."); if (!fill_queue()) { for (int i = 0; i < 10 && want_on && gen == opt_gen; i++) sleep(1); continue; } }
        if (gen != opt_gen) { qn = qi = 0; continue; }
        play_song(&queue[qi++], gen);
        if (gen != opt_gen) qn = qi = 0;
        while (want_on && !skip_req && gen == opt_gen && ring_w != ring_r && qi >= qn) usleep(20000);   /* the last of a queue plays out before the next is asked for */
    }
    ring_r = ring_w;
    running = 0;
    return NULL;
}
/* the frontend, once a second: is this sidebar on the glass? */
void navi_active(int on)
{
    if (on && !running) { want_on = 1; running = 1; if (pthread_create(&th, NULL, player, NULL)) { running = 0; want_on = 0; } else pthread_detach(th); }
    else if (!on && want_on) want_on = 0;                            /* the thread notices, empties the ring and ends */
}

/* ---- the scene ------------------------------------------------------------- */
static void glyph(cv_t *c, int x, int y, unsigned char ch, uint32_t v)
{
    const uint8_t *gl;
    if (!c->font || c->frows <= 0) return;
    gl = c->font + (unsigned) ch * (unsigned) c->frows;
    for (int gy = 0; gy < c->frows; gy++) for (int gx = 0; gx < 8; gx++) if (gl[gy] & (0x80 >> gx)) pset(c, x + gx, y + gy, v);
}
/* text wrapped to the strip, at most `lines` of it; returns the y after it */
static int words(cv_t *c, int x, int y, const char *s, uint32_t v, int lines)
{
    int cols = (c->w - 2 * x) / 8, fh = c->frows > 0 ? c->frows : 16;
    if (cols < 4 || !c->font) return y;
    while (*s && lines-- > 0) {
        int n = (int) strlen(s), take = n > cols ? cols : n;
        if (n > cols) { int k = take; while (k > cols / 2 && s[k] != ' ') k--; if (s[k] == ' ') take = k; }   /* break at a space when there is one in the back half */
        for (int i = 0; i < take; i++) glyph(c, x + i * 8, y, (unsigned char) s[i], (lines == 0 && n > take && i >= take - 2) ? RGB(120, 130, 150) : v);
        s += take; while (*s == ' ') s++;
        y += fh + 1;
    }
    return y;
}
void s_navidrome(cv_t *c, uint32_t t, int side)
{
    int w = c->w, h = c->h, k = scale_of(w), playing, dur, fh = c->frows > 0 ? c->frows : 16;
    char title[96], artist[96], album[96], note[96];
    unsigned el = played / 48000u;
    pthread_mutex_lock(&meta_mu);
    snprintf(title, sizeof title, "%s", meta.title); snprintf(artist, sizeof artist, "%s", meta.artist);
    snprintf(album, sizeof album, "%s", meta.album); snprintf(note, sizeof note, "%s", meta.note); dur = meta.duration;
    pthread_mutex_unlock(&meta_mu);
    playing = running && ring_w != ring_r;

    vgrad(c, 0, h, RGB(8, 10, 22), RGB(20, 12, 40));                  /* a hi-fi's dark glass */
    for (int y = 0; y < h; y += 3) rectb(c, 0, y, w, 1, RGB(0, 0, 0), 40);

    /* the bars: the music's own bands when there is music, a slow swell when
     * there is not (so the scene is alive in the tests, and while it waits) */
    { int bw = (w - 6 * k) / BANDS, base = h - 6 * k, top = side ? h / 8 : h * 56 / 100, span = base - top;
      if (bw < 1) bw = 1;
      for (int b = 0; b < BANDS; b++) {
          int bi = side ? BANDS - 1 - b : b;
          int lv = playing ? level[bi] : 40 + (isin((int)(t / 9) + bi * 140 + side * 300) + 256) * 60 / 512;
          int bh = span * lv / 255, x = 3 * k + b * bw;
          for (int y = 0; y < bh; y += 3 * k) {
              int f = y * 256 / (span > 0 ? span : 1);
              uint32_t v = f < 140 ? mix(RGB(40, 220, 120), RGB(230, 220, 60), f * 256 / 140) : mix(RGB(230, 220, 60), RGB(240, 60, 40), (f - 140) * 256 / 116);
              rect(c, x, base - y - 2 * k, bw - k, 2 * k, playing ? v : mix(v, RGB(20, 12, 40), 150));
          }
      } }
    if (side) { glow(c, w / 2, h / 12, w / 3 + 2, playing ? RGB(60, 200, 120) : RGB(90, 90, 120), 60 + (isin((int)(t / 5)) + 256) / 12); return; }

    /* the left panel: what is playing */
    { int x = 3 * k, y = 4 * k;
      y = words(c, x, y, "NAVIDROME", RGB(120, 200, 255), 1);
      rect(c, x, y + 1, w - 2 * x, 1, RGB(60, 90, 140)); y += 5;
      if (title[0] && running) {
          y = words(c, x, y, title, RGB(255, 255, 255), 4) + 3;
          y = words(c, x, y, artist, RGB(255, 210, 120), 3) + 3;
          y = words(c, x, y, album, RGB(150, 160, 190), 3) + 5;
          { int bwid = w - 2 * x, done = dur > 0 ? (int)((unsigned) bwid * (el > (unsigned) dur ? (unsigned) dur : el) / (unsigned) dur) : 0; char tm[24];
            rect(c, x, y, bwid, 3 * k, RGB(30, 36, 60)); rect(c, x, y, done, 3 * k, RGB(80, 200, 140)); y += 3 * k + 3;
            snprintf(tm, sizeof tm, "%u:%02u/%d:%02d", el / 60, el % 60, dur / 60, dur % 60); y = words(c, x, y, tm, RGB(150, 160, 190), 1); }
      }
      if (note[0] || !running) words(c, x, y + 3, note[0] ? note : "a radio beside the machine: choose me in F12 and I play", RGB(255, 170, 90), 6);
      (void) fh; }
}
