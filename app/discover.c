/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* Finding patches: DICE makes a whole patch (or one section of it) from a seed, within ranges that keep it a
 * sound rather than noise, so the same seed is the same patch and turning the seed browses them; MUTATE nudges
 * the continuous parameters; MORPH blends two stored patches, A and B; a history of sixteen steps goes back and
 * forward through what DICE, MUTATE and MORPH did. Main loop only (ui.c). */
#define HIST_N 16u
enum { SEC_OSC = 1u, SEC_FILT = 2u, SEC_ENV = 4u, SEC_LFO = 8u, SEC_FX = 16u, SEC_MOD = 32u, SEC_ARP = 64u,
       SEC_PATCH = 63u };
static uint16_t dice_seed = 0x2A17;
static uint32_t dice_counter = 0x9E37u, mutations, mutate_t;
static patch_t hist[HIST_N] PLAT_POOL, morph_a PLAT_POOL, morph_b PLAT_POOL;
static uint8_t hist_n, hist_i, have_a, have_b;   /* hist_i: the entry en_patch came from, 0..hist_n-1 */
static int32_t morph_pos = 64;

static uint32_t drnd(uint32_t *s)            /* xorshift, 0..0xFFFFFF */
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x >> 8;
}
static uint32_t drange(uint32_t *s, uint32_t lo, uint32_t hi) { return lo + drnd(s) % (hi - lo + 1u); }

static uint32_t section_of(uint32_t id)
{
    if (id <= P_SYNC)
        return SEC_OSC;
    if (id <= P_DRIVE)
        return SEC_FILT;
    if (id <= P_M_LVL)
        return SEC_ENV;
    if (id <= P_LFO2_SHAPE)
        return SEC_LFO;
    if (id <= P_LEVEL)
        return id == P_LEVEL ? 0u : SEC_OSC;
    if (id <= P_REV_SEND)
        return SEC_FX;
    if (id <= P_MNAME4)
        return 0u;                           /* the macros are the player's */
    if (id <= P_ARP_SLOCK)
        return SEC_ARP;
    if (id <= P_FM_RATE)
        return SEC_OSC;
    return SEC_MOD;
}

static int param_is_enum(uint32_t id)
{
    uint32_t k;
    switch (id) {
    case P_OSC1_MODEL: case P_OSC2_MODEL: case P_SYNC: case P_FILT_TYPE: case P_M_LOOP: case P_LFO1_SHAPE:
    case P_LFO2_SHAPE: case P_ENS_MODE: case P_MNAME1: case P_MNAME2: case P_MNAME3: case P_MNAME4:
    case P_ARP_ON: case P_ARP_MODE: case P_ARP_DIV: case P_ARP_LATCH: case P_ARP_SLOCK: case P_FM_VOICE:
        return 1;
    default:
        break;
    }
    if (id >= P_MOD0_SRC) {
        k = (id - P_MOD0_SRC) % 3u;
        return k != 2u;
    }
    return 0;
}

