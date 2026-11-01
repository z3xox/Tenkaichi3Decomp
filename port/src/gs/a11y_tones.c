/*
 * Accessibility: the fight's sounds (port/src/a11y_fight.c decides when; this makes and plays them), and the two
 * cues of a transformation and the Ultimate Blast within reach (a11y_game.c, through a11y_fight.c), which a11y.c made
 * before: one synthesiser for all of them.
 *
 *   distance   a short bright ping (A6, 60 ms, a click first), repeated: a11y_fight.c spaces the pings by the
 *              distance to the rival (closer, faster) and gives each its pitch by the rival's height (above, higher;
 *              below, lower), as Street Fighter 6's distance sound; within the game's own close range two in a row
 *   edge       a gust of noise between 250 and 600 Hz, repeated faster as the fighter nears the stage's wall; a high
 *              hiss (4 to 8 kHz) for its ceiling, a low rumble for its floor
 *   ring       two low knocks: out over the grass of the World Tournament ring, where landing loses
 *   health     two beeps, high then low (E6, C6), as the low-health alarm of the Pokémon games
 *   ki         three quick notes going up (G6, B6, D7): the ki is full; a fourth higher, max power
 *   where      a sonar ping (A5) from where the rival is (Select in a fight: PortA11y_ToneFrom)
 *   cues       a tone that slides up (a transformation more within reach) and a small bell struck twice (G5: the
 *              Ultimate Blast)
 *
 * Each family has a band of its own, away from the others' notes (port/tests/a11y_tones renders them all and
 * port/tools/a11y_check.py checks that no two share a note): the fight's music, voices and blows fill the middle,
 * where all of these were before, and a tick of 25 ms there was a click lost in the mix. The stereo is kept for where
 * a sound is: whose it is goes by its timbre. Yours (and the ones that are nobody's) are clean; the rival's are the
 * same notes with odd harmonics over them, a buzz. What tells of the rival is placed by the rival's direction (from
 * the left to the right, and muffled behind you; a11y_fight.c decides when): the "where" ping, the distance tick and
 * the rival's own sounds (health, ki, the two cues), which keep their buzz behind too (muffle_rival). Yours stay in
 * the middle. The game's Mono setting puts everything in the middle.
 *
 * Made here from sine waves and noise (nothing of the game's sounds), on a stream of their own for each kind (they
 * can sound together), at the cues' volume (a11y_cue_volume, a loudness scale: a11y.c). Only integers come from
 * a11y_fight.c: it is built soft-float.
 */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern SDL_AudioDeviceID Port_AudioDevice(void); /* snd_adx.c */
extern int Port_AudioMono(void);
extern int PortA11y_CueVolume(void);             /* a11y.c: a11y_cue_volume */
extern int PortA11y_On(void);
extern void PortA11y_Note(const char *text);

#define TONE_RATE 48000
#define TONE_MAX (TONE_RATE * 450 / 1000) /* the longest sound (the bell) */
#define TONE_MS (TONE_RATE / 1000)
#define TONE_TAU 6.2831853f

/* (the same list in a11y_fight.c and a11y_glossary.c; the two cues after the kinds a11y_fight.c plays itself) */
enum {
    TONE_DISTANCE, TONE_DISTANCE_CLOSE, TONE_EDGE_WALL, TONE_EDGE_TOP, TONE_EDGE_FLOOR, TONE_RING, TONE_HEALTH, TONE_KI,
    TONE_WHERE, TONE_CUE_TRANSFORM, TONE_CUE_ULTIMATE, TONE_KINDS
};
#define TONE_STREAMS (TONE_CUE_TRANSFORM + 1) /* the two cues share one, so that two at once sound one after the other */
#define TIMBRE_CLEAN 0
#define TIMBRE_BRIGHT 1 /* octave and twelfth over it */
#define TIMBRE_BUZZ 2   /* odd harmonics: the rival's */

static SDL_AudioStream *sStream[TONE_STREAMS];
static int sTried;

/* The gain for the cues' volume (percent): a loudness scale, 3 dB each tenth, from -30 dB at 0 % (which is silence)
   to the full scale at 100 %. The default 60 % is -12 dB, as loud as a tick was at 60 % when the scale was a plain
   fraction of the amplitude; that one gave only 4.4 dB from 60 % to 100 %, this one 12. */
