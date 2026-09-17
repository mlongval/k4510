/* snd_k4510.c -- [K4510] DOOM's sound effects, on the machine's PCM DAC.
 *
 * Doc, 2026-09-17: "next can we add the sound effects and the engine".
 *
 * DOOM's effects are DMX lumps: eight header bytes, then unsigned 8-bit
 * samples at 11025 Hz (a few at 22050).  The K4510's design has always had a
 * DigiMAX beside the OPL2 -- four 8-bit DACs, "built-in, always present"
 * (docs/K4510-Design.md, A-09) -- and until today it was a line in a table.
 * Now it is fitted (core/digimax.c), and unsigned 8-bit at 11 kHz is as near
 * to that chip's native tongue as a sound format gets.
 *
 * So, as with the music: nothing here reaches a sound card.  Up to eight of
 * DOOM's channels are mixed to ONE stream of unsigned bytes at 11025 Hz, a
 * thread keeps about 50 ms of it ahead in a ring in the shared segment, and
 * the emulator clocks those bytes into DAC 0 at 11025 a second.  The machine
 * is mono, so separation is heard as nothing; volume is honoured.
 *
 * This replaces i_sdlsound.c, which is not built: it wants SDL_mixer, and the
 * Tube's co-processor has no SDL.  The mixing it did is thirty lines.
 */
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "doomtype.h"
#include "deh_str.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

/* doomgeneric_k4510.c owns the shared segment */
int  k4510_pcm_room(void);                 /* how many more samples the ring will take */
int  k4510_pcm_fill(void);                 /* how many are waiting in it */
void k4510_pcm_push(const uint8_t *s, int n);

#define K4_RATE     11025
#define K4_CHANNELS 8
#define K4_LEAD     (K4_RATE / 20)         /* keep 50 ms ahead: under that a late thread is heard, over it the shotgun lags the trigger */

typedef struct {
    const uint8_t *data;                   /* the samples, past the lump's header; NULL when idle */
    uint32_t len, pos, step;               /* pos and step in 16.16: a 22050 Hz lump steps two samples for one */
    int vol;                               /* 0..127 */
} k4_chan_t;

static k4_chan_t chans[K4_CHANNELS];
static pthread_mutex_t mix_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t mix_thread;
static volatile int mix_running;
static boolean sfx_prefix;

static void *MixThread(void *unused)
{
    (void) unused;
    while (mix_running) {
        uint8_t block[256];
        int want = K4_LEAD - k4510_pcm_fill(), room = k4510_pcm_room();
        if (want > room) want = room;
        if (want > (int) sizeof block) want = (int) sizeof block;
        if (want > 0) {
            pthread_mutex_lock(&mix_mutex);
            for (int i = 0; i < want; i++) {
                int acc = 0;
                for (int c = 0; c < K4_CHANNELS; c++) {
                    k4_chan_t *ch = &chans[c];
                    if (!ch->data) continue;
                    if ((ch->pos >> 16) >= ch->len) { ch->data = NULL; continue; }
                    acc += ((int) ch->data[ch->pos >> 16] - 128) * ch->vol;
                    ch->pos += ch->step;
                }
                /* One sound at full volume fills the DAC; more than one clips at
                 * the rails, which is what DMX's own 8-bit mixing did.  The first
                 * version halved this for headroom, and at DOOM's default effects
                 * volume (8 of 15) a pistol shot then peaked at 32 of 127 -- under
                 * the music, and Doc had to ask whether the effects played at all. */
                acc /= 127;
                if (acc > 127) acc = 127; else if (acc < -128) acc = -128;
                block[i] = (uint8_t)(acc + 128);
            }
            pthread_mutex_unlock(&mix_mutex);
            k4510_pcm_push(block, want);
        }
        { struct timespec t = { 0, 4000000L }; nanosleep(&t, NULL); }     /* 4 ms: ~44 samples a turn */
    }
    return NULL;
}

