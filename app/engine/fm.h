/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* Six-operator FM, DX7-shaped: 32 algorithms, operator ratios or fixed frequencies, four-rate four-level
 * envelopes in the DX7's 0..99 units, velocity and rate scaling, feedback on one operator. A voice is 104 bytes;
 * a bank is 32 of them, imported from DX7 SysEx or diced. Rendered per voice as oscillator A (MODEL_FM6). */
#pragma once
#include <stdint.h>

#define FM_OPS 6u
#define FM_BANK 32u
#define FM_LIVE 32u                      /* the 33rd voice: the dice's scratch, not saved */
#define FM_NAME 11u

typedef struct {
    uint8_t r[4], l[4];                  /* envelope rates and levels, 0..99 */
    uint8_t coarse;                      /* ratio 0.5 (0), 1..31; fixed: 1, 10, 100, 1000 Hz (coarse & 3) */
    uint8_t fine;                        /* 0..99: ratio x (1 + fine / 100); fixed: x 10^(fine / 100) */
    int8_t detune;                       /* -7..+7, about 3 cents each */
    uint8_t level;                       /* output level 0..99 */
    uint8_t vel;                         /* velocity sensitivity 0..7 */
    uint8_t mode;                        /* 0 ratio, 1 fixed */
    uint8_t ks;                          /* rate scaling 0..7 */
} fm_op_t;

typedef struct {
    fm_op_t op[FM_OPS];                  /* op[0] is OP1, the lowest */
    uint8_t alg;                         /* 0..31 */
    uint8_t fb;                          /* 0..7 */
    int8_t transpose;                    /* semitones */
    char name[FM_NAME];
} fm_voice_t;

typedef struct {
    float ph[FM_OPS], inc[FM_OPS], lvl[FM_OPS], amp[FM_OPS], out[FM_OPS];
    float fb1, fb2, fbg;                 /* the feedback memory and its gain for this tick */
    float ratio[FM_OPS];                 /* the operator's frequency multiplier (or its fixed Hz) */
    float lvlmax[FM_OPS];                /* its level with velocity applied, 0..99 */
    float carrier_gain;
    uint8_t stage[FM_OPS];               /* 0 idle, 1..3 towards l[0..2], 4 release towards l[3], 5 holding l[2] */
    uint8_t nmod[FM_OPS], modi[FM_OPS][3];   /* its modulators, by index */
    uint8_t carriers, active, fbop;      /* bit k: op k is a carrier / is used; the feedback op, 255 none */
    uint8_t fixed[FM_OPS];
} fm_state_t;

void fm_init(void);                      /* the tables (once) */
void fm_note_on(fm_state_t *s, const fm_voice_t *v, float note, float vel01, int retrigger);
void fm_note_off(fm_state_t *s);
/* once per control tick: envelopes and increments. note: MIDI note (fractional); rate_x: envelope speed, 1 = as
 * stored; bright: added to the modulators' levels (0 = as stored); fb_add: added to the feedback 0..7 */
void fm_tick(fm_state_t *s, const fm_voice_t *v, float note, float fs, float rate_x, float bright, float fb_add);
float fm_sample(fm_state_t *s);          /* one output sample, about +-1 */
int fm_silent(const fm_state_t *s);      /* every carrier's envelope at rest */

void fm_import_dx7_packed(const uint8_t *p, fm_voice_t *out);   /* one voice of a 32-voice bulk dump (128 bytes) */
void fm_import_dx7_single(const uint8_t *p, fm_voice_t *out);   /* a single-voice dump's 155 bytes */
void fm_dice(fm_voice_t *out, uint32_t *rng);                   /* a playable random voice */
