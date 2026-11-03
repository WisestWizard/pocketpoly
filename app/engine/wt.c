/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* The wavetables' spectra, and their synthesis by inverse DFT from a 128-point sine table (no trig at runtime).
 *   A  ANALOG  sine, triangle, saw, square, narrowing pulse: the classic shapes in one sweep
 *   B  VOX     two formant peaks moving through vowel positions over a harmonic series
 *   C  GLASS   sparse partials and a bright cluster that climbs: digital, PPG-like
 * Every frame is normalised to the same peak (its full mip's), so a position sweep keeps its level and so
 * does a note crossing from one mip to the next. */
#include <stdint.h>
#include "fastmath.h"
#include "wt.h"

#define NH 64u                            /* harmonics in a spectrum (the top level) */
static const uint8_t MIP_MAX[WT_MIPS] = {64, 32, 16, 8, 4};
static float sin128[WT_LEN];
static float spec[NH + 1u];

static float gauss(float x, float w) { return fm_expf(-(x * x) / (2.0f * w * w)); }

static void spectrum(uint32_t table, uint32_t frame)
{
    float t = (float)frame / (float)(WT_FRAMES - 1u), h;
    uint32_t k;
    for (k = 0; k <= NH; k++)
        spec[k] = 0.0f;
    if (table == 0u) {                    /* ANALOG: keyframes at t = 0, .25, .5, .75, 1 */
        float seg = t * 4.0f, f = seg - fm_floorf(seg);
        uint32_t s = (uint32_t)seg;
        if (seg >= 4.0f) {                /* the last frame is the last keyframe itself */
            s = 3u;
            f = 1.0f;
        }
        for (k = 1; k <= NH; k++) {
            float a[5];
            h = (float)k;
            a[0] = k == 1u ? 1.0f : 0.0f;                                          /* sine */
            a[1] = (k & 1u) ? ((k & 2u) ? -1.0f : 1.0f) / (h * h) : 0.0f;          /* triangle */
            a[2] = 1.0f / h;                                                        /* saw */
            a[3] = (k & 1u) ? 1.0f / h : 0.0f;                                      /* square */
            a[4] = fm_sinf(3.14159265f * h * 0.12f) / h;                            /* 12 % pulse */
            spec[k] = a[s] * (1.0f - f) + a[s + 1u] * f;
        }
    } else if (table == 1u) {             /* VOX: F1 and F2 (in harmonics of a 150 Hz voice) sweep A E I O U */
        static const float F1[5] = {5.3f, 3.5f, 1.9f, 3.0f, 2.1f}, F2[5] = {8.0f, 13.0f, 15.0f, 5.6f, 4.0f};
        float seg = t * 4.0f, f = seg - fm_floorf(seg), f1, f2;
        uint32_t s = (uint32_t)seg;
        if (seg >= 4.0f) {
            s = 3u;
            f = 1.0f;
        }
        f1 = F1[s] * (1.0f - f) + F1[s + 1u] * f;
        f2 = F2[s] * (1.0f - f) + F2[s + 1u] * f;
        for (k = 1; k <= 40u; k++) {
            h = (float)k;
            spec[k] = (gauss(h - f1, 1.2f) + 0.6f * gauss(h - f2, 1.8f) + 0.08f / h) / (0.6f + 0.1f * h);
        }
    } else {                              /* GLASS: a few low partials, then a cluster that climbs and thins */
        float c = 6.0f + t * 34.0f, w = 1.5f + t * 2.5f;
        spec[1] = 1.0f;
        spec[2] = 0.35f * (1.0f - t);
        spec[3] = 0.25f;
        spec[5] = 0.18f * t;
        for (k = 4; k <= NH; k++) {
            h = (float)k;
            spec[k] += 0.9f * gauss(h - c, w) * ((k & 1u) ? 1.0f : 0.55f) + 0.5f * gauss(h - 2.0f * c, w * 1.5f);
        }
    }
}

void wt_build(int16_t *dst)
{
    uint32_t i, table, mip, frame, k;
    for (i = 0; i < WT_LEN; i++)
        sin128[i] = fm_sin_turns((float)i / (float)WT_LEN);
    for (table = 0; table < WT_TABLES; table++)
        for (frame = 0; frame < WT_FRAMES; frame++) {
            float g = 0.0f;
            spectrum(table, frame);
            for (mip = 0; mip < WT_MIPS; mip++) {   /* one gain per frame, the full mip's: a note crossing a mip
                                                     * keeps its level (each mip has fewer of the same partials) */
                float wave[WT_LEN], peak = 1e-6f;
                int16_t *out = (int16_t *)wt_frame(dst, table, mip, frame);
                uint32_t hmax = MIP_MAX[mip];
                for (i = 0; i < WT_LEN; i++)
                    wave[i] = 0.0f;
                for (k = 1; k <= hmax && k <= NH; k++) {
                    float a = spec[k];
                    if (a == 0.0f)
                        continue;
                    for (i = 0; i < WT_LEN; i++)
                        wave[i] += a * sin128[(i * k) & (WT_LEN - 1u)];
                }
                if (mip == 0u) {
                    for (i = 0; i < WT_LEN; i++)
                        if (fm_fabsf(wave[i]) > peak)
                            peak = fm_fabsf(wave[i]);
                    g = 30000.0f / peak;
                }
                for (i = 0; i < WT_LEN; i++) {
                    float v = wave[i] * g;
                    out[i] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
                }
            }
        }
}
