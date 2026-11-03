/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* Wavetables, built at boot into the arena: three tables of 16 frames, each frame 128 samples, in five
 * band-limited copies (mip levels) with 64, 32, 16, 8 and 4 harmonics at most. */
#pragma once
#include <stdint.h>

#define WT_TABLES 3u
#define WT_FRAMES 16u
#define WT_LEN 128u
#define WT_MIPS 5u
#define WT_BYTES (WT_TABLES * WT_MIPS * WT_FRAMES * WT_LEN * 2u)   /* 61440 */

void wt_build(int16_t *dst);              /* all tables into dst (WT_BYTES) */
static inline const int16_t *wt_frame(const int16_t *base, uint32_t table, uint32_t mip, uint32_t frame)
{
    return base + ((table * WT_MIPS + mip) * WT_FRAMES + frame) * WT_LEN;
}
