/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* What Pocket Poly saves: the settings (octave, Bluetooth, the preset in use) and the bank of user patches, each a
 * store object; the factory patches built at boot from an INIT patch and a list of changes each. Slots 0..7 are
 * F1..F8, 8..23 are U1..U16. */
#define SET_MAGIC 0x50504C34u            /* "PPL4": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t ble_off;                     /* Bluetooth switched off (REC held); 0, the default: on */
    uint8_t preset;                      /* the slot in use */
    uint8_t bpm;                         /* the arp's internal tempo, 40..240 */
    uint8_t midi_in;                     /* 0 omni, 1..16 */
    uint8_t midi_out;                    /* 1..16 */
    uint8_t clock_out;                   /* 0 / 1 */
    uint8_t local;                       /* 1: the keys play the engine; 0: MIDI out only */
} settings_t;
static settings_t set;
static uint8_t set_dirty, patch_dirty;   /* changed since the last save: SAVE is lit */

#define BANK_MAGIC_V4 0x344B4E42u        /* "BNK4": the v0.4 parameter set (FM6), SPREAD 0..127 one way */
#define BANK_MAGIC 0x354B4E42u           /* "BNK5": SPREAD bipolar, off at 64 */
#define NFACTORY 8u
#define NUSER 16u
#define NSLOTS (NFACTORY + NUSER)
typedef struct {
    uint32_t magic;
    uint8_t count, psize, rsv[2];
    patch_t u[NUSER];
} bank_t;
static bank_t bank PLAT_POOL;
static patch_t factory[NFACTORY] PLAT_POOL;

/* the FM bank: 32 voices saved as one object, and the LIVE voice the dice writes */
#define FMBANK_MAGIC 0x31424D46u         /* "FMB1" */
#define NFMFACTORY 8u
typedef struct {
    uint32_t magic;
    uint8_t count, vsize, rsv[2];
    fm_voice_t v[FM_BANK];
} fmbank_t;
/* one copy in RAM: the engine reads the bank's 32 voices in place, and LIVE, which must follow them directly */
static struct {
    fmbank_t bank;                       /* 3336 B: magic, count, size, v[32] */
    fm_voice_t live;                     /* the 33rd voice the engine sees: fm_voices()[FM_LIVE] */
} fmstore;
#define fmbank (fmstore.bank)
#define fm_live (fmstore.live)
static uint8_t fmbank_dirty, bank_dirty;   /* changed since stored: the FM bank, the user bank */
static const fm_voice_t *fm_voices(void) { return fmstore.bank.v; }

