/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* Pocket Poly on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and the parts: what is saved (project.c), the screen and controls (ui.c) and the
 * engine (engine/engine.c, its own -O2 unit). */
#include "engine.h"
static volatile uint8_t midi_edited;     /* a CC changed a patch parameter: the screen redraws */
static void midi_settings_apply(void);
static void midi_cc(uint32_t cc, uint32_t v);
#include "project.c"
#include "discover.c"
#include "ui.c"

static uint32_t en_arena[EN_ARENA_BYTES / 4u] PLAT_POOL __attribute__((aligned(8)));   /* the engine's wavetables, delay and
                                                                                        * reverb: word-aligned for its int16 and float lines */

static void midi_in(void)                    /* audio interrupt: the packets that came in, straight to the engine */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24 & 0x7Fu;
        uint32_t full = (pkt >> 8) & 0xFFu;
        if (full < 0xF8u) {                  /* (clock and active sensing would flood the screen) */
            midi_last = pkt;
            midi_last_port = (uint32_t)port;
            midi_count++;
        }
        if (full >= 0xF8u) {                 /* real time: the arp's clock */
            if (full == 0xF8u)
                en_clock();
            else if (full == 0xFAu || full == 0xFBu)
                en_clock_start();
            else if (full == 0xFCu)
                en_clock_stop();
            continue;
        }
        if (set.midi_in && (full & 0x0Fu) != (uint32_t)set.midi_in - 1u)   /* not our channel */
            continue;
        switch (st) {
        case 0x90u:
            en_note_on_now(d1, d2);
            break;
        case 0x80u:
            en_note_off_now(d1);
            break;
        case 0xA0u:
            en_pressure(d1, d2);
            break;
        case 0xB0u:
            midi_cc(d1, d2);
            break;
        case 0xD0u:
            en_channel_pressure(d1);
            break;
        case 0xC0u:
            pc_pending = (int32_t)d1;
            break;
        case 0xE0u:
            en_bend(d1 | d2 << 7);
            break;
        default:
            break;
        }
    }
}

void app_overload(void)
{
    en_overload();
    ui_overload();
}

/* DX7 SysEx into the FM bank: a 32-voice bulk dump (43 0n 09 20 00, 4096 bytes, checksum) fills the bank; a
 * single voice (43 0n 00 01 1B, 155 bytes, checksum) goes into the slot the patch uses (or slot 1 for LIVE).
 * p: the bytes between F0 and F7. Main loop: saving the bank writes the flash. */
/* the CC map (render context): 1 wheel, 7 volume, 64 sustain, 16-19 the macros, 74 cutoff, 71 resonance, 72 release,
 * 73 attack, 91 reverb send, 93 ensemble mix, 94 delay send, 120 all sound off, 121 reset controllers, 123 all notes off */
static void midi_param(uint32_t id, uint32_t v)
{
    en_patch.p[id] = (uint8_t)(v & 127u);
    patch_dirty = 1;
    midi_edited = 1;
}
static void midi_cc(uint32_t cc, uint32_t v)
{
    switch (cc) {
    case 1: en_wheel(v); break;
    case 7: en_volume(v); break;
    case 64: en_sustain(v); break;
    case 16: case 17: case 18: case 19: midi_param(P_MACRO1 + (cc - 16u), v); break;
    case 74: midi_param(P_CUTOFF, v); break;
    case 71: midi_param(P_RESO, v); break;
    case 72: midi_param(P_A_REL, v); break;
    case 73: midi_param(P_A_ATK, v); break;
    case 91: midi_param(P_REV_SEND, v); break;
    case 93: midi_param(P_ENS_MIX, v); break;
    case 94: midi_param(P_DLY_SEND, v); break;
    case 120: en_panic(); break;
    case 121: en_reset_controllers(); break;
    case 123: en_all_off(); break;
    default: break;
    }
}

static void midi_settings_apply(void)    /* the MIDI settings into the engine */
{
    en_midi_channel((uint32_t)set.midi_out - 1u);
    en_clock_out(set.clock_out);
}

static int32_t fm_import_slot = -1;      /* the slot the next single voice goes to (tools/fm1_dx7.py), else the patch's */