static boolean K4_Init(boolean use_sfx_prefix)
{
    sfx_prefix = use_sfx_prefix;
    memset(chans, 0, sizeof chans);
    mix_running = 1;
    if (pthread_create(&mix_thread, NULL, MixThread, NULL) != 0) { mix_running = 0; return false; }
    return true;
}
static void K4_Shutdown(void)
{
    if (!mix_running) return;
    mix_running = 0;
    pthread_join(mix_thread, NULL);
}

static int K4_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[9];
    if (sfx->link != NULL) sfx = sfx->link;
    if (sfx_prefix) M_snprintf(name, sizeof name, "ds%s", DEH_String(sfx->name));
    else M_StringCopy(name, DEH_String(sfx->name), sizeof name);
    return W_GetNumForName(name);
}

static void K4_Update(void) { }

static void K4_UpdateSoundParams(int channel, int vol, int sep)
{
    (void) sep;                            /* one speaker */
    if (channel < 0 || channel >= K4_CHANNELS) return;
    pthread_mutex_lock(&mix_mutex);
    chans[channel].vol = vol < 0 ? 0 : vol > 127 ? 127 : vol;
    pthread_mutex_unlock(&mix_mutex);
}

/* A DMX sound lump: 03 00, the rate, the length (32 bits), then the samples
 * with 16 bytes of padding before and after that DMX never played. */
static int K4_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    int lump, lumplen; const uint8_t *d; uint32_t rate, len;
    (void) sep;
    if (channel < 0 || channel >= K4_CHANNELS) return -1;
    lump = sfx->lumpnum;
    lumplen = W_LumpLength(lump);
    d = W_CacheLumpNum(lump, PU_STATIC);   /* PU_STATIC and never released: the mixer thread reads it, and the zone must not move it under us */
    if (lumplen < 8 || d[0] != 0x03 || d[1] != 0x00) return -1;
    rate = (uint32_t) d[2] | (uint32_t) d[3] << 8;
    len = (uint32_t) d[4] | (uint32_t) d[5] << 8 | (uint32_t) d[6] << 16 | (uint32_t) d[7] << 24;
    if (len > (uint32_t)(lumplen - 8) || len <= 48 || rate < 4000) return -1;
    pthread_mutex_lock(&mix_mutex);
    chans[channel].data = d + 8 + 16;      /* past the header and the leading pad */
    chans[channel].len = len - 32;
    chans[channel].pos = 0;
    chans[channel].step = (uint32_t)(((uint64_t) rate << 16) / K4_RATE);
    chans[channel].vol = vol < 0 ? 0 : vol > 127 ? 127 : vol;
    pthread_mutex_unlock(&mix_mutex);
    return channel;
}

static void K4_StopSound(int channel)
{
    if (channel < 0 || channel >= K4_CHANNELS) return;
    pthread_mutex_lock(&mix_mutex);
    chans[channel].data = NULL;
    pthread_mutex_unlock(&mix_mutex);
}

static boolean K4_SoundIsPlaying(int channel)
{
    boolean r;
    if (channel < 0 || channel >= K4_CHANNELS) return false;
    pthread_mutex_lock(&mix_mutex);
    r = chans[channel].data != NULL;
    pthread_mutex_unlock(&mix_mutex);
    return r;
}

static void K4_CacheSounds(sfxinfo_t *sounds, int num_sounds) { (void) sounds; (void) num_sounds; }

static snddevice_t k4_devices[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32 };

sound_module_t sound_k4510_module =
{
    k4_devices,
    sizeof k4_devices / sizeof k4_devices[0],
    K4_Init,
    K4_Shutdown,
    K4_GetSfxLumpNum,
    K4_Update,
    K4_UpdateSoundParams,
    K4_StartSound,
    K4_StopSound,
    K4_SoundIsPlaying,
    K4_CacheSounds,
};