/* an operator: R1 R2 R3 R4 L1 L2 L3 L4 coarse fine detune level vel mode ks */
#define OP(r1, r2, r3, r4, l1, l2, l3, l4, co, fi, de, lv, ve, ks) {{r1, r2, r3, r4}, {l1, l2, l3, l4}, co, fi, de, lv, ve, 0, ks}
static const fm_voice_t FM_FACTORY[NFMFACTORY] = {
    {{OP(95, 28, 20, 50, 99, 85, 0, 0, 1, 0, 0, 99, 2, 0),  OP(99, 45, 25, 50, 99, 60, 0, 0, 14, 0, 0, 52, 6, 2),
      OP(95, 30, 22, 50, 99, 88, 0, 0, 1, 0, 3, 99, 2, 0),  OP(99, 50, 28, 50, 99, 65, 0, 0, 1, 0, 0, 70, 5, 1),
      OP(95, 28, 20, 50, 99, 85, 0, 0, 1, 0, -3, 96, 2, 0), OP(99, 55, 30, 50, 99, 70, 0, 0, 1, 0, 0, 62, 5, 1)},
     4, 6, 0, "E.PIANO"},
    {{OP(99, 50, 30, 65, 99, 92, 88, 0, 1, 0, 0, 99, 1, 0),  OP(99, 45, 35, 65, 99, 55, 40, 0, 1, 0, 0, 72, 6, 1),
      OP(99, 50, 30, 65, 99, 92, 90, 0, 0, 0, 0, 90, 1, 0),  OP(99, 45, 35, 65, 99, 50, 38, 0, 1, 0, 0, 62, 5, 1),
      OP(99, 45, 35, 65, 99, 45, 30, 0, 2, 0, 0, 52, 5, 1),  OP(99, 42, 35, 65, 99, 40, 28, 0, 3, 0, 0, 44, 6, 1)},
     0, 5, 0, "FM BASS"},
    {{OP(72, 50, 50, 60, 99, 95, 92, 0, 1, 0, -2, 88, 1, 1), OP(66, 45, 50, 60, 99, 78, 72, 0, 1, 0, 0, 86, 4, 1),
      OP(72, 50, 50, 60, 99, 95, 92, 0, 1, 0, 2, 99, 1, 1),  OP(72, 50, 50, 60, 99, 95, 92, 0, 1, 0, 0, 99, 1, 1),
      OP(66, 45, 50, 60, 99, 80, 74, 0, 1, 0, -1, 92, 1, 1), OP(62, 45, 50, 60, 99, 82, 76, 0, 1, 0, 0, 84, 4, 1)},
     21, 7, 0, "BRASS"},
    {{OP(99, 22, 15, 40, 99, 70, 0, 0, 1, 0, 0, 99, 1, 0),  OP(99, 25, 15, 40, 99, 60, 0, 0, 3, 50, 0, 80, 4, 2),
      OP(99, 20, 15, 40, 99, 70, 0, 0, 2, 0, 5, 85, 1, 0),  OP(99, 25, 15, 40, 99, 55, 0, 0, 7, 1, 0, 70, 4, 2),
      OP(99, 20, 15, 40, 99, 65, 0, 0, 1, 0, -4, 70, 1, 0), OP(99, 30, 20, 40, 99, 50, 0, 0, 14, 0, 0, 60, 4, 2)},
     4, 3, 0, "BELL"},
    {{OP(55, 40, 30, 42, 99, 95, 92, 0, 1, 0, 0, 99, 0, 0),  OP(45, 35, 30, 42, 99, 70, 64, 0, 1, 0, 4, 68, 0, 0),
      OP(55, 40, 30, 42, 99, 95, 92, 0, 1, 0, -4, 95, 0, 0), OP(45, 35, 30, 42, 99, 72, 66, 0, 3, 0, 0, 58, 0, 0),
      OP(55, 40, 30, 42, 99, 92, 88, 0, 2, 0, 3, 72, 0, 0),  OP(45, 35, 30, 42, 99, 70, 62, 0, 1, 0, -3, 62, 0, 0)},
     5, 2, 0, "GLASS PAD"},
    {{OP(99, 35, 25, 60, 99, 50, 0, 0, 1, 0, 0, 99, 2, 1),  OP(99, 45, 0, 60, 99, 0, 0, 0, 4, 0, 0, 75, 5, 2),
      OP(99, 35, 25, 60, 99, 50, 0, 0, 2, 0, 2, 60, 2, 1),  OP(99, 50, 0, 60, 99, 0, 0, 0, 10, 0, 0, 50, 5, 2),
      OP(99, 35, 25, 60, 99, 50, 0, 0, 1, 0, -2, 40, 2, 1), OP(99, 50, 0, 60, 99, 0, 0, 0, 1, 0, 0, 0, 0, 0)},
     0, 0, 0, "MARIMBA"},
    {{OP(99, 99, 99, 90, 99, 99, 99, 0, 0, 0, 0, 99, 0, 0),  OP(99, 99, 99, 90, 99, 99, 99, 0, 1, 0, 0, 90, 0, 0),
      OP(99, 99, 99, 90, 99, 99, 99, 0, 2, 0, 0, 85, 0, 0),  OP(99, 99, 99, 90, 99, 99, 99, 0, 3, 0, 0, 70, 0, 0),
      OP(99, 99, 99, 90, 99, 99, 99, 0, 4, 0, 0, 60, 0, 0),  OP(99, 99, 99, 90, 99, 99, 99, 0, 6, 0, 0, 50, 0, 0)},
     31, 0, 0, "ORGAN"},
    {{OP(99, 55, 50, 60, 99, 95, 92, 0, 1, 0, 0, 99, 2, 1),  OP(99, 42, 50, 60, 99, 70, 62, 0, 1, 0, 2, 92, 5, 1),
      OP(99, 40, 50, 60, 99, 60, 50, 0, 2, 0, -2, 84, 5, 1), OP(99, 55, 50, 60, 99, 95, 92, 0, 1, 0, -4, 90, 2, 1),
      OP(99, 55, 50, 60, 99, 90, 86, 0, 2, 0, 0, 68, 2, 1),  OP(99, 45, 50, 60, 99, 76, 70, 0, 1, 0, 4, 84, 5, 1)},
     15, 7, 0, "FM LEAD"},
};

