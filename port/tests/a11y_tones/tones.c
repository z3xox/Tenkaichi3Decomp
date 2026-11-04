/* The accessibility's sounds (port/src/gs/a11y_tones.c) on their own, without the game or a sound device: each one is
 * rendered as it is played at the full cues' volume and measured, one line of text each, for
 * port/tools/a11y_check.py ("tones"), which checks that they are loud and long enough and that no two kinds share a
 * note. Optionally each is written as a WAV file too, to listen to.
 *
 *   clang -O1 -I/usr/local/include port/tests/a11y_tones/tones.c -lSDL3 -lm -o a11y_tones_test   (the release
 *   container has SDL3 there: port/release/Dockerfile)
 *   a11y_tones_test [wav folder] > tones.txt
 *   python3 port/tools/a11y_check.py tones tones.txt
 *
 * Lines (all numbers plain, a full stop for decimals):
 *   sound <name> kind <k> who <w> ms <duration> peak <dBFS> rms <dBFS> attack_ms <to 3 dB under the peak>
 *         main <Hz of the strongest peak> centroid <Hz> band <share of the energy, 0..1, in the kind's band or 0>
 *         notes <Hz,Hz,...: the frequencies that stay put 30 ms or more (notes_of); "-" none; for the sounds of
 *         noise (a band given) not looked for>
 *   place <az> left <dB> right <dB> centroid <Hz> mono <0|1>      the "where" ping from a direction
 *   gain <percent> <dB>                                            the cues' volume scale
 */
#include "../../src/gs/a11y_tones.c"
#include <stdlib.h>

/* What a11y_tones.c asks of the rest of the program. */
static int sMonoTest;
SDL_AudioDeviceID Port_AudioDevice(void) { return 0; }
int Port_AudioMono(void) { return sMonoTest; }
int PortA11y_CueVolume(void) { return 100; }
int PortA11y_On(void) { return 1; }
void PortA11y_Note(const char *text) { (void)text; }

#define N 32768 /* the transform's length: 1.46 Hz a bin at 48 kHz */

static void fft(float *re, float *im, int n) {
    int i, j, k, len;

    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2.0 * 3.14159265358979323846 / len;
        for (i = 0; i < n; i += len) {
            for (k = 0; k < len / 2; k++) {
                float wr = (float)cos(a * k), wi = (float)sin(a * k);
                float xr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                float xi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k + len / 2] = re[i + k] - xr;
                im[i + k + len / 2] = im[i + k] - xi;
                re[i + k] += xr;
                im[i + k] += xi;
            }
        }
    }
}

static float sRe[N], sIm[N], sPow[N / 2];

/* The power spectrum of one channel of the frames (the sound whole, its ends tapered over 2 ms). */
static void spectrum(const int16_t *pcm, int frames, int ch) {
    int i, n = frames > N ? N : frames, taper = 2 * TONE_MS;

    for (i = 0; i < N; i++) {
        float w = i >= n ? 0.0f : i < taper ? (float)i / taper : n - i < taper ? (float)(n - i) / taper : 1.0f;
        sRe[i] = i < n ? (float)pcm[i * 2 + ch] / 32768.0f * w : 0.0f;
        sIm[i] = 0.0f;
    }
    fft(sRe, sIm, N);
    for (i = 0; i < N / 2; i++) {
        sPow[i] = sRe[i] * sRe[i] + sIm[i] * sIm[i];
    }
}

static float hz(int bin) { return (float)bin * TONE_RATE / N; }

/* The notes of a sound: frequencies that stay put for 30 ms or more. In frames of 43 ms (every 5 ms), the peaks
   within 6 dB of the frame's strongest (frames 25 dB under the loudest are left out); a peak whose frequency is found
   again, within a sixth of a semitone, in the frames of the next 30 ms is a note. A glide moves through a note in less
   than that, noise has no peak that stays, and harmonics are under the 6 dB. Into notes[] (Hz); returns how many. */