/* one parameter from the seed's stream, within its sane range */
static uint8_t dice_param(uint32_t id, uint32_t *s)
{
    uint32_t r = drnd(s);
    switch (id) {
    case P_OSC1_MODEL: return (uint8_t)(r % MODEL_COUNT);
    case P_OSC2_MODEL: return (uint8_t)(r % (MODEL_COUNT - 1u));   /* (no FM6 on B) */
    case P_OSC1_SHAPE: case P_OSC2_SHAPE: return (uint8_t)(r % 128u);
    case P_OSC1_LVL: return (uint8_t)drange(s, 70, 127);
    case P_OSC2_LVL: return (uint8_t)(r % 4u ? drange(s, 30, 120) : 0u);
    case P_OSC2_SEMI: {
        static const uint8_t SEMI[9] = {64, 64, 64, 33, 52, 76, 83, 95, 84};   /* 0, 0, 0, -12, -5, +5, +7, +12, +7.6 */
        return SEMI[r % 9u];
    }
    case P_OSC2_FINE: return (uint8_t)drange(s, 56, 72);
    case P_SUB_LVL: return (uint8_t)(r % 2u ? drange(s, 20, 100) : 0u);
    case P_NOISE_LVL: return (uint8_t)(r % 4u ? 0u : drange(s, 5, 40));
    case P_XMOD: return (uint8_t)(r % 3u ? 0u : drange(s, 20, 110));
    case P_RING: return (uint8_t)(r % 4u ? 0u : drange(s, 20, 90));
    case P_SYNC: return (uint8_t)(r % 5u ? 0u : 127u);
    case P_CUTOFF: return (uint8_t)drange(s, 45, 127);
    case P_RESO: return (uint8_t)drange(s, 0, 95);
    case P_FILT_TYPE: return (uint8_t)(r % 10u < 7u ? FILT_LP : r % 2u ? FILT_BP : FILT_HP);
    case P_FENV_AMT: return (uint8_t)drange(s, 30, 115);
    case P_KEYTRK: return (uint8_t)drange(s, 0, 127);
    case P_DRIVE: return (uint8_t)(r % 2u ? 0u : drange(s, 10, 90));
    case P_F_ATK: case P_A_ATK: case P_M_ATK: return (uint8_t)(r % 3u ? drange(s, 0, 40) : drange(s, 40, 100));
    case P_F_DEC: case P_A_DEC: case P_M_DEC: return (uint8_t)drange(s, 35, 105);
    case P_F_SUS: return (uint8_t)drange(s, 0, 110);
    case P_A_SUS: return (uint8_t)(r % 3u ? drange(s, 60, 127) : drange(s, 0, 60));
    case P_F_REL: case P_A_REL: return (uint8_t)drange(s, 30, 105);
    case P_M_LOOP: return (uint8_t)(r % 4u ? 0u : 127u);
    case P_M_LVL: return (uint8_t)drange(s, 60, 127);
    case P_LFO1_RATE: case P_LFO2_RATE: return (uint8_t)drange(s, 15, 100);
    case P_LFO1_SHAPE: case P_LFO2_SHAPE: return (uint8_t)(r % LFO_COUNT);
    case P_LFO1_PITCH: return (uint8_t)(r % 4u ? 0u : drange(s, 2, 25));
    case P_LFO1_CUT: return (uint8_t)(r % 2u ? 0u : drange(s, 5, 60));
    case P_VEL_AMP: return (uint8_t)drange(s, 30, 100);
    case P_SPREAD: return (uint8_t)drange(s, 32, 104);   /* either way, never to the edges */
    case P_DRIFT: return (uint8_t)drange(s, 5, 70);
    case P_GLIDE: return (uint8_t)(r % 5u ? 0u : drange(s, 10, 60));
    case P_ENS_MODE: return (uint8_t)(r % ENS_COUNT);
    case P_ENS_MIX: return (uint8_t)drange(s, 30, 100);
    case P_DLY_TIME: return (uint8_t)drange(s, 40, 110);
    case P_DLY_FDBK: return (uint8_t)drange(s, 20, 80);
    case P_DLY_SEND: return (uint8_t)(r % 3u ? drange(s, 0, 60) : 0u);
    case P_REV_SIZE: return (uint8_t)drange(s, 40, 115);
    case P_REV_DAMP: return (uint8_t)drange(s, 20, 90);
    case P_REV_SEND: return (uint8_t)drange(s, 0, 85);
    case P_ARP_MODE: return (uint8_t)(r % ARP_MODE_COUNT);
    case P_ARP_DIV: return (uint8_t)(r % 3u ? DIV_16 : r % 2u ? DIV_8 : DIV_16T);
    case P_ARP_OCT: return (uint8_t)(r % 2u ? 0u : 43u);   /* one or two octaves */
    case P_ARP_GATE: return (uint8_t)drange(s, 15, 70);
    case P_ARP_SWING: return (uint8_t)(r % 2u ? 0u : drange(s, 10, 60));
    case P_ARP_STEPS: return (uint8_t)(r % 2u ? 127u : 60u);   /* 16 or 8 steps */
    case P_ARP_HITS: return (uint8_t)drange(s, 60, 127);
    case P_ARP_ROT: return (uint8_t)drange(s, 0, 127);
    case P_ARP_PROB: return (uint8_t)drange(s, 85, 127);
    case P_ARP_RATCHET: return (uint8_t)(r % 2u ? 0u : drange(s, 10, 45));
    case P_ARP_TLEN: return (uint8_t)drange(s, 20, 110);
    case P_ARP_TLOCK: return (uint8_t)drange(s, 60, 120);
    case P_ARP_SEED_LO: case P_ARP_SEED_HI: return (uint8_t)(r % 128u);
    case P_FM_VOICE: return (uint8_t)(r % 2u ? FM_LIVE : drange(s, 0, FM_BANK - 1u));
    case P_FM_FDBK: return (uint8_t)drange(s, 40, 96);
    case P_FM_RATE: return (uint8_t)drange(s, 44, 84);
    default: break;
    }
    if (id >= P_MOD0_SRC) {                  /* slots 6..8 (the first five carry the macros): a source, a destination */
        uint32_t slot = (id - P_MOD0_SRC) / 3u, f = (id - P_MOD0_SRC) % 3u;
        static const uint8_t SRCS[7] = {MS_LFO1, MS_LFO2, MS_MENV, MS_VEL, MS_KEY, MS_PRESS, MS_RAND};
        static const uint8_t DSTS[10] = {MD_PITCH, MD_CUTOFF, MD_RESO, MD_SHAPE1, MD_SHAPE2, MD_PITCH2, MD_XMOD,
                                         MD_RING, MD_PAN, MD_LFO1_RATE};
        if (slot < 5u)
            return en_patch.p[id];
        if (f == 0u)
            return r % 3u ? SRCS[r % 7u] : MS_NONE;
        if (f == 1u)
            return DSTS[r % 10u];
        return (uint8_t)(r % 2u ? drange(s, 72, 110) : drange(s, 18, 56));
    }
    return en_patch.p[id];
}