float PortA11y_CueGainAt(int percent) {
    return percent <= 0 ? 0.0f : powf(10.0f, (float)((percent > 100 ? 100 : percent) - 100) * 0.3f / 20.0f);
}

/* One wave of `freq` at phase x (cycles), in a timbre. */
static float wave_at(float x, int timbre) {
    float s = sinf(TONE_TAU * x);

    if (timbre == TIMBRE_BRIGHT) {
        s += 0.3f * sinf(TONE_TAU * 2.0f * x) + 0.12f * sinf(TONE_TAU * 3.0f * x);
    } else if (timbre == TIMBRE_BUZZ) {
        s = 0.8f * (s + sinf(TONE_TAU * 3.0f * x) / 3.0f + sinf(TONE_TAU * 5.0f * x) / 5.0f + sinf(TONE_TAU * 7.0f * x) / 7.0f);
    }
    return s;
}

/* A note of `freq` Hz from sample `at`, `len` samples long: in over 3 ms, out over the last 12, added to out. */
static void add_note(float *out, int at, int len, float freq, float amp, int timbre) {
    int i, edge = 3 * TONE_MS;

    for (i = 0; i < len && at + i < TONE_MAX; i++) {
        float env = i < edge ? (float)i / edge : len - i < edge * 4 ? (float)(len - i) / (edge * 4) : 1.0f;
        out[at + i] += amp * env * wave_at(freq * (float)i / TONE_RATE, timbre);
    }
}

/* The same noise every time (the sounds are the same each time they play, and the test renders them alike). */
static uint32_t sNoise;
static float noise(void) {
    sNoise = sNoise * 1664525u + 1013904223u;
    return (float)(int32_t)sNoise / 2147483648.0f;
}

/* A ping: a click of noise (2 ms, its highs only), then the note held 20 ms and dying away by `len`. */
static void add_ping(float *out, int at, int len, float freq, float amp, int timbre) {
    int i, click = 2 * TONE_MS, hold = 20 * TONE_MS;
    float last = 0.0f, tau = (float)(len - hold) / 3.4f; /* (-30 dB at the end) */

    sNoise = 0x2545F491u;
    for (i = 0; i < len && at + i < TONE_MAX; i++) {
        float env = i < TONE_MS ? (float)i / TONE_MS : i < hold ? 1.0f : expf(-(float)(i - hold) / tau);
        float s = env * wave_at(freq * (float)i / TONE_RATE, timbre);
        if (len - i < 3 * TONE_MS) {
            s *= (float)(len - i) / (3 * TONE_MS);
        }
        if (i < click) {
            float n = noise();
            s += 0.7f * (n - last) * (1.0f - (float)i / click); /* (the difference of two: a click without lows) */
            last = n;
        }
        out[at + i] += amp * s;
    }
}

/* Noise through a band-pass (two of RBJ's, constant peak gain) from lo to hi Hz, `len` samples, in over 8 ms and out
   over 25, scaled to `amp` at its loudest. */
static void add_band(float *out, int at, int len, float lo, float hi, float amp) {
    static float tmp[TONE_MAX];
    float fc = sqrtf(lo * hi), q = fc / (hi - lo), w = TONE_TAU * fc / TONE_RATE, alpha = sinf(w) / (2.0f * q);
    float b0 = alpha / (1.0f + alpha), b2 = -b0, a1 = -2.0f * cosf(w) / (1.0f + alpha), a2 = (1.0f - alpha) / (1.0f + alpha);
    float x1[2] = {0}, x2[2] = {0}, y1[2] = {0}, y2[2] = {0}, peak = 0.0f;
    int i, k, pre = 20 * TONE_MS; /* (the filters run a little before the sound starts: no swell at its start) */

    if (len > TONE_MAX - at) {
        len = TONE_MAX - at;
    }
    sNoise = 0x9E3779B9u;
    for (i = -pre; i < len; i++) {
        float s = noise();
        for (k = 0; k < 2; k++) {
            float y = b0 * s + b2 * x2[k] - a1 * y1[k] - a2 * y2[k];
            x2[k] = x1[k];
            x1[k] = s;
            y2[k] = y1[k];
            y1[k] = y;
            s = y;
        }
        if (i >= 0) {
            float env = i < 8 * TONE_MS ? (float)i / (8 * TONE_MS) : len - i < 25 * TONE_MS ? (float)(len - i) / (25 * TONE_MS) : 1.0f;
            tmp[i] = s * env;
            peak = fabsf(tmp[i]) > peak ? fabsf(tmp[i]) : peak;
        }
    }
    for (i = 0; i < len && peak > 0.0f; i++) {
        out[at + i] += amp * tmp[i] / peak;
    }
}