static void fmbank_defaults(void)
{
    uint32_t i;
    fmbank.magic = FMBANK_MAGIC;
    fmbank.count = FM_BANK;
    fmbank.vsize = sizeof(fm_voice_t);
    fmbank.rsv[0] = fmbank.rsv[1] = 0;
    for (i = 0; i < FM_BANK; i++)
        fmbank.v[i] = FM_FACTORY[i % NFMFACTORY];
}

static void fm_voice_fix(fm_voice_t *v)
{
    uint32_t k, i;
    for (k = 0; k < FM_OPS; k++) {
        fm_op_t *o = &v->op[k];
        for (i = 0; i < 4u; i++) {
            if (o->r[i] > 99u)
                o->r[i] = 99;
            if (o->l[i] > 99u)
                o->l[i] = 99;
        }
        if (o->coarse > 31u)
            o->coarse = 31;
        if (o->fine > 99u)
            o->fine = 99;
        if (o->detune < -7 || o->detune > 7)
            o->detune = 0;
        if (o->level > 99u)
            o->level = 99;
        o->vel &= 7u;
        o->mode &= 1u;
        o->ks &= 7u;
    }
    if (v->alg > 31u)
        v->alg = 0;
    if (v->fb > 7u)
        v->fb = 7;
    if (v->transpose < -24 || v->transpose > 24)
        v->transpose = 0;
    for (i = 0; i < FM_NAME - 1u && v->name[i]; i++)
        if (v->name[i] < 32 || v->name[i] > 126)
            v->name[i] = '?';
    v->name[FM_NAME - 1u] = 0;
}

static const char *fm_voice_name(uint32_t i) { return i < FM_BANK ? fmbank.v[i].name : fm_live.name; }

