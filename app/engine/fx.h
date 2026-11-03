/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* The master effects, in order: ensemble (Juno-style chorus, two LFOs, antiphase left and right), tape delay
 * (one line, wow and flutter, dark saturated feedback), plate reverb (Dattorro 1997, 16-bit lines), a peak
 * limiter. Stereo float in place; memory from the arena. */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t ens_mode;                   /* ENS_* */
    float ens_mix;                       /* 0..1 */
    float dly_time_s;                    /* 0.02 .. 0.83 */
    float dly_fdbk;                      /* 0..1 */
    float dly_send;                      /* 0..1 */
    float rev_decay;                     /* 0..1 */
    float rev_damp;                      /* 0..1 */
    float rev_send;                      /* 0..1 */
    uint32_t bypass;                     /* FX_BYP_*: a stage skipped, its lines left as they are */
} fx_params_t;

#define FX_BYP_ENS 1u
#define FX_BYP_DLY 2u
#define FX_BYP_REV 4u

uint32_t fx_bytes(void);                 /* the arena the effects need */
void fx_init(void *arena);
void fx_process(float *l, float *r, uint32_t n, const fx_params_t *p);
void fx_peaks(float *ens, float *dly, float *rev);
uint32_t fx_running(void);               /* the stages not at rest: 1 ensemble, 2 delay, 4 reverb */   /* each stage's output peak in the last block it ran */