/* A knock: a low note dying fast, with a click over it so that it is heard over the music's bass. */
static void add_knock(float *out, int at, float freq, float amp) {
    int i, len = 70 * TONE_MS;
    float last = 0.0f;

    sNoise = 0x7F4A7C15u;
    for (i = 0; i < len && at + i < TONE_MAX; i++) {
        float x = (float)i / TONE_RATE, env = (i < TONE_MS ? (float)i / TONE_MS : 1.0f) * expf(-x / 0.025f);
        float s = env * wave_at(freq * x, TIMBRE_BRIGHT);
        if (i < 3 * TONE_MS) {
            float n = noise();
            s += 0.5f * (n - last) * (1.0f - (float)i / (3 * TONE_MS));
            last = n;
        }
        out[at + i] += amp * s;
    }
}

/* The transformation's cue: a tone that slides up from E4 to B5 (three times its pitch) over 240 ms and holds a
   moment, like powering up. */
static int cue_transform(float *out, int timbre) {
    int len = 320 * TONE_MS, glide = 240 * TONE_MS, i;
    float x = 0.0f, f, s;

    for (i = 0; i < len; i++) {
        f = 329.63f * powf(3.0f, (float)(i < glide ? i : glide) / glide);
        x += f / TONE_RATE; /* (the phase is summed: the pitch moves) */
        x -= x >= 1.0f ? 1.0f : 0.0f;
        s = 0.7f * wave_at(x, timbre == TIMBRE_BUZZ ? TIMBRE_BUZZ : TIMBRE_BRIGHT);
        s *= (i < 5 * TONE_MS ? (float)i / (5 * TONE_MS) : 1.0f) * (len - i < 50 * TONE_MS ? (float)(len - i) / (50 * TONE_MS) : 1.0f);
        out[i] += s;
    }
    return len;
}

/* The Ultimate Blast's cue: a small bell, G5, struck twice; its partials (1, 2.76 and 5.40 times the note) fall
   away, the high ones faster. The rival's has the buzz on its first partial. */
static int cue_ultimate(float *out, int timbre) {
    int len = 420 * TONE_MS, i, k, t;
    float x, s;

    for (i = 0; i < len; i++) {
        s = 0.0f;
        for (k = 0; k < 2; k++) {
            if ((t = i - k * 120 * TONE_MS) < 0) {
                continue;
            }
            x = (float)t / TONE_RATE;
            s += (t < 5 * TONE_MS ? (float)t / (5 * TONE_MS) : 1.0f) *
                 (expf(-9.0f * x) * wave_at(784.0f * x, timbre == TIMBRE_BUZZ ? TIMBRE_BUZZ : TIMBRE_CLEAN) +
                  0.35f * expf(-18.0f * x) * sinf(TONE_TAU * 2163.8f * x) + 0.15f * expf(-30.0f * x) * sinf(TONE_TAU * 4233.6f * x));
        }
        s *= 0.6f * (len - i < 50 * TONE_MS ? (float)(len - i) / (50 * TONE_MS) : 1.0f);
        out[i] += s;
    }
    return len;
}

/* The wave of a kind, -1..1 at most, into out (TONE_MAX samples); returns its length. `pitch` in semitones from the
   kind's own note; `who` 1: the rival's timbre. */