#define END 255u
/* the INIT patch: one saw, the filter open, a plain envelope, every send off, every macro and slot empty */
static void patch_init(patch_t *p, const char *name)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        p->p[i] = 0;
    for (i = 0; i < PATCH_NAME; i++)
        p->name[i] = 0;
    for (i = 0; i < PATCH_NAME - 1u && name[i]; i++)
        p->name[i] = name[i];
    p->p[P_OSC1_MODEL] = MODEL_SAW;
    p->p[P_OSC1_LVL] = 100;
    p->p[P_OSC2_MODEL] = MODEL_SAW;
    p->p[P_OSC2_SEMI] = 64;
    p->p[P_OSC2_FINE] = 64;
    p->p[P_CUTOFF] = 110;
    p->p[P_RESO] = 10;
    p->p[P_FENV_AMT] = 64;
    p->p[P_KEYTRK] = 40;
    p->p[P_F_DEC] = 70;
    p->p[P_F_REL] = 70;
    p->p[P_A_ATK] = 2;
    p->p[P_A_DEC] = 64;
    p->p[P_A_SUS] = 127;
    p->p[P_A_REL] = 60;
    p->p[P_M_ATK] = 0;
    p->p[P_M_DEC] = 70;
    p->p[P_M_LVL] = 127;
    p->p[P_LFO1_RATE] = 60;
    p->p[P_LFO2_RATE] = 40;
    p->p[P_VEL_AMP] = 60;
    p->p[P_DRIFT] = 10;
    p->p[P_SPREAD] = 64;
    p->p[P_LEVEL] = 100;
    p->p[P_DLY_TIME] = 70;
    p->p[P_DLY_FDBK] = 45;
    p->p[P_REV_SIZE] = 70;
    p->p[P_REV_DAMP] = 50;
    p->p[P_MACRO1] = p->p[P_MACRO2] = p->p[P_MACRO3] = p->p[P_MACRO4] = 0;
    p->p[P_MNAME1] = MN_BRITE;
    p->p[P_MNAME2] = MN_TIMBRE;
    p->p[P_MNAME3] = MN_MOTION;
    p->p[P_MNAME4] = MN_SPACE;
    for (i = 0; i < EN_NSLOTS; i++)
        p->p[P_MOD(i, 2)] = 64;
    p->p[P_ARP_ON] = 0;                      /* the arp: off, up, sixteenths, one octave, 60 % gate, every step */
    p->p[P_ARP_MODE] = ARP_UP;
    p->p[P_ARP_DIV] = DIV_16;
    p->p[P_ARP_OCT] = 0;
    p->p[P_ARP_GATE] = 36;
    p->p[P_ARP_SWING] = 0;
    p->p[P_ARP_STEPS] = 127;
    p->p[P_ARP_HITS] = 127;
    p->p[P_ARP_ROT] = 0;
    p->p[P_ARP_PROB] = 127;
    p->p[P_ARP_RATCHET] = 0;
    p->p[P_ARP_LATCH] = 0;
    p->p[P_ARP_TLEN] = 54;
    p->p[P_ARP_TLOCK] = 90;
    p->p[P_ARP_SEED_LO] = 0x34;
    p->p[P_ARP_SEED_HI] = 0x24;
    p->p[P_ARP_SLOCK] = 127;
    p->p[P_FM_VOICE] = 0;
    p->p[P_FM_FDBK] = 64;
    p->p[P_FM_RATE] = 64;
    /* the four macros do something on every patch: brightness, timbre, motion, space */
    p->p[P_MOD(0, 0)] = MS_MACRO1; p->p[P_MOD(0, 1)] = MD_CUTOFF;   p->p[P_MOD(0, 2)] = 110;
    p->p[P_MOD(1, 0)] = MS_MACRO2; p->p[P_MOD(1, 1)] = MD_SHAPE1;   p->p[P_MOD(1, 2)] = 120;
    p->p[P_MOD(2, 0)] = MS_MACRO3; p->p[P_MOD(2, 1)] = MD_LFO1_RATE; p->p[P_MOD(2, 2)] = 100;
    p->p[P_MOD(3, 0)] = MS_MACRO4; p->p[P_MOD(3, 1)] = MD_REV_SEND; p->p[P_MOD(3, 2)] = 110;
    p->p[P_MOD(4, 0)] = MS_MACRO4; p->p[P_MOD(4, 1)] = MD_DLY_SEND; p->p[P_MOD(4, 2)] = 90;
}

static void patch_set(patch_t *p, const uint8_t *changes)   /* pairs of parameter, value; END ends */
{
    while (changes[0] != END) {
        p->p[changes[0]] = changes[1];
        changes += 2;
    }
}

/* the factory patches as changes to INIT: what makes each one itself */
static const uint8_t PATCH_GLASS[] = {
    P_OSC1_MODEL, MODEL_WT_C, P_OSC1_SHAPE, 30, P_OSC2_MODEL, MODEL_SINE, P_OSC2_LVL, 50, P_OSC2_SEMI, 76,
    P_CUTOFF, 100, P_RESO, 20, P_FENV_AMT, 84, P_KEYTRK, 90,
    P_F_DEC, 84, P_F_SUS, 30, P_A_ATK, 0, P_A_DEC, 96, P_A_SUS, 50, P_A_REL, 84,
    P_M_DEC, 90, P_MOD(5, 0), MS_MENV, P_MOD(5, 1), MD_SHAPE1, P_MOD(5, 2), 100,
    P_MOD(6, 0), MS_VEL, P_MOD(6, 1), MD_CUTOFF, P_MOD(6, 2), 90,
    P_LFO2_RATE, 30, P_MOD(7, 0), MS_LFO2, P_MOD(7, 1), MD_PAN, P_MOD(7, 2), 100,
    P_VEL_AMP, 90, P_DRIFT, 20,
    P_ENS_MODE, ENS_CHORUS1, P_ENS_MIX, 60, P_REV_SEND, 55, P_REV_SIZE, 85, P_DLY_SEND, 25, P_DLY_TIME, 84,
    END};
