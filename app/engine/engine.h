/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* Pocket Poly's engine: an 8-voice stereo hybrid polysynth. Its own -O2 units (engine.c, wt.c, fx.c): float on
 * the FPU, no platform calls, memory from an arena the app hands over at init. The app owns the live patch
 * (en_patch) and hands note and controller events in. */
#pragma once
#include <stdint.h>
#include "fm.h"

#define EN_FS 44117.6f                   /* the codec's real rate: 192 MHz / 17 / 256 */
#define EN_CTL 32u                       /* samples per control tick */
#define EN_NVOICES 8u
#define EN_NSLOTS 8u                     /* modulation matrix slots */
#define EN_ARENA_BYTES (220u * 1024u)    /* what en_init wants from the app's pool: wavetables, delay, reverb */

enum {
    P_OSC1_MODEL, P_OSC1_SHAPE, P_OSC1_LVL, P_SUB_LVL,
    P_OSC2_MODEL, P_OSC2_SHAPE, P_OSC2_SEMI, P_OSC2_FINE, P_OSC2_LVL,
    P_NOISE_LVL, P_XMOD, P_RING, P_SYNC,
    P_CUTOFF, P_RESO, P_FILT_TYPE, P_FENV_AMT, P_KEYTRK, P_DRIVE,
    P_F_ATK, P_F_DEC, P_F_SUS, P_F_REL,
    P_A_ATK, P_A_DEC, P_A_SUS, P_A_REL,
    P_M_ATK, P_M_DEC, P_M_LOOP, P_M_LVL,
    P_LFO1_RATE, P_LFO1_SHAPE, P_LFO1_PITCH, P_LFO1_CUT,
    P_LFO2_RATE, P_LFO2_SHAPE,
    P_VEL_AMP, P_SPREAD, P_DRIFT, P_GLIDE, P_LEVEL,
    P_ENS_MODE, P_ENS_MIX, P_DLY_TIME, P_DLY_FDBK, P_DLY_SEND, P_REV_SIZE, P_REV_DAMP, P_REV_SEND,
    P_MACRO1, P_MACRO2, P_MACRO3, P_MACRO4, P_MNAME1, P_MNAME2, P_MNAME3, P_MNAME4,
    P_ARP_ON, P_ARP_MODE, P_ARP_DIV, P_ARP_OCT, P_ARP_GATE, P_ARP_SWING, P_ARP_STEPS, P_ARP_HITS, P_ARP_ROT,
    P_ARP_PROB, P_ARP_RATCHET, P_ARP_LATCH, P_ARP_TLEN, P_ARP_TLOCK, P_ARP_SEED_LO, P_ARP_SEED_HI, P_ARP_SLOCK,
    P_FM_VOICE, P_FM_FDBK, P_FM_RATE,       /* oscillator A as FM6: the bank voice (0..31, 32 = LIVE), feedback and rate offsets */
    P_MOD0_SRC, P_MOD0_DST, P_MOD0_AMT,  /* then slots 1..7, three each */
    P_COUNT = P_MOD0_SRC + 3 * EN_NSLOTS
};
#define P_MOD(slot, field) (P_MOD0_SRC + 3 * (slot) + (field))   /* field: 0 source, 1 destination, 2 amount */

enum { MODEL_SAW, MODEL_PULSE, MODEL_TRI, MODEL_SINE, MODEL_PD, MODEL_WT_A, MODEL_WT_B, MODEL_WT_C, MODEL_FM6, MODEL_COUNT };
enum { FILT_LP, FILT_BP, FILT_HP, FILT_COUNT };
enum { LFO_TRI, LFO_SINE, LFO_SQR, LFO_SAW, LFO_SH, LFO_COUNT };
enum { ENS_OFF, ENS_CHORUS1, ENS_CHORUS2, ENS_ENSEMBLE, ENS_COUNT };
enum { MS_NONE, MS_LFO1, MS_LFO2, MS_MENV, MS_VEL, MS_KEY, MS_PRESS, MS_WHEEL, MS_RAND,
       MS_MACRO1, MS_MACRO2, MS_MACRO3, MS_MACRO4, MS_COUNT };
enum { MD_NONE, MD_PITCH, MD_CUTOFF, MD_RESO, MD_SHAPE1, MD_SHAPE2, MD_PITCH2, MD_XMOD, MD_RING, MD_LEVEL, MD_PAN,
       MD_LFO1_RATE, MD_DLY_SEND, MD_REV_SEND, MD_ENS_MIX, MD_DRIVE, MD_COUNT };
enum { ARP_UP, ARP_DOWN, ARP_UPDN, ARP_ORDER, ARP_RANDOM, ARP_WALK, ARP_CONV, ARP_DIVG, ARP_PINKY, ARP_THUMB,
       ARP_CHORD, ARP_TURING, ARP_MODE_COUNT };