static int tone_wave(int kind, int who, int pitch, float *out) {
    float move = powf(2.0f, (float)pitch / 12.0f);
    int timbre = who == 1 ? TIMBRE_BUZZ : TIMBRE_CLEAN, len = 0;

    memset(out, 0, sizeof(float) * TONE_MAX);
    switch (kind) {
    case TONE_DISTANCE: /* a ping of 60 ms */
        add_ping(out, 0, 60 * TONE_MS, 1760.0f * move, 0.75f, TIMBRE_BRIGHT);
        len = 60 * TONE_MS;
        break;
    case TONE_DISTANCE_CLOSE: /* two, 70 ms apart (a11y_fight.c spaces these 150 ms at least) */
        add_ping(out, 0, 60 * TONE_MS, 1760.0f * move, 0.75f, TIMBRE_BRIGHT);
        add_ping(out, 70 * TONE_MS, 60 * TONE_MS, 1760.0f * move, 0.75f, TIMBRE_BRIGHT);
        len = 130 * TONE_MS;
        break;
    case TONE_EDGE_WALL: /* 90 ms: shorter than the closest spacing, 100 ms */
        add_band(out, 0, 90 * TONE_MS, 250.0f, 600.0f, 0.9f);
        len = 90 * TONE_MS;
        break;
    case TONE_EDGE_TOP:
        add_band(out, 0, 90 * TONE_MS, 4000.0f, 8000.0f, 0.7f);
        len = 90 * TONE_MS;
        break;
    case TONE_EDGE_FLOOR: /* (not played yet: the fight has no floor alert) */
        add_band(out, 0, 90 * TONE_MS, 35.0f, 70.0f, 0.9f);
        len = 90 * TONE_MS;
        break;
    case TONE_RING: /* two low knocks, D3 and A2 */
        add_knock(out, 0, 146.83f, 0.6f);
        add_knock(out, 110 * TONE_MS, 110.0f, 0.6f);
        len = 180 * TONE_MS;
        break;
    case TONE_HEALTH: /* E6 then C6, 90 ms each, 40 ms apart */
        add_note(out, 0, 90 * TONE_MS, 1318.5f, 0.8f, timbre);
        add_note(out, 130 * TONE_MS, 90 * TONE_MS, 1046.5f, 0.8f, timbre);
        len = 220 * TONE_MS;
        break;
    case TONE_KI: /* G6, B6, D7 (moved by pitch: max power a fourth higher), 60 ms each, touching */
        add_note(out, 0, 60 * TONE_MS, 1568.0f * move, 0.7f, timbre);
        add_note(out, 60 * TONE_MS, 60 * TONE_MS, 1975.5f * move, 0.7f, timbre);
        add_note(out, 120 * TONE_MS, 90 * TONE_MS, 2349.3f * move, 0.7f, timbre);
        len = 210 * TONE_MS;
        break;
    case TONE_WHERE: /* a sonar ping, A5, ringing 180 ms */
        add_ping(out, 0, 180 * TONE_MS, 880.0f * move, 0.85f, TIMBRE_BRIGHT);
        len = 180 * TONE_MS;
        break;
    case TONE_CUE_TRANSFORM:
        len = cue_transform(out, timbre);
        break;
    case TONE_CUE_ULTIMATE:
        len = cue_ultimate(out, timbre);
        break;
    }
    return len;
}

/* The muffle's start softened over 8 ms and its faint echo 40 ms after. */
static void soften(float *w, int len) {
    int i, echo = 40 * TONE_MS;

    for (i = 0; i < len && i < 8 * TONE_MS; i++) {
        w[i] *= (float)i / (8 * TONE_MS);
    }
    for (i = len - 1; i >= echo; i--) {
        w[i] += 0.3f * w[i - echo];
    }
}

/* Behind: the same sound muffled (two low-passes at 1.2 kHz), its start softened and a faint echo 40 ms after, as
   heard from the back. */
static void muffle(float *w, int len) {
    float a = 1.0f - expf(-TONE_TAU * 1200.0f / TONE_RATE), y1 = 0.0f, y2 = 0.0f;
    int i;

    for (i = 0; i < len; i++) {
        y1 += a * (w[i] - y1);
        y2 += a * (y1 - y2);
        w[i] = y2 * 1.6f;
    }
    soften(w, len);
}

/* Behind, a sound of the rival's (w, its wave): the notes muffled as any other's, and the buzz (what the rival's has
   over the same sound of yours) over them, softened and echoed alike but not low-passed, at the level the muffle
   leaves the notes. The low-pass alone takes the buzz away (the notes are at 1 to 3 kHz, the buzz is their 3rd to
   7th harmonics): measured by the spectral centroid, the rival's health beeps behind came out as bright as yours in
   front (1151 Hz against 1182) and would be taken for yours. This way each of the rival's sounds behind is 9 to 17 %
   duller than in front and 16 to 28 % brighter than yours. */