static const uint8_t PATCH_PDBASS[] = {
    P_OSC1_MODEL, MODEL_PD, P_OSC1_SHAPE, 60, P_OSC1_LVL, 115, P_OSC2_MODEL, MODEL_PULSE, P_OSC2_SHAPE, 20, P_OSC2_LVL, 70,
    P_OSC2_SEMI, 52, P_SUB_LVL, 80, P_CUTOFF, 70, P_RESO, 30, P_FENV_AMT, 96, P_KEYTRK, 30, P_DRIVE, 45,
    P_F_DEC, 62, P_F_SUS, 15, P_F_REL, 50, P_A_DEC, 70, P_A_SUS, 110, P_A_REL, 45,
    P_M_DEC, 64, P_MOD(5, 0), MS_MENV, P_MOD(5, 1), MD_SHAPE1, P_MOD(5, 2), 118,
    P_MOD(6, 0), MS_VEL, P_MOD(6, 1), MD_SHAPE1, P_MOD(6, 2), 90,
    P_VEL_AMP, 80, P_DRIFT, 20, P_LEVEL, 127, P_DLY_SEND, 0, P_REV_SEND, 10, END};
static const uint8_t PATCH_WIRE[] = {
    P_OSC1_MODEL, MODEL_SAW, P_OSC2_MODEL, MODEL_SAW, P_OSC2_LVL, 90, P_OSC2_SEMI, 83, P_OSC2_FINE, 66, P_SYNC, 127,
    P_RING, 60, P_CUTOFF, 95, P_RESO, 25, P_FENV_AMT, 90, P_KEYTRK, 60, P_DRIVE, 25,
    P_F_DEC, 60, P_F_SUS, 0, P_F_REL, 55, P_A_ATK, 0, P_A_DEC, 80, P_A_SUS, 0, P_A_REL, 70,
    P_M_DEC, 72, P_MOD(5, 0), MS_MENV, P_MOD(5, 1), MD_PITCH2, P_MOD(5, 2), 92,
    P_MOD(6, 0), MS_VEL, P_MOD(6, 1), MD_CUTOFF, P_MOD(6, 2), 96,
    P_VEL_AMP, 95, P_DRIFT, 25,
    P_DLY_SEND, 45, P_DLY_TIME, 78, P_DLY_FDBK, 60, P_REV_SEND, 30,
    P_ARP_MODE, ARP_UPDN, P_ARP_OCT, 43, P_ARP_GATE, 25, P_ARP_STEPS, 127, P_ARP_HITS, 87, P_ARP_RATCHET, 20, END};
static const uint8_t PATCH_EVOLVE[] = {
    P_OSC1_MODEL, MODEL_WT_A, P_OSC1_SHAPE, 25, P_OSC2_MODEL, MODEL_WT_B, P_OSC2_SHAPE, 40, P_OSC2_LVL, 80,
    P_OSC2_FINE, 70, P_SUB_LVL, 20, P_CUTOFF, 85, P_RESO, 15, P_FENV_AMT, 70, P_KEYTRK, 50,
    P_F_ATK, 90, P_F_DEC, 100, P_F_SUS, 60, P_F_REL, 100, P_A_ATK, 92, P_A_DEC, 90, P_A_SUS, 115, P_A_REL, 108,
    P_LFO1_RATE, 35, P_LFO1_PITCH, 4, P_LFO2_RATE, 22, P_LFO2_SHAPE, LFO_SINE,
    P_MOD(5, 0), MS_LFO2, P_MOD(5, 1), MD_SHAPE1, P_MOD(5, 2), 104,
    P_MOD(6, 0), MS_LFO1, P_MOD(6, 1), MD_SHAPE2, P_MOD(6, 2), 86,
    P_MOD(7, 0), MS_KEY, P_MOD(7, 1), MD_SHAPE1, P_MOD(7, 2), 80,
    P_VEL_AMP, 40, P_DRIFT, 45,
    P_ENS_MODE, ENS_ENSEMBLE, P_ENS_MIX, 80, P_REV_SEND, 60, P_REV_SIZE, 100, P_REV_DAMP, 40, P_DLY_SEND, 20,
    P_DLY_TIME, 96, P_DLY_FDBK, 55, END};
