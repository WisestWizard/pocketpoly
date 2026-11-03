/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
#include <stdint.h>
#include "fastmath.h"
#include "engine.h"
#include "fx.h"

#define FS EN_FS
#define ENS_LEN 512u                     /* 11.6 ms a channel */
#define DLY_LEN 36864u                   /* 0.836 s, int16 */
#define TWO_PI 6.28318531f

/* Dattorro's plate, its lengths scaled from 29761 Hz to ours (x 1.482) */
#define RV_PRE 441u
#define RV_AP1 210u
#define RV_AP2 159u
#define RV_AP3 562u
#define RV_AP4 410u
#define RV_LAP1 1008u                    /* 996 + 12 of modulation */
#define RV_LD1 6600u
#define RV_LAP2 2668u
#define RV_LD2 5513u
#define RV_RAP1 1358u                    /* 1346 + 12 */
#define RV_RD1 6250u
#define RV_RAP2 3936u
#define RV_RD2 4688u
#define RV_TOTAL (RV_PRE + RV_AP1 + RV_AP2 + RV_AP3 + RV_AP4 + RV_LAP1 + RV_LD1 + RV_LAP2 + RV_LD2 + RV_RAP1 + RV_RD1 + RV_RAP2 + RV_RD2)

typedef struct { int16_t *buf; uint32_t len, pos; } line_t;

static float *ens_l, *ens_r;
static uint32_t ens_pos;
static float ens_ph1, ens_ph2;

static int16_t *dly;
static uint32_t dly_pos;
static float dly_read, dly_lp, dly_dc_x, dly_dc_y, wow_ph, flut_ph;

static line_t rv[13];
static float rv_bw, rv_lp_l, rv_lp_r, rv_mod_ph;

static float lim_gain = 1.0f;
static uint32_t ens_tail, dly_tail, rev_tail;   /* blocks a stage still runs once nothing passes through it: with its
                                                 * input and output silent that long, it holds only zeros and rests */
static uint32_t dly_live, rv_live;       /* the stage's lines had something in them last block */
static float pk_ens, pk_dly, pk_rev;      /* each stage's output peak, decaying: a diagnostic (fx_peaks) */

static float block_peak(const float *l, const float *r, uint32_t n)
{
    float pk = 0.0f;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float a = l[i] < 0.0f ? -l[i] : l[i], b = r[i] < 0.0f ? -r[i] : r[i];
        pk = a > pk ? a : pk;
        pk = b > pk ? b : pk;
    }
    return pk;
}
uint32_t fx_running(void) { return (ens_tail ? 1u : 0u) | (dly_tail ? 2u : 0u) | (rev_tail ? 4u : 0u); }
void fx_peaks(float *ens, float *dly, float *rev)
{
    *ens = pk_ens;
    *dly = pk_dly;
    *rev = pk_rev;
}

uint32_t fx_bytes(void) { return ENS_LEN * 2u * sizeof(float) + DLY_LEN * 2u + RV_TOTAL * 2u + 64u; }

static inline float tri(float ph) { return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; }

/* a 16-bit delay line: write, read at an integer or fractional distance back */
static inline void line_put(line_t *d, float v)
{
    float c = v * 32767.0f;
    d->buf[d->pos] = (int16_t)(c > 32767.0f ? 32767.0f : c < -32767.0f ? -32767.0f : c);
    d->pos = d->pos + 1u == d->len ? 0u : d->pos + 1u;
}
static inline float line_at(const line_t *d, uint32_t back)   /* back >= 1, before this step's write */
{
    uint32_t i = d->pos + d->len - back;
    if (i >= d->len)
        i -= d->len;
    return (float)d->buf[i] * (1.0f / 32767.0f);
}
static inline float line_at_f(const line_t *d, float back)
{
    uint32_t b = (uint32_t)back;
    float f = back - (float)b, a = line_at(d, b), c = line_at(d, b + 1u);
    return a + (c - a) * f;
}
/* a Schroeder allpass on a line of length d->len: w[n] = x + g w[n-L], y = w[n-L] - g w[n]. (The first version
 * returned w[n-L] - g x, which is not an allpass: it peaks at up to 1/(1-g) and made the plate run away.) */
static inline float allpass(line_t *d, float x, float g)
{
    float v = line_at(d, d->len - 1u), w = x + g * v;
    line_put(d, w);
    return v - g * w;
}
static inline float allpass_mod(line_t *d, float x, float g, float back)
{
    float v = line_at_f(d, back), w = x + g * v;
    line_put(d, w);
    return v - g * w;
}