enum { DIV_4, DIV_4T, DIV_8, DIV_8T, DIV_16, DIV_16T, DIV_32, DIV_COUNT };
enum { MN_MACRO, MN_BRITE, MN_TIMBRE, MN_MOTION, MN_SPACE, MN_DRIVE, MN_WOBBLE, MN_SHAPE, MN_FM, MN_DECAY,
       MN_TAIL, MN_AIR, MN_WIDTH, MN_GRIT, MN_COUNT };

#define PATCH_NAME 12u
typedef struct {
    char name[PATCH_NAME];
    uint8_t p[P_COUNT];                  /* every parameter 0..127 */
} patch_t;

extern patch_t en_patch;                 /* the live patch: the UI writes it, the render reads it */
void en_fm_voices_set(const fm_voice_t *v);   /* the FM bank: 32 voices and the LIVE one, the app's */

int en_init(void *arena, uint32_t bytes);           /* 0 = ok, -1 = arena too small (no wavetables, no FX) */
void en_note_on(uint32_t note, uint32_t vel);       /* main loop: queued to the render */
void en_note_off(uint32_t note);
void en_note_on_now(uint32_t note, uint32_t vel);   /* from the render (MIDI in): at once */
void en_note_off_now(uint32_t note);
void en_pressure(uint32_t note, uint32_t v);        /* poly aftertouch for a note (keys or MIDI), 0..127 */
void en_channel_pressure(uint32_t v);               /* channel aftertouch: every note */
void en_wheel(uint32_t v);                          /* the mod wheel, CC 1 */
void en_bend(uint32_t bend14);                      /* pitch bend, 0..16383, 8192 centre, +-2 semitones */
void en_sustain(uint32_t on);                       /* CC 64: held notes stay after release (the arp: latched) */
void en_volume(uint32_t cc);                        /* CC 7: 0..127, a gain on the output (127 = as is) */
void en_midi_channel(uint32_t ch0);                 /* the channel the arp (and the keys) send on, 0..15 */
void en_clock_out(int on);                          /* send MIDI clock, start and stop while the arp runs internally */
void en_reset_controllers(void);                    /* CC 121: wheel, bend, pressure, sustain, volume to rest */
void en_all_off(void);                              /* every held note released */
void en_panic(void);                                /* silence now */
void en_render(int32_t *lr, uint32_t n, uint32_t master_q12);
uint32_t en_voices(void);                           /* voices sounding */
int32_t en_sounding_note(void);                     /* the newest voice still sounding, -1 for none */
uint32_t en_voices_max(void);                       /* the cap, lowered by overload */
void en_overload(void);                             /* the render ran long: one voice fewer until a restart */
/* the FX layer: bits EN_FX_ENS, EN_FX_DLY, EN_FX_REV, EN_FX_DRIVE. Bypass is a live state, not the patch's; active
 * says which effects the patch has on and not bypassed (for the keys' lights) */
#define EN_FX_ENS 1u
#define EN_FX_DLY 2u
#define EN_FX_REV 4u
#define EN_FX_DRIVE 8u
void en_fx_bypass(uint32_t bits);
uint32_t en_fx_bypass_get(void);
uint32_t en_fx_active(void);
uint32_t en_fx_running(void);                       /* the stages running now (EN_FX_ENS/DLY/REV): silent ones rest */
float en_cutoff_hz(uint32_t p);                     /* the UI shows the engine's own mappings */
float en_time_s(uint32_t p);
float en_dly_time_s(uint32_t p);

/* the arpeggiator: with P_ARP_ON, note events feed it instead of the voices; it plays the voices and sends its
 * notes over USB and Bluetooth MIDI (channel 1). Its clock is internal (en_tempo) until MIDI clock arrives. */
void en_tempo(uint32_t bpm);                        /* 40..240 */
uint32_t en_tempo_get(void);
void en_clock(void);                                /* a MIDI clock (24 a beat): the arp follows while they come */
void en_clock_start(void);                          /* MIDI start / continue: the pattern from its first step */
void en_clock_stop(void);                           /* MIDI stop: the arp's notes off */
int en_arp_running(void);                           /* notes held (or latched) and the arp on */
uint32_t en_arp_held(void);                         /* notes in the arp */
uint32_t en_arp_step(void);                         /* steps since it started */
uint32_t en_arp_fired(void);                        /* notes it has played */
void en_arp_fired_reset(void);
int en_clock_ext(void);                             /* following MIDI clock */
void en_peaks(float *bus, float *out, float *ens, float *dly, float *rev);   /* diagnostic meters, 0..1 */