static uint8_t hist_live;                    /* en_patch is a present the history does not hold yet */

static void hist_push(void)                  /* en_patch as it is, before a change (the change makes a live present) */
{
    uint32_t i;
    if (hist_n && hist_i + 1u < hist_n)      /* a change after going back drops the redo */
        hist_n = hist_i + 1u;
    if (hist_n == HIST_N) {
        for (i = 1; i < HIST_N; i++)
            hist[i - 1u] = hist[i];
        hist_n--;
    }
    hist[hist_n++] = en_patch;
    hist_i = hist_n - 1u;
    hist_live = 1;
}

static int hist_step(int32_t d)              /* -1 back, +1 forward: 0 = moved */
{
    if (!hist_n)
        return -1;
    if (d < 0) {
        if (hist_live) {                     /* going back from a live present: keep it to come back to */
            hist_push();
            hist_i = hist_n - 2u;
        } else if (hist_i == 0u) {
            return -1;
        } else {
            hist_i--;
        }
    } else {
        if (hist_live || hist_i + 1u >= hist_n)
            return -1;
        hist_i++;
    }
    hist_live = 0;
    en_patch = hist[hist_i];
    patch_dirty = 1;
    return 0;
}

static void hex4(uint32_t v, char *b)
{
    static const char HEX[] = "0123456789ABCDEF";
    b[0] = HEX[(v >> 12) & 15u];
    b[1] = HEX[(v >> 8) & 15u];
    b[2] = HEX[(v >> 4) & 15u];
    b[3] = HEX[v & 15u];
    b[4] = 0;
}

