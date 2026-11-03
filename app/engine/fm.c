/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
#include <stdint.h>
#include "fastmath.h"
#include "fm.h"

#define MOD_TURNS 1.0f                   /* a modulator at full level swings the carrier's phase +-1 turn */
#define FB_TURNS 0.75f

/* the 32 algorithms as charted: for each operator the operators that modulate it (bit k = op k+1), the carriers,
 * the feedback operator. A few loops that feed back through two operators are drawn as self-feedback. */
typedef struct { uint8_t mods[6], carriers, fb; } alg_t;
#define B(n) (1u << ((n) - 1u))
static const alg_t ALG[32] = {
    {{B(2), 0, B(4), B(5), B(6), 0}, B(1) | B(3), 6},
    {{B(2), 0, B(4), B(5), B(6), 0}, B(1) | B(3), 2},
    {{B(2), B(3), 0, B(5), B(6), 0}, B(1) | B(4), 6},
    {{B(2), B(3), 0, B(5), B(6), 0}, B(1) | B(4), 4},
    {{B(2), 0, B(4), 0, B(6), 0}, B(1) | B(3) | B(5), 6},
    {{B(2), 0, B(4), 0, B(6), 0}, B(1) | B(3) | B(5), 5},
    {{B(2), 0, B(4) | B(5), 0, B(6), 0}, B(1) | B(3), 6},
    {{B(2), 0, B(4) | B(5), 0, B(6), 0}, B(1) | B(3), 4},
    {{B(2), 0, B(4) | B(5), 0, B(6), 0}, B(1) | B(3), 2},
    {{B(2), B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(4), 3},
    {{B(2), B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(4), 6},
    {{B(2), 0, B(4) | B(5) | B(6), 0, 0, 0}, B(1) | B(3), 2},
    {{B(2), 0, B(4) | B(5) | B(6), 0, 0, 0}, B(1) | B(3), 6},
    {{B(2), 0, B(4), B(5) | B(6), 0, 0}, B(1) | B(3), 6},
    {{B(2), 0, B(4), B(5) | B(6), 0, 0}, B(1) | B(3), 2},
    {{B(2) | B(3) | B(5), 0, B(4), 0, B(6), 0}, B(1), 6},
    {{B(2) | B(3) | B(5), 0, B(4), 0, B(6), 0}, B(1), 2},
    {{B(2) | B(3) | B(4), 0, 0, B(5), B(6), 0}, B(1), 3},
    {{B(2), B(3), 0, B(6), B(6), 0}, B(1) | B(4) | B(5), 6},
    {{B(3), B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(2) | B(4), 3},
    {{B(3), B(3), 0, B(6), B(6), 0}, B(1) | B(2) | B(4) | B(5), 3},
    {{B(2), 0, B(6), B(6), B(6), 0}, B(1) | B(3) | B(4) | B(5), 6},
    {{0, B(3), 0, B(6), B(6), 0}, B(1) | B(2) | B(4) | B(5), 6},
    {{0, 0, B(6), B(6), B(6), 0}, B(1) | B(2) | B(3) | B(4) | B(5), 6},
    {{0, 0, 0, B(6), B(6), 0}, B(1) | B(2) | B(3) | B(4) | B(5), 6},
    {{0, B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(2) | B(4), 6},
    {{0, B(3), 0, B(5) | B(6), 0, 0}, B(1) | B(2) | B(4), 3},
    {{B(2), 0, B(4), B(5), 0, 0}, B(1) | B(3) | B(6), 5},
    {{0, 0, B(4), 0, B(6), 0}, B(1) | B(2) | B(3) | B(5), 6},
    {{0, 0, B(4), B(5), 0, 0}, B(1) | B(2) | B(3) | B(6), 5},
    {{0, 0, 0, 0, B(6), 0}, B(1) | B(2) | B(3) | B(4) | B(5), 6},
    {{0, 0, 0, 0, 0, 0}, 63u, 6},
};

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

#define SIN_N 2048u
static int16_t fmsin[SIN_N];             /* a sine in turns, no interpolation: FM's noise floor is far below it */
static float LEVEL_AMP[100];             /* a level 0..99 as amplitude: 0.75 dB a step down from 99, times 1/32767 for the table */
static float RATE_UNITS[100];            /* a rate 0..99 as level units a second: 99 crosses the range in ~4 ms, 0 in ~40 s */

void fm_init(void)
{
    uint32_t i;
    for (i = 0; i < SIN_N; i++)
        fmsin[i] = (int16_t)(fm_sin_turns((float)i / (float)SIN_N) * 32767.0f);
    for (i = 0; i < 100u; i++) {
        LEVEL_AMP[i] = i ? fm_exp2f(((float)i - 99.0f) * (0.75f / 6.0206f)) * (1.0f / 32767.0f) : 0.0f;
        RATE_UNITS[i] = 99.0f / (0.004f * fm_exp2f((99.0f - (float)i) * (1.0f / 7.5f)));
    }
}

static inline float level_amp(float l)   /* interpolated: the envelope moves in fractions of a level */
{
    uint32_t i;
    float fr;
    if (l <= 0.0f)
        return 0.0f;
    if (l >= 99.0f)
        return LEVEL_AMP[99];
    i = (uint32_t)l;
    fr = l - (float)i;
    return LEVEL_AMP[i] + (LEVEL_AMP[i + 1u] - LEVEL_AMP[i]) * fr;
}

void fm_note_on(fm_state_t *s, const fm_voice_t *v, float note, float vel01, int retrigger)
{
    const alg_t *a = &ALG[v->alg < 32u ? v->alg : 0u];
    uint32_t k, j, n = 0;
    (void)note;
    s->carriers = a->carriers;
    s->fbop = (uint8_t)(a->fb ? a->fb - 1u : 255u);
    s->active = 0;
    for (k = 0; k < FM_OPS; k++) {
        const fm_op_t *o = &v->op[k];
        if (!retrigger) {
            s->ph[k] = 0.0f;
            s->lvl[k] = 0.0f;
            s->out[k] = 0.0f;
        }
        s->stage[k] = 1;
        s->amp[k] = 0.0f;
        s->nmod[k] = 0;
        for (j = k + 1u; j < FM_OPS; j++)   /* its modulators: always the higher operators */
            if ((a->mods[k] >> j) & 1u && s->nmod[k] < 3u)
                s->modi[k][s->nmod[k]++] = (uint8_t)j;
        s->lvlmax[k] = (float)o->level - (1.0f - vel01) * (float)o->vel * 6.0f;   /* velocity: up to -42 levels */
        if (s->lvlmax[k] < 0.0f)
            s->lvlmax[k] = 0.0f;
        s->fixed[k] = o->mode;
        if (o->mode) {
            static const float FIXED[4] = {1.0f, 10.0f, 100.0f, 1000.0f};
            s->ratio[k] = FIXED[o->coarse & 3u] * fm_exp2f((float)o->fine * (3.3219f / 100.0f));
        } else {
            s->ratio[k] = (o->coarse ? (float)o->coarse : 0.5f) * (1.0f + (float)o->fine * 0.01f)
                          * fm_exp2f((float)o->detune * (3.0f / 1200.0f));
        }
        if (o->level > 0u)
            s->active |= (uint8_t)(1u << k);
    }
    for (k = 0; k < FM_OPS; k++)         /* an operator with level 0 modulates nothing: drop it from the lists */
        for (j = 0; j < s->nmod[k]; j++)
            if (!((s->active >> s->modi[k][j]) & 1u)) {
                s->modi[k][j] = s->modi[k][s->nmod[k] - 1u];
                s->nmod[k]--;
                j--;
            }
    s->fb1 = s->fb2 = 0.0f;
    for (k = 0; k < FM_OPS; k++)
        n += (s->carriers >> k) & 1u;
    s->carrier_gain = n ? 2.0f / (1.0f + (float)n * fm_rsqrtf((float)n)) : 0.0f;   /* 1, .83, .73 ... .58 for 1..6 carriers:
                                                                                   * between a coherent and a random sum */
}

void fm_note_off(fm_state_t *s)
{
    uint32_t k;
    for (k = 0; k < FM_OPS; k++)
        if (s->stage[k])
            s->stage[k] = 4;
}

int fm_silent(const fm_state_t *s)
{
    uint32_t k;
    for (k = 0; k < FM_OPS; k++)
        if ((s->carriers >> k) & 1u && (s->stage[k] != 0u || s->lvl[k] > 0.5f))
            return 0;
    return 1;
}

void fm_tick(fm_state_t *s, const fm_voice_t *v, float note, float fs, float rate_x, float bright, float fb_add)
{
    const float tick_s = 32.0f / fs;
    float fb = clampf((float)v->fb + fb_add, 0.0f, 7.0f) * (1.0f / 7.0f);
    float base = 440.0f * fm_exp2f((note + (float)v->transpose - 69.0f) * (1.0f / 12.0f)) / fs;   /* one exp2 a tick */
    float ks = (note - 48.0f) * (1.0f / 16.0f);
    uint32_t k;
    s->fbg = fb * fb * FB_TURNS;
    for (k = 0; k < FM_OPS; k++) {
        const fm_op_t *o = &v->op[k];
        uint32_t st = s->stage[k];
        float lvl;
        if (!((s->active >> k) & 1u))
            continue;
        if (st >= 1u && st <= 4u) {        /* towards the stage's level at its rate; a stage ends on arrival */
            uint32_t i = st - 1u;
            float tgt = (float)o->l[i], r = (float)o->r[i] + (float)o->ks * ks, step;
            r = r < 0.0f ? 0.0f : r > 99.0f ? 99.0f : r;
            step = RATE_UNITS[(uint32_t)r] * rate_x * tick_s;
            if (s->lvl[k] < tgt) {
                s->lvl[k] += step;
                if (s->lvl[k] >= tgt) {
                    s->lvl[k] = tgt;
                    st = st == 3u ? 5u : st == 4u ? 0u : st + 1u;
                }
            } else {
                s->lvl[k] -= step;
                if (s->lvl[k] <= tgt) {
                    s->lvl[k] = tgt;
                    st = st == 3u ? 5u : st == 4u ? 0u : st + 1u;
                }
            }
            s->stage[k] = (uint8_t)st;
        }
        lvl = s->lvlmax[k];
        if (!((s->carriers >> k) & 1u))
            lvl = clampf(lvl + bright, 0.0f, 99.0f);
        s->amp[k] = level_amp(lvl - (99.0f - s->lvl[k]));
        s->inc[k] = s->fixed[k] ? s->ratio[k] / fs : base * s->ratio[k];
        if (s->inc[k] > 0.45f)
            s->inc[k] = 0.45f;
    }
}

float fm_sample(fm_state_t *s)
{
    float sum = 0.0f;
    int k;
    for (k = (int)FM_OPS - 1; k >= 0; k--) {   /* OP6 first: modulators are always the higher operators */
        float m, p, o;
        uint32_t j, n;
        if (!((s->active >> k) & 1u))
            continue;
        n = s->nmod[k];
        m = n ? s->out[s->modi[k][0]] : 0.0f;
        for (j = 1; j < n; j++)
            m += s->out[s->modi[k][j]];
        if ((uint32_t)k == s->fbop)
            m += (s->fb1 + s->fb2) * (0.5f * s->fbg);
        s->ph[k] += s->inc[k];
        if (s->ph[k] >= 1.0f)
            s->ph[k] -= 1.0f;
        p = s->ph[k] + m * MOD_TURNS;
        p -= (float)(int32_t)p;            /* the modulation can reach a few turns either way */
        if (p < 0.0f)
            p += 1.0f;
        o = (float)fmsin[(uint32_t)(p * (float)SIN_N) & (SIN_N - 1u)] * s->amp[k];
        s->out[k] = o;
        if ((uint32_t)k == s->fbop) {
            s->fb2 = s->fb1;
            s->fb1 = o;
        }
        if ((s->carriers >> k) & 1u)
            sum += o;
    }
    return sum * s->carrier_gain;
}

/* ------------------------------------------------------------------ DX7 SysEx --- */
static void copy_name(fm_voice_t *out, const uint8_t *p)
{
    uint32_t i;
    for (i = 0; i < 10u; i++) {
        uint8_t c = p[i] & 127u;
        out->name[i] = (char)(c < 32u || c > 126u ? ' ' : c);
    }
    out->name[10] = 0;
}

void fm_import_dx7_packed(const uint8_t *p, fm_voice_t *out)   /* 128 bytes: OP6 first, 17 bytes each */
{
    uint32_t k, i;
    for (k = 0; k < FM_OPS; k++) {
        const uint8_t *q = p + (5u - k) * 17u;   /* the dump stores OP6 first */
        fm_op_t *o = &out->op[k];
        for (i = 0; i < 4u; i++) {
            o->r[i] = (uint8_t)(q[i] > 99u ? 99u : q[i]);
            o->l[i] = (uint8_t)(q[4u + i] > 99u ? 99u : q[4u + i]);
        }
        o->ks = (uint8_t)(q[12] & 7u);
        o->detune = (int8_t)((int)((q[12] >> 3) & 15u) - 7);
        o->vel = (uint8_t)((q[13] >> 2) & 7u);
        o->level = (uint8_t)(q[14] > 99u ? 99u : q[14]);
        o->mode = (uint8_t)(q[15] & 1u);
        o->coarse = (uint8_t)((q[15] >> 1) & 31u);
        o->fine = (uint8_t)(q[16] > 99u ? 99u : q[16]);
    }
    out->alg = (uint8_t)(p[110] & 31u);
    out->fb = (uint8_t)(p[111] & 7u);
    out->transpose = (int8_t)((int)(p[117] & 63u) - 24);
    copy_name(out, p + 118);
}

void fm_import_dx7_single(const uint8_t *p, fm_voice_t *out)   /* 155 bytes, unpacked: OP6 first, 21 bytes each */
{
    uint32_t k, i;
    for (k = 0; k < FM_OPS; k++) {
        const uint8_t *q = p + (5u - k) * 21u;
        fm_op_t *o = &out->op[k];
        for (i = 0; i < 4u; i++) {
            o->r[i] = (uint8_t)(q[i] > 99u ? 99u : q[i]);
            o->l[i] = (uint8_t)(q[4u + i] > 99u ? 99u : q[4u + i]);
        }
        o->ks = (uint8_t)(q[13] & 7u);
        o->vel = (uint8_t)(q[15] & 7u);
        o->level = (uint8_t)(q[16] > 99u ? 99u : q[16]);
        o->mode = (uint8_t)(q[17] & 1u);
        o->coarse = (uint8_t)(q[18] & 31u);
        o->fine = (uint8_t)(q[19] > 99u ? 99u : q[19]);
        o->detune = (int8_t)((int)(q[20] & 15u) - 7);
    }
    out->alg = (uint8_t)(p[134] & 31u);
    out->fb = (uint8_t)(p[135] & 7u);
    out->transpose = (int8_t)((int)(p[144] & 63u) - 24);
    copy_name(out, p + 145);
}

/* ------------------------------------------------------------------ dice --- */
static uint32_t frnd(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x >> 8;
}
static uint32_t frange(uint32_t *s, uint32_t lo, uint32_t hi) { return lo + frnd(s) % (hi - lo + 1u); }

void fm_dice(fm_voice_t *out, uint32_t *rng)
{
    static const uint8_t CAR_COARSE[8] = {1, 1, 1, 1, 2, 2, 0, 3};
    static const uint8_t MOD_COARSE[12] = {1, 1, 2, 2, 3, 4, 5, 7, 0, 1, 3, 14};
    static const uint8_t MOD_FINE[12] = {0, 0, 0, 0, 0, 0, 0, 1, 0, 41, 50, 0};
    /* envelope templates: pluck, keys, pad, organ, swell */
    static const uint8_t ENV[5][8] = {{99, 60, 30, 60, 99, 70, 0, 0}, {95, 45, 25, 55, 99, 85, 60, 0},
                                      {45, 40, 30, 50, 99, 90, 80, 0}, {99, 99, 99, 85, 99, 99, 99, 0},
                                      {30, 60, 40, 45, 99, 75, 70, 0}};
    uint32_t k, t = frange(rng, 0, 4), alg = frange(rng, 0, 31);
    const alg_t *a = &ALG[alg];
    out->alg = (uint8_t)alg;
    out->fb = (uint8_t)(frnd(rng) % 3u ? frange(rng, 2, 7) : 0u);
    out->transpose = 0;
    for (k = 0; k < FM_OPS; k++) {
        fm_op_t *o = &out->op[k];
        uint32_t carrier = (a->carriers >> k) & 1u, i, tt = frnd(rng) % 3u ? t : frange(rng, 0, 4);
        for (i = 0; i < 4u; i++) {
            int32_t r = (int32_t)ENV[tt][i] + (int32_t)frange(rng, 0, 16) - 8, l = (int32_t)ENV[tt][4u + i];
            if (!carrier && i < 3u)
                l = l * (int32_t)frange(rng, 60, 100) / 100;
            o->r[i] = (uint8_t)(r < 0 ? 0 : r > 99 ? 99 : r);
            o->l[i] = (uint8_t)(l < 0 ? 0 : l > 99 ? 99 : l);
        }
        if (carrier) {
            o->coarse = CAR_COARSE[frnd(rng) % 8u];
            o->fine = (uint8_t)(frnd(rng) % 4u ? 0u : frange(rng, 0, 3));
            o->level = (uint8_t)frange(rng, 90, 99);
            o->vel = (uint8_t)frange(rng, 0, 3);
        } else {
            uint32_t m = frnd(rng) % 12u;
            o->coarse = MOD_COARSE[m];
            o->fine = MOD_FINE[m];
            o->level = (uint8_t)frange(rng, 40, 86);
            o->vel = (uint8_t)frange(rng, 2, 7);
        }
        o->detune = (int8_t)((int)frange(rng, 0, 6) - 3);
        o->mode = 0;
        o->ks = (uint8_t)frange(rng, 0, 3);
    }
    {
        static const char N[] = "FM DICE";
        for (k = 0; k < FM_NAME; k++)
            out->name[k] = k < 7u ? N[k] : 0;
    }
}