#define SG_FRAME 2048
#define SG_FFT 8192
#define SG_HOP 256
#define SG_PEAKS 16
static int notes_of(const int16_t *pcm, int frames, float *notes, int cap) {
    static float pk[TONE_MAX / SG_HOP + 1][SG_PEAKS], energy[TONE_MAX / SG_HOP + 1];
    static int npk[TONE_MAX / SG_HOP + 1];
    int nf = 0, f, i, j, k, n = 0, run = (30 * TONE_MS + SG_HOP - 1) / SG_HOP;
    float loudest = 0.0f;

    for (f = 0; f * SG_HOP < frames && f <= TONE_MAX / SG_HOP; f++, nf++) {
        float max = 0.0f, e = 0.0f;
        for (i = 0; i < SG_FFT; i++) {
            int at = f * SG_HOP + i - SG_FRAME / 2; /* (a frame centred on its hop) */
            float w = i < SG_FRAME ? 0.5f - 0.5f * cosf(TONE_TAU * (float)i / (SG_FRAME - 1)) : 0.0f;
            sRe[i] = i < SG_FRAME && at >= 0 && at < frames ? (float)pcm[at * 2] / 32768.0f * w : 0.0f;
            sIm[i] = 0.0f;
        }
        fft(sRe, sIm, SG_FFT);
        for (i = 0; i < SG_FFT / 2; i++) {
            sPow[i] = sRe[i] * sRe[i] + sIm[i] * sIm[i];
            max = sPow[i] > max ? sPow[i] : max;
            e += sPow[i];
        }
        energy[f] = e;
        loudest = e > loudest ? e : loudest;
        npk[f] = 0;
        for (i = 2; i < SG_FFT / 2 - 2 && npk[f] < SG_PEAKS; i++) {
            if (sPow[i] >= max * 0.251f && sPow[i] >= sPow[i - 1] && sPow[i] > sPow[i + 1]) {
                /* the peak's frequency between its bins (a parabola through the three in dB) */
                float a = 10.0f * log10f(sPow[i - 1] + 1e-20f), b = 10.0f * log10f(sPow[i] + 1e-20f), c = 10.0f * log10f(sPow[i + 1] + 1e-20f);
                float d = a - 2.0f * b + c != 0.0f ? 0.5f * (a - c) / (a - 2.0f * b + c) : 0.0f;
                pk[f][npk[f]++] = ((float)i + d) * TONE_RATE / SG_FFT;
            }
        }
    }
    for (f = 0; f + run <= nf; f++) {
        if (energy[f] < loudest * 0.00316f) {
            continue;
        }
        for (k = 0; k < npk[f]; k++) {
            float hzk = pk[f][k];
            int ok = 1, dup = 0;
            for (j = f + 1; j < f + run && ok; j++) {
                int found = 0, m;
                for (m = 0; m < npk[j] && !found; m++) {
                    found = fabsf(pk[j][m] - hzk) <= hzk * 0.0097f;
                }
                ok = found && energy[j] >= loudest * 0.00316f;
            }
            for (j = 0; j < n && !dup; j++) {
                dup = fabsf(notes[j] - hzk) <= notes[j] * 0.0297f;
            }
            if (ok && !dup && n < cap) {
                notes[n++] = hzk;
            }
        }
    }
    return n;
}

static float centroid(void) {
    double sum = 0.0, w = 0.0;
    int i;

    for (i = 1; i < N / 2; i++) {
        sum += sPow[i] * hz(i);
        w += sPow[i];
    }
    return w > 0.0 ? (float)(sum / w) : 0.0f;
}

/* The share of the energy between lo and hi Hz. */
static float band_share(float lo, float hi) {
    double in = 0.0, all = 0.0;
    int i;

    for (i = 1; i < N / 2; i++) {
        all += sPow[i];
        in += hz(i) >= lo && hz(i) <= hi ? sPow[i] : 0.0;
    }
    return all > 0.0 ? (float)(in / all) : 0.0f;
}

static float level_db(const int16_t *pcm, int frames, int ch, int rms) {
    double sum = 0.0, peak = 0.0;
    int i;

    for (i = 0; i < frames; i++) {
        double v = fabs((double)pcm[i * 2 + ch] / 32768.0);
        sum += v * v;
        peak = v > peak ? v : peak;
    }
    if (rms) {
        return frames > 0 && sum > 0.0 ? (float)(10.0 * log10(sum / frames)) : -999.0f;
    }
    return peak > 0.0 ? (float)(20.0 * log10(peak)) : -999.0f;
}