static const uint8_t PATCH_FMBELL[] = {
    P_OSC1_MODEL, MODEL_FM6, P_FM_VOICE, 3, P_OSC1_SHAPE, 64, P_OSC2_LVL, 0, P_CUTOFF, 127, P_RESO, 0, P_FENV_AMT, 64,
    P_KEYTRK, 0, P_A_ATK, 0, P_A_DEC, 110, P_A_SUS, 127, P_A_REL, 104, P_LEVEL, 127,
    P_MOD(5, 0), MS_VEL, P_MOD(5, 1), MD_SHAPE1, P_MOD(5, 2), 92,
    P_MOD(1, 1), MD_SHAPE1, P_MNAME2, MN_FM,
    P_VEL_AMP, 80, P_DRIFT, 15,
    P_ENS_MODE, ENS_CHORUS2, P_ENS_MIX, 40, P_REV_SEND, 60, P_REV_SIZE, 95, P_DLY_SEND, 30, P_DLY_TIME, 88,
    P_DLY_FDBK, 50, P_ARP_MODE, ARP_TURING, P_ARP_DIV, DIV_8, P_ARP_OCT, 43, P_ARP_GATE, 60, P_ARP_TLOCK, 100, END};
static const uint8_t PATCH_SYNCLEAD[] = {
    P_OSC1_MODEL, MODEL_SAW, P_OSC2_MODEL, MODEL_SAW, P_OSC1_LVL, 40, P_OSC2_LVL, 110, P_OSC2_SEMI, 71, P_SYNC, 127,
    P_CUTOFF, 105, P_RESO, 20, P_FENV_AMT, 76, P_KEYTRK, 70, P_DRIVE, 70,
    P_F_DEC, 80, P_F_SUS, 60, P_A_ATK, 4, P_A_DEC, 60, P_A_SUS, 110, P_A_REL, 55,
    P_M_ATK, 70, P_M_DEC, 90, P_MOD(5, 0), MS_MENV, P_MOD(5, 1), MD_PITCH2, P_MOD(5, 2), 100,
    P_MOD(6, 0), MS_WHEEL, P_MOD(6, 1), MD_PITCH2, P_MOD(6, 2), 100,
    P_MOD(7, 0), MS_LFO1, P_MOD(7, 1), MD_PITCH, P_MOD(7, 2), 68,
    P_LFO1_RATE, 78, P_LFO1_SHAPE, LFO_SINE, P_VEL_AMP, 50, P_DRIFT, 30, P_GLIDE, 30,
    P_DLY_SEND, 35, P_DLY_TIME, 82, P_DLY_FDBK, 40, P_REV_SEND, 25, END};
static const uint8_t PATCH_VOX[] = {
    P_OSC1_MODEL, MODEL_WT_B, P_OSC1_SHAPE, 50, P_OSC2_MODEL, MODEL_WT_B, P_OSC2_SHAPE, 70, P_OSC2_LVL, 85,
    P_OSC2_FINE, 71, P_CUTOFF, 100, P_RESO, 10, P_FENV_AMT, 64, P_KEYTRK, 60,
    P_A_ATK, 70, P_A_DEC, 80, P_A_SUS, 120, P_A_REL, 90,
    P_LFO1_RATE, 48, P_LFO1_PITCH, 6, P_LFO2_RATE, 26,
    P_MOD(5, 0), MS_LFO2, P_MOD(5, 1), MD_SHAPE1, P_MOD(5, 2), 108,
    P_MOD(6, 0), MS_LFO2, P_MOD(6, 1), MD_SHAPE2, P_MOD(6, 2), 24,
    P_MOD(7, 0), MS_PRESS, P_MOD(7, 1), MD_SHAPE1, P_MOD(7, 2), 100,
    P_VEL_AMP, 50, P_DRIFT, 55,
    P_ENS_MODE, ENS_ENSEMBLE, P_ENS_MIX, 70, P_REV_SEND, 55, P_REV_SIZE, 90, P_DLY_SEND, 15, END};