static void muffle_rival(int kind, int pitch, float *w, int len) {
    static float body[TONE_MAX];
    float before = 0.0f, after = 0.0f, k;
    int i;

    if (tone_wave(kind, 0, pitch, body) != len) {
        muffle(w, len);
        return;
    }
    for (i = 0; i < len; i++) {
        w[i] -= body[i]; /* (the buzz alone) */
        before += body[i] * body[i];
    }
    muffle(body, len);
    for (i = 0; i < len; i++) {
        after += body[i] * body[i];
    }
    k = before > 0.0f ? sqrtf(after / before) : 0.0f;
    soften(w, len);
    for (i = 0; i < len; i++) {
        w[i] = body[i] + k * w[i];
    }
}

/* The left and right gains of a direction (hundredths of a degree, 0 ahead, + right, +-18000 behind): a power-keeping
   pan, the back mirrored onto the front (behind is told by the muffle), the far ear never under a tenth; the middle
   is 1 and 1, as an unplaced sound. In Mono, the middle. */
static void pan_of(int az, float *l, float *r) {
    float a = (float)(az % 36000) / 100.0f, p, th;

    a = a > 180.0f ? a - 360.0f : a < -180.0f ? a + 360.0f : a;
    p = fabsf(a) <= 90.0f ? a / 90.0f : (a > 0.0f ? 180.0f - a : -180.0f - a) / 90.0f;
    th = (p + 1.0f) * 0.785398163f;
    *l = 1.41421356f * cosf(th);
    *r = 1.41421356f * sinf(th);
    *l = *l > 1.0f ? 1.0f : *l < 0.1f ? 0.1f : *l;
    *r = *r > 1.0f ? 1.0f : *r < 0.1f ? 0.1f : *r;
    if (Port_AudioMono()) {
        *l = *r = 1.0f;
    }
}

/* 1 behind (more than 100 degrees from ahead). */
static int behind(int az) {
    int a = az % 36000;

    a = a > 18000 ? a - 36000 : a < -18000 ? a + 36000 : a;
    return a > 10000 || a < -10000;
}

/* The sound of a kind as it is played, 16-bit stereo: `who` 1 the rival's timbre; at `az` (hundredths of a degree)
   when `placed`, else in the middle; `gain` the volume, 0..1. Into out (TONE_MAX frames); returns the frames. */
static int tone_pcm(int kind, int who, int pitch, int placed, int az, float gain, int16_t *out) {
    static float wave[TONE_MAX];
    float l = 1.0f, r = 1.0f, a, b;
    int len, i;

    pitch = pitch < -24 ? -24 : pitch > 24 ? 24 : pitch;
    len = tone_wave(kind, who, pitch, wave);
    if (placed) {
        pan_of(az, &l, &r);
        if (behind(az)) {
            if (who == 1) {
                muffle_rival(kind, pitch, wave, len);
            } else {
                muffle(wave, len);
            }
        }
    }
    {
        float peak = 0.0f; /* every sound peaks 1 dB under the full scale at the full volume: the same loudness to start from */
        for (i = 0; i < len; i++) {
            peak = fabsf(wave[i]) > peak ? fabsf(wave[i]) : peak;
        }
        gain *= peak > 0.0f ? 0.89f / peak : 0.0f;
    }
    for (i = 0; i < len; i++) {
        a = wave[i] * gain * l;
        b = wave[i] * gain * r;
        out[i * 2] = (int16_t)((a > 1.0f ? 1.0f : a < -1.0f ? -1.0f : a) * 32767.0f);
        out[i * 2 + 1] = (int16_t)((b > 1.0f ? 1.0f : b < -1.0f ? -1.0f : b) * 32767.0f);
    }
    return len;
}

static void play_locked(int kind, int who, int pitch, int loud, int placed, int az);

/* (the game's thread plays the fight's sounds, the settings window and the glossary's timer theirs: one at a time
   through the buffers) */
static void play(int kind, int who, int pitch, int loud, int placed, int az) {
    static SDL_SpinLock lock;

    if (!PortA11y_On() || kind < 0 || kind >= TONE_KINDS || loud <= 0) {
        return;
    }
    SDL_LockSpinlock(&lock);
    play_locked(kind, who, pitch, loud, placed, az);
    SDL_UnlockSpinlock(&lock);
}