void fx_init(void *arena)
{
    uint8_t *a = (uint8_t *)arena;
    static const uint32_t L[13] = {RV_PRE, RV_AP1, RV_AP2, RV_AP3, RV_AP4, RV_LAP1, RV_LD1, RV_LAP2, RV_LD2,
                                   RV_RAP1, RV_RD1, RV_RAP2, RV_RD2};
    uint32_t i, k;
    ens_l = (float *)a;
    a += ENS_LEN * sizeof(float);
    ens_r = (float *)a;
    a += ENS_LEN * sizeof(float);
    dly = (int16_t *)a;
    a += DLY_LEN * 2u;
    for (i = 0; i < 13u; i++) {
        rv[i].buf = (int16_t *)a;
        rv[i].len = L[i];
        rv[i].pos = 0;
        a += L[i] * 2u;
        for (k = 0; k < L[i]; k++)
            rv[i].buf[k] = 0;
    }
    for (i = 0; i < ENS_LEN; i++)
        ens_l[i] = ens_r[i] = 0.0f;
    for (i = 0; i < DLY_LEN; i++)
        dly[i] = 0;
    ens_pos = dly_pos = 0;
    ens_tail = dly_tail = rev_tail = 0;
    dly_live = rv_live = 0;
    ens_ph1 = ens_ph2 = wow_ph = flut_ph = rv_mod_ph = 0.0f;
    dly_read = 4410.0f;
    dly_lp = dly_dc_x = dly_dc_y = rv_bw = rv_lp_l = rv_lp_r = 0.0f;
    lim_gain = 1.0f;
}

