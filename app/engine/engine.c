/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* Pocket Poly's engine. Each voice: two oscillators, each a model (polyBLEP saw and pulse, a triangle integrated
 * from the BLEP pulse, a sine, Casio-style phase distortion, three wavetables with band-limited mip levels),
 * oscillator B hard-synced to A and ring-modulated with it if asked, B's output modulating A's phase (XMOD: two-
 * operator FM with any carrier), a square sub, noise, drive into a Simper state-variable filter, amp and filter
 * envelopes, a loopable modulation envelope, slow random drift, a pan. Two LFOs for all voices, an 8-slot
 * modulation matrix with four macros among its sources. Voices sum, soft-clip, run through the effects (fx.c)
 * and scale to the codec's 24 bits. Everything moves once per control tick (32 samples); amplitude ramps across
 * it. */
#include <stdint.h>
#include "fastmath.h"
#include "plat.h"                       /* the second core only: plat_cpu2_* */
#ifndef FM1B_CPU2
#define FM1B_CPU2 0
#endif
#include "engine.h"
#include "wt.h"
#include "fx.h"

#define MAXBLOCK 256u
#define PI_F 3.14159265f
#define MAX_INC 0.45f

patch_t en_patch;

typedef struct { float level; uint8_t stage; } env_t;   /* 0 idle, 1 attack, 2 decay, 3 sustain, 4 release */
typedef struct {
    uint8_t note, vel, gate, active, pedal;   /* pedal: released while sustained, waiting for the pedal */
    uint32_t age;
    float pitch, pitch_tgt;
    float ph1, ph2, phs, tri1, tri2, s2;
    float ic1, ic2, dcx, dcy;            /* the filter's state; the DC blocker's */
    float amp, drift, off1, off2, pan, rnd, press, shape1, shape2;
    env_t aenv, fenv, menv;
    uint32_t rng;
    fm_state_t fm;                       /* oscillator A as FM6 */
} voice_t;

typedef struct {                         /* one oscillator's settings for a tick */
    uint32_t model;
    float pw, pwdc, pd_k, pd_a, pd_b, ffrac;
    const int16_t *f0, *f1;
} oscp_t;

typedef struct {                         /* what a block shares between its voices */
    float aa, ad, as, ar, fa, fd, fs, fr, ma, mdc, mlvl;
    float glide, level, vel_amp, keytrk, fenv, cut_oct, res, drive, drift, semi, fine;
    float lfo1_pitch, lfo1_cut, lvl1, lvl2, lsub, lnoi, xmod, ring, shape1, shape2, macro[4];
    const fm_voice_t *fmv;               /* the FM voice when model1 is FM6, else 0 */
    float fm_rate, fm_bright, fm_fb;
    uint8_t model1, model2, ft, sync, mloop, need_b;
    uint8_t nslots, src[EN_NSLOTS], dst[EN_NSLOTS];
    float amt[EN_NSLOTS];
    float lfo1_v[MAXBLOCK / EN_CTL], lfo2_v[MAXBLOCK / EN_CTL];   /* the LFOs at each tick of the block */
} blk_t;
static blk_t g_blk PLAT_POOL;             /* the block's settings, shared with the second core */

static voice_t vc[EN_NVOICES];
static uint32_t age_ctr, voices_max = EN_NVOICES;
static int32_t last_note = -1;
static float last_pitch = 60.0f, bend, wheel, chan_press;
static uint8_t press[128];
static float lfo1_ph, lfo1_out, lfo1_sh, lfo2_ph, lfo2_out, lfo2_sh;
static uint32_t lfo_rng = 0x9E3779B9u;
static float cut_s = 64.0f, res_s, gain_now;
static float mixl[MAXBLOCK] PLAT_POOL, mixr[MAXBLOCK] PLAT_POOL;     /* this core's voices */
static float mixl2[MAXBLOCK] PLAT_POOL, mixr2[MAXBLOCK] PLAT_POOL;   /* the second core's */
static int16_t *wt;
static int have_arena;
static const fm_voice_t *fm_voices;
void en_fm_voices_set(const fm_voice_t *v) { fm_voices = v; }
static float sintab[1025] PLAT_POOL;
static float pk_bus, pk_out;             /* the voice bus and the output, last block: a diagnostic */
static uint32_t ovl_cool, ovl_flag, calm_blocks;  /* the load governor: one voice at a time, back when calm */
static uint32_t fx_bypass;               /* EN_FX_*: the FX layer's live switches */
static uint32_t sustain, midi_ch, clock_out;
static float volume = 1.0f, clk_acc;

typedef struct { uint8_t on, note, vel; } ev_t;
static ev_t evq[64];
static volatile uint32_t ev_w, ev_r;

/* ------------------------------------------------------------------ the arpeggiator --- */
#define ARP_MAXHELD 16u
#define ARP_MAXSEQ 64u
static struct {
    uint8_t held[ARP_MAXHELD], vel[ARP_MAXHELD], rel[ARP_MAXHELD], nheld, all_rel;   /* as played; released (latch) */
    uint8_t seq[ARP_MAXSEQ], seqlen;     /* the notes sorted, over the octaves */
    uint8_t ord[2 * ARP_MAXSEQ], period; /* the mode's order through seq, one pattern long */
    uint8_t playing[8], nplaying;        /* the notes sounding from the last step */
    uint8_t step_notes[8], step_nn;      /* the last step's notes, for its ratchet repeats */
    uint8_t on_prev, running, step_due, walk, step_vel;
    uint32_t pos, now, origin, next_step, gate_off, step_len, rat_left, rat_len, rat_at, fired, turing, rng;
    uint32_t clk_count, clk_last, clk_period, due_at;
    uint8_t ext;
} arp;
static uint32_t bpm = 120;
static void voice_note_on(uint32_t note, uint32_t vel);
static void voice_note_off(uint32_t note, uint32_t pedal);

