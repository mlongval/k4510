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
#include "../../core/io.h"                 /* io_audio_gaps: the machine's own underruns */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>

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
static volatile int want_on, skip_req, running, paused, forced;
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
static volatile int gain_pct = 150;               /* RADIO VOLUME, and the options file's gain=: a song is quieter than the OPL at full tilt, so a half again */
static volatile unsigned dropouts;                 /* samples wanted with the ring empty: what a crackle is, counted */
void navi_mix(int16_t *out, int n, int master_q15)   /* master_q15: the machine's own volume (F12 / the volume keys), 0..32768 */
{
    static const float coef[BANDS + 1] = { 0.90f, 0.55f, 0.30f, 0.16f, 0.085f, 0.045f, 0.024f, 0.012f, 0.006f };
    static float acc[BANDS]; static int cnt;
    /* Two clocks.  The song arrives at the server's pace, which is real time;
     * the machine takes samples at ITS 48 kHz, which runs a few percent off
     * real time (the CPU clock is a setting; the sound follows it).  Eaten
     * faster than it comes, the ring ran dry for a moment every minute or so
     * -- Doc's brainshot, 2026-09-17: "crackling".  So the ring is read
     * through a resampler that SLOWS when the ring runs low: down to 2% under
     * pitch at a quarter full, back to true at half.  Never faster: a full
     * ring only means the player is waiting, which is what it does.  A
     * listener cannot hear 2%; a listener can hear a gap. */
    static double phase, step = 1.0; static int16_t prev_s;
    if (!running || paused) return;
    if (skip_req) { ring_r = ring_w; phase = 0; }  /* a skip is heard NOW: whatever the player is still pushing of the old song is dropped here until it has let go of it */
    { unsigned fill = ring_w - ring_r; double want = fill >= RING_N / 2 ? 1.0 : fill <= RING_N / 4 ? 0.98 : 1.0 - 0.02 * ((double)(RING_N / 2 - fill) / (RING_N / 4)); step += (want - step) * 0.001; }
    for (int i = 0; i < n; i++) {
        int s = 0;
        phase += step;
        while (phase >= 1.0) { unsigned r = ring_r; if (r != ring_w) { prev_s = ring[r & (RING_N - 1)]; ring_r = r + 1; played++; } else { dropouts++; prev_s = (int16_t)(prev_s * 7 / 8); } phase -= 1.0; }
        s = prev_s;
        { int r = (int)((int64_t) s * gain_pct / 100 * master_q15 >> 15);   /* the radio's own gain, then the machine's volume: the keys govern it again */
          int v = out[i] + r; out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
        /* The one-poles, with a hair of DC so they never decay into denormal
         * floats: a denormal is a hundred times slower on x86, and this runs
         * on the emulation thread, once a sample -- the machine's own sound
         * starved and crackled (Doc, 2026-09-17: "VERY scratchy"). */
        { float x = (float) s + 1e-3f, prev = x;
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
    if (!strcmp(key, "gain")) { int g = atoi(value); if (g >= 10 && g <= 300) gain_pct = g; return; }
    if (!strcmp(key, "server")) { dst = o_server; max = sizeof o_server; }
    else if (!strcmp(key, "user")) { dst = o_user; max = sizeof o_user; }
    else if (!strcmp(key, "password")) { dst = o_pass; max = sizeof o_pass; }
    else if (!strcmp(key, "play")) {                                  /* the file's choice is the DEFAULT: it takes over only when the file changes, so RADIO's choice is not undone every second */
        static char file_play[64];
        if (!*value) value = "random";
        pthread_mutex_lock(&meta_mu);
        if (strcmp(file_play, value)) { snprintf(file_play, sizeof file_play, "%s", value); snprintf(o_play, sizeof o_play, "%s", value); opt_gen++; }
        pthread_mutex_unlock(&meta_mu);
        return;
    }
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
/* One JSON object's worth: from the '{' at p to its matching '}' */
static const char *obj_end(const char *p)
{
    int depth = 0, instr = 0;
    for (; *p; p++) {
        if (instr) { if (*p == '\\' && p[1]) p++; else if (*p == '"') instr = 0; continue; }
        if (*p == '"') instr = 1; else if (*p == '{') depth++; else if (*p == '}' && --depth == 0) return p;
    }
    return NULL;
}
/* the songs of one album, appended to the queue */
static void queue_album(const char *id)
{
    char url[700], ex[80], *js;
    snprintf(ex, sizeof ex, "id=%s", id);
    api_url(url, sizeof url, "getAlbum.view", ex);
    if ((js = fetch_text(url, 1024 * 1024))) { parse_songs(js, "song"); free(js); }   /* parse_songs starts the queue over; queue_album's callers keep what came before */
}
/* "album:NAME", "artist:NAME", "song:WORDS": Subsonic's search3, then the songs */
static int fill_by_search(const char *play, char *why, size_t whymax)
{
    char url[700], q[200], ex[300], *js; const char *what = strchr(play, ':') + 1; int n = 0;
    urlenc(what, q, sizeof q);
    snprintf(ex, sizeof ex, "query=%s&artistCount=%d&albumCount=%d&songCount=%d", q, !strncasecmp(play, "artist:", 7) ? 1 : 0, !strncasecmp(play, "album:", 6) ? 1 : 0, !strncasecmp(play, "song:", 5) ? 30 : 0);
    api_url(url, sizeof url, "search3.view", ex);
    if (!(js = fetch_text(url, 512 * 1024))) { snprintf(why, whymax, "no answer from the server"); return 0; }
    if (api_failed(js, why, whymax)) { free(js); return 0; }
    if (!strncasecmp(play, "song:", 5)) n = parse_songs(js, "song");
    else {
        char id[48] = ""; const char *key = !strncasecmp(play, "album:", 6) ? "\"album\":[" : "\"artist\":[", *p = strstr(js, key);
        if (p && (p = strchr(p, '{'))) { const char *e = obj_end(p); if (e) json_str(p, e, "id", id, sizeof id); }
        if (!id[0]) { snprintf(why, whymax, "the server knows no %.7s called %.40s", play, what); free(js); return 0; }
        if (!strncasecmp(play, "album:", 6)) { qn = qi = 0; queue_album(id); n = qn; }
        else {                                            /* an artist: their albums, first to last, until the queue is full */
            char aurl[700], aex[80], *ajs; snprintf(aex, sizeof aex, "id=%s", id);
            api_url(aurl, sizeof aurl, "getArtist.view", aex);
            if ((ajs = fetch_text(aurl, 512 * 1024))) {
                char ids[8][48]; int na = 0; const char *p2 = strstr(ajs, "\"album\":[");
                while (na < 8 && p2 && (p2 = strchr(p2, '{'))) { const char *e = obj_end(p2); if (!e) break; if (json_str(p2, e, "id", ids[na], sizeof ids[na])) na++; p2 = e + 1; if (*p2 == ']') break; }
                free(ajs);
                qn = qi = 0;
                for (int a = 0; a < na && qn < QMAX; a++) { int before = qn; song_t keep[QMAX]; memcpy(keep, queue, sizeof keep); queue_album(ids[a]); if (before) { int got = qn; if (before + got > QMAX) got = QMAX - before; memmove(queue + before, queue, (size_t) got * sizeof queue[0]); memcpy(queue, keep, (size_t) before * sizeof queue[0]); qn = before + got; } }
                n = qn;
            }
        }
    }
    free(js);
    if (!n) snprintf(why, whymax, "nothing to play for %.40s", what);
    return n;
}
static int fill_queue(void)
{
    char url[700], play[64], why[96], *js; int n = 0;
    pthread_mutex_lock(&meta_mu); snprintf(play, sizeof play, "%s", o_play); pthread_mutex_unlock(&meta_mu);
    if (!strncasecmp(play, "album:", 6) || !strncasecmp(play, "artist:", 7) || !strncasecmp(play, "song:", 5)) {
        n = fill_by_search(play, why, sizeof why);        /* RADIO ALBUM / ARTIST / SONG */
        if (!n) { set_note(why); return 0; }
    } else if (strcasecmp(play, "random")) {
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
        if (!got_any) dropouts = 0;                                  /* the silence before a song's first byte is not a dropout */
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
    if (forced > 0) on = 1;                                          /* RADIO ON / PLAY: it plays whether or not the sidebar is on the glass */
    if (forced < 0) on = 0;                                          /* RADIO OFF: and stays off even with the sidebar up, until RADIO PLAY (it said "off" and played on, 2026-09-17) */
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

/* ---- RADIO, the machine's command (core/io.c hands the line over) ----------- */
static void radio_line(char *reply, size_t max, const char *fmt, ...)
{
    size_t n = strlen(reply); va_list ap;
    if (n + 2 >= max) return;
    va_start(ap, fmt); vsnprintf(reply + n, max - n - 1, fmt, ap); va_end(ap);
    strcat(reply, "\n");
}
void navi_command(const char *cmd, char *reply, size_t max)
{
    char word[16], rest[128]; int i = 0;
    while (*cmd == ' ') cmd++;
    for (; *cmd && *cmd != ' ' && i < 15; cmd++) word[i++] = (char)((*cmd >= 'a' && *cmd <= 'z') ? *cmd - 32 : *cmd);
    word[i] = 0;
    while (*cmd == ' ') cmd++;
    snprintf(rest, sizeof rest, "%s", cmd);
    { size_t l = strlen(rest); while (l && rest[l - 1] == ' ') rest[--l] = 0; }
    reply[0] = 0;
    if (!word[0] || !strcmp(word, "STATUS") || !strcmp(word, "HELP")) {
        char title[96], artist[96], album[96], note[96]; int dur; unsigned el = played / 48000u;
        pthread_mutex_lock(&meta_mu);
        snprintf(title, sizeof title, "%s", meta.title); snprintf(artist, sizeof artist, "%s", meta.artist); snprintf(album, sizeof album, "%s", meta.album);
        snprintf(note, sizeof note, "%s", meta.note); dur = meta.duration;
        pthread_mutex_unlock(&meta_mu);
        if (!running) radio_line(reply, max, "the radio is off (RADIO PLAY starts it; or choose the Navidrome sidebar)");
        else if (title[0]) { radio_line(reply, max, "%s %s", paused ? "paused: " : "playing:", title); radio_line(reply, max, "         %s -- %s   %u:%02u/%d:%02d", artist, album, el / 60, el % 60, dur / 60, dur % 60); }
        if (note[0]) radio_line(reply, max, "%s", note);
        if (running) radio_line(reply, max, "volume %d%%   buffer %u%%   dropouts %u   machine sound gaps %u (since power-on: crackle that is not the radio's)", gain_pct, (ring_w - ring_r) * 100u / RING_N, dropouts, (unsigned) io_audio_gaps);
        if (!word[0] || !strcmp(word, "HELP")) {
            radio_line(reply, max, "RADIO PLAY            start (random songs, or what PLAY last chose)");
            radio_line(reply, max, "RADIO PLAY name       a playlist    RADIO ALBUM name    RADIO ARTIST name");
            radio_line(reply, max, "RADIO SONG words      songs whose names match   RADIO SEARCH words  look only");
            radio_line(reply, max, "RADIO NEXT / PAUSE / RESUME / OFF / VOLUME n     (Ctrl+Alt+N is also NEXT)");
        }
        return;
    }
    if (!strcmp(word, "VOLUME") || !strcmp(word, "VOL") || !strcmp(word, "GAIN")) {
        int g = atoi(rest);
        if (g < 10 || g > 300) { radio_line(reply, max, "RADIO VOLUME 10..300 (percent; 100 is the song as it is; the machine's own volume is on top)"); return; }
        gain_pct = g; radio_line(reply, max, "volume %d%%", g); return;
    }
    if (!strcmp(word, "NEXT")) { if (!running) { radio_line(reply, max, "the radio is off"); return; } skip_req = 1; radio_line(reply, max, "next"); return; }
    if (!strcmp(word, "PAUSE")) { paused = 1; radio_line(reply, max, "paused"); return; }
    if (!strcmp(word, "RESUME")) { paused = 0; radio_line(reply, max, "playing"); return; }
    if (!strcmp(word, "OFF") || !strcmp(word, "STOP")) { forced = -1; paused = 0; navi_active(0); radio_line(reply, max, "off"); return; }
    if (!strcmp(word, "ON")) { forced = 1; paused = 0; navi_active(1); radio_line(reply, max, "on"); return; }
    if (!strcmp(word, "PLAY") || !strcmp(word, "ALBUM") || !strcmp(word, "ARTIST") || !strcmp(word, "SONG")) {
        char want[96];
        if (!strcmp(word, "PLAY")) snprintf(want, sizeof want, "%s", rest[0] ? rest : "");
        else snprintf(want, sizeof want, "%s:%s", !strcmp(word, "ALBUM") ? "album" : !strcmp(word, "ARTIST") ? "artist" : "song", rest);
        if (strcmp(word, "PLAY") && !rest[0]) { radio_line(reply, max, "RADIO %s needs a name", word); return; }
        if (want[0]) { pthread_mutex_lock(&meta_mu); snprintf(o_play, sizeof o_play, "%s", want); opt_gen++; pthread_mutex_unlock(&meta_mu); }   /* a new choice: the player starts over on it */
        forced = 1; paused = 0; skip_req = running; navi_active(1);
        radio_line(reply, max, want[0] ? "asking the server for %s" : "playing", rest[0] ? rest : "random songs");
        return;
    }
    if (!strcmp(word, "SEARCH")) {
        char url[700], q[200], ex[200], *js, why[96];
        if (!rest[0]) { radio_line(reply, max, "RADIO SEARCH needs words"); return; }
        urlenc(rest, q, sizeof q);
        snprintf(ex, sizeof ex, "query=%s&artistCount=3&albumCount=5&songCount=6", q);
        api_url(url, sizeof url, "search3.view", ex);
        if (!(js = fetch_text(url, 512 * 1024))) { radio_line(reply, max, "no answer from the server (set server, user and password in the sidebar's options)"); return; }
        if (api_failed(js, why, sizeof why)) { radio_line(reply, max, "%s", why); free(js); return; }
        { static const struct { const char *key, *label, *namekey; } kinds[3] = { { "\"artist\":[", "artist", "name" }, { "\"album\":[", "album ", "name" }, { "\"song\":[", "song  ", "title" } };
          int any = 0;
          for (int k = 0; k < 3; k++) {
              const char *p = strstr(js, kinds[k].key);
              while (p && (p = strchr(p, '{'))) {
                  const char *e = obj_end(p); char nm[80], by[80]; if (!e) break;
                  json_str(p, e, kinds[k].namekey, nm, sizeof nm); if (!json_str(p, e, "artist", by, sizeof by)) by[0] = 0;
                  radio_line(reply, max, "%s  %.40s%s%.30s", kinds[k].label, nm, by[0] ? " -- " : "", by); any = 1;
                  p = e + 1; if (*p == ']') break;
              }
          }
          if (!any) radio_line(reply, max, "nothing matches %s", rest); }
        free(js);
        return;
    }
    radio_line(reply, max, "RADIO: no such word as %s (RADIO alone lists them)", word);
}