void fx_process(float *l, float *r, uint32_t n, const fx_params_t *p)
{
    uint32_t i;
    float bus_pk = block_peak(l, r, n);  /* what the next stage gets: silence into a silent stage lets it rest */
    /* ensemble */
    {
        uint32_t ens_on = !(p->bypass & FX_BYP_ENS) && p->ens_mode != ENS_OFF && p->ens_mix > 0.001f;
        if (bus_pk > 0.0f && ens_on)
            ens_tail = ENS_LEN / n + 2u; /* its line holds the last 11.6 ms */
        else if (ens_tail)
            ens_tail--;
    }
    if (ens_tail) {
        float inc1 = 0.513f / FS, inc2 = 0.863f / FS, depth, base = 0.0035f * FS, wet = 0.7f * p->ens_mix,
              dry = 1.0f - 0.35f * p->ens_mix;
        depth = (p->ens_mode == ENS_CHORUS1 ? 0.0012f : p->ens_mode == ENS_CHORUS2 ? 0.0018f : 0.0015f) * FS;
        for (i = 0; i < n; i++) {
            float m, dl, dr, wl, wr;
            uint32_t b0;
            ens_ph1 += inc1;
            if (ens_ph1 >= 1.0f)
                ens_ph1 -= 1.0f;
            ens_ph2 += inc2;
            if (ens_ph2 >= 1.0f)
                ens_ph2 -= 1.0f;
            m = p->ens_mode == ENS_CHORUS1 ? tri(ens_ph1) : p->ens_mode == ENS_CHORUS2 ? tri(ens_ph2)
                                                                                       : 0.5f * (tri(ens_ph1) + tri(ens_ph2));
            ens_l[ens_pos] = l[i];
            ens_r[ens_pos] = r[i];
            dl = base + depth * m;
            dr = base - depth * m;
            b0 = (uint32_t)dl;
            {
                uint32_t i0 = (ens_pos + ENS_LEN - b0) & (ENS_LEN - 1u), i1 = (i0 + ENS_LEN - 1u) & (ENS_LEN - 1u);
                float f = dl - (float)b0;
                wl = ens_l[i0] + (ens_l[i1] - ens_l[i0]) * f;
            }
            b0 = (uint32_t)dr;
            {
                uint32_t i0 = (ens_pos + ENS_LEN - b0) & (ENS_LEN - 1u), i1 = (i0 + ENS_LEN - 1u) & (ENS_LEN - 1u);
                float f = dr - (float)b0;
                wr = ens_r[i0] + (ens_r[i1] - ens_r[i0]) * f;
            }
            ens_pos = (ens_pos + 1u) & (ENS_LEN - 1u);
            l[i] = l[i] * dry + wl * wet;
            r[i] = r[i] * dry + wr * wet;
        }
        pk_ens = block_peak(l, r, n);
        bus_pk = pk_ens;
    } else {
        float inc1 = 0.513f / FS, inc2 = 0.863f / FS;
        for (i = 0; i < n; i++) {        /* at rest or off: its LFOs keep time, sample by sample, and the line empties
                                          * (switched on again, it does not replay what passed when it went off) */
            ens_ph1 += inc1;
            if (ens_ph1 >= 1.0f)
                ens_ph1 -= 1.0f;
            ens_ph2 += inc2;
            if (ens_ph2 >= 1.0f)
                ens_ph2 -= 1.0f;
            ens_l[ens_pos] = ens_r[ens_pos] = 0.0f;
            ens_pos = (ens_pos + 1u) & (ENS_LEN - 1u);
        }
        pk_ens = 0.0f;
    }
    /* tape delay: mono in, the line read at a slowly wandering distance, dark and soft in the feedback */
    if ((p->dly_send > 0.001f && bus_pk > 0.0f) || dly_live)
        dly_tail = DLY_LEN / n + 2u;     /* the line's length: nothing in or out that long, and it is all zeros */
    else if (dly_tail)
        dly_tail--;
    if (dly_tail && !(p->bypass & FX_BYP_DLY)) {
        float tgt = p->dly_time_s * FS, slew = 0.0004f, fb = p->dly_fdbk * 0.92f, lpc = 0.35f, send = p->dly_send;
        int32_t live = 0;
        if (tgt > (float)(DLY_LEN - 128u))   /* wow and flutter read up to 0.18 % further back */
            tgt = (float)(DLY_LEN - 128u);
        if (tgt < 64.0f)
            tgt = 64.0f;
        for (i = 0; i < n; i++) {
            float in = (l[i] + r[i]) * 0.5f * send, d, out, back, c, wet;
            uint32_t b, i0, i1;
            dly_read += (tgt - dly_read) * slew;       /* a time change bends the pitch, like tape */
            wow_ph += 0.35f / FS;
            if (wow_ph >= 1.0f)
                wow_ph -= 1.0f;
            flut_ph += 6.3f / FS;
            if (flut_ph >= 1.0f)
                flut_ph -= 1.0f;
            back = dly_read * (1.0f + 0.0015f * fm_sin_turns(wow_ph) + 0.0003f * fm_sin_turns(flut_ph));
            b = (uint32_t)back;
            i0 = dly_pos + DLY_LEN - b;
            if (i0 >= DLY_LEN)
                i0 -= DLY_LEN;
            i1 = i0 == 0u ? DLY_LEN - 1u : i0 - 1u;
            live |= dly[i0] | dly[i1];
            d = (float)dly[i0] + ((float)dly[i1] - (float)dly[i0]) * (back - (float)b);
            out = d * (1.0f / 32767.0f);
            wet = out;
            c = in + out * fb;
            dly_lp += (c - dly_lp) * lpc;               /* the repeats get darker */
            c = dly_lp * 1.2f;                          /* and softer: a cubic clip */
            c = c > 1.5f ? 1.5f : c < -1.5f ? -1.5f : c;
            c = (c - c * c * c * (4.0f / 27.0f)) * 0.85f;
            {
                float y = c - dly_dc_x + 0.995f * dly_dc_y;   /* no DC build-up */
                dly_dc_x = c;
                dly_dc_y = y;
                c = y;
            }
            c *= 32767.0f;
            dly[dly_pos] = (int16_t)(c > 32767.0f ? 32767.0f : c < -32767.0f ? -32767.0f : c);
            live |= dly[dly_pos];
            dly_pos = dly_pos + 1u == DLY_LEN ? 0u : dly_pos + 1u;
            l[i] += wet;
            r[i] += wet;
        }
        dly_live = live != 0;
        dly_lp = fm_flush(dly_lp);
        dly_dc_x = fm_flush(dly_dc_x);
        dly_dc_y = fm_flush(dly_dc_y);
        pk_dly = block_peak(l, r, n);
        bus_pk = pk_dly;
    } else {
        float tgt = p->dly_time_s * FS, slew = 0.0004f;
        if (tgt > (float)(DLY_LEN - 128u))
            tgt = (float)(DLY_LEN - 128u);
        if (tgt < 64.0f)
            tgt = 64.0f;
        for (i = 0; i < n; i++) {        /* at rest: the time still settles, wow and flutter keep their phase */
            dly_read += (tgt - dly_read) * slew;
            wow_ph += 0.35f / FS;
            if (wow_ph >= 1.0f)
                wow_ph -= 1.0f;
            flut_ph += 6.3f / FS;
            if (flut_ph >= 1.0f)
                flut_ph -= 1.0f;
        }
        pk_dly = 0.0f;
        dly_live = 0;
    }
    /* plate reverb */
    if ((p->rev_send > 0.001f && bus_pk > 0.0f) || rv_live)
        rev_tail = 80u;                  /* past the tank's loop (62 blocks): nothing in or out that long, all zeros */
    else if (rev_tail)
        rev_tail--;
    if (rev_tail && !(p->bypass & FX_BYP_REV)) {
        float live = 0.0f;
        float decay = 0.25f + p->rev_decay * 0.68f, damp = 0.05f + p->rev_damp * 0.7f, send = p->rev_send,
              bw = 0.6f, dif1 = 0.70f, dif2 = 0.50f,
              out_g = 4.0f * 0.6f * 0.11f * (1.0f - decay) * (1.0f / 0.38f);   /* the taps' sum, brought to about the
                                                                 * dry level at full send, whatever the decay; the 4 undoes
                                                                 * the input's 1/4, headroom for the allpass nodes */
        for (i = 0; i < n; i++) {
            float x = (l[i] + r[i]) * 0.125f * send, a, tl, tr, outl, outr, mod;
            rv_mod_ph += 0.6f / FS;
            if (rv_mod_ph >= 1.0f)
                rv_mod_ph -= 1.0f;
            mod = 8.0f * fm_sin_turns(rv_mod_ph);
            rv_bw += (x - rv_bw) * bw;
            line_put(&rv[0], rv_bw);
            a = line_at(&rv[0], RV_PRE - 1u);
            a = allpass(&rv[1], a, 0.75f);
            a = allpass(&rv[2], a, 0.75f);
            a = allpass(&rv[3], a, 0.625f);
            a = allpass(&rv[4], a, 0.625f);
            /* the tank: each side fed by the other's end */
            tl = a + decay * line_at(&rv[12], RV_RD2 - 1u);
            tr = a + decay * line_at(&rv[8], RV_LD2 - 1u);
            tl = allpass_mod(&rv[5], tl, -dif1, 996.0f + mod);
            line_put(&rv[6], tl);
            tl = line_at(&rv[6], RV_LD1 - 1u);
            rv_lp_l += (tl - rv_lp_l) * (1.0f - damp);
            tl = allpass(&rv[7], rv_lp_l * decay, dif2);
            line_put(&rv[8], tl);
            tr = allpass_mod(&rv[9], tr, -dif1, 1346.0f - mod);
            line_put(&rv[10], tr);
            tr = line_at(&rv[10], RV_RD1 - 1u);
            rv_lp_r += (tr - rv_lp_r) * (1.0f - damp);
            tr = allpass(&rv[11], rv_lp_r * decay, dif2);
            line_put(&rv[12], tr);
            /* Dattorro's output taps, scaled */
            outl = out_g * (line_at(&rv[10], 394u) + line_at(&rv[10], 4407u) - line_at(&rv[11], 2835u)
                           + line_at(&rv[12], 2958u) - line_at(&rv[6], 2949u) - line_at(&rv[7], 277u)
                           - line_at(&rv[8], 1580u));
            outr = out_g * (line_at(&rv[6], 523u) + line_at(&rv[6], 5339u) - line_at(&rv[7], 1976u)
                           + line_at(&rv[8], 3990u) - line_at(&rv[10], 2340u) - line_at(&rv[11], 118u)
                           - line_at(&rv[12], 1070u));
            live += fm_fabsf(a) + fm_fabsf(outl) + fm_fabsf(outr);
            l[i] += outl;
            r[i] += outr;
        }
        rv_live = live > 0.0f;
        pk_rev = block_peak(l, r, n);
    } else {
        for (i = 0; i < n; i++) {        /* at rest, the tank's modulation keeps its phase */
            rv_mod_ph += 0.6f / FS;
            if (rv_mod_ph >= 1.0f)
                rv_mod_ph -= 1.0f;
        }
        pk_rev = 0.0f;
        rv_live = 0;
    }
    /* the limiter: instant attack, 80 ms release, so nothing the effects add can clip */
    {
        float rel = 1.0f / (0.08f * FS), thr = 0.95f;
        for (i = 0; i < n; i++) {
            float pk = fm_fabsf(l[i]) > fm_fabsf(r[i]) ? fm_fabsf(l[i]) : fm_fabsf(r[i]);
            if (pk * lim_gain > thr)
                lim_gain = thr / pk;
            else
                lim_gain += (1.0f - lim_gain) * rel;
            l[i] *= lim_gain;
            r[i] *= lim_gain;
        }
    }
}