static void play_locked(int kind, int who, int pitch, int loud, int placed, int az) {
    static int16_t pcm[TONE_MAX * 2];
    SDL_AudioDeviceID dev;
    SDL_AudioSpec spec;
    float gain;
    int len, k;

    if ((gain = PortA11y_CueGainAt(PortA11y_CueVolume())) <= 0.0f) {
        return;
    }
    if (!sTried) {
        static int noted;
        char note[160];
        if ((dev = Port_AudioDevice()) == 0) {
            if (!noted++) {
                PortA11y_Note("tones: no sound device yet");
            }
            return; /* (no sound device, or none yet) */
        }
        sTried = 1;
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = TONE_RATE;
        for (k = 0; k < TONE_STREAMS; k++) {
            if ((sStream[k] = SDL_CreateAudioStream(&spec, NULL)) != NULL && !SDL_BindAudioStream(dev, sStream[k])) {
                SDL_DestroyAudioStream(sStream[k]);
                sStream[k] = NULL;
            }
        }
        snprintf(note, sizeof(note), "tones: on sound device %u, %s", (unsigned)dev, sStream[0] != NULL ? "ready" : SDL_GetError());
        PortA11y_Note(note);
    }
    k = kind >= TONE_CUE_TRANSFORM ? TONE_CUE_TRANSFORM : kind;
    if (sStream[k] == NULL) {
        return;
    }
    gain *= (float)(loud > 100 ? 100 : loud) / 100.0f;
    {
        /* (the first of each kind, yours and the rival's apart, goes to the log: that the fight asked for it and it
           was played, and from where) */
        static int played;
        int bit = 1 << (kind * 2 + (who == 1));
        if (!(played & bit)) {
            char note[160], from[40] = "in the middle";
            played |= bit;
            if (placed) {
                snprintf(from, sizeof(from), "from %+d degrees%s", az / 100, behind(az) ? ", muffled" : "");
            }
            snprintf(note, sizeof(note), "tones: first of kind %d played (%s, loud %d, gain %.1f dB, %s%s)", kind,
                     who == 1 ? "the rival's" : "yours", loud, 20.0f * log10f(gain), from, Port_AudioMono() ? ", mono" : "");
            PortA11y_Note(note);
        }
    }
    len = tone_pcm(kind, who, pitch, placed, az, gain, pcm);
    /* A sound of the same kind still waiting for more than half of the new one is dropped: the newest says the most;
       a shorter tail is let finish, not cut mid-wave. Not the cues: each of those is something to hear. */
    if (kind < TONE_CUE_TRANSFORM && SDL_GetAudioStreamQueued(sStream[k]) > len * 2) {
        SDL_ClearAudioStream(sStream[k]);
    }
    SDL_PutAudioStreamData(sStream[k], pcm, len * 4);
}

/* Plays a sound in the middle: kind as above; `who` 1 the rival's (its timbre), else yours or nobody's; pitch in
   semitones; loud in percent of the cues' volume. */
void PortA11y_Tone(int kind, int who, int pitch, int loud) {
    play(kind, who, pitch, loud, 0, 0);
}

/* Plays a sound from a direction: az in hundredths of a degree from ahead (+ to the right, +-18000 behind). */
void PortA11y_ToneAt(int kind, int az, int pitch, int loud) {
    play(kind, 0, pitch, loud, 1, az);
}

/* The same with whose it is: `who` 1 the rival's (its timbre, kept behind too). For a11y_fight.c: the rival's own
   sounds from where the rival is; kind may be a cue's (TONE_CUE_TRANSFORM + n), played as PortA11y_Cue plays it. */
void PortA11y_ToneFrom(int kind, int who, int az, int pitch, int loud) {
    play(kind, who, pitch, loud, 1, az);
}

/* A cue for a11y_fight.c (the game's thread): kind 0, one transformation more can be taken; kind 1, the Ultimate
   Blast can be fired. `who` 1: the rival's. Two at the same time sound one after the other. */
void PortA11y_Cue(int who, int kind) {
    if ((unsigned)kind <= 1) {
        play(TONE_CUE_TRANSFORM + kind, who, 0, 100, 0, 0);
    }
}

/* For port/tests/a11y_tones (no game, no sound device): the 16-bit stereo frames of a sound at the full volume, `who`
   and `placed` / `az` as above, into out (TONE_MAX frames); returns the frames. */
int PortA11y_TonePcm(int kind, int who, int pitch, int placed, int az, int16_t *out) {
    if (kind < 0 || kind >= TONE_KINDS) {
        return 0;
    }
    return tone_pcm(kind, who, pitch, placed, az, 1.0f, out);
}

int PortA11y_ToneMax(void) {
    return TONE_MAX;
}