static inline float rnd01(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

static inline float blep(float t, float dt)
{
    if (t < dt) {
        t *= 1.0f / dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * (1.0f / dt);
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
static inline float sin_turns(float ph)      /* a 1024-point table: the polynomial costs too much per sample here */
{
    float x;
    uint32_t i;
    if (ph >= 1.0f)
        ph -= 1.0f;
    x = ph * 1024.0f;
    i = (uint32_t)x;
    return sintab[i] + (sintab[i + 1u] - sintab[i]) * (x - (float)i);
}
static inline float softclip(float x)        /* cubic, smooth to +-1 at +-1.5: the drive and the voice bus */
{
    x = x > 1.5f ? 1.5f : x < -1.5f ? -1.5f : x;
    return x - x * x * x * (4.0f / 27.0f);
}
static inline float p01(int id) { return (float)en_patch.p[id] * (1.0f / 127.0f); }
static inline float pbi(int id)
{
    float v = ((float)en_patch.p[id] - 64.0f) * (1.0f / 63.0f);
    return v < -1.0f ? -1.0f : v;
}
static inline float note_hz(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
/* a one-pole coefficient per control tick so that k time constants fit in t seconds */
static inline float tick_coef(float t, float k) { return 1.0f - fm_expf(-(float)EN_CTL * k / (t * EN_FS)); }

float en_cutoff_hz(uint32_t p) { return 20.0f * fm_exp2f((float)p * (9.8f / 127.0f)); }
float en_time_s(uint32_t p) { return 0.001f * fm_exp2f((float)p * (13.3f / 127.0f)); }
float en_dly_time_s(uint32_t p) { return 0.02f * fm_exp2f((float)p * (5.38f / 127.0f)); }

/* ------------------------------------------------------------------ voices --- */
static inline void env_tick(env_t *e, float a, float d, float s, float r)
{
    switch (e->stage) {
    case 1:
        e->level += (1.25f - e->level) * a;  /* aims past 1 so the attack ends with slope */
        if (e->level >= 1.0f) {
            e->level = 1.0f;
            e->stage = 2;
        }
        break;
    case 2:
        e->level += (s - e->level) * d;
        if (fm_fabsf(e->level - s) < 0.001f) {
            e->level = s;
            e->stage = 3;
        }
        break;
    case 3:
        e->level += (s - e->level) * d;      /* follows a live change of SUS */
        break;
    case 4:
        e->level -= e->level * r;
        if (e->level < 0.0003f) {
            e->level = 0.0f;
            e->stage = 0;
        }
        break;
    default:
        break;
    }
}

static inline void menv_tick(env_t *e, float a, float d, int loop)   /* attack to 1, decay to 0; loops if asked */
{
    if (e->stage == 1) {
        e->level += (1.25f - e->level) * a;
        if (e->level >= 1.0f) {
            e->level = 1.0f;
            e->stage = 2;
        }
    } else if (e->stage == 2) {
        e->level -= e->level * d;
        if (e->level < 0.002f) {
            e->level = 0.0f;
            e->stage = loop ? 1u : 0u;
        }
    }
}

static void voice_start(voice_t *v, uint32_t note, uint32_t vel)
{
    static const float PANPOS[EN_NVOICES] = {-1.0f, 1.0f, -0.5f, 0.5f, -0.75f, 0.75f, -0.25f, 0.25f};
    int sounding = v->active && v->aenv.stage;
    if (!v->active) {                        /* a fresh start; a retrigger keeps its phases and filter state */
        v->ph1 = rnd01(&v->rng);
        v->ph2 = rnd01(&v->rng);
        v->phs = 0.0f;
        v->tri1 = v->tri2 = v->s2 = 0.0f;
        v->ic1 = v->ic2 = v->dcx = v->dcy = 0.0f;
        v->amp = 0.0f;
        v->aenv.level = v->fenv.level = v->menv.level = 0.0f;
        v->shape1 = p01(P_OSC1_SHAPE);
        v->shape2 = p01(P_OSC2_SHAPE);
    }
    v->note = (uint8_t)note;
    v->vel = (uint8_t)(vel ? vel : 1u);
    v->gate = 1;
    v->pedal = 0;
    v->active = 1;
    v->age = ++age_ctr;
    v->pitch_tgt = (float)note;
    v->pitch = en_patch.p[P_GLIDE] ? (sounding ? v->pitch : last_pitch) : v->pitch_tgt;
    v->aenv.stage = 1;
    v->fenv.stage = 1;
    v->menv.stage = 1;
    if (en_patch.p[P_OSC1_MODEL] == MODEL_FM6 && fm_voices) {
        uint32_t fv = en_patch.p[P_FM_VOICE] <= FM_LIVE ? en_patch.p[P_FM_VOICE] : 0u;
        fm_note_on(&v->fm, &fm_voices[fv], v->pitch_tgt, (float)v->vel * (1.0f / 127.0f), sounding);
    }
    {                                        /* SPREAD, off at the centre: up, the keyboard across the stereo field (low
                                              * notes left); down, the voices at alternating places (the classic spread) */
        float sp = ((float)en_patch.p[P_SPREAD] - 64.0f) * (1.0f / 63.0f);
        if (sp > 0.0f) {
            float kp = ((float)note - 60.0f) * (1.0f / 30.0f);
            v->pan = sp * (kp < -1.0f ? -1.0f : kp > 1.0f ? 1.0f : kp);
        } else
            v->pan = -sp * PANPOS[v - vc];
    }
    v->rnd = rnd01(&v->rng) * 2.0f - 1.0f;
    v->press = 0.0f;
    last_pitch = v->pitch_tgt;
    last_note = (int32_t)note;
}

static voice_t *voice_pick(uint32_t note)
{
    voice_t *best = 0;
    uint32_t i;
    for (i = 0; i < voices_max; i++)         /* the same key still held: its voice again */
        if (vc[i].active && vc[i].gate && vc[i].note == note)
            return &vc[i];
    for (i = 0; i < voices_max; i++)
        if (!vc[i].active)
            return &vc[i];
    for (i = 0; i < voices_max; i++)         /* else the oldest released voice */
        if (!vc[i].gate && (!best || vc[i].age < best->age))
            best = &vc[i];
    if (best)
        return best;
    for (i = 0; i < voices_max; i++)         /* else the oldest of all */
        if (!best || vc[i].age < best->age)
            best = &vc[i];
    return best;
}

static void voice_note_on(uint32_t note, uint32_t vel) { voice_start(voice_pick(note & 127u), note & 127u, vel & 127u); }

static void voice_release(voice_t *v)
{
    v->gate = 0;
    v->pedal = 0;
    v->aenv.stage = 4;
    v->fenv.stage = 4;
    fm_note_off(&v->fm);
}

static void voice_note_off(uint32_t note, uint32_t pedal)   /* pedal: the sustain pedal is down, the note waits for it */
{
    uint32_t i;
    for (i = 0; i < EN_NVOICES; i++)
        if (vc[i].active && vc[i].gate && vc[i].note == note) {
            if (pedal)
                vc[i].pedal = 1;
            else
                voice_release(&vc[i]);
        }
}

static void arp_note_on(uint32_t note, uint32_t vel);
static void arp_note_off(uint32_t note);
static void arp_clear(void);
static void arp_notes_off(void);
static void arp_build(void);

void en_note_on_now(uint32_t note, uint32_t vel)
{
    if (!vel) {
        en_note_off_now(note);
        return;
    }
    if (en_patch.p[P_ARP_ON] > 63u)
        arp_note_on(note & 127u, vel & 127u);
    else
        voice_note_on(note, vel);
}

void en_note_off_now(uint32_t note)
{
    if (en_patch.p[P_ARP_ON] > 63u)
        arp_note_off(note & 127u);
    else
        voice_note_off(note, sustain);
}

void en_note_on(uint32_t note, uint32_t vel)
{
    uint32_t w = ev_w;
    if (((w + 1u) & 63u) == ev_r)
        return;
    evq[w].on = 1;
    evq[w].note = (uint8_t)note;
    evq[w].vel = (uint8_t)vel;
    __asm__ volatile("" ::: "memory");       /* the payload lands before the index the interrupt reads */
    ev_w = (w + 1u) & 63u;
}

void en_note_off(uint32_t note)
{
    uint32_t w = ev_w;
    if (((w + 1u) & 63u) == ev_r)
        return;
    evq[w].on = 0;
    evq[w].note = (uint8_t)note;
    evq[w].vel = 0;
    __asm__ volatile("" ::: "memory");
    ev_w = (w + 1u) & 63u;
}

static void drain(void)
{
    while (ev_r != ev_w) {
        ev_t e = evq[ev_r];
        ev_r = (ev_r + 1u) & 63u;
        if (e.on)
            en_note_on_now(e.note, e.vel);
        else
            en_note_off_now(e.note);
    }
}

void en_pressure(uint32_t note, uint32_t v) { press[note & 127u] = (uint8_t)(v & 127u); }
void en_channel_pressure(uint32_t v) { chan_press = (float)(v & 127u) * (1.0f / 127.0f); }
void en_wheel(uint32_t v) { wheel = (float)(v & 127u) * (1.0f / 127.0f); }
void en_bend(uint32_t bend14) { bend = ((float)(int32_t)bend14 - 8192.0f) * (2.0f / 8192.0f); }

void en_all_off(void)
{
    uint32_t i;
    arp_clear();
    for (i = 0; i < EN_NVOICES; i++)
        if (vc[i].active && vc[i].gate)
            voice_release(&vc[i]);
}

void en_sustain(uint32_t on)
{
    uint32_t i;
    sustain = on > 63u;
    if (!sustain) {
        for (i = 0; i < EN_NVOICES; i++) /* the pedal lifts: what it held goes */
            if (vc[i].active && vc[i].gate && vc[i].pedal)
                voice_release(&vc[i]);
        if (en_patch.p[P_ARP_ON] > 63u && en_patch.p[P_ARP_LATCH] <= 63u) {   /* the arp: notes it latched for the pedal go */
            uint32_t n = 0, any = 0;
            for (i = 0; i < arp.nheld; i++)
                if (!arp.rel[i]) {
                    arp.held[n] = arp.held[i];
                    arp.vel[n] = arp.vel[i];
                    arp.rel[n] = 0;
                    n++;
                    any = 1;
                }
            arp.nheld = (uint8_t)n;
            arp.all_rel = 0;
            arp_build();
            if (!any)
                arp_clear();
        }
    }
}
void en_volume(uint32_t cc) { volume = (float)(cc & 127u) * (1.0f / 127.0f); }
void en_midi_channel(uint32_t ch0) { midi_ch = ch0 & 15u; }
void en_clock_out(int on) { clock_out = on != 0; }
void en_reset_controllers(void)          /* (not the volume: MIDI RP-015 leaves CC 7 alone) */
{
    uint32_t i;
    wheel = 0.0f;
    bend = 0.0f;
    chan_press = 0.0f;
    for (i = 0; i < 128u; i++)
        press[i] = 0;
    en_sustain(0);
}

void en_panic(void)
{
    uint32_t i;
    ev_r = ev_w;
    for (i = 0; i < EN_NVOICES; i++) {
        vc[i].active = vc[i].gate = 0;
        vc[i].aenv.stage = vc[i].fenv.stage = vc[i].menv.stage = 0;
        vc[i].aenv.level = vc[i].fenv.level = vc[i].menv.level = 0.0f;
        vc[i].amp = 0.0f;
    }
    for (i = 0; i < 128u; i++)
        press[i] = 0;
    chan_press = 0.0f;
    last_note = -1;
    arp_clear();                             /* its notes off and Stop still go out */
}

int en_init(void *arena, uint32_t bytes)
{
    uint32_t i, seed = 0x2545F491u;
    for (i = 0; i < EN_NVOICES; i++) {
        vc[i].rng = seed = seed * 1664525u + 1013904223u;
        vc[i].off1 = rnd01(&vc[i].rng) - 0.5f;   /* each voice its own slight detune, like eight real oscillators */
        vc[i].off2 = rnd01(&vc[i].rng) - 0.5f;
    }
    for (i = 0; i <= 1024u; i++)
        sintab[i] = fm_sin_turns((float)i / 1024.0f);
    fm_init();
    en_panic();
    voices_max = EN_NVOICES;
    ovl_cool = ovl_flag = calm_blocks = 0;
    have_arena = arena && bytes >= WT_BYTES + fx_bytes();
    if (have_arena) {
        wt = (int16_t *)arena;
        wt_build(wt);
        fx_init((uint8_t *)arena + WT_BYTES);
    }
    return have_arena ? 0 : -1;
}

uint32_t en_voices(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < EN_NVOICES; i++)
        n += vc[i].active;
    return n;
}

int32_t en_sounding_note(void)
{
    const voice_t *best = 0;
    uint32_t i;
    for (i = 0; i < EN_NVOICES; i++)
        if (vc[i].active && (!best || vc[i].age > best->age))
            best = &vc[i];
    return best ? best->note : -1;
}

uint32_t en_voices_max(void) { return voices_max; }

void en_overload(void)                       /* the platform, from the render: it ran over 90 % */
{
    ovl_flag = 1;
    if (!ovl_cool && voices_max > 3u) {
        voices_max--;
        ovl_cool = 90;                       /* about half a second before the next */
    }
}

/* ------------------------------------------------------------------ the arp's steps --- */
static const float DIV_BEATS[DIV_COUNT] = {1.0f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};
static const uint8_t DIV_CLOCKS[DIV_COUNT] = {24, 16, 12, 8, 6, 4, 3};   /* MIDI clocks a step */

static void arp_midi(uint32_t on, uint32_t note, uint32_t vel)
{
    plat_midi_send((on ? 0x09u | (0x90u | midi_ch) << 8 : 0x08u | (0x80u | midi_ch) << 8) | note << 16 | (on ? vel : 0u) << 24,
                   TO_USB | TO_BLE);
}

static void arp_notes_off(void)
{
    uint32_t i;
    for (i = 0; i < arp.nplaying; i++) {
        voice_note_off(arp.playing[i], 0);   /* the arp's own steps: never held by the pedal */
        arp_midi(0, arp.playing[i], 0);
    }
    arp.nplaying = 0;
}

/* seq: the held notes sorted, repeated an octave up for each extra octave; ord: the mode's path through it */
static void arp_build(void)
{
    const uint8_t *p = en_patch.p;
    uint32_t octs = 1u + p[P_ARP_OCT] * 3u / 127u, mode = p[P_ARP_MODE], i, j, n = 0, o;
    uint8_t sorted[ARP_MAXHELD];
    for (i = 0; i < arp.nheld; i++) {       /* insertion sort, as played -> ascending */
        uint32_t v = arp.held[i];
        for (j = n; j > 0 && sorted[j - 1] > v; j--)
            sorted[j] = sorted[j - 1];
        sorted[j] = (uint8_t)v;
        n++;
    }
    arp.seqlen = 0;
    for (o = 0; o < octs; o++)
        for (i = 0; i < n && arp.seqlen < ARP_MAXSEQ; i++) {
            uint32_t note = (mode == ARP_ORDER ? arp.held[i] : sorted[i]) + 12u * o;
            if (note < 128u)
                arp.seq[arp.seqlen++] = (uint8_t)note;
        }
    n = arp.seqlen;
    arp.period = 0;
    if (!n)
        return;
    switch (mode) {
    case ARP_DOWN:
        for (i = 0; i < n; i++)
            arp.ord[arp.period++] = (uint8_t)(n - 1u - i);
        break;
    case ARP_UPDN:
        for (i = 0; i < n; i++)
            arp.ord[arp.period++] = (uint8_t)i;
        for (i = n > 2u ? n - 2u : 0u; i > 0; i--)
            arp.ord[arp.period++] = (uint8_t)i;
        break;
    case ARP_CONV:
        for (i = 0; i < n; i++)
            arp.ord[arp.period++] = (uint8_t)((i & 1u) ? n - 1u - i / 2u : i / 2u);
        break;
    case ARP_DIVG:
        for (i = 0; i < n; i++)
            arp.ord[arp.period++] = (uint8_t)((i & 1u) ? n - 1u - i / 2u : i / 2u);
        for (i = 0; i < n / 2u; i++) {      /* the converging path, read backwards */
            uint8_t t = arp.ord[i];
            arp.ord[i] = arp.ord[n - 1u - i];
            arp.ord[n - 1u - i] = t;
        }
        break;
    case ARP_PINKY:
        for (i = 0; i + 1u < n; i++) {
            arp.ord[arp.period++] = (uint8_t)(n - 1u);
            arp.ord[arp.period++] = (uint8_t)i;
        }
        if (n == 1u)
            arp.ord[arp.period++] = 0;
        break;
    case ARP_THUMB:
        for (i = 1; i < n; i++) {
            arp.ord[arp.period++] = 0;
            arp.ord[arp.period++] = (uint8_t)i;
        }
        if (n == 1u)
            arp.ord[arp.period++] = 0;
        break;
    default:                                /* UP, ORDER; RANDOM / WALK / TURING / CHORD pick their own */
        for (i = 0; i < n; i++)
            arp.ord[arp.period++] = (uint8_t)i;
        break;
    }
}

static void arp_start_run(void)
{
    if (clock_out && !arp.ext)
        plat_midi_send(0x0Fu | 0xFAu << 8, TO_USB | TO_BLE);   /* start */
    clk_acc = 0.0f;
    arp.running = 1;
    arp.pos = 0;
    arp.origin = arp.now;
    arp.next_step = arp.now;
    arp.step_due = !arp.ext;
    arp.rat_left = 0;
    arp.walk = 0;
    arp.rng = (uint32_t)(en_patch.p[P_ARP_SEED_LO] | en_patch.p[P_ARP_SEED_HI] << 7) * 2654435761u + 1u;
    arp.turing = arp.rng;
}

static void arp_note_on(uint32_t note, uint32_t vel)
{
    uint32_t i;
    if (arp.all_rel) {                       /* latched and every key up: a new chord replaces the old */
        arp.nheld = 0;
        arp.all_rel = 0;
    }
    for (i = 0; i < arp.nheld; i++)
        if (arp.held[i] == note) {
            arp.rel[i] = 0;
            arp.vel[i] = (uint8_t)vel;
            return;
        }
    if (arp.nheld < ARP_MAXHELD) {
        arp.held[arp.nheld] = (uint8_t)note;
        arp.vel[arp.nheld] = (uint8_t)vel;
        arp.rel[arp.nheld] = 0;
        arp.nheld++;
    }
    arp_build();
    if (!arp.running)
        arp_start_run();
}

static void arp_note_off(uint32_t note)
{
    uint32_t i, j, any = 0;
    for (i = 0; i < arp.nheld; i++)
        if (arp.held[i] == note) {
            if (en_patch.p[P_ARP_LATCH] > 63u || sustain) {
                arp.rel[i] = 1;
            } else {
                for (j = i; j + 1u < arp.nheld; j++) {
                    arp.held[j] = arp.held[j + 1u];
                    arp.vel[j] = arp.vel[j + 1u];
                    arp.rel[j] = arp.rel[j + 1u];
                }
                arp.nheld--;
            }
            break;
        }
    for (i = 0; i < arp.nheld; i++)
        any |= !arp.rel[i];
    arp.all_rel = arp.nheld && !any;
    arp_build();
    if (!arp.nheld) {
        if (clock_out && arp.running && !arp.ext)
            plat_midi_send(0x0Fu | 0xFCu << 8, TO_USB | TO_BLE);
        arp_notes_off();
        arp.running = 0;
    }
}

static void arp_clear(void)
{
    if (clock_out && arp.running && !arp.ext)
        plat_midi_send(0x0Fu | 0xFCu << 8, TO_USB | TO_BLE);   /* stop */
    arp_notes_off();
    arp.nheld = 0;
    arp.all_rel = 0;
    arp.running = 0;
}

static float arp_beat_samples(void)
{
    if (arp.ext && arp.clk_period)
        return (float)arp.clk_period * 24.0f;
    return 60.0f * EN_FS / (float)bpm;
}

/* one step: the Euclidean mask, the seed, the chance, the mode's note(s), the ratchet, the gate */
static void arp_step(void)
{
    const uint8_t *p = en_patch.p;
    uint32_t steps = 1u + p[P_ARP_STEPS] * 15u / 127u, hits = p[P_ARP_HITS] * 16u / 127u, rot = p[P_ARP_ROT] * 15u / 127u;
    uint32_t mode = p[P_ARP_MODE], n = arp.seqlen, i, k, hit, div = p[P_ARP_DIV] < DIV_COUNT ? p[P_ARP_DIV] : DIV_16;
    float L = arp_beat_samples() * DIV_BEATS[div], gate = (5.0f + (float)p[P_ARP_GATE] * (195.0f / 127.0f)) * 0.01f;
    float swing = (float)p[P_ARP_SWING] * (1.0f / 127.0f) * L * (1.0f / 3.0f);
    uint8_t notes[8];
    uint32_t nn = 0, vel = 100;
    if (hits > steps)
        hits = steps;
    arp.step_len = (uint32_t)L;
    if (arp.pos % steps == 0u && p[P_ARP_SLOCK] > 63u)   /* the pattern repeats: the same randomness each time */
        arp.rng = (uint32_t)(p[P_ARP_SEED_LO] | p[P_ARP_SEED_HI] << 7) * 2654435761u + 1u;
    hit = hits && ((((arp.pos % steps) + rot) * hits) % steps) < hits;   /* Bjorklund by Bresenham */
    if (hit && n && rnd01(&arp.rng) < (float)p[P_ARP_PROB] * (1.0f / 127.0f)) {
        switch (mode) {
        case ARP_CHORD:
            for (i = 0; i < n && nn < 8u; i++)
                notes[nn++] = arp.seq[i];
            break;
        case ARP_RANDOM:
            notes[nn++] = arp.seq[(uint32_t)(rnd01(&arp.rng) * (float)n) % n];
            break;
        case ARP_WALK:
            arp.walk = (uint8_t)((arp.walk + (rnd01(&arp.rng) < 0.5f ? n - 1u : 1u)) % n);
            notes[nn++] = arp.seq[arp.walk];
            break;
        case ARP_TURING: {
            uint32_t len = 2u + p[P_ARP_TLEN] * 14u / 127u, out = (arp.turing >> (len - 1u)) & 1u, bit;
            bit = rnd01(&arp.rng) < (float)p[P_ARP_TLOCK] * (1.0f / 127.0f) ? out : (rnd01(&arp.rng) < 0.5f);
            arp.turing = ((arp.turing << 1) | bit) & ((1u << len) - 1u);
            notes[nn++] = arp.seq[(arp.turing >> (len > 6u ? len - 6u : 0u)) % n];
            break;
        }
        default:
            k = arp.ord[arp.period ? arp.pos % arp.period : 0u];
            notes[nn++] = arp.seq[k < n ? k : 0u];
            break;
        }
        for (i = 0; i < arp.nheld; i++)      /* the step's velocity: its key's (any octave), else the first key's */
            if (i == 0u || (notes[0] >= arp.held[i] && (notes[0] - arp.held[i]) % 12u == 0u))
                vel = arp.vel[i];
    }
    arp.rat_left = 0;                        /* a new step ends a pending ratchet */
    arp.step_vel = (uint8_t)vel;
    arp.step_nn = (uint8_t)nn;
    for (i = 0; i < nn; i++)
        arp.step_notes[i] = notes[i];
    if (gate >= 1.0f) {                      /* a tie: the new notes start before the old ones stop */
        uint8_t prev[8];
        uint32_t np = arp.nplaying;
        for (i = 0; i < np; i++)
            prev[i] = arp.playing[i];
        arp.nplaying = 0;
        for (i = 0; i < nn; i++) {
            uint32_t held = 0;
            for (k = 0; k < np; k++)
                held |= notes[i] == prev[k];
            if (!held) {                     /* a note that goes on is neither re-sent nor re-attacked */
                voice_note_on(notes[i], vel);
                arp_midi(1, notes[i], vel);
            }
            arp.playing[arp.nplaying++] = notes[i];
        }
        for (i = 0; i < np; i++) {
            uint32_t still = 0;
            for (k = 0; k < nn; k++)
                still |= notes[k] == prev[i];
            if (!still) {
                voice_note_off(prev[i], 0);
                arp_midi(0, prev[i], 0);
            }
        }
        arp.gate_off = 0;
    } else {
        arp_notes_off();
        for (i = 0; i < nn; i++) {
            voice_note_on(notes[i], vel);
            arp_midi(1, notes[i], vel);
            arp.playing[arp.nplaying++] = notes[i];
        }
        arp.gate_off = nn ? arp.now + (uint32_t)(L * gate) : 0u;
    }
    arp.fired += nn;
    if (nn && rnd01(&arp.rng) < (float)p[P_ARP_RATCHET] * (1.0f / 127.0f)) {   /* a ratchet: the note again, twice or thrice */
        uint32_t sub = rnd01(&arp.rng) < 0.6f ? 2u : 3u;
        arp.rat_left = sub - 1u;
        arp.rat_len = (uint32_t)(L / (float)sub);
        arp.rat_at = arp.now + arp.rat_len;  /* the repeats land at even parts of the step */
        arp.gate_off = arp.now + (uint32_t)((float)arp.rat_len * (gate < 1.0f ? gate : 0.8f));
    }
    arp.pos++;
    if (!arp.ext)
        arp.next_step = arp.origin + (uint32_t)((float)arp.pos * L) + ((arp.pos & 1u) ? (uint32_t)swing : 0u);
}

static void arp_ratchet(void)                /* a ratchet's repeat of the last step's notes */
{
    uint32_t i, n = arp.step_nn;
    arp_notes_off();
    for (i = 0; i < n; i++) {
        voice_note_on(arp.step_notes[i], arp.step_vel);
        arp_midi(1, arp.step_notes[i], arp.step_vel);
        arp.playing[arp.nplaying++] = arp.step_notes[i];
    }
    arp.fired += n;
    arp.gate_off = arp.now + (uint32_t)((float)arp.rat_len * 0.6f);
}

static void arp_tick(uint32_t m)             /* every control tick, before the voices */
{
    uint32_t on = en_patch.p[P_ARP_ON] > 63u;
    if (on != arp.on_prev) {
        uint32_t i;
        arp.on_prev = (uint8_t)on;
        if (!on)
            arp_clear();
        else
            for (i = 0; i < EN_NVOICES; i++) /* notes the keys gated: their note-offs now reach the arp, not them */
                if (vc[i].active && vc[i].gate)
                    voice_release(&vc[i]);
    }
    arp.now += m;
    if (arp.ext && arp.now - arp.clk_last > (uint32_t)(2.0f * EN_FS)) {
        arp.ext = 0;                         /* the clock stopped: the internal tempo goes on from here */
        if (arp.running) {
            const uint8_t *p = en_patch.p;
            uint32_t div = p[P_ARP_DIV] < DIV_COUNT ? p[P_ARP_DIV] : DIV_16;
            arp.origin = arp.now - (uint32_t)((float)arp.pos * arp_beat_samples() * DIV_BEATS[div]);
            arp.next_step = arp.now;
        }
    }
    if (!on || !arp.running)
        return;
    if (clock_out && !arp.ext) {             /* 24 clocks a beat from the internal tempo */
        float per = arp_beat_samples() * (1.0f / 24.0f);
        clk_acc += (float)m;
        while (clk_acc >= per) {
            clk_acc -= per;
            plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB | TO_BLE);
        }
    }
    if (arp.gate_off && arp.now >= arp.gate_off) {
        arp.gate_off = 0;
        arp_notes_off();
    }
    if (arp.rat_left && arp.now >= arp.rat_at) {
        arp.rat_left--;
        arp.rat_at += arp.rat_len;
        arp_ratchet();
    }
    if (arp.ext) {
        if (arp.step_due && arp.now >= arp.due_at) {
            arp.step_due = 0;
            arp_step();
        }
    } else if (arp.now >= arp.next_step) {
        arp_step();
    }
}

void en_tempo(uint32_t v) { bpm = v < 40u ? 40u : v > 240u ? 240u : v; }
uint32_t en_tempo_get(void) { return bpm; }

void en_clock(void)
{
    const uint8_t *p = en_patch.p;
    uint32_t div = p[P_ARP_DIV] < DIV_COUNT ? p[P_ARP_DIV] : DIV_16, cps = DIV_CLOCKS[div];
    if (arp.ext && arp.now > arp.clk_last)
        arp.clk_period = arp.clk_period ? (arp.clk_period * 3u + (arp.now - arp.clk_last)) / 4u : arp.now - arp.clk_last;
    arp.clk_last = arp.now;
    if (!arp.ext)
        arp.step_due = 0;                    /* the internal schedule's pending step is not the clock's */
    arp.ext = 1;
    if (arp.clk_count % cps == 0u && arp.running) {
        float L = arp_beat_samples() * DIV_BEATS[div];
        arp.step_due = 1;                    /* the odd steps swing late */
        arp.due_at = arp.now + (((arp.clk_count / cps) & 1u) ? (uint32_t)((float)p[P_ARP_SWING] * (1.0f / 127.0f) * L / 3.0f) : 0u);
    }
    arp.clk_count++;
}

void en_clock_start(void)
{
    arp.clk_count = 0;
    arp.pos = 0;
    arp.ext = 1;
    arp.clk_last = arp.now;
}

void en_clock_stop(void) { arp_notes_off(); }

int en_arp_running(void) { return arp.running && en_patch.p[P_ARP_ON] > 63u; }
uint32_t en_arp_held(void) { return arp.nheld; }
uint32_t en_arp_step(void) { return arp.pos; }
uint32_t en_arp_fired(void) { return arp.fired; }
void en_arp_fired_reset(void) { arp.fired = 0; }
int en_clock_ext(void) { return arp.ext; }

void en_fx_bypass(uint32_t bits) { fx_bypass = bits & (EN_FX_ENS | EN_FX_DLY | EN_FX_REV | EN_FX_DRIVE); }
uint32_t en_fx_bypass_get(void) { return fx_bypass; }
uint32_t en_fx_running(void) { return fx_running(); }
uint32_t en_fx_active(void)              /* what the patch has on, less the bypassed */
{
    const uint8_t *p = en_patch.p;
    uint32_t a = 0;
    if (p[P_ENS_MODE] != ENS_OFF && p[P_ENS_MIX])
        a |= EN_FX_ENS;
    if (p[P_DLY_SEND])
        a |= EN_FX_DLY;
    if (p[P_REV_SEND])
        a |= EN_FX_REV;
    if (p[P_DRIVE])
        a |= EN_FX_DRIVE;
    return a & ~fx_bypass;
}

/* the diagnostic meters: the voice bus and the output (last block), the effects' stages (fx.c) */
void en_peaks(float *bus, float *out, float *ens, float *dly, float *rev)
{
    *bus = pk_bus;
    *out = pk_out;
    fx_peaks(ens, dly, rev);
}

/* ------------------------------------------------------------------ oscillators --- */
/* an oscillator's settings for this tick: the pulse width, the phase-distortion bend, the wavetable frames */
static void osc_setup(oscp_t *o, uint32_t model, float shape, float inc)
{
    o->model = model;
    o->pw = 0.5f + shape * 0.45f;
    o->pwdc = 2.0f * o->pw - 1.0f;
    if (model == MODEL_PD) {
        float hz = inc * EN_FS, d = shape * (hz < 2500.0f ? 1.0f : 2500.0f / hz), k = 0.5f - 0.49f * d;
        o->pd_k = k;
        o->pd_a = 0.5f / k;
        o->pd_b = 0.5f / (1.0f - k);
    } else if (model >= MODEL_WT_A) {
        if (!have_arena) {
            o->model = MODEL_SAW;
            return;
        }
        {
            float maxh = MAX_INC / inc, pos = shape * (float)(WT_FRAMES - 1u);
            uint32_t mip = maxh >= 64.0f ? 0u : maxh >= 32.0f ? 1u : maxh >= 16.0f ? 2u : maxh >= 8.0f ? 3u : 4u;
            uint32_t f = (uint32_t)pos;
            if (f >= WT_FRAMES - 1u)
                f = WT_FRAMES - 2u;
            o->ffrac = pos - (float)f;
            o->f0 = wt_frame(wt, model - MODEL_WT_A, mip, f);
            o->f1 = o->f0 + WT_LEN;
        }
    }
}

static inline float osc_read(const oscp_t *o, float ph, float dt, float *tri)
{
    float t2, s;
    switch (o->model) {
    case MODEL_PULSE:
        t2 = ph + 1.0f - o->pw;
        if (t2 >= 1.0f)
            t2 -= 1.0f;
        return (ph < o->pw ? 1.0f : -1.0f) + blep(ph, dt) - blep(t2, dt) - o->pwdc;
    case MODEL_TRI:
        t2 = ph + 0.5f;
        if (t2 >= 1.0f)
            t2 -= 1.0f;
        s = (ph < 0.5f ? 1.0f : -1.0f) + blep(ph, dt) - blep(t2, dt);
        *tri = *tri * 0.9995f + s * (4.0f * dt);
        return *tri;
    case MODEL_SINE:
        return sin_turns(ph);
    case MODEL_PD:
        s = ph < o->pd_k ? ph * o->pd_a : 0.5f + (ph - o->pd_k) * o->pd_b;
        return sin_turns(s + 0.25f);
    case MODEL_WT_A:
    case MODEL_WT_B:
    case MODEL_WT_C: {
        float x = ph * (float)WT_LEN, a, b;
        uint32_t i = (uint32_t)x, j = (i + 1u) & (WT_LEN - 1u);
        float fr = x - (float)i;
        i &= WT_LEN - 1u;
        a = (float)o->f0[i] + ((float)o->f0[j] - (float)o->f0[i]) * fr;
        b = (float)o->f1[i] + ((float)o->f1[j] - (float)o->f1[i]) * fr;
        return (a + (b - a) * o->ffrac) * (1.0f / 32768.0f);
    }
    default:
        return 2.0f * ph - 1.0f - blep(ph, dt);
    }
}

/* ------------------------------------------------------------------ render --- */
static inline float source(const voice_t *v, const blk_t *b, uint32_t src, float vel, uint32_t t)
{
    switch (src) {
    case MS_LFO1: return b->lfo1_v[t];
    case MS_LFO2: return b->lfo2_v[t];
    case MS_MENV: return v->menv.level * b->mlvl;
    case MS_VEL: return vel;
    case MS_KEY: return (v->pitch_tgt - 60.0f) * (1.0f / 24.0f);
    case MS_PRESS: return v->press;
    case MS_WHEEL: return wheel;
    case MS_RAND: return v->rnd;
    case MS_MACRO1: case MS_MACRO2: case MS_MACRO3: case MS_MACRO4: return b->macro[src - MS_MACRO1];
    default: return 0.0f;
    }
}

static void voice_chunk(voice_t *v, const blk_t *b, uint32_t i0, uint32_t m, uint32_t t, float *ml, float *mr)
{
    float lfo1 = b->lfo1_v[t];
    float md[MD_COUNT], pitch, fc, g, k, a1, a2, a3, inc1, inc2, vel, amp_step, xidx, ring, pan, pt, ratio;
    float ph1 = v->ph1, ph2 = v->ph2, phs = v->phs, ic1 = v->ic1, ic2 = v->ic2, amp = v->amp, s2 = v->s2, panl, panr;
    float dcx = v->dcx, dcy = v->dcy;
    oscp_t o1, o2;
    uint32_t j, s, rng = v->rng;
    env_tick(&v->aenv, b->aa, b->ad, b->as, b->ar);
    env_tick(&v->fenv, b->fa, b->fd, b->fs, b->fr);
    menv_tick(&v->menv, b->ma, b->mdc, b->mloop);
    if (!v->aenv.stage) {
        v->active = 0;
        return;
    }
    vel = (float)v->vel * (1.0f / 127.0f);
    pt = (float)(press[v->note] > (uint8_t)(chan_press * 127.0f) ? press[v->note] : (uint8_t)(chan_press * 127.0f)) * (1.0f / 127.0f);
    v->press += (pt - v->press) * 0.15f;
    for (j = 0; j < MD_COUNT; j++)
        md[j] = 0.0f;
    for (s = 0; s < b->nslots; s++)
        md[b->dst[s]] += source(v, b, b->src[s], vel, t) * b->amt[s];
    v->pitch += (v->pitch_tgt - v->pitch) * b->glide;
    v->drift += (rnd01(&rng) - 0.5f) * 0.05f - v->drift * 0.02f;
    pitch = v->pitch + bend + lfo1 * b->lfo1_pitch + b->drift * (v->drift * 0.12f) + md[MD_PITCH] * 24.0f;
    inc1 = note_hz(pitch + b->drift * v->off1 * 0.08f) * (1.0f / EN_FS);
    inc2 = note_hz(pitch + b->semi + b->fine + b->drift * v->off2 * 0.08f + md[MD_PITCH2] * 24.0f) * (1.0f / EN_FS);
    if (inc1 > MAX_INC)
        inc1 = MAX_INC;
    if (inc2 > MAX_INC)
        inc2 = MAX_INC;
    ratio = inc2 / inc1;
    v->shape1 += (clamp01(b->shape1 + md[MD_SHAPE1]) - v->shape1) * 0.3f;
    v->shape2 += (clamp01(b->shape2 + md[MD_SHAPE2]) - v->shape2) * 0.3f;
    if (b->fmv)
        fm_tick(&v->fm, b->fmv, pitch, EN_FS, b->fm_rate, b->fm_bright, b->fm_fb);
    else
        osc_setup(&o1, b->model1, v->shape1, inc1);
    osc_setup(&o2, b->model2, v->shape2, inc2);
    xidx = clamp01(b->xmod + md[MD_XMOD]);
    xidx = xidx * xidx * 0.5f;
    ring = clamp01(b->ring + md[MD_RING]);
    fc = 20.0f * fm_exp2f(b->cut_oct + b->keytrk * (v->pitch - 60.0f) * (1.0f / 12.0f) + b->fenv * v->fenv.level
                          + lfo1 * b->lfo1_cut + md[MD_CUTOFF] * 5.0f);
    if (fc < 20.0f)
        fc = 20.0f;
    if (fc > 0.45f * EN_FS)
        fc = 0.45f * EN_FS;
    g = fm_tanf(PI_F * fc * (1.0f / EN_FS));
    k = 2.0f - 2.0f * clamp01(b->res + md[MD_RESO]) * 0.985f;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    amp_step = (v->aenv.level * (1.0f - b->vel_amp * (1.0f - vel)) * b->level * clamp01(0.5f + 0.5f * md[MD_LEVEL]) * 2.0f
                - amp) * (1.0f / (float)m);
    pan = v->pan + md[MD_PAN];
    pan = pan < -1.0f ? -1.0f : pan > 1.0f ? 1.0f : pan;
    panl = fm_cosf((pan + 1.0f) * (0.25f * PI_F));
    panr = fm_sinf((pan + 1.0f) * (0.25f * PI_F));
    for (j = 0; j < m; j++) {
        float x, s1, p1, v1, v2, v3, y;
        int wrapped = 0;
        ph1 += inc1;
        if (ph1 >= 1.0f) {
            ph1 -= 1.0f;
            wrapped = 1;
        }
        ph2 += inc2;
        if (ph2 >= 1.0f)
            ph2 -= 1.0f;
        if (b->sync && wrapped)
            ph2 = ph1 * ratio;
        phs += inc1 * 0.5f;
        if (phs >= 1.0f)
            phs -= 1.0f;
        if (b->fmv) {                        /* A is the FM voice: B mixes and rings, but cannot modulate it */
            s1 = fm_sample(&v->fm);
            if (b->need_b) {
                s2 = osc_read(&o2, ph2, inc2, &v->tri2);
                x = s1 * b->lvl1 + s2 * b->lvl2 + ring * s1 * s2;
            } else {
                x = s1 * b->lvl1;
            }
        } else if (b->need_b) {
            s2 = osc_read(&o2, ph2, inc2, &v->tri2);
            p1 = ph1 + xidx * s2;
            if (p1 >= 1.0f)
                p1 -= 1.0f;
            else if (p1 < 0.0f)
                p1 += 1.0f;
            s1 = osc_read(&o1, p1, inc1, &v->tri1);
            x = s1 * b->lvl1 + s2 * b->lvl2 + ring * s1 * s2;
        } else {
            x = osc_read(&o1, ph1, inc1, &v->tri1) * b->lvl1;
        }
        if (b->lsub > 0.0f)
            x += ((phs < 0.5f ? 1.0f : -1.0f) + blep(phs, inc1 * 0.5f)) * b->lsub;
        if (b->lnoi > 0.0f)
            x += (rnd01(&rng) * 2.0f - 1.0f) * b->lnoi;
        if (b->drive > 1.001f)
            x = softclip(x * b->drive);
        v3 = x - ic2;
        v1 = a1 * ic1 + a2 * v3;
        v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = v1 + v1 - ic1;
        ic2 = v2 + v2 - ic2;
        y = b->ft == FILT_LP ? v2 : b->ft == FILT_BP ? v1 : x - k * v1 - v2;
        dcy = y - dcx + 0.9979f * dcy;       /* no DC: hard sync and narrow pulses leave an offset */
        dcx = y;
        y = dcy;
        amp += amp_step;
        y *= amp;
        ml[i0 + j] += y * panl;
        mr[i0 + j] += y * panr;
    }
    v->ph1 = ph1;
    v->ph2 = ph2;
    v->phs = phs;
    v->s2 = s2;
    v->ic1 = ic1;
    v->ic2 = ic2;
    v->dcx = dcx;
    v->dcy = dcy;
    v->amp = amp;
    v->rng = rng;
}

static float lfo_tick(float *ph, float *sh, float inc, uint32_t shape)   /* one control tick of an LFO */
{
    *ph += inc;
    if (*ph >= 1.0f) {
        *ph -= fm_floorf(*ph);               /* (the matrix can push the rate past a cycle a tick) */
        *sh = rnd01(&lfo_rng) * 2.0f - 1.0f;
    }
    switch (shape) {
    case LFO_SINE: return sin_turns(*ph);
    case LFO_SQR: return *ph < 0.5f ? 1.0f : -1.0f;
    case LFO_SAW: return 1.0f - 2.0f * *ph;
    case LFO_SH: return *sh;
    default: return *ph < 0.5f ? 4.0f * *ph - 1.0f : 3.0f - 4.0f * *ph;
    }
}

/* voices lo..hi-1 of the block into ml / mr, every tick */
static void render_voices(uint32_t lo, uint32_t hi, uint32_t n, float *ml, float *mr)
{
    uint32_t i, v, t = 0;
    for (i = 0; i < n; i += EN_CTL, t++) {
        uint32_t m = n - i < EN_CTL ? n - i : EN_CTL;
        for (v = lo; v < hi; v++)
            if (vc[v].active)
                voice_chunk(&vc[v], &g_blk, i, m, t, ml, mr);
    }
}

static volatile uint32_t job_n;
#if FM1B_CPU2
static void half_job(uint32_t arg)       /* the second core: the first four voices */
{
    (void)arg;
    render_voices(0, EN_NVOICES / 2u, job_n, mixl2, mixr2);
}
#endif

void en_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    const uint8_t *p = en_patch.p;
    blk_t *bp = &g_blk;
    fx_params_t fx;
    float gmd[MD_COUNT], gain = (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f * volume, lfo1_inc, lfo2_inc;
    uint32_t i, s, t, on_two = 0;
    drain();
    if (n > MAXBLOCK)
        n = MAXBLOCK;
    for (i = 0; i < n; i++)
        mixl[i] = mixr[i] = mixl2[i] = mixr2[i] = 0.0f;
    if (ovl_cool)                            /* the governor: a voice back after about seven calm seconds */
        ovl_cool--;
    if (ovl_flag) {
        ovl_flag = 0;
        calm_blocks = 0;
    } else if (++calm_blocks > 1200u && voices_max < EN_NVOICES) {
        voices_max++;
        calm_blocks = 0;
    }
    cut_s += ((float)p[P_CUTOFF] - cut_s) * 0.3f;      /* the knobs glide a little: no zipper on a sweep */
    res_s += ((float)p[P_RESO] - res_s) * 0.3f;
    bp->aa = tick_coef(en_time_s(p[P_A_ATK]), 1.61f);
    bp->ad = tick_coef(en_time_s(p[P_A_DEC]), 4.0f);
    bp->as = p01(P_A_SUS);
    bp->ar = tick_coef(en_time_s(p[P_A_REL]), 4.0f);
    bp->fa = tick_coef(en_time_s(p[P_F_ATK]), 1.61f);
    bp->fd = tick_coef(en_time_s(p[P_F_DEC]), 4.0f);
    bp->fs = p01(P_F_SUS);
    bp->fr = tick_coef(en_time_s(p[P_F_REL]), 4.0f);
    bp->ma = tick_coef(en_time_s(p[P_M_ATK]), 1.61f);
    bp->mdc = tick_coef(en_time_s(p[P_M_DEC]), 4.0f);
    bp->mlvl = p01(P_M_LVL);
    bp->mloop = p[P_M_LOOP] > 63u;
    bp->glide = p[P_GLIDE] ? tick_coef(0.02f * fm_exp2f(p01(P_GLIDE) * 7.0f), 3.0f) : 1.0f;
    bp->level = p01(P_LEVEL) * 0.35f;
    bp->vel_amp = p01(P_VEL_AMP);
    bp->keytrk = p01(P_KEYTRK);
    bp->fenv = pbi(P_FENV_AMT) * 6.0f;
    bp->cut_oct = cut_s * (9.8f / 127.0f);
    bp->res = res_s * (1.0f / 127.0f);
    bp->drift = p01(P_DRIFT);
    bp->semi = fm_floorf((float)((int32_t)p[P_OSC2_SEMI] - 64) * (24.0f / 63.0f) + 0.5f);   /* whole semitones */
    bp->fine = pbi(P_OSC2_FINE) * 0.5f;
    bp->lfo1_pitch = p01(P_LFO1_PITCH) * 2.0f;
    bp->lfo1_cut = p01(P_LFO1_CUT) * 4.0f;
    bp->lvl1 = p01(P_OSC1_LVL);
    bp->lvl2 = p01(P_OSC2_LVL);
    bp->lsub = p01(P_SUB_LVL);
    bp->lnoi = p01(P_NOISE_LVL) * 0.5f;
    bp->xmod = p01(P_XMOD);
    bp->ring = p01(P_RING);
    bp->shape1 = p01(P_OSC1_SHAPE);
    bp->shape2 = p01(P_OSC2_SHAPE);
    bp->model1 = p[P_OSC1_MODEL] < MODEL_COUNT ? p[P_OSC1_MODEL] : 0u;
    bp->model2 = p[P_OSC2_MODEL] < MODEL_COUNT ? p[P_OSC2_MODEL] : 0u;
    bp->ft = p[P_FILT_TYPE];
    bp->sync = p[P_SYNC] > 63u;
    for (i = 0; i < 4u; i++)
        bp->macro[i] = p01(P_MACRO1 + (int)i);
    bp->nslots = 0;
    for (s = 0; s < EN_NSLOTS; s++) {        /* the matrix, decoded once a block */
        uint32_t src = p[P_MOD(s, 0)], dst = p[P_MOD(s, 1)];
        if (src && src < MS_COUNT && dst && dst < MD_COUNT && p[P_MOD(s, 2)] != 64u) {
            bp->src[bp->nslots] = (uint8_t)src;
            bp->dst[bp->nslots] = (uint8_t)dst;
            bp->amt[bp->nslots] = pbi(P_MOD(s, 2));
            bp->nslots++;
        }
    }
    for (i = 0; i < MD_COUNT; i++)            /* the global destinations, from the global sources */
        gmd[i] = 0.0f;
    for (s = 0; s < bp->nslots; s++) {
        float val;
        switch (bp->src[s]) {
        case MS_LFO1: val = lfo1_out; break;
        case MS_LFO2: val = lfo2_out; break;
        case MS_WHEEL: val = wheel; break;
        case MS_MACRO1: case MS_MACRO2: case MS_MACRO3: case MS_MACRO4: val = bp->macro[bp->src[s] - MS_MACRO1]; break;
        default: val = 0.0f; break;
        }
        gmd[bp->dst[s]] += val * bp->amt[s];
    }
    bp->drive = (fx_bypass & EN_FX_DRIVE) ? 1.0f : 1.0f + clamp01(p01(P_DRIVE) + gmd[MD_DRIVE]) * 7.0f;
    bp->fmv = 0;
    if (bp->model1 == MODEL_FM6 && fm_voices)
        bp->fmv = &fm_voices[p[P_FM_VOICE] <= FM_LIVE ? p[P_FM_VOICE] : 0u];
    bp->fm_rate = fm_exp2f(((float)p[P_FM_RATE] - 64.0f) * (1.0f / 32.0f));
    bp->fm_bright = ((float)p[P_OSC1_SHAPE] - 64.0f) * 0.5f;
    bp->fm_fb = ((float)p[P_FM_FDBK] - 64.0f) * (1.0f / 16.0f);
    bp->need_b = bp->lvl2 > 0.0f || bp->xmod > 0.0f || bp->ring > 0.0f || bp->sync;
    lfo1_inc = 0.05f * fm_exp2f(p01(P_LFO1_RATE) * 9.2f + gmd[MD_LFO1_RATE] * 4.0f) * ((float)EN_CTL / EN_FS);
    if (lfo1_inc > 0.5f)
        lfo1_inc = 0.5f;
    lfo2_inc = 0.05f * fm_exp2f(p01(P_LFO2_RATE) * 9.2f) * ((float)EN_CTL / EN_FS);
    for (i = 0, t = 0; i < n; i += EN_CTL, t++) {   /* the LFOs for every tick, before the voices (both cores read them) */
        lfo1_out = bp->lfo1_v[t] = lfo_tick(&lfo1_ph, &lfo1_sh, lfo1_inc, p[P_LFO1_SHAPE]);
        lfo2_out = bp->lfo2_v[t] = lfo_tick(&lfo2_ph, &lfo2_sh, lfo2_inc, p[P_LFO2_SHAPE]);
    }
    arp_tick(n);                             /* the arp's note events for this block, before the voices render */
    job_n = n;
#if FM1B_CPU2
    if (plat_cpu2_on()) {                    /* the second core takes voices 1-4 while this one does 5-8 */
        plat_cpu2_sync();
        on_two = plat_cpu2_run(half_job, 0) == 1;
    }
#endif
    render_voices(on_two ? EN_NVOICES / 2u : 0u, EN_NVOICES, n, mixl, mixr);
#if FM1B_CPU2
    if (on_two) {
        if (plat_cpu2_wait() != 0) {         /* given up: this core does that half too */
            render_voices(0, EN_NVOICES / 2u, n, mixl, mixr);
        } else {
            plat_cpu2_sync();
            for (i = 0; i < n; i++) {
                mixl[i] += mixl2[i];
                mixr[i] += mixr2[i];
            }
        }
    }
#endif
    pk_bus = 0.0f;
    for (i = 0; i < n; i++) {                /* the voice bus, kept within +-1 before the effects */
        float a;
        mixl[i] = softclip(mixl[i]);
        mixr[i] = softclip(mixr[i]);
        a = mixl[i] < 0.0f ? -mixl[i] : mixl[i];
        pk_bus = a > pk_bus ? a : pk_bus;
    }
    if (have_arena) {
        fx.ens_mode = p[P_ENS_MODE] < ENS_COUNT ? p[P_ENS_MODE] : 0u;
        fx.ens_mix = clamp01(p01(P_ENS_MIX) + gmd[MD_ENS_MIX]);
        fx.dly_time_s = en_dly_time_s(p[P_DLY_TIME]);
        fx.dly_fdbk = p01(P_DLY_FDBK);
        fx.dly_send = clamp01(p01(P_DLY_SEND) + gmd[MD_DLY_SEND]);
        fx.rev_decay = p01(P_REV_SIZE);
        fx.rev_damp = p01(P_REV_DAMP);
        fx.rev_send = clamp01(p01(P_REV_SEND) + gmd[MD_REV_SEND]);
        fx.bypass = fx_bypass & (EN_FX_ENS | EN_FX_DLY | EN_FX_REV);
        fx_process(mixl, mixr, n, &fx);
    }
    pk_out = 0.0f;
    for (i = 0; i < n; i++) {
        float a = mixl[i] < 0.0f ? -mixl[i] : mixl[i];
        pk_out = a > pk_out ? a : pk_out;
    }
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;    /* ~5 ms: turning MASTER does not click */
        lr[2 * i] = (int32_t)(mixl[i] * gain_now);
        lr[2 * i + 1] = (int32_t)(mixr[i] * gain_now);
    }
}
