/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann
 * Copyright (C) 2026 Charles Vestal (the panel and Bluetooth handling from fm1-base's template) */
/* Pocket Poly's screen and controls. Pages of four parameters on KNOB 1-4: HOME (the four macros), OSC A / OSC B /
 * MIX / VOICE (EDIT), FILTER / FX 1 / FX 2 (FX), AMP ENV / FLT ENV / MOD ENV (ENV), LFO 1 / LFO 2 (LFO), MOD (the
 * matrix, a slot at a time) / MACROS (their names) (SEL); SELECT steps through the pages, ALGO sets the filter
 * type, PRESET browses the slots, SAVE opens the save dialog (PRESET or KNOB 1 picks a user slot, OCT+ saves,
 * OCT- backs out), OCT-/OCT+ the keys' octave, REC Bluetooth (held: on / off; pressed: a Bluetooth MIDI device).
 * The keys play with velocity and pressure; pressure also reaches the engine as a modulation source. */
enum { PG_HOME, PG_OSCA, PG_OSCB, PG_MIX, PG_VOICE, PG_FILT, PG_FX1, PG_FX2, PG_AENV, PG_FENV, PG_MENV,
       PG_LFO1, PG_LFO2, PG_MOD, PG_MACROS, PG_ARP1, PG_ARP2, PG_ARP3, PG_ARP4, PG_DISC, PG_FM, PG_MIDI, PG_COUNT };
/* values on knobs that are not patch parameters */
enum { PID_SLOT = P_COUNT, PID_BPM, PID_ASEED, PID_SEED, PID_MUTATE, PID_MORPH, PID_HIST, PID_MIDI_IN, PID_MIDI_OUT,
       PID_CLKOUT, PID_LOCAL, PID_COUNT };
typedef struct {
    const char *title;
    uint8_t p[4];
} page_t;
static const page_t PAGES[PG_COUNT] = {
    {"HOME", {P_MACRO1, P_MACRO2, P_MACRO3, P_MACRO4}},
    {"OSC A", {P_OSC1_MODEL, P_OSC1_SHAPE, P_OSC1_LVL, P_SUB_LVL}},
    {"OSC B", {P_OSC2_MODEL, P_OSC2_SHAPE, P_OSC2_SEMI, P_OSC2_FINE}},
    {"MIX", {P_OSC2_LVL, P_NOISE_LVL, P_XMOD, P_RING}},
    {"VOICE", {P_SYNC, P_DRIFT, P_VEL_AMP, P_LEVEL}},
    {"FILTER", {P_CUTOFF, P_RESO, P_KEYTRK, P_DRIVE}},
    {"FX 1", {P_ENS_MODE, P_ENS_MIX, P_DLY_SEND, P_REV_SEND}},
    {"FX 2", {P_DLY_TIME, P_DLY_FDBK, P_REV_SIZE, P_REV_DAMP}},
    {"AMP ENV", {P_A_ATK, P_A_DEC, P_A_SUS, P_A_REL}},
    {"FLT ENV", {P_F_ATK, P_F_DEC, P_F_SUS, P_F_REL}},
    {"MOD ENV", {P_M_ATK, P_M_DEC, P_M_LOOP, P_M_LVL}},
    {"LFO 1", {P_LFO1_RATE, P_LFO1_SHAPE, P_LFO1_PITCH, P_LFO1_CUT}},
    {"LFO 2", {P_LFO2_RATE, P_LFO2_SHAPE, P_SPREAD, P_GLIDE}},
    {"MOD", {0, 0, 0, 0}},               /* the matrix: SLOT, then the slot's source, destination, amount */
    {"MACROS", {P_MNAME1, P_MNAME2, P_MNAME3, P_MNAME4}},
    {"ARP 1", {P_ARP_MODE, P_ARP_DIV, P_ARP_OCT, P_ARP_GATE}},
    {"ARP 2", {P_ARP_SWING, P_ARP_HITS, P_ARP_STEPS, P_ARP_ROT}},
    {"ARP 3", {P_ARP_PROB, P_ARP_RATCHET, P_ARP_LATCH, 0}},      /* K4: the tempo */
    {"TURING", {P_ARP_TLEN, P_ARP_TLOCK, 0, P_ARP_SLOCK}},       /* K3: the arp's seed */
    {"DISCOVER", {0, 0, 0, 0}},          /* SEED, MUTATE, MORPH, HISTORY */
    {"FM", {P_FM_VOICE, P_FM_FDBK, P_FM_RATE, P_OSC1_SHAPE}},   /* oscillator A as FM6 */
    {"MIDI", {0, 0, 0, 0}},              /* IN CH, OUT CH, CLOCK OUT, LOCAL: the settings */
};

enum { K_PCT, K_MODEL, K_FILT, K_LFO, K_ENS, K_ONOFF, K_SRC, K_DST, K_MNAME, K_BI, K_SPREAD, K_TIME, K_DLYTIME, K_HZ,
       K_SEMI, K_CENT, K_SLOT, K_ARPMODE, K_DIV, K_OCTS, K_GATE, K_STEPS, K_HITS, K_ROT, K_TLEN, K_BPM, K_HEX,
       K_COUNT_, K_MORPH, K_HIST, K_FMVOICE, K_CHIN, K_CHOUT, K_SETONOFF };
typedef struct {
    const char *label;
    uint8_t kind;
} pmeta_t;
static const pmeta_t PM[P_COUNT] = {
    [P_OSC1_MODEL] = {"MODEL", K_MODEL}, [P_OSC1_SHAPE] = {"SHAPE", K_PCT},  [P_OSC1_LVL] = {"LEVEL", K_PCT},
    [P_SUB_LVL] = {"SUB", K_PCT},        [P_OSC2_MODEL] = {"MODEL", K_MODEL}, [P_OSC2_SHAPE] = {"SHAPE", K_PCT},
    [P_OSC2_SEMI] = {"SEMI", K_SEMI},    [P_OSC2_FINE] = {"FINE", K_CENT},   [P_OSC2_LVL] = {"OSC B", K_PCT},
    [P_NOISE_LVL] = {"NOISE", K_PCT},    [P_XMOD] = {"XMOD", K_PCT},         [P_RING] = {"RING", K_PCT},
    [P_SYNC] = {"SYNC", K_ONOFF},        [P_CUTOFF] = {"CUT", K_HZ},         [P_RESO] = {"RES", K_PCT},
    [P_FILT_TYPE] = {"TYPE", K_FILT},    [P_FENV_AMT] = {"FENV", K_BI},      [P_KEYTRK] = {"KEYTRK", K_PCT},
    [P_DRIVE] = {"DRIVE", K_PCT},        [P_F_ATK] = {"ATK", K_TIME},        [P_F_DEC] = {"DEC", K_TIME},
    [P_F_SUS] = {"SUS", K_PCT},          [P_F_REL] = {"REL", K_TIME},        [P_A_ATK] = {"ATK", K_TIME},
    [P_A_DEC] = {"DEC", K_TIME},         [P_A_SUS] = {"SUS", K_PCT},         [P_A_REL] = {"REL", K_TIME},
    [P_M_ATK] = {"ATK", K_TIME},         [P_M_DEC] = {"DEC", K_TIME},        [P_M_LOOP] = {"LOOP", K_ONOFF},
    [P_M_LVL] = {"LEVEL", K_PCT},        [P_LFO1_RATE] = {"RATE", K_PCT},    [P_LFO1_SHAPE] = {"SHAPE", K_LFO},
    [P_LFO1_PITCH] = {"PITCH", K_PCT},   [P_LFO1_CUT] = {"CUT", K_PCT},      [P_LFO2_RATE] = {"RATE", K_PCT},
    [P_LFO2_SHAPE] = {"SHAPE", K_LFO},   [P_VEL_AMP] = {"VEL>AMP", K_PCT},   [P_SPREAD] = {"SPREAD", K_SPREAD},
    [P_DRIFT] = {"DRIFT", K_PCT},        [P_GLIDE] = {"GLIDE", K_PCT},       [P_LEVEL] = {"LEVEL", K_PCT},
    [P_ENS_MODE] = {"ENS", K_ENS},       [P_ENS_MIX] = {"ENS MIX", K_PCT},   [P_DLY_TIME] = {"DLY TIME", K_DLYTIME},
    [P_DLY_FDBK] = {"DLY FDBK", K_PCT},  [P_DLY_SEND] = {"DLY SEND", K_PCT}, [P_REV_SIZE] = {"REV SIZE", K_PCT},
    [P_REV_DAMP] = {"REV DAMP", K_PCT},  [P_REV_SEND] = {"REV SEND", K_PCT}, [P_MACRO1] = {"M1", K_PCT},
    [P_MACRO2] = {"M2", K_PCT},          [P_MACRO3] = {"M3", K_PCT},         [P_MACRO4] = {"M4", K_PCT},
    [P_MNAME1] = {"M1 NAME", K_MNAME},   [P_MNAME2] = {"M2 NAME", K_MNAME},  [P_MNAME3] = {"M3 NAME", K_MNAME},
    [P_MNAME4] = {"M4 NAME", K_MNAME},   [P_ARP_ON] = {"ARP", K_ONOFF},        [P_ARP_MODE] = {"MODE", K_ARPMODE},
    [P_ARP_DIV] = {"DIV", K_DIV},        [P_ARP_OCT] = {"OCTAVES", K_OCTS},    [P_ARP_GATE] = {"GATE", K_GATE},
    [P_ARP_SWING] = {"SWING", K_PCT},    [P_ARP_STEPS] = {"STEPS", K_STEPS},   [P_ARP_HITS] = {"HITS", K_HITS},
    [P_ARP_ROT] = {"ROTATE", K_ROT},     [P_ARP_PROB] = {"CHANCE", K_PCT},     [P_ARP_RATCHET] = {"RATCHET", K_PCT},
    [P_ARP_LATCH] = {"LATCH", K_ONOFF},  [P_ARP_TLEN] = {"LENGTH", K_TLEN},    [P_ARP_TLOCK] = {"LOCK", K_PCT},
    [P_ARP_SEED_LO] = {"SEED", K_PCT},   [P_ARP_SEED_HI] = {"SEED", K_PCT},    [P_ARP_SLOCK] = {"REPEAT", K_ONOFF},
    [P_FM_VOICE] = {"VOICE", K_FMVOICE}, [P_FM_FDBK] = {"FEEDBACK", K_BI},      [P_FM_RATE] = {"RATE", K_BI},
};
static const pmeta_t PM_PSEUDO[PID_COUNT - P_COUNT] = {
    {"SLOT", K_SLOT}, {"BPM", K_BPM}, {"SEED", K_HEX}, {"SEED", K_HEX}, {"MUTATE", K_COUNT_}, {"MORPH", K_MORPH},
    {"HISTORY", K_HIST}, {"IN CH", K_CHIN}, {"OUT CH", K_CHOUT}, {"CLOCK OUT", K_SETONOFF}, {"LOCAL", K_SETONOFF}};
static const char *const ARP_NAME[ARP_MODE_COUNT] = {"UP", "DOWN", "UP DN", "ORDER", "RANDOM", "WALK", "CONV",
                                                     "DIVERGE", "PINKY", "THUMB", "CHORD", "TURING"};
static const char *const DIV_NAME[DIV_COUNT] = {"1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const pmeta_t PM_MOD[3] = {{"SRC", K_SRC}, {"DST", K_DST}, {"AMT", K_BI}};
static const char *const MODEL_NAME[MODEL_COUNT] = {"SAW", "PULSE", "TRI", "SINE", "PD", "WT A", "WT B", "WT C", "FM6"};
static const char *const FILT_NAME[FILT_COUNT] = {"LP", "BP", "HP"};
static const char *const LFO_NAME[LFO_COUNT] = {"TRI", "SINE", "SQR", "SAW", "S&H"};
static const char *const ENS_NAME[ENS_COUNT] = {"OFF", "CHOR I", "CHOR II", "ENSEMBLE"};
static const char *const SRC_NAME[MS_COUNT] = {"OFF", "LFO 1", "LFO 2", "MOD ENV", "VELO", "KEY", "PRESS",
                                               "WHEEL", "RANDOM", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4"};
static const char *const DST_NAME[MD_COUNT] = {"OFF", "PITCH", "CUTOFF", "RESO", "SHAPE A", "SHAPE B", "PITCH B",
                                               "XMOD", "RING", "LEVEL", "PAN", "LFO1 RT", "DLY SND", "REV SND",
                                               "ENS MIX", "DRIVE"};
static const char *const MNAME[MN_COUNT] = {"MACRO", "BRITE", "TIMBRE", "MOTION", "SPACE", "DRIVE", "WOBBLE",
                                            "SHAPE", "FM", "DECAY", "TAIL", "AIR", "WIDTH", "GRIT"};
static const char *const BTN_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                         "PLAY", "REC", "OCT-", "OCT+"};

static volatile uint32_t midi_last, midi_last_port, midi_count;   /* the last message in (shown), and how many */
static volatile int32_t pc_pending = -1;     /* a program change from MIDI, loaded by the main loop */
static volatile uint8_t overload_seen;

static struct {
    uint8_t page, dialog;                /* the page shown; the save dialog is up */
    uint8_t save_to;                     /* the dialog's user slot 0..15 */
    uint8_t slot;                        /* the MOD page's slot 0..7 */
    char msg[2][24];                     /* a short message over the header */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t vel[NKEYS], prs[NKEYS];      /* each key's last velocity, and its pressure while held */
    uint8_t looking;                     /* Bluetooth MIDI devices: REC pressed, the scan's list up (lib/btdev) */
    uint32_t rec_t;                      /* REC went down (0: not held, or its hold already acted) */
    uint32_t seq_t;                      /* SEQ went down: a tap dices, a hold opens DISCOVER */
    uint32_t fx_t;                       /* FX went down: held, the keys switch the effects; a tap cycles its pages */
    uint32_t fx_up;                      /* FX came up: key edges right after it are the button's jolt, not notes */
    uint8_t fx_used;                     /* a key switched an effect while FX was held: no page on release */
    uint32_t dkeys;                      /* the digital key matrix at the last input (the FX layer reads this one) */
    uint32_t keynotes;                   /* bit k: key k started a note (so its release ends it, layer or not) */
    uint32_t keyeng, keymidi;            /* bit k: that note went to the engine / out over MIDI */
    uint8_t keynote[NKEYS], keych[NKEYS];   /* the note and channel it went out as (octave or channel may change) */
    uint32_t seq_with;                   /* the page buttons held when SEQ went down: the section to dice */
    uint32_t meter_t;                    /* the footer's meters were drawn */
    uint8_t redraw;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void signed_text(int32_t v, char *b)  /* with its sign: +12, -3, 0 */
{
    b[0] = 0;
    if (v > 0)
        text_cat(b, "+");
    int_text(v, b + (v > 0));
}

static void dec1_text(float f, const char *unit, char *b)   /* one decimal: 1.2S, 2.4K */
{
    int32_t d = (int32_t)(f * 10.0f + 0.5f);
    char u[4];
    int_text(d / 10, b);
    text_cat(b, ".");
    int_text(d % 10, u);
    text_cat(b, u);
    text_cat(b, unit);
}

/* the parameter on knob k of page pg; PG_MOD's knob 1 is the slot (P_COUNT marks it) */
static uint32_t page_param(uint32_t pg, uint32_t k)
{
    if (pg == PG_MOD)
        return k == 0u ? (uint32_t)PID_SLOT : (uint32_t)P_MOD(ui.slot, k - 1u);
    if (pg == PG_ARP3 && k == 3u)
        return PID_BPM;
    if (pg == PG_ARP4 && k == 2u)
        return PID_ASEED;
    if (pg == PG_DISC)
        return PID_SEED + k;
    if (pg == PG_MIDI)
        return PID_MIDI_IN + k;
    return PAGES[pg].p[k];
}

static const pmeta_t *meta(uint32_t id)
{
    if (id >= P_COUNT)
        return &PM_PSEUDO[id - P_COUNT];
    if (id >= P_MOD0_SRC)
        return &PM_MOD[(id - P_MOD0_SRC) % 3u];
    return &PM[id];
}

/* the arp's stepped values: what 0..127 means, and back */
static uint32_t steps_of(uint32_t kind)      /* how many values a stepped kind has */
{
    switch (kind) {
    case K_OCTS: return 4u;
    case K_STEPS: return 16u;
    case K_HITS: return 17u;
    case K_ROT: return 16u;
    case K_TLEN: return 15u;
    default: return 0u;
    }
}
static uint32_t stepped(uint32_t v, uint32_t kind) { return v * (steps_of(kind) - 1u) / 127u; }
/* the smallest 0..127 value the engine reads as step d (it truncates): the round trip is exact */
static uint32_t unstepped(uint32_t d, uint32_t kind) { return (d * 127u + steps_of(kind) - 2u) / (steps_of(kind) - 1u); }
static uint32_t arp_seed(void) { return en_patch.p[P_ARP_SEED_LO] | en_patch.p[P_ARP_SEED_HI] << 7; }

static const char *label_of(uint32_t id)     /* a macro's card carries the macro's name */
{
    if (id >= P_COUNT)
        return PM_PSEUDO[id - P_COUNT].label;
    if (id >= P_MACRO1 && id <= P_MACRO4) {
        uint32_t n = en_patch.p[P_MNAME1 + (id - P_MACRO1)];
        return MNAME[n < MN_COUNT ? n : 0];
    }
    if (id == P_OSC1_SHAPE && en_patch.p[P_OSC1_MODEL] == MODEL_FM6)
        return "BRIGHT";
    return meta(id)->label;
}

static void val_text(uint32_t id, char *b)   /* a parameter's value as the screen shows it */
{
    uint32_t v = id < P_COUNT ? en_patch.p[id] : 0u, kind = meta(id)->kind;
    float f;
    char u[8];
    b[0] = 0;
    switch (kind) {
    case K_ARPMODE: text_cat(b, ARP_NAME[v < ARP_MODE_COUNT ? v : 0]); break;
    case K_DIV: text_cat(b, DIV_NAME[v < DIV_COUNT ? v : 0]); break;
    case K_OCTS: int_text((int32_t)stepped(v, kind) + 1, b); break;
    case K_STEPS: int_text((int32_t)stepped(v, kind) + 1, b); break;
    case K_HITS: int_text((int32_t)stepped(v, kind), b); break;
    case K_ROT: int_text((int32_t)stepped(v, kind), b); break;
    case K_TLEN: int_text((int32_t)stepped(v, kind) + 2, b); break;
    case K_GATE:
        int_text(5 + (int32_t)v * 195 / 127, b);
        text_cat(b, "%");
        break;
    case K_BPM:
        if (en_clock_ext())
            text_cat(b, "EXT");
        else
            int_text((int32_t)en_tempo_get(), b);
        break;
    case K_HEX: hex4(id == PID_ASEED ? arp_seed() : dice_seed, b); break;
    case K_COUNT_: int_text((int32_t)mutations, b); break;
    case K_MORPH:
        if (!have_a || !have_b) {
            text_cat(b, have_a ? "SET B" : "SET A");
        } else {
            int_text(morph_pos * 100 / 127, b);
            text_cat(b, "%");
        }
        break;
    case K_HIST:
        int_text(hist_n ? (int32_t)hist_i + 1 : 0, b);
        text_cat(b, "/");
        int_text((int32_t)hist_n, u);
        text_cat(b, u);
        break;
    case K_SLOT: int_text((int32_t)ui.slot + 1, b); break;
    case K_CHIN:
        if (set.midi_in)
            int_text((int32_t)set.midi_in, b);
        else
            text_cat(b, "OMNI");
        break;
    case K_CHOUT: int_text((int32_t)set.midi_out, b); break;
    case K_SETONOFF: text_cat(b, (id == PID_CLKOUT ? set.clock_out : set.local) ? "ON" : "OFF"); break;
    case K_FMVOICE:
        if (v == FM_LIVE) {
            text_cat(b, "LIVE");
        } else {
            int_text((int32_t)v + 1, b);
            text_cat(b, " ");
            text_cat(b, fm_voice_name(v < FM_BANK ? v : 0));
        }
        break;
    case K_MODEL: text_cat(b, MODEL_NAME[v < MODEL_COUNT ? v : 0]); break;
    case K_FILT: text_cat(b, FILT_NAME[v < FILT_COUNT ? v : 0]); break;
    case K_LFO: text_cat(b, LFO_NAME[v < LFO_COUNT ? v : 0]); break;
    case K_ENS: text_cat(b, ENS_NAME[v < ENS_COUNT ? v : 0]); break;
    case K_SRC: text_cat(b, SRC_NAME[v < MS_COUNT ? v : 0]); break;
    case K_DST: text_cat(b, DST_NAME[v < MD_COUNT ? v : 0]); break;
    case K_MNAME: text_cat(b, MNAME[v < MN_COUNT ? v : 0]); break;
    case K_ONOFF: text_cat(b, v > 63u ? "ON" : "OFF"); break;
    case K_SPREAD:
        if (v == 64u)
            text_cat(b, "OFF");
        else {
            char n[8];
            int_text(v > 64u ? (int32_t)v - 64 : 64 - (int32_t)v, n);
            text_cat(b, v > 64u ? "KEYS " : "VOICES ");
            text_cat(b, n);
        }
        break;
    case K_BI: signed_text((int32_t)v - 64, b); break;
    case K_SEMI: signed_text((((int32_t)v - 64) * 24 + (v >= 64u ? 31 : -31)) / 63, b); break;   /* rounded, as the engine */
    case K_CENT:
        signed_text(((int32_t)v - 64) * 50 / 63, b);
        text_cat(b, "C");
        break;
    case K_TIME:
    case K_DLYTIME:
        f = kind == K_TIME ? en_time_s(v) : en_dly_time_s(v);
        if (f < 1.0f) {
            int_text((int32_t)(f * 1000.0f + 0.5f), b);
            text_cat(b, "MS");
        } else {
            dec1_text(f, "S", b);
        }
        break;
    case K_HZ:
        f = en_cutoff_hz(v);
        if (f < 1000.0f)
            int_text((int32_t)(f + 0.5f), b);
        else
            dec1_text(f * 0.001f, "K", b);
        break;
    default:
        int_text((int32_t)(v * 100u + 63u) / 127, b);
        break;
    }
}

/* ------------------------------------------------------------------ messages --- */
static void ui_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void octave_text(char *t)
{
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
}

/* a screen reader that has just started listening (or asks) hears the page, the preset and the load */
static void speak(void)
{
    char t[192], s[4], n[8];
    if (ann_on() && ann_where()) {
        slot_name(set.preset, s);
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, ui.dialog ? "SAVE TO" : PAGES[ui.page].title);
        text_cat(t, ". PRESET ");
        text_cat(t, s);
        text_cat(t, " ");
        text_cat(t, en_patch.name);
        text_cat(t, ". VOICES ");               /* and the load, so a USB remote can read it (tools/fm1_remote.py) */
        int_text((int32_t)en_voices(), n);
        text_cat(t, n);
        text_cat(t, " OF ");
        int_text((int32_t)en_voices_max(), n);
        text_cat(t, n);
        text_cat(t, ". CPU ");
        int_text((int32_t)plat_cpu_pct(), n);
        text_cat(t, n);
        text_cat(t, "%. FXB ");
        int_text((int32_t)en_fx_bypass_get(), n);
        text_cat(t, n);
        {                                    /* the meters, in dB: what each stage puts out (a diagnostic) */
            static const char *const NAMES[5] = {". BUS ", " OUT ", " ENS ", " DLY ", " REV "};
            float pk[5];
            uint32_t i;
            en_peaks(&pk[0], &pk[1], &pk[2], &pk[3], &pk[4]);
            for (i = 0; i < 5u; i++) {
                int32_t db = pk[i] > 0.00001f ? (int32_t)(20.0f * fm_log2f(pk[i]) * 0.30103f) : -99;
                text_cat(t, NAMES[i]);
                int_text(db, n);
                text_cat(t, n);
            }
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void note_name(int32_t note, char *b)
{
    static const char *const N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[12];
    b[0] = 0;
    text_cat(b, N[note % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
}

static void key_on(uint32_t k, uint32_t v)   /* key k lands: a note to the engine and/or out, remembered for its release */
{
    uint32_t bit = 1u << k, ch = (uint32_t)set.midi_out - 1u, note = (uint32_t)(53 + (int32_t)k + 12 * set.octave);
    ui.vel[k] = (uint8_t)v;
    ui.prs[k] = 0;
    ui.keynote[k] = (uint8_t)note;
    ui.keych[k] = (uint8_t)ch;
    ui.keynotes |= bit;
    ui.keyeng &= ~bit;
    ui.keymidi &= ~bit;
    if (set.local) {
        ui.keyeng |= bit;
        en_note_on(note, v);
    }
    if (en_patch.p[P_ARP_ON] <= 63u || !set.local) {   /* the arp sends its own notes */
        ui.keymidi |= bit;
        plat_midi_send(0x09u | (0x90u | ch) << 8 | note << 16 | v << 24, TO_USB | TO_BLE);
    }
}

static void key_off(uint32_t k)              /* key k lifts: exactly what its landing started ends */
{
    uint32_t bit = 1u << k, note = ui.keynote[k], ch = ui.keych[k];
    ui.keynotes &= ~bit;
    ui.prs[k] = 0;
    en_pressure(note, 0);
    if (ui.keyeng & bit)
        en_note_off(note);
    if (ui.keymidi & bit)
        plat_midi_send(0x08u | (0x80u | ch) << 8 | note << 16, TO_USB | TO_BLE);
    ui.keyeng &= ~bit;
    ui.keymidi &= ~bit;
}

static void octave_step(int32_t d)
{
    int32_t o = clamp(set.octave + d, -2, 2);
    char t[4];
    if (o == set.octave)
        return;
    set.octave = (int8_t)o;                  /* (a key held across the change ends the note it started) */
    set_dirty = 1;
    octave_text(t);
    ui_say("OCTAVE", t);
}

int pp_ble_off(void) { return set.ble_off; }

/* REC held a second: Bluetooth off or on (kept with the settings, so it stays so at the next power-on) */
static void ble_toggle(void)
{
    set.ble_off = (uint8_t)!set.ble_off;
    set_dirty = 1;
    plat_ble_enable(!set.ble_off);
    ui_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
}

/* REC: a Bluetooth MIDI device (FM1B_BLE_CENTRAL, lib/btdev): look for devices, then connect to the one ALGO
 * picks, then let it go */
static void ble_device_say(void)
{
    char name[21];
    if (btdev_found(btdev_pick(), name, 0))
        ui_say("DEVICE", name);
    else
        ui_say("BLE", "SCANNING");
}
static void ble_device_button(void)
{
    int ev;
    if (!btdev_busy() && !ui.looking) {
        ui.looking = 1;
        ui_say("BLE", "SCANNING");
        return;
    }
    ev = btdev_save();
    ui.looking = 0;
    ui_say(ev == BTDEV_CONNECTING ? btdev_word(ev) : "BLE", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
}

/* ------------------------------------------------------------------ editing --- */
static void param_edit(uint32_t id, int32_t e)   /* a knob's detents onto parameter id */
{
    int32_t v = en_patch.p[id], hi = 127, step = e;
    char t[16];
    switch (meta(id)->kind) {
    case K_MODEL: hi = id == P_OSC2_MODEL ? MODEL_COUNT - 2 : MODEL_COUNT - 1; step = e > 0 ? 1 : -1; break;   /* no FM6 on B */
    case K_FILT: hi = FILT_COUNT - 1; step = e > 0 ? 1 : -1; break;
    case K_LFO: hi = LFO_COUNT - 1; step = e > 0 ? 1 : -1; break;
    case K_ENS: hi = ENS_COUNT - 1; step = e > 0 ? 1 : -1; break;
    case K_SRC: hi = MS_COUNT - 1; step = e > 0 ? 1 : -1; break;
    case K_DST: hi = MD_COUNT - 1; step = e > 0 ? 1 : -1; break;
    case K_MNAME: hi = MN_COUNT - 1; step = e > 0 ? 1 : -1; break;
    case K_ONOFF: v = v > 63 ? 0 : 127; step = 0; break;
    case K_FMVOICE: hi = FM_LIVE; step = e > 0 ? 1 : -1; break;
    case K_ARPMODE: hi = ARP_MODE_COUNT - 1; step = e > 0 ? 1 : -1; break;
    case K_DIV: hi = DIV_COUNT - 1; step = e > 0 ? 1 : -1; break;
    case K_OCTS: case K_STEPS: case K_HITS: case K_ROT: case K_TLEN: {
        uint32_t kind = meta(id)->kind, d = stepped((uint32_t)v, kind);
        v = (int32_t)unstepped((uint32_t)clamp((int32_t)d + (e > 0 ? 1 : -1), 0, (int32_t)steps_of(kind) - 1), kind);
        step = 0;
        break;
    }
    default:
        if (e >= 3 || e <= -3)             /* turned fast: bigger steps */
            step = e * 3;
        break;
    }
    v = clamp(v + step, 0, hi);
    if (v == (int32_t)en_patch.p[id])
        return;
    en_patch.p[id] = (uint8_t)v;
    patch_dirty = 1;
    val_text(id, t);
    ui_say(label_of(id), t);
}

static void preset_step(int32_t e)
{
    int32_t i = clamp((int32_t)set.preset + (e > 0 ? 1 : -1), 0, (int32_t)NSLOTS - 1);
    char s[4];
    if (i == (int32_t)set.preset)
        return;
    project_load_slot((uint32_t)i);
    slot_name((uint32_t)i, s);
    ui_say(s, en_patch.name);
}

static void page_set(uint32_t pg)
{
    ui.page = (uint8_t)pg;
    ui.dialog = 0;
    ui.redraw = 1;
    ui_say(PAGES[pg].title, "");
}

static void page_cycle(uint32_t first, uint32_t last)   /* a button's pages in turn, first..last */
{
    page_set(ui.page >= first && ui.page < last ? ui.page + 1u : first);
}

static void save_confirm(void)
{
    char s[4];
    project_store_user(ui.save_to);
    ui.dialog = 0;
    slot_name(set.preset, s);
    if (project_save() == 0)
        ui_say("SAVED", s);
    else
        ui_say("SAVE", "FAILED");
}

/* ------------------------------------------------------------------ the FX layer --- */
static const uint8_t FXKEY[4] = {0, 2, 4, 6};    /* F3 G3 A3 B3: ensemble, delay, reverb, drive */
static const char *const FXNAME[4] = {"ENSEMBLE", "DELAY", "REVERB", "DRIVE"};

static uint32_t fx_keys_lit(void)        /* the layer's lights: a key per effect, lit while it is heard */
{
    uint32_t a = en_fx_active(), m = 0, i;
    for (i = 0; i < 4u; i++)
        if (a & (1u << i))
            m |= 1u << FXKEY[i];
    return m;
}

static void fx_key(uint32_t k)           /* a key pressed while FX is held: that effect on or off */
{
    uint32_t i, b = en_fx_bypass_get();
    for (i = 0; i < 4u; i++)
        if (FXKEY[i] == k) {
            b ^= 1u << i;
            en_fx_bypass(b);
            ui.fx_used = 1;
            ui_say(FXNAME[i], (b & (1u << i)) ? "OFF" : "ON");
            return;
        }
}

/* ------------------------------------------------------------------ input --- */
static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, fx_layer = btn >> B_FX & 1u, dkeys = plat_keys();
    int32_t e;
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    keys = touch_keys();
    if (fx_layer && ui.fx_t && plat_ms() - ui.fx_t > 80u) {   /* the layer: the plain matrix, not the sensed rows
                                                               * (a button press can jolt their readings), and not
                                                               * in the first moments after FX went down */
        for (k = 0; k < 4u; k++) {
            uint32_t bit = 1u << FXKEY[k];
            if ((dkeys & bit) && !(ui.dkeys & bit))
                fx_key(FXKEY[k]);
        }
    }
    ui.dkeys = dkeys;
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            if (fx_layer || ui.fx_t || plat_ms() - ui.fx_up < 120u)   /* FX held, or just released: no note */
                continue;
            key_on(k, touch_vel(k));
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (ui.keynotes & bit)           /* (else it switched an effect: nothing to end) */
                key_off(k);
        } else if ((keys & bit) && (ui.keynotes & bit) && touch_pressure(k) != ui.prs[k]) {   /* pressure: poly aftertouch */
            ui.prs[k] = touch_pressure(k);
            en_pressure(ui.keynote[k], ui.prs[k]);
            plat_midi_send(0x0Au | (0xA0u | (uint32_t)ui.keych[k]) << 8 | (uint32_t)ui.keynote[k] << 16
                           | (uint32_t)ui.prs[k] << 24, TO_USB | TO_BLE);
        }
    }
    for (k = 0; k < NB; k++) {               /* a button pressed */
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (ui.dialog) {                     /* the save dialog: OCT+ saves, anything else backs out */
            if (k == B_OCTUP)
                save_confirm();
            else if (k == B_REC && FM1B_BLE)
                ui.rec_t = plat_ms();
            else {
                ui.dialog = 0;
                ui.redraw = 1;
                ui_say("SAVE", "CANCELLED");
            }
            continue;
        }
        switch (k) {
        case B_OCTDN:
            if (ui.page == PG_DISC) {
                discover_set(0);
                ui_say("SET A", en_patch.name);
            } else {
                octave_step(-1);
            }
            break;
        case B_OCTUP:
            if (ui.page == PG_DISC) {
                discover_set(1);
                ui_say("SET B", en_patch.name);
            } else {
                octave_step(1);
            }
            break;
        case B_PLAY: {
            uint32_t held = ui.keynotes, k;
            for (k = 0; k < NKEYS; k++)      /* held keys: their notes end the old way and start again the new way */
                if (held >> k & 1u)
                    key_off(k);
            en_patch.p[P_ARP_ON] = en_patch.p[P_ARP_ON] > 63u ? 0u : 127u;
            patch_dirty = 1;
            for (k = 0; k < NKEYS; k++)
                if (held >> k & 1u)
                    key_on(k, ui.vel[k]);
            ui_say("ARP", en_patch.p[P_ARP_ON] ? "ON" : "OFF");
            break;
        }
        case B_ARP: page_cycle(PG_ARP1, PG_ARP4); break;
        case B_SEQ:                          /* on release: a tap dices (the pages held: that section), a hold opens DISCOVER */
            ui.seq_t = plat_ms();
            ui.seq_with = btn & (1u << B_EDIT | 1u << B_FX | 1u << B_ENV | 1u << B_LFO | 1u << B_SEL | 1u << B_ARP);
            break;
        case B_HOME: page_set(PG_HOME); break;
        case B_EDIT:
            if (!(btn >> B_SEQ & 1u))
                page_set(ui.page == PG_VOICE ? PG_FM : ui.page == PG_FM ? PG_OSCA
                         : ui.page >= PG_OSCA && ui.page < PG_VOICE ? ui.page + 1u : PG_OSCA);
            break;
        case B_FX:                           /* on release: a tap cycles the pages, unless a key switched an effect */
            ui.fx_t = plat_ms();
            ui.fx_used = 0;
            ui.redraw = 1;
            break;
        case B_ENV: if (!(btn >> B_SEQ & 1u)) page_cycle(PG_AENV, PG_MENV); break;
        case B_LFO: if (!(btn >> B_SEQ & 1u)) page_cycle(PG_LFO1, PG_LFO2); break;
        case B_SEL:
            if (!(btn >> B_SEQ & 1u))
                page_set(ui.page == PG_MOD ? PG_MACROS : ui.page == PG_MACROS ? PG_MIDI : PG_MOD);
            break;
        case B_SAVE: {
            char s[4];
            ui.dialog = 1;
            ui.save_to = set.preset >= NFACTORY ? (uint8_t)(set.preset - NFACTORY) : ui.save_to;
            ui.redraw = 1;
            slot_name(NFACTORY + ui.save_to, s);
            ui_say("SAVE TO", s);
            break;
        }
        case B_REC:
            if (FM1B_BLE)
                ui.rec_t = plat_ms();        /* a press: on release (a hold switches Bluetooth) */
            else
                ui_say("BUTTON", "REC");
            break;
        default: ui_say("BUTTON", BTN_NAME[k]); break;
        }
    }
    if (ui.fx_t && !(btn >> B_FX & 1u)) {    /* FX released */
        uint32_t held = plat_ms() - ui.fx_t;
        ui.fx_t = 0;
        ui.fx_up = plat_ms();
        ui.redraw = 1;
        if (!ui.fx_used && held < 600u && !(btn >> B_SEQ & 1u))
            page_cycle(PG_FILT, PG_FX2);
    }
    if (ui.seq_t) {
        if (btn >> B_SEQ & 1u) {
            if (plat_ms() - ui.seq_t >= 800u) {
                ui.seq_t = 0;
                page_set(PG_DISC);
            }
        } else {
            uint32_t w = ui.seq_with, sec;
            ui.seq_t = 0;
            sec = w & 1u << B_EDIT ? SEC_OSC : w & 1u << B_FX ? SEC_FILT | SEC_FX : w & 1u << B_ENV ? SEC_ENV
                  : w & 1u << B_LFO ? SEC_LFO : w & 1u << B_SEL ? SEC_MOD : w & 1u << B_ARP ? SEC_ARP : SEC_PATCH;
            discover_dice(sec);
            if (sec == SEC_PATCH) {
                ui_say("DICE", en_patch.name);
            } else {
                char t[8];
                hex4(dice_seed, t);
                ui_say(sec == SEC_OSC ? "DICE OSC" : sec == SEC_ENV ? "DICE ENV" : sec == SEC_LFO ? "DICE LFO"
                       : sec == SEC_MOD ? "DICE MOD" : sec == SEC_ARP ? "DICE ARP" : "DICE FILTER FX", t);
            }
        }
    }
    if (FM1B_BLE && ui.rec_t) {             /* REC: held a second, Bluetooth on / off; a press, a device */
        if (btn >> B_REC & 1u) {
            if (plat_ms() - ui.rec_t >= 1000u) {
                ui.rec_t = 0;
                ble_toggle();
            }
        } else {
            ui.rec_t = 0;
            if (FM1B_BLE_CENTRAL && !set.ble_off)
                ble_device_button();
            else
                ui_say("BUTTON", "REC");
        }
    }
    for (k = 0; k < NE; k++) {               /* an encoder turned */
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.redraw = 1;
        if (k == EN_ALGO && FM1B_BLE_CENTRAL && ui.looking) {
            btdev_choose(btdev_pick() + e);
            ble_device_say();
        } else if (ui.dialog && (k == EN_PRESET || k == EN_K1)) {
            char s[4];
            ui.save_to = (uint8_t)clamp((int32_t)ui.save_to + (e > 0 ? 1 : -1), 0, (int32_t)NUSER - 1);
            slot_name(NFACTORY + ui.save_to, s);
            ui_say("SAVE TO", s);
        } else if (k == EN_PRESET) {
            preset_step(e);
        } else if (k == EN_ALGO) {
            param_edit(P_FILT_TYPE, e);
        } else if (k == EN_SELECT) {
            page_set((uint32_t)clamp((int32_t)ui.page + (e > 0 ? 1 : -1), 0, PG_COUNT - 1));
        } else if (k >= EN_K1 && k <= EN_K4) {
            uint32_t id = page_param(ui.page, k - EN_K1);
            char t[16];
            switch (id) {
            case PID_SLOT:
                ui.slot = (uint8_t)clamp((int32_t)ui.slot + (e > 0 ? 1 : -1), 0, (int32_t)EN_NSLOTS - 1);
                int_text((int32_t)ui.slot + 1, t);
                ui_say("SLOT", t);
                break;
            case PID_BPM:
                set.bpm = (uint8_t)clamp((int32_t)set.bpm + (e >= 3 || e <= -3 ? e * 2 : e), 40, 240);
                en_tempo(set.bpm);
                set_dirty = 1;
                val_text(id, t);
                ui_say("BPM", t);
                break;
            case PID_ASEED: {
                uint32_t sd = (arp_seed() + (uint32_t)(e >= 3 || e <= -3 ? e * 37 : e)) & 0x3FFFu;
                en_patch.p[P_ARP_SEED_LO] = (uint8_t)(sd & 127u);
                en_patch.p[P_ARP_SEED_HI] = (uint8_t)(sd >> 7);
                patch_dirty = 1;
                val_text(id, t);
                ui_say("ARP SEED", t);
                break;
            }
            case PID_SEED:
                discover_seed_step(e);
                val_text(id, t);
                ui_say("SEED", t);
                break;
            case PID_MUTATE:
                discover_mutate(e);
                ui_say("MUTATED", en_patch.name);
                break;
            case PID_MORPH:
                if (have_a && have_b) {
                    discover_morph(e);
                    val_text(id, t);
                    ui_say("MORPH", t);
                } else {
                    ui_say("MORPH", have_a ? "OCT+ SETS B" : "OCT- SETS A");
                }
                break;
            case PID_MIDI_IN:
                set.midi_in = (uint8_t)clamp((int32_t)set.midi_in + (e > 0 ? 1 : -1), 0, 16);
                set_dirty = 1;
                en_all_off();                /* notes held on the old channel would never get their note-offs */
                val_text(id, t);
                ui_say("MIDI IN", t);
                break;
            case PID_MIDI_OUT:
                set.midi_out = (uint8_t)clamp((int32_t)set.midi_out + (e > 0 ? 1 : -1), 1, 16);
                set_dirty = 1;
                midi_settings_apply();
                val_text(id, t);
                ui_say("MIDI OUT", t);
                break;
            case PID_CLKOUT:
                set.clock_out = (uint8_t)!set.clock_out;
                set_dirty = 1;
                midi_settings_apply();
                ui_say("CLOCK OUT", set.clock_out ? "ON" : "OFF");
                break;
            case PID_LOCAL:
                set.local = (uint8_t)!set.local;
                set_dirty = 1;
                if (!set.local)
                    en_all_off();
                ui_say("LOCAL", set.local ? "ON" : "OFF");
                break;
            case PID_HIST:
                if (hist_step(e > 0 ? 1 : -1) == 0) {
                    val_text(id, t);
                    ui_say("HISTORY", t);
                } else {
                    ui_say("HISTORY", e > 0 ? "AT THE END" : "AT THE START");
                }
                break;
            default:
                param_edit(id, e);
                break;
            }
        }
    }
    if (pc_pending >= 0) {                   /* a program change from MIDI */
        char s[4];
        uint32_t i = (uint32_t)pc_pending;
        pc_pending = -1;
        if (i < NSLOTS) {
            project_load_slot(i);
            slot_name(i, s);
            ui_say(s, en_patch.name);
        }
    }
    if (overload_seen) {
        char t[4];
        overload_seen = 0;
        int_text((int32_t)en_voices_max(), t);
        ui_say("LOAD: VOICES", t);
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);                            /* the unlit buttons glow, readable in the dark */
    plat_leds(btn | ((set_dirty || patch_dirty) ? 1u << B_SAVE : 0u) | (en_patch.p[P_ARP_ON] > 63u ? 1u << B_PLAY : 0u),
              fx_layer ? fx_keys_lit() : keys);
}

static void ui_input_only(void) { input(); }
static void ui_overload(void) { overload_seen = 1; }

/* ------------------------------------------------------------------ drawing --- */
static void card(int32_t x, int32_t y, uint32_t knob, uint32_t id)   /* one parameter on one knob */
{
    char t[16];
    uint32_t v = id < P_COUNT ? en_patch.p[id]
                 : id == PID_SLOT ? (ui.slot + 1u) * 127u / EN_NSLOTS
                 : id == PID_BPM ? ((uint32_t)set.bpm - 40u) * 127u / 200u
                 : id == PID_MORPH ? (uint32_t)morph_pos
                 : id == PID_HIST ? (hist_n ? (uint32_t)(hist_i + 1u) * 127u / hist_n : 0u) : 0u;
    int32_t w;
    static const char *const KN[4] = {"K1", "K2", "K3", "K4"};
    const char *label = label_of(id);
    cv_rect(x, y, 114, 66, C_LINE);
    cv_rect(x + 1, y + 1, 112, 64, C_BLACK);
    cv_text(x + 7, y + 5, &FONT_XS, label, C_GRAY);
    cv_text(x + 114 - 7 - text_w(&FONT_XS, KN[knob]), y + 5, &FONT_XS, KN[knob], C_DIM);
    val_text(id, t);
    cv_text(x + 7, y + 22, &FONT_M, t, C_HI);
    w = (int32_t)(v * 100u / 127u);
    cv_rect(x + 7, y + 54, 100, 5, C_DIM);
    if (w)
        cv_rect(x + 7, y + 54, w, 5, C_HI);
}

static void draw_dialog(void)
{
    char s[4], t[32];
    slot_name(NFACTORY + ui.save_to, s);
    cv_begin(240, 144, C_BLACK);
    cv_text(10, 8, &FONT_B, "SAVE TO", C_GRAY);
    cv_text(10, 34, &FONT_L, s, C_HI);
    t[0] = 0;
    text_cat(t, slot_patch(NFACTORY + ui.save_to)->name);
    cv_text(10 + text_w(&FONT_L, s) + 14, 44, &FONT_M, t, C_AMB);
    cv_text(10, 84, &FONT_XS, "PRESET OR K1: SLOT", C_GRAY);
    cv_text(10, 102, &FONT_XS, "OCT+ SAVE   OCT- BACK", C_WHITE);
    cv_blit(0, 32);
}

static void draw(void)
{
    char t[24], s[4];
    int32_t note = en_sounding_note(), x;
    uint32_t i, now = plat_ms(), pct = plat_master() * 100u / 4096u, nv = en_voices(), nmax = en_voices_max();
    /* the header: the page, Bluetooth, the note; or a message for a moment */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        x = cv_text(10, 6, &FONT_B, ui.dialog ? "SAVE" : PAGES[ui.page].title, C_WHITE);
        if (FM1B_BLE)                        /* Bluetooth: grey while it waits, lit while a computer is connected */
            x = cv_text(x + 8, 10, &FONT_XS, "BLE", plat_ble_connected() ? C_HI : C_DIM);
        if (FM1B_BLE_CENTRAL)                /* a Bluetooth MIDI device: lit while connected */
            cv_text(x + 6, 10, &FONT_XS, "DEV", plat_ble_device() == BLE_DEV_READY ? C_HI : C_DIM);
        if (note >= 0) {
            note_name(note, t);
            cv_text(230 - text_w(&FONT_B, t), 6, &FONT_B, t, C_HI);
        }
    }
    cv_blit(0, 0);
    /* the body: four cards, the save dialog, or the FX layer while FX is held */
    if (ui.fx_t && (ui.btn >> B_FX & 1u)) {
        uint32_t a = en_fx_active(), b = en_fx_bypass_get(), i;
        static const char *const KEYN[4] = {"F3", "G3", "A3", "B3"};
        cv_begin(240, 144, C_BLACK);
        cv_text(10, 6, &FONT_B, "FX LAYER: THE KEYS SWITCH", C_GRAY);
        for (i = 0; i < 4u; i++) {
            int32_t y = 30 + (int32_t)i * 27;
            const char *state = (b & (1u << i)) ? "OFF" : (a & (1u << i)) ? "ON" : "NOT IN PATCH";
            cv_text(10, y, &FONT_B, KEYN[i], C_DIM);
            cv_text(44, y, &FONT_B, FXNAME[i], C_WHITE);
            cv_text(150, y, &FONT_B, state, (b & (1u << i)) ? C_GRAY : (a & (1u << i)) ? C_HI : C_DIM);
        }
        cv_blit(0, 32);
    } else if (ui.dialog) {
        draw_dialog();
    } else {
        cv_begin(240, 144, C_BLACK);
        for (i = 0; i < 4u; i++)
            card(4 + (int32_t)(i & 1u) * 118, 4 + (int32_t)(i >> 1) * 70, i, page_param(ui.page, i));
        cv_blit(0, 32);
    }
    /* the footer: the preset, the voices, the filter type, the octave, the volume, MIDI, the load */
    cv_begin(240, 64, C_BLACK);
    slot_name(set.preset, s);
    x = cv_text(10, 6, &FONT_B, s, C_GRAY);
    cv_text(x + 8, 6, &FONT_B, en_patch.name, patch_dirty ? C_WHITE : C_HI);
    for (i = 0; i < EN_NVOICES; i++)         /* a block a voice: lit while it sounds, dark past the cap */
        cv_rect(10 + (int32_t)i * 11, 32, 8, 8, i < nv ? C_HI : i < nmax ? C_DIM : C_LINE);
    cv_text(104, 30, &FONT_XS, en_patch.p[P_ARP_ON] > 63u ? (en_arp_running() ? "ARP>" : "ARP") : FILT_NAME[en_patch.p[P_FILT_TYPE] < FILT_COUNT ? en_patch.p[P_FILT_TYPE] : 0], C_AMB);
    octave_text(t);
    cv_text(130, 30, &FONT_XS, "OCT", C_GRAY);
    cv_text(156, 30, &FONT_XS, t, C_HI);
    cv_text(180, 30, &FONT_XS, "VOL", C_GRAY);
    int_text((int32_t)pct, t);
    cv_text(206, 30, &FONT_XS, t, C_HI);
    if (midi_count) {                        /* the last MIDI message in, and where from */
        static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
        static const char HEX[] = "0123456789ABCDEF";
        uint32_t m = midi_last, b;
        char h[12];
        for (b = 0; b < 3u; b++) {
            uint32_t v = (m >> (8u + 8u * b)) & 0xFFu;
            h[3 * b] = HEX[v >> 4];
            h[3 * b + 1] = HEX[v & 15u];
            h[3 * b + 2] = b < 2u ? ' ' : 0;
        }
        cv_text(10, 48, &FONT_XS, "MIDI", C_GRAY);
        cv_text(44, 48, &FONT_XS, midi_last_port < NPORTS ? PORT[midi_last_port] : "", C_HI);
        cv_text(74, 48, &FONT_XS, h, C_WHITE);
    }
    int_text((int32_t)plat_cpu_pct(), t);
    text_cat(t, "%");
    cv_text(180, 48, &FONT_XS, "CPU", C_GRAY);
    cv_text(206, 48, &FONT_XS, t, C_DIM);
    cv_blit(0, 176);
    ui.meter_t = now;
}

static void ui_init(void)
{
    touch_on(1);                             /* velocity and pressure: the keys' readings on */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.page = PG_HOME;
    ui.redraw = 1;
}

static void ui_frame(void)
{
    static uint32_t last_pct, last_ble, last_midi, last_voices;
    uint32_t now = plat_ms(), pct = plat_master() * 100u / 4096u, nv = en_voices();
    input();
    speak();
    if (FM1B_BLE_CENTRAL) {                  /* looking: a device found is named at once (REC connects to it) */
        static int found;
        int n = ui.looking ? btdev_count() : 0, ev;
        btdev_page(ui.looking && !set.ble_off);
        if (n > found && btdev_pick() < n)
            ble_device_say();
        found = n;
        while ((ev = btdev_event()) != BTDEV_NONE)   /* a device connected, given up or lost */
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                ui_say("DEVICE", btdev_word(ev));
    }
    if (midi_count != last_midi) {
        last_midi = midi_count;
        ui.redraw = 1;
    }
    if (midi_edited) {
        midi_edited = 0;
        ui.redraw = 1;
    }
    if (nv != last_voices && now - ui.meter_t > 80u) {   /* the voice meter, not every frame */
        last_voices = nv;
        ui.redraw = 1;
    }
    if (pct != last_pct || (uint32_t)(plat_ble_connected() | plat_ble_device() << 1) != last_ble ||
        (ui.msg_until && now >= ui.msg_until)) {
        last_pct = pct;
        last_ble = (uint32_t)(plat_ble_connected() | plat_ble_device() << 1);
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}