static void measure(const char *name, int kind, int who, int pitch, const char *wavDir, float lo, float hi) {
    static int16_t pcm[TONE_MAX * 2];
    int frames = PortA11y_TonePcm(kind, who, pitch, 0, 0, pcm), i, np, best = 1, first = -1, last = -1, attack = 0;
    float max = 0.0f, peakDb = level_db(pcm, frames, 0, 0), limit, notes[32];

    /* duration: from the first to the last sample within 40 dB of the peak; attack: to 3 dB under it */
    limit = powf(10.0f, (peakDb - 40.0f) / 20.0f) * 32768.0f;
    for (i = 0; i < frames; i++) {
        if (abs(pcm[i * 2]) >= limit) {
            first = first < 0 ? i : first;
            last = i;
        }
    }
    for (i = 0; i < frames && abs(pcm[i * 2]) < powf(10.0f, (peakDb - 3.0f) / 20.0f) * 32768.0f; i++) {
    }
    attack = i;
    spectrum(pcm, frames, 0);
    for (i = 1; i < N / 2; i++) {
        if (sPow[i] > max) {
            max = sPow[i];
            best = i;
        }
    }
    printf("sound %s kind %d who %d ms %.1f peak %.1f rms %.1f attack_ms %.1f main %.1f centroid %.1f band %.3f notes ",
           name, kind, who, first < 0 ? 0.0 : (double)(last - first) * 1000.0 / TONE_RATE, peakDb,
           level_db(pcm, frames, 0, 1), (double)attack * 1000.0 / TONE_RATE, hz(best), centroid(),
           lo > 0.0f ? band_share(lo, hi) : 0.0f);
    np = lo > 0.0f ? 0 : notes_of(pcm, frames, notes, 32); /* (noise has no notes: its band is what it has) */
    for (i = 0; i < np; i++) {
        printf("%s%.1f", i > 0 ? "," : "", notes[i]);
    }
    printf("%s\n", np == 0 ? "-" : "");
    if (wavDir != NULL) {
        char path[512];
        FILE *fp;
        uint32_t bytes = (uint32_t)frames * 4, v32;
        uint16_t v16;
        snprintf(path, sizeof(path), "%s/%s.wav", wavDir, name);
        if ((fp = fopen(path, "wb")) != NULL) {
            fwrite("RIFF", 1, 4, fp); v32 = 36 + bytes; fwrite(&v32, 4, 1, fp);
            fwrite("WAVEfmt ", 1, 8, fp); v32 = 16; fwrite(&v32, 4, 1, fp);
            v16 = 1; fwrite(&v16, 2, 1, fp); v16 = 2; fwrite(&v16, 2, 1, fp);
            v32 = TONE_RATE; fwrite(&v32, 4, 1, fp); v32 = TONE_RATE * 4; fwrite(&v32, 4, 1, fp);
            v16 = 4; fwrite(&v16, 2, 1, fp); v16 = 16; fwrite(&v16, 2, 1, fp);
            fwrite("data", 1, 4, fp); fwrite(&bytes, 4, 1, fp);
            fwrite(pcm, 4, (size_t)frames, fp);
            fclose(fp);
        }
    }
}

static void place(int az, int mono) {
    static int16_t pcm[TONE_MAX * 2];
    int frames;

    sMonoTest = mono;
    frames = PortA11y_TonePcm(TONE_WHERE, 0, 0, 1, az, pcm);
    spectrum(pcm, frames, 0);
    {
        float c = centroid();
        spectrum(pcm, frames, 1);
        c = (c + centroid()) / 2.0f;
        printf("place %d left %.1f right %.1f centroid %.1f mono %d\n", az, level_db(pcm, frames, 0, 1),
               level_db(pcm, frames, 1, 1), c, mono);
    }
    sMonoTest = 0;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : NULL;
    static const int kGain[] = {0, 10, 30, 60, 85, 100};
    int i;

    measure("tick", TONE_DISTANCE, 0, 0, dir, 0, 0);
    measure("tick_close", TONE_DISTANCE_CLOSE, 0, 0, dir, 0, 0);
    measure("wall", TONE_EDGE_WALL, 0, 0, dir, 250.0f, 600.0f);
    measure("ceiling", TONE_EDGE_TOP, 0, 0, dir, 4000.0f, 8000.0f);
    measure("floor", TONE_EDGE_FLOOR, 0, 0, dir, 45.0f, 90.0f);
    measure("ring", TONE_RING, 0, 0, dir, 0, 0);
    measure("health", TONE_HEALTH, 0, 0, dir, 0, 0);
    measure("health_rival", TONE_HEALTH, 1, 0, dir, 0, 0);
    measure("ki", TONE_KI, 0, 0, dir, 0, 0);
    measure("ki_rival", TONE_KI, 1, 0, dir, 0, 0);
    measure("max_power", TONE_KI, 0, 5, dir, 0, 0);
    measure("where", TONE_WHERE, 0, 0, dir, 0, 0);
    measure("transform", TONE_CUE_TRANSFORM, 0, 0, dir, 0, 0);
    measure("transform_rival", TONE_CUE_TRANSFORM, 1, 0, dir, 0, 0);
    measure("ultimate", TONE_CUE_ULTIMATE, 0, 0, dir, 0, 0);
    measure("ultimate_rival", TONE_CUE_ULTIMATE, 1, 0, dir, 0, 0);
    place(0, 0);
    place(9000, 0);
    place(-9000, 0);
    place(4500, 0);
    place(2000, 0);
    place(18000, 0);
    place(13500, 0);
    place(9000, 1);
    for (i = 0; i < (int)(sizeof(kGain) / sizeof(kGain[0])); i++) {
        float g = PortA11y_CueGainAt(kGain[i]);
        printf("gain %d %.1f\n", kGain[i], g > 0.0f ? 20.0f * log10f(g) : -999.0f);
    }
    return 0;
}