static void factory_build(void)
{
    patch_init(&factory[0], "INIT");
    patch_init(&factory[1], "GLASS KEYS");
    patch_set(&factory[1], PATCH_GLASS);
    patch_init(&factory[2], "PD BASS");
    patch_set(&factory[2], PATCH_PDBASS);
    patch_init(&factory[3], "WIRE PLUCK");
    patch_set(&factory[3], PATCH_WIRE);
    patch_init(&factory[4], "EVOLVE PAD");
    patch_set(&factory[4], PATCH_EVOLVE);
    patch_init(&factory[5], "FM BELL");
    patch_set(&factory[5], PATCH_FMBELL);
    patch_init(&factory[6], "SYNC LEAD");
    patch_set(&factory[6], PATCH_SYNCLEAD);
    patch_init(&factory[7], "VOX");
    patch_set(&factory[7], PATCH_VOX);
}

static void patch_fix(patch_t *p)        /* a patch from the flash or a bank: every field in range */
{
    uint32_t i;
    if (p->p[P_OSC1_MODEL] >= MODEL_COUNT)
        p->p[P_OSC1_MODEL] = MODEL_SAW;
    if (p->p[P_OSC2_MODEL] >= MODEL_FM6)     /* (oscillator B has no FM6) */
        p->p[P_OSC2_MODEL] = MODEL_SAW;
    if (p->p[P_FILT_TYPE] >= FILT_COUNT)
        p->p[P_FILT_TYPE] = FILT_LP;
    if (p->p[P_LFO1_SHAPE] >= LFO_COUNT)
        p->p[P_LFO1_SHAPE] = LFO_TRI;
    if (p->p[P_LFO2_SHAPE] >= LFO_COUNT)
        p->p[P_LFO2_SHAPE] = LFO_TRI;
    if (p->p[P_ENS_MODE] >= ENS_COUNT)
        p->p[P_ENS_MODE] = ENS_OFF;
    if (p->p[P_ARP_MODE] >= ARP_MODE_COUNT)
        p->p[P_ARP_MODE] = ARP_UP;
    if (p->p[P_FM_VOICE] > FM_LIVE)
        p->p[P_FM_VOICE] = 0;
    if (p->p[P_ARP_DIV] >= DIV_COUNT)
        p->p[P_ARP_DIV] = DIV_16;
    for (i = 0; i < 4u; i++)
        if (p->p[P_MNAME1 + i] >= MN_COUNT)
            p->p[P_MNAME1 + i] = MN_MACRO;
    for (i = 0; i < EN_NSLOTS; i++) {
        if (p->p[P_MOD(i, 0)] >= MS_COUNT)
            p->p[P_MOD(i, 0)] = MS_NONE;
        if (p->p[P_MOD(i, 1)] >= MD_COUNT)
            p->p[P_MOD(i, 1)] = MD_NONE;
    }
    for (i = 0; i < P_COUNT; i++)
        if (p->p[i] > 127u)
            p->p[i] = 127u;
    for (i = 0; i < PATCH_NAME - 1u && p->name[i]; i++)
        if (p->name[i] < 32 || p->name[i] > 126)
            p->name[i] = '?';
    p->name[PATCH_NAME - 1u] = 0;
}

static void bank_defaults(void)
{
    uint32_t i;
    bank.magic = BANK_MAGIC;
    bank.count = NUSER;
    bank.psize = sizeof(patch_t);
    bank.rsv[0] = bank.rsv[1] = 0;
    for (i = 0; i < NUSER; i++)
        bank.u[i] = factory[i % NFACTORY];
}

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.ble_off = 0;
    set.preset = 0;
    set.bpm = 120;
    set.midi_in = 0;
    set.midi_out = 1;
    set.clock_out = 0;
    set.local = 1;
}