int app_sysex(const uint8_t *p, uint32_t n)
{
    char t[16];                          /* "32 " and a ten-letter name */
    uint32_t i, sum = 0;
    if (n == 5u && p[0] == 0x7Du && p[1] == 0x50u && p[2] == 0x50u && p[3] == 0x01u) {   /* Pocket Poly: slot for the next voice */
        fm_import_slot = p[4] < FM_BANK ? (int32_t)p[4] : -1;
        return 1;
    }
    if (n < 6u || p[0] != 0x43u || (p[1] & 0x70u) != 0u)
        return 0;
    if (p[2] == 0x09u && p[3] == 0x20u && p[4] == 0x00u) {   /* a 32-voice bulk dump: 4104 bytes, beyond the
                                                             * platform's 640-byte frames; the tool splits it */
        ui_say("DX7 BANK", "USE FM1_DX7.PY");
        return 1;
    }
    if (p[2] == 0x00u && p[3] == 0x01u && p[4] == 0x1Bu && n >= 5u + 155u + 1u) {
        fm_voice_t v;
        uint32_t slot = fm_import_slot >= 0 ? (uint32_t)fm_import_slot : en_patch.p[P_FM_VOICE] < FM_BANK ? en_patch.p[P_FM_VOICE] : 0u;
        int last = fm_import_slot < 0 || fm_import_slot == (int32_t)FM_BANK - 1;
        fm_import_slot = -1;
        for (i = 0; i < 155u; i++)
            sum += p[5u + i];
        if (((sum + p[5u + 155u]) & 0x7Fu) != 0u) {
            ui_say("DX7 VOICE", "BAD CHECKSUM");
            return 1;
        }
        fm_import_dx7_single(p + 5u, &v);
        fmbank_store(slot, &v);
        if (last) {                          /* a bank sent voice by voice saves once, at its end: the FM bank only */
            if (en_patch.p[P_FM_VOICE] != slot) {
                en_patch.p[P_FM_VOICE] = (uint8_t)slot;
                patch_dirty = 1;
            }
            if (!plat_store_save(OBJ_FMBANK, &fmbank, sizeof fmbank))
                fmbank_dirty = 0;
        }
        int_text((int32_t)slot + 1, t);
        text_cat(t, " ");
        text_cat(t, v.name);
        ui_say("DX7 VOICE", t);
        return 1;
    }
    return 0;
}

/* ---- the contract */
void app_init(void)
{
    en_init(en_arena, sizeof en_arena);
#if FM1B_CPU2
    plat_cpu2_set(1);                        /* the second core renders half the voices; one core if it fails */
#endif
    project_load();                          /* defaults when nothing (or another format) is saved */
    en_tempo(set.bpm);
    midi_settings_apply();
    project_load_slot(set.preset);
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    midi_in();
    en_render(lr, n, master_q12);
}
void app_panic(void) { en_panic(); }
void app_ui_init(void) { ui_init(); }
void app_frame(void) { ui_frame(); }
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }

#ifdef PLAT_HOST
/* the simulator's checks: octave, note (the newest voice sounding, -1 none), voices, cutoff, preset, page,
 * dialog, dirty, vel, prs (the last key's), pN (parameter N) */
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "note"))
        *got = en_sounding_note();
    else if (!strcmp(what, "voices"))
        *got = (int)en_voices();
    else if (!strcmp(what, "cutoff"))
        *got = en_patch.p[P_CUTOFF];
    else if (!strcmp(what, "preset"))
        *got = set.preset;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "dialog"))
        *got = ui.dialog;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty || patch_dirty;
    else if (!strcmp(what, "vel")) {
        uint32_t k;
        *got = 0;
        for (k = 0; k < NKEYS; k++)
            if (ui.keys >> k & 1u)
                *got = ui.vel[k];
    } else if (!strcmp(what, "prs")) {
        uint32_t k;
        *got = 0;
        for (k = 0; k < NKEYS; k++)
            if (ui.keys >> k & 1u)
                *got = ui.prs[k];
    } else if (!strcmp(what, "arp_on"))
        *got = en_arp_running();
    else if (!strcmp(what, "arp_held"))
        *got = (int)en_arp_held();
    else if (!strcmp(what, "arp_step"))
        *got = (int)en_arp_step();
    else if (!strcmp(what, "arp_fired"))
        *got = (int)en_arp_fired();
    else if (!strcmp(what, "arp_reset")) {   /* a check that zeroes the count: "expect arp_reset 0" */
        en_arp_fired_reset();
        *got = 0;
    }
    else if (!strcmp(what, "bpm"))
        *got = (int)en_tempo_get();
    else if (!strcmp(what, "ext"))
        *got = en_clock_ext();
    else if (!strcmp(what, "seed"))
        *got = dice_seed;
    else if (!strcmp(what, "hist"))
        *got = hist_n;
    else if (!strcmp(what, "hist_i"))
        *got = hist_i;
    else if (!strcmp(what, "morph"))
        *got = morph_pos;
    else if (!strcmp(what, "mutations"))
        *got = (int)mutations;
    else if (!strcmp(what, "fmvoice"))
        *got = en_patch.p[P_FM_VOICE];
    else if (!strcmp(what, "fmdirty"))
        *got = fmbank_dirty;
    else if (!strcmp(what, "fxbypass"))
        *got = (int)en_fx_bypass_get();
    else if (!strcmp(what, "fxactive"))
        *got = (int)en_fx_active();
    else if (!strcmp(what, "fxrun"))
        *got = (int)en_fx_running();
    else if (!strcmp(what, "ch_in"))
        *got = set.midi_in;
    else if (!strcmp(what, "ch_out"))
        *got = set.midi_out;
    else if (!strcmp(what, "local"))
        *got = set.local;
    else if (what[0] == 'p' && what[1] >= '0' && what[1] <= '9')
        *got = en_patch.p[atoi(what + 1) % P_COUNT];
    else
        return 0;
    return 1;
}
#endif