static void dice_apply(uint16_t seed, uint32_t sections)   /* the seed's patch, for these sections of en_patch */
{
    uint32_t s = (uint32_t)seed * 2654435761u + 0x1234567u, id;
    char h[5];
    for (id = 0; id < P_COUNT; id++)
        if (section_of(id) & sections)
            en_patch.p[id] = dice_param(id, &s);
    if ((sections & SEC_OSC) && en_patch.p[P_OSC1_MODEL] == MODEL_FM6) {   /* a diced FM6 gets a diced voice too */
        fm_voice_t fv;
        uint32_t r = s;
        fm_dice(&fv, &r);
        fm_live_set(&fv);
        en_patch.p[P_FM_VOICE] = FM_LIVE;
    }
    if (sections & SEC_ARP) {                /* a new arp seed with the arp */
        uint32_t r = drnd(&s);
        en_patch.p[P_ARP_SEED_LO] = (uint8_t)(r & 127u);
        en_patch.p[P_ARP_SEED_HI] = (uint8_t)((r >> 7) & 127u);
    }
    if ((sections & SEC_PATCH) == SEC_PATCH) {
        hex4(seed, h);
        en_patch.name[0] = 'D';
        en_patch.name[1] = 'I';
        en_patch.name[2] = 'C';
        en_patch.name[3] = 'E';
        en_patch.name[4] = ' ';
        en_patch.name[5] = h[0];
        en_patch.name[6] = h[1];
        en_patch.name[7] = h[2];
        en_patch.name[8] = h[3];
        en_patch.name[9] = 0;
    }
    patch_fix(&en_patch);
    patch_dirty = 1;
}

static void discover_dice(uint32_t sections) /* a new seed: a new patch (or section), the old one in the history */
{
    hist_push();
    dice_counter = dice_counter * 1103515245u + 12345u + plat_ms();
    dice_seed = (uint16_t)(dice_counter >> 8);
    dice_apply(dice_seed, sections);
}

static void discover_seed_step(int32_t e)    /* the seed turned: the neighbouring patch, no history */
{
    dice_seed = (uint16_t)(dice_seed + (uint16_t)(e >= 3 || e <= -3 ? e * 37 : e));
    dice_apply(dice_seed, SEC_PATCH);
}

static void discover_mutate(int32_t e)       /* small steps on the continuous parameters, a third of them */
{
    uint32_t now = plat_ms(), s = dice_counter ^ now, id, amt = (uint32_t)(e < 0 ? -e : e) * 4u;
    if (now - mutate_t > 1000u)              /* a burst of turns is one history step */
        hist_push();
    mutate_t = now;
    dice_counter = s * 1664525u + 1013904223u;
    for (id = 0; id < P_COUNT; id++) {
        int32_t v, d;
        if (!(section_of(id) & SEC_PATCH) || param_is_enum(id) || id >= P_ARP_ON)
            continue;
        if (drnd(&s) % 3u)
            continue;
        d = (int32_t)(drnd(&s) % (2u * amt + 1u)) - (int32_t)amt;
        d += (int32_t)(drnd(&s) % (2u * amt + 1u)) - (int32_t)amt;
        v = clamp((int32_t)en_patch.p[id] + d / 2, 0, 127);
        en_patch.p[id] = (uint8_t)v;
    }
    mutations++;
    patch_dirty = 1;
}

static void discover_set(int which)          /* A (0) or B (1) = the live patch */
{
    if (which)
        morph_b = en_patch, have_b = 1;
    else
        morph_a = en_patch, have_a = 1;
}

static void discover_morph(int32_t e)        /* the position turned: the blend of A and B */
{
    uint32_t id;
    if (!have_a || !have_b)
        return;
    morph_pos = clamp(morph_pos + (e >= 3 || e <= -3 ? e * 2 : e), 0, 127);
    for (id = 0; id < P_COUNT; id++) {
        int32_t a = morph_a.p[id], b = morph_b.p[id];
        if (!(section_of(id) & SEC_PATCH))
            continue;
        en_patch.p[id] = (uint8_t)(param_is_enum(id) ? (morph_pos < 64 ? a : b)
                                                     : a + ((b - a) * morph_pos + (b >= a ? 63 : -63)) / 127);
    }
    en_patch.name[0] = 'M';
    en_patch.name[1] = 'O';
    en_patch.name[2] = 'R';
    en_patch.name[3] = 'P';
    en_patch.name[4] = 'H';
    en_patch.name[5] = 0;
    patch_fix(&en_patch);
    patch_dirty = 1;
}