static void project_load(void)
{
    uint32_t i;
    factory_build();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.ble_off > 1u || set.preset >= NSLOTS || set.bpm < 40u || set.bpm > 240u
        || set.midi_in > 16u || set.midi_out < 1u || set.midi_out > 16u || set.clock_out > 1u || set.local > 1u)
        settings_defaults();
    if (plat_store_load(OBJ_PATCHES, &bank, sizeof bank) != (int)sizeof bank
        || (bank.magic != BANK_MAGIC && bank.magic != BANK_MAGIC_V4) || bank.count != NUSER || bank.psize != sizeof(patch_t))
        bank_defaults();
    if (bank.magic == BANK_MAGIC_V4) {       /* a v0.4 bank: its SPREAD values meant something else; off, saved as BNK5 */
        for (i = 0; i < NUSER; i++)
            bank.u[i].p[P_SPREAD] = 64;
        bank.magic = BANK_MAGIC;
        bank_dirty = 1;
    }
    for (i = 0; i < NUSER; i++)
        patch_fix(&bank.u[i]);
    if (plat_store_load(OBJ_FMBANK, &fmbank, sizeof fmbank) != (int)sizeof fmbank || fmbank.magic != FMBANK_MAGIC
        || fmbank.count != FM_BANK || fmbank.vsize != sizeof(fm_voice_t))
        fmbank_defaults();
    for (i = 0; i < FM_BANK; i++)
        fm_voice_fix(&fmbank.v[i]);
    fm_live = FM_FACTORY[3];
    en_fm_voices_set(fm_voices());
    set_dirty = patch_dirty = fmbank_dirty = 0;
}

static int project_save(void)            /* the settings, the patches and, when it changed, the FM bank; 0 = saved */
{
    int r = plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    if (bank_dirty && plat_store_save(OBJ_PATCHES, &bank, sizeof bank))
        r = -1;
    if (fmbank_dirty && plat_store_save(OBJ_FMBANK, &fmbank, sizeof fmbank))
        r = -1;
    if (!r)
        set_dirty = patch_dirty = fmbank_dirty = bank_dirty = 0;
    return r;
}

static void fmbank_store(uint32_t slot, const fm_voice_t *v)   /* a voice into the bank (an import) */
{
    if (slot >= FM_BANK)
        return;
    fmbank.v[slot] = *v;
    fm_voice_fix(&fmbank.v[slot]);
    fmbank_dirty = 1;
}

static void fm_live_set(const fm_voice_t *v)   /* the LIVE voice (the dice's) */
{
    fm_live = *v;
    fm_voice_fix(&fm_live);
}

static const patch_t *slot_patch(uint32_t i) { return i < NFACTORY ? &factory[i] : &bank.u[i - NFACTORY]; }

static void slot_name(uint32_t i, char *b) /* "F1" .. "F8", "U1" .. "U16" */
{
    uint32_t n = i < NFACTORY ? i + 1u : i - NFACTORY + 1u;
    *b++ = i < NFACTORY ? 'F' : 'U';
    if (n >= 10u)
        *b++ = (char)('0' + n / 10u);
    *b++ = (char)('0' + n % 10u);
    *b = 0;
}

static void project_load_slot(uint32_t i) /* the slot's patch into the engine */
{
    en_patch = *slot_patch(i);
    patch_fix(&en_patch);
    if (set.preset != i) {
        set.preset = (uint8_t)i;
        set_dirty = 1;
    }
    patch_dirty = 0;
}

static void project_store_user(uint32_t u)   /* the live patch into user slot u, as the preset in use */
{
    if (en_patch.p[P_FM_VOICE] == FM_LIVE) { /* the dice's voice goes with the patch: bank voice 9 + u is its own */
        fmbank.v[NFMFACTORY + u] = fm_live;
        en_patch.p[P_FM_VOICE] = (uint8_t)(NFMFACTORY + u);
        fmbank_dirty = 1;
    }
    bank.u[u] = en_patch;
    bank_dirty = 1;
    set.preset = (uint8_t)(NFACTORY + u);
    set_dirty = 1;
}
