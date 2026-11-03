# M-VAVE FM-1 — Research Findings

Compiled 2026-10-30 from the Firmware Hub, the aroum resource hub / reverse-engineering notes, the official `m-vave/fm1_open` SDK headers, the Felucca platform repos, and community firmware docs. Every claim carries a source tag; tags resolve in section 13. Remaining unknowns are listed in section 11; nothing marked verified there should need re-research.

Project goal this feeds: a synth-first firmware (not a groovebox) that prioritises sound quality, a deep arpeggiator, and ways to discover good random patterns and patches.

Updated 2026-10-30 (platform pass): RAM, flash map, clock, sample rate and the `fm1-base` location were resolved from source — see `platform-decision.md`. Rows below marked **[verified]** were corrected in that pass.

---

## 1. The stock product

| | |
|---|---|
| Engine | 6-operator FM, 32 algorithms, Dexed-like. 12 voices on V14/V15. 128 presets (banks A–D for SysEx import). [synthtopia-intro][synthanatomy] |
| Controls | 27 silicone keys (standard piano layout), 7 rotary encoders + dedicated volume, 14 function buttons, color TFT. [synthtopia-intro][felucca-bsp] |
| I/O | 3.5 mm TRS stereo out (engine + FX are mono, mirrored), 3.5 mm TRS MIDI in (adapter not included), USB-C (power, data, USB-MIDI, USB audio, firmware update), Bluetooth MIDI, built-in speaker. [synthanatomy] |
| Power | 2000 mAh, ~12 h claimed. [synthtopia-intro] |
| Price | ~$60–100 depending on channel (AliExpress ~$70, Amazon EU ~$100). [synthanatomy] |
| Stock arp | directions, octave, rate, tempo, gate, swing, latch, sync. [synthanatomy] |
| Stock seq | 16 steps, mono/poly, 16 patterns, rate, gate, swing, transpose, rests. No chords, probability, ratchets or automation. [synthanatomy] |
| Stock FX | filter (LP/HP/BP), chorus, phaser, distortion, delay, reverb (room/hall/plate), 4 params each, saved per patch. Reviewer: "low quality", recommends external FX. [synthanatomy] |
| V16 (2026-10-21) | 8-part multitimbral, 20 shared voices, 64-step seq, 8×8 arrangement matrix, true-stereo global FX, ±100 cent per-voice tuning, DX7 `.syx` bank import/export. Package 718,196 bytes. Only distributed from m-vave.com. [aroum][hub] |
| Reviewer verdict | Closer to a DX7 than the SMK-37 Pro, "no jitter or glitches", solid for the price but "not on point" with a real DX7. Knobs feel cheap. Arp/seq basic. Patch bank navigation unclear. [synthanatomy] |

---

## 2. Hardware

| Item | Fact | Source |
|---|---|---|
| SoC | JieLi **AC7911B** (AC791N / "WL82" family), QFN-48 6×6 mm, marking `JL C156211-11A-B`. Dual-core **pi32v2** RISC with FPU, DC–320 MHz. Wi-Fi 802.11 b/g/n, Bluetooth EDR + BLE. 24 MHz crystal. | [aroum-RE] |
| Clock | **[verified]** felucca-bsp `chip/fm1_clock.h`: the stock boot path runs the core at **360 MHz** (PLL1 = 24 MHz / 12 × 270 = 540 MHz, system clock = PLL1 × 2/3); every community firmware runs there. The SDK's `clk_set` ceiling is 320 MHz. Changing the clock shifts SFC flash timing, SPI/UART dividers, USB 48 MHz and the audio clock. | [felucca-bsp-cpu][felucca-bsp-exp][melodee] |
| FPU | Single precision. Melodee's CZ-1/FM6/PROPHET use it; GATOR runs Plaits float DSP in `.cc` files on it. | [melodee][gator] |
| RAM | **[verified] No external RAM is usable**: fm1-base README ("the chip has no external RAM") and M-VAVE's own linker (`SDRAM_SIZE = 0`). Internal SRAM ≈ **512 KB** (fm1-base `device/app.ld`): RAMTEXT 32 KB @ `0x01C00000`, RAM 96 KB @ `0x01C08000` (`.data`/`.bss`; 128 KB in WiFi builds), POOL **336 KB** @ `0x01C20000` (big buffers, `PLAT_POOL`; ~58 KB of it the screen canvas), NOINIT 15.6 KB. Melodee: reverb ≈19 KB per active slot, Room/Chorus 2 KiB per slot, punch FX request 64 KiB. The RE doc's "8 MB SDRAM" is a chip option the FM-1 does not expose. | [fm1-base][fm1_open-ld][melodee][aroum-RE] |
| Flash | **1 MiB** SiP NOR, Puya P25Q80H (JEDEC `0x856014`), SFC on PD0–PD6, sector **4096 B**. XIP through the cache; SRAM limits only `.bss`/`.data`. RE doc contradicts itself (1 MB vs "4 MB or 8 MB"); felucca-bsp treats stock as **1 MiB** and its expanded-flash work (chip swap, JEDEC size detection) is beta. | [aroum-RE][felucca-bsp-exp][fm1_open] |
| Flash layout | **[verified]** Mask-ROM UBOOT + head at `0x0000–0x3FFF` (never overwrite). fm1-base map (`device/app.ld`, `include/plat.h`): **app slot 581 KB** (XIP `0x02000120`, len `0x8DFBC`); XDATA0 `0x93000–0x97000` (16 KB); **DATA `0x97000–0xE0000` (292 KB)** holding the app's store objects (A/B 4 KiB sector pairs, 3840 B payload each); update staging `0xE0000–0xE5000`; XDATA1 `0xE5000–0xFC000` (92 KB); settings pair at `0xFC000`. ≈400 KB for our data. Installers reject packages larger than the app slot. Stock V16 package ≈718 KB (its own layout). | [fm1-base][aroum-RE] |
| Audio path | **ALNK0 I2S / left-justified 24-bit DMA → external codec + headphone amp.** MCLK PB8/PC0, BCLK PC1, LRCK PC5/PC2, codec power/reset PC6 (active high). Double-buffered DMA; application owns two half-buffers of `int32` (L, R, 24-bit left-justified). Codec part number, noise floor, line vs headphone level: **not documented anywhere found.** | [aroum-RE][felucca-bsp-audio] |
| Sample rate / block | **[verified]** Official SDK: `FM1_SAMPLE_RATE 44100`, `FM1_BLOCK_FRAMES 128` (2.9 ms), signed **Q23** in `int32`. fm1-base: **44.1 kHz family, fixed** (`device/main_fm1.c` `FS 44100`), `APP_BLOCK 256` = 5.8 ms, `app_render(int32_t *lr, n, master_q12)` 24-bit in `int32`. The codec rate is an MCLK divider: 192 MHz/17/256 = **44,117.6 Hz** actual; GATOR's own HAL picks 160 MHz/13/256 = **48,076.9 Hz** (`fw_rate.h`) — 48 k is possible on the hardware but not exposed by fm1-base. Felucca-Engines control tick `CTL = 32`. | [fm1_open][fm1-base][gator-rate][fe-engines] |
| Display | **240×240 colour TFT, ST7789**, hardware SPI1 (MOSI PC10, SCK PC9, D/C PC8, CS PC7 active-low, backlight PA2 active-low). No full framebuffer in the Felucca platform: canvas ≤ 240×124, screens drawn in strips; official SDK DMA strips of `240×16` px. UI loop ≈66 Hz. SPI1 can be shared with a microSD via inverted /CS (community mod). | [aroum-RE][gator][fm1_open] |
| Key matrix | 11 columns × 6 rows, diode-isolated, behind two cascaded 74HC595 (SER PA4, SRCLK PA3, RCLK PA1). Rows PA0, PA5–PA8, PB7 with pull-ups (low = closed). PA0 and PB7 carry only encoders. Scan tick ≈10 kHz (`FM1_LED_TICK_US 100`), one column per tick. Debounce: press 2 frames, release 8 frames (~9 ms). | [felucca-bsp-input][aroum-RE] |
| Keys | 41 key IDs: 0–13 buttons, 14–40 the **27 note keys, F3 (MIDI 53) … G5**. `FM1_KEYMAP[6][11]`. Experimental **velocity/pressure** via analog rows PA7/PA8 ("fm1-velocity", off by default). | [felucca-bsp-input][gator] |
| Buttons (14) | `FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT_DOWN OCT_UP`. Only OCT− / OCT+ have fixed BSP names (0, 1). | [gator][felucca-bsp-input] |
| Encoders (7) | `SELECT ALGO PRESET KNOB1 KNOB2 KNOB3 KNOB4`, quadrature, detent steps (+ = clockwise). `FM1_NENC 7`. Encoder 6 on PB7. | [gator][felucca-bsp-input] |
| MASTER pot | Read through the ADC (10-bit SAR, ch 4 / PB6), delivered as 0–1023. Battery divider on PB1 (ch 3). | [gator][aroum-RE] |
| LEDs | Row lines PA9, PA10, PH6/PH4, PH9 × 11 columns → up to 44 LEDs under keys/buttons. BSP API: per-column lit / dim / breath / mid levels, `fm1_led_key(id,on)`, power-on sweep and idle animations. | [felucca-bsp-input] |
| MIDI | **TRS MIDI IN only**: UART1 RX PH8, 31,250 baud, optocoupler, DMA ring buffer. **No TRS MIDI OUT on the PCB** (RE doc proposes routing UART1 TX to USB D+ when no host is attached, or an IOMAP remap that sacrifices `DACL`). USB-MIDI in/out. BLE MIDI exposed by the official SDK; the BSP's `CTL-1` example does Bluetooth MIDI. Opt-in USB-MIDI **host** in the BSP (direct or one powered hub). | [aroum-RE][felucca-bsp][fm1_open] |
| USB | Full speed, software 1.5 kΩ pull-up. Class-compliant MIDI + audio (UAC) + CDC serial in the Felucca platform; Felucca records the master to the host at 44.1/48 kHz, Melodee offers 4 mono stems; official SDK exposes USB playback read (Q23) and Bluetooth A2DP read (16-bit). USB class can be reconfigured (keyboard/mouse firmwares exist — the Hub flags them). | [felucca-feat][melodee][fm1_open][hub-kb] |
| Debug | **No JTAG/SWD/UART pads, no hardware recovery button.** | [aroum-RE] |
| Battery | 2000 mAh, ~12 h. | [synthtopia-intro] |

---

## 3. CPU and platform gotchas

- **Silicon bug.** A 64-bit multiply adjacent to a 16-bit load can leave a register one bit off (loop index `0x12` read as `0x00800012`). 1.2 billion checked multiplies alone: no errors; 32-bit loads beside 64-bit multiplies: 2 errors in 4 billion; 16-bit loads: crashes within minutes in AAC/SBR code. Fix: keep 64-bit multiplies in a `noinline` helper (`mul_q31`) that uses the product immediately; cost ≈50 % slower on that decoder. Unknown whether 320 MHz avoids it. [felucca-bsp-cpu]
- **No malloc, no libc** in the Felucca platform. Static buffers; large ones in `FW_POOL`. C side has **no float** (build fails if float is linked in C); float belongs in `.cc` files built for the FPU. C++ static constructors do not run — placement-new in `app_init`. [gator]
- **Audio ISR rules:** no drawing, no flash access, no waiting, no `fw_midi_send`. Critical section against the ISR: `fm1_irq_off()/fm1_irq_on()` from the main loop only. [gator]
- **Hot audio code in RAM.** Official SDK places audio code in `.volatile_ram_code` (`FM1_AUDIO_CODE`) and audio lookup tables in `.volatile_ram.fm1`, because "audio code and every callee must be safe during Flash programming" (XIP from flash is unavailable while the flash is being written). [fm1_open]
- **Flash writes stop audio.** `fw_store_save` erases a sector with interrupts off; GATOR: "do not save while playing". Felucca 1.6 fixed a 17 ms LED pause caused by autosave. Storage pattern in GATOR: 2 slots × 3840 B, A/B writes + CRC, power-loss safe. [gator][felucca-rn]
- **Watchdog ≈ 8 s** restarts the device if the frame loop blocks. [gator]
- **Second core (CPU1).** Beta. Job worker: `fm1_cpu1_run` / `fm1_cpu1_wait`, one job at a time, check word per job; a misread, fault or late job retires CPU1 until power-off. Melodee renders voices on it (PROPHET 8 voices dual-core vs 5 single; shared voice budget 16 → 18 units). Some units misread shared RAM. Felucca ships it off by default (MENU > SYSTEM > 2ND CORE). [felucca-bsp-exp][melodee][felucca-rn]
- **Measured DSP costs.** Felucca, one part, 8 voices: ANALOG **17 % CPU**, ADPCM **54 %** (all four parts on ADPCM → dropouts). Melodee: FM6 up to 16 voices/track with FPU; PROPHET voice = 2 budget units on two cores, 3 on one. Felucca delay buffer holds 1.49 s. X0X degrades quality rather than dropping out under load. [felucca-build][melodee][felucca-rn][x0x]
- **OTA re-entry is mandatory.** A custom firmware must keep a way back into update mode (OCT− at power-on in the Felucca loader, or SysEx `0x01`/`0x02`), otherwise the unit is "permanently soft-locked" against MIDI updates. [aroum-RE][hub-kb]
- **Identity.** Packages announce `FM-1_NNN`; use a unique `FM-1_9NN`, never `FM-1_015` (official V15). The Hub shows "on your FM-1" only when exactly one firmware matches. [hub-kb]
- **Shared flash.** Every firmware stores presets/projects in the same flash region in its own format; switching firmware can hide or destroy data. Provide a backup/restore path in the editor. [hub-kb]
- **Hub listing** requires `fmhub.json` (`what`, `author`, `kinds`, optional `play`/`editor`/`cheat`/`image`) and a release built by the shared workflow `zvenson/fm1-build/.github/workflows/build.yml@v1` with matching SHA-256 for the "Built from source" badge. [hub-kb]

---

## 4. Firmware format, update protocol, recovery

- **Package:** `.fwsc` = M-VAVE updater container (app + update loader). Markers `@JMUA`, `JLUFW`, target tag `AC791N` at `+0x424`. Validation = container header, device type, version, min size > 64 KB, per-block byte checksums. **No RSA/ECDSA/AES signature verification.** [aroum-RE]
- **Update protocol (device-pull SysEx over USB-MIDI):** host sends enter-upgrade `F0 22 24 35 7F F7`; device writes an `UPDATA_PARM` record and re-enumerates as USB `4D4A:4155` "ota-FM-1"; device *requests* chunks with `F0 00 32 41 41 [f1:4][addr:4][len:4] … F7` (7-bit groups, little-endian, `len = (length<<4)|flashtype`, 8→7 LSB-first packing, checksum `~(flashtype + Σdata + Σaddr + Σlen) & 0xFF`); completion = device requests `0xF0000000` and host replies `"success\0"`. Handshake `F0 00 32 45 00 00 00 40 7F F7` → identity `FM-1_015` / `FM-1_014`. Felucca's loader marker is `FELUCCA-LOADER-1`; Felucca USB VID:PID `1209:0001`. [aroum-RE][felucca-bsp][gator]
- **Mask-ROM bootloader:** enumerates as `WL82 UBOOT1.00` mass-storage (VID `0x4C4A`, PID `0x8057`). Triggers reported: hold OCT− (and OCT+) at power-on for ~10 s, repeated reset loops, three 10 s boot failures. Cannot be erased by firmware. [aroum-RE][hub-kb]
- **Recovery tools:** `jl-uboot-tool`, `MvaveFM1Unbricker` (full flash backup first, MIT), `M-Flasher` + `m-bootloader` (hold REC → download mode, on-device recovery menu), FM-1 Transporter (RP2040 on D+/D− pulsing boot key `0x16EF` at ~50 kHz), Flipper Zero trigger, Android backup/restore apps, Hub Rescue page (Linux). [aroum][hub-kb]
- **Known soft-brick:** Felucca 0.9-beta → 1.0.x; fixed by erasing `0x97000` (len `0x8000`) and `0xFC000` (len `0x2000`) via `jl-uboot-tool`. SMK-37 shares the SoC and bootloader; its factory calibration lives at `0xC1000` and must never be overwritten. [aroum-RE]
- **Installer behaviour:** Chrome/Edge Web MIDI, ~1 minute, resumable if interrupted (for `FM-1_9NN` identities), rollback to official V15 from the Hub. Older firmwares (SLOOP, early Felucca betas) cannot be flashed over directly; Baud Girl requires V15 first. [hub-kb][aroum]

---

## 5. Toolchain and build

| | |
|---|---|
| Compiler | JieLi Linux toolchain, **clang 4.0.1 for pi32v2**, fetched by `tools/get_toolchain.sh` into `~/.jieli/toolchain`. Windows variant 2.5.3 for the official SDK (`FM1_PI32_TOOLCHAIN`, CPU target `r3`). [felucca-build][fm1_open] |
| SDK | `fw-AC79_AIoT_SDK` from Gitee, tag `AC79NN_SDK_V1.2.1_2023-12-13`; Felucca uses three files: `cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`. JieLi SDK is Apache-2.0. [felucca-build][felucca-bsp] |
| macOS | Build runs each JieLi tool in a `linux/amd64` Debian container (Rosetta on Apple silicon); native on Linux x86-64. Python 3 + Pillow + fonttools; optional Node (web tests) and Emscripten (browser emulator). [felucca-build] |
| Commands | `./build.sh` (`--release 1.0`) → `build/felucca.bin`, `loader/ota.bin`, `build/felucca.fwsc`. Install: web installer or `python3 tools/fm1_install.py build/felucca.fwsc` (`pip install mido python-rtmidi`). [felucca-build][gator] |
| Felucca build flags | `FELUCCA_FLASH`, `_OTA`, `_CDC`, `_CDC_DEFAULT`, `_UAC`, `_UAC_48K`, `_UAC_AS_FIRST`, `_CPU2`, `_UART`, `_SLICE`, `_ICONS` (default 1); `_FM4`, `_ADPCM` (default 0); `_USB_LAYOUT` 0–3. [felucca-build] |
| Host simulation | GATOR: "the same app code on macOS / Linux → WAV and PNG" (`host/`, `tools/sim.py`). Felucca: `tests/run_tests.sh`, browser emulator `web/emu/build.sh`. Felucca-Engines: `tests/run.sh` renders 122 phrases and hashes against `golden.txt`, ASan/UBSan. X0X: `host/x0x_host` runs the full app from scripts. [gator][felucca-build][fe-engines][x0x] |
| Emulators | `simonjohansson/fm1-emulator` (Rust; macOS arm64 + Linux x86-64; runs `.fwsc`/`.ufw`/`.bin`; V15 and Felucca confirmed, several others stop on unsupported instructions), `joakimeriksson/fm1-emu` (browser), `fm1-dev-wiki`, `foma` Docker env with hot reload + one-click WebMIDI flash (`./fm1 dev`), Podman container that builds Dinghy/Felucca/ORBIT headlessly. [hub-kb][aroum] |
| Reverse-engineering | `pi32v2-elf-objdump`, Ghidra module `ghidra-jieli`, `isd_download`/`fw_pack` for UFW containers. [aroum-RE] |
| CI | `zvenson/fm1-build@v1` reusable workflow; tag push builds and attests; `gh attestation verify`. [hub-kb] |

---

## 6. Platform options

| Route | What it is | Licence | Pros | Cons |
|---|---|---|---|---|
| **Felucca platform** — `hugelton/Felucca` app on `felucca-bsp` + `Felucca-Engines` | What SLOOP, Clipper, GATOR, X0X, Melodee, ORBIT fork. App contract (GATOR `docs/DEVELOPING.md`): `app_init()`, `app_frame(const fw_input_t*)` at ~66 Hz, `app_audio(int32_t *lr, uint32_t frames)` in the ISR; `fw_store_load/save`, `fw_midi_read/send`, `lcd_fill`/`draw_text_box`/canvas `cv_*`, `fw_led_*`, `fw_stats.cpu_q8`, `FW_NOTE_INC`, `FW_SINE`. | App **GPL-3.0-only**; bsp **MPL-2.0**; engines **MIT** | Mature OTA loader, web installer, USB MIDI/audio/CDC, web editor, browser emulator, host sim, largest community and docs, Docker build on macOS | Anything derived from the Felucca *app* is GPL-3.0; the app carries groovebox/sequencer/UI baggage if forked wholesale |
| **fm1-base + felucca-bsp** — https://github.com/charlesvestal/fm1-base (AMB-1, CTL-1, Radio; X0X and FoMni moving to it) | Platform layer above the BSP: `plat.h` (68 functions — input, LEDs, MIDI USB/TRS/BLE with routing, USB host, BLE on two stacks, BT audio, WiFi, store objects, flash data windows, SysEx, CPU2 jobs, profiling), canvas drawing + fonts, `fastmath.h`, host simulator with scripted tests, browser emulator, `.rfm1` iOS host, `tools/build.py` from `fm1.toml`, installer/site/backup tools. Template app = 77 KB flash. | **MPL-2.0** (file-level; provenance verified in its LICENSING.md) | Our app any licence; no inherited groovebox code; one maintained base shared by several firmwares; simulator-first workflow needs no JieLi toolchain | Beta (interface may change before 1.0); 44.1 kHz only today; no DSP library beyond `fastmath.h` — engines and FX are ours. **Chosen — see `platform-decision.md`.** |
| **Official `m-vave/fm1_open` SDK** | App source (GPLv3 **with FM1 SDK linking exception**) on top of a **closed binary engine**: `fm1_engine_render(int32_t*, frames)` fills Q23 blocks in the I2S ISR; app owns master gain, USB/BT mixing, recording. Headers: `fm1.h`, `fm1_audio_io.h`, `fm1_block.h`, `fm1_device.h`, `fm1_engine_control.h`, `fm1_sea.h`. Windows-only PowerShell build; WASI web preview. | GPLv3+exception / proprietary engine | Official; BLE MIDI + Bluetooth audio + USB playback exposed; `struct fm1_controls { int16_t encoders[7] … }` | **Engine is a blob** — unusable for "our own sound"; Windows toolchain; smaller community |

Precedent for a non-Felucca-app synth on the Felucca platform: **GATOR** (replaces the instrument, keeps Felucca's HAL/loader/USB/installer, USB descriptors stay `1209:0001 "Felucca"`). Precedent for MPL-clean: **AMB-1**.

---

## 7. Ecosystem map (synth-relevant subset of ~80 firmwares)

| Firmware | Author / base | What matters for us |
|---|---|---|
| **Felucca** 1.7.0 | Leo Kuroshita (Hügelton); platform | 13 engines (ANALOG, FM6 Dexed-based 32 alg, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, PHYS, NOISE, SLICE, DRUM). 4 tracks share **8 voices**. 64-step seq with chords/ties/accent/slide/chance/velocity/ratchets 2–4/conditions/FILL/p-locks/automation (128 records)/NUDGE. SEQ TOOLS: CLEAR REVERSE SHIFT RANDOM COOK EUCLID DOUBLE HALF. **Arp modes:** UP DN UPDN RND ORD REPEAT DNUP UP+8 CONV DIVG PINKY THUMB WALK CHORD. 16 scales, one-finger chords with voicings. LFO (sync, S&H, slew), 4-slot mod matrix per track with VEL/TIME humanize. Per-track dist + INSERT + SLICER; sends chorus/delay (dotted)/reverb (HALL ≤4.5 s, ROOM, SPRING); master limiter. FX hold-layer: repeat, reverse, filter sweeps, tape stop, freeze, harmonizer/shimmer, flanger, phaser, REV/ECHO THROW. 32 user presets, 4 projects. MIDI: fixed CCs + 16 MIDI LEARN, per-track channels, PC, clock in (int/USB/TRS). USB audio out. [felucca-feat][felucca-eng] |
| **Clipper** 1.3 | isod89; from SLOOP ← Felucca | **Closest "synth, not groovebox" precedent.** 23 engines (HYBRID 3-osc/2-filter/16-slot matrix/4 macros; FMX 6-op opsix-style; FM6 DX7 .syx; DIGITAL 4-op; WAVE 16 tables + 4 user; WSEQ; VECTOR; JUNO; ANALOG 8 filter models; ACID; STRINGS; MACRO; PHASE; VPS; TERRAIN; PHYS; TRIO; LOFI; WHEEL; VOICE; NOISE; GRAIN; SAMPLE). 12-slot mod matrix on every engine (LFO1–3, ENV1–2, vel, key, MW, AT, random, MPE slide, MOD SEQ lanes, macros). 4 macros BRITE/TIMBR/MOTN/SPACE on CC16–19. SPREAD, VINTAGE drift, 4-lane MOD SEQ. Per-sound INSERT. Master: dist, stereo chorus, delay (MONO/PING-PONG/TAPE), 4 reverbs (ROOM/PLATE/HALL/SHIMMER), DJ filter, 3-band EQ, bus comp, look-ahead limiter. Tuning systems + Scala. **8 voices (12/16 build option)**; POLY/MONO/LEGATO/UNISON/UNI2/UNI4, layer/split/MULTI. **Arp: 15 modes incl. TURNG (Turing machine), SPICE/DICE variations, swing, latch follows TRN/ROOT/SCALE/QNT, KBD HOLD.** Randomizer by section (EDIT/ENV/MOD/FX/OPS) with RAND/BACK; **MORPH SET A/SET B**; UNDO/COMPARE (16 steps); 16 favourites; presets filtered by engine. MPE. Known gaps: CPU at 12/16 voices, crackles, **no factory sound-design pass**, no manual. [clipper] |
| **GATOR** 0.6 | npi3pak; Felucca platform (cut from ORBIT) | Mutable **Plaits** (24 models, 3 banks, MIT) ported unchanged at **48 kHz, mono**, using the FPU. LPG (DECAY/COLOUR), ADSR 1 ms–10 s, 2 LFOs × 6 shapes, 8-slot matrix, GATE/TRIG/DRONE, HOLD, glide, legato, 16 flash presets. Proves Mutable float DSP runs here. Documents the platform API. [gator] |
| **Melodee** 1.4 | keremimo; Felucca 1.0 fork | PROPHET (8 voices dual-core / 5 single), FM6 (16 voices), CZ-1, SID, DRUM, MIDI Track. 3 insert slots/track (Phaser, Width, Formant, Tape, Spit, Room, Reverb, Chorus, Drive, Crush, Flanger, pedals), shared 3-slot rack, 27 SLOOP master punch FX. Note tail / note echo. 4-stem USB capture, USB host. Runs at bootloader's 360 MHz. [melodee] |
| **AMB-1** 1.1 | Charles Vestal; **fm1-base + felucca-bsp (MPL)** | Eno-style generative: bells on tape loops of unequal length, breathing pad voices, gliding bass, Euclidean percussion (Bjorklund/Toussaint). Tape echo per *Discreet Music*, long tape loop, **Dattorro plate (JAES 1997)**. "Tend, don't perform": each key adds/removes a scale note; 8 pieces; hold any button 1 s for a help card. Reports screen state to a USB screen reader. [amb1] |
| **X0X** beta | Charles Vestal; Felucca fork | 909/808, two Open303 (Devilfish ranges) each with a **TB-3PO seed-locked acid line generator**, breakbeat generator (8 slices, rearranged live). Parts have independent length/rate. Shared reverb + tape delay, master compressor with kick-keyed pumping. **Degrades quality instead of dropping out** under load. Engines tested against originals; full app runs on host. [x0x] |
| **SLOOP** | isod89; Felucca-based | 4-track groovebox (3 synth + drums), the base of Clipper and many forks; its arp/Turing/SPICE/DICE originate here. [synthtopia-alt][clipper] |
| **Stock V14–V16** | M-VAVE; closed engine | See section 1. V16's 20 voices / 8 parts / stereo FX is the official ceiling to beat on sound. [aroum][hub] |
| Others | Baud Girl FM-1+VA (closed; VA + 8-bit + FM, performance QoL, 64-step seq), Lunar Modulator (5 engines, 160 models, p-locks), FM-106 (Juno), DUELL, ETCH, ORBIT (dspaudio), FoMni (Omnichord), fm1-chord, Polly, MYCELIUM/Jangada (generative grooves), sloopDX (Dexed `msfa` core, Apache-2.0), zp12 (12-bit sampler), Floyd FM, winampFM. Also Doom/NES/Game Boy ports and stock-V15 patchers. | [hub][aroum][synthtopia-alt] |

---

## 8. MIDI conventions observed

- **Stock:** "Midi Chn" (default All) receives notes, PB, PC, CC1/2/4/64. "Effect Chn" (default 2) receives CC0–23 as fixed FX controls (filter CC0–3, reverb CC4–7, delay CC8–11, dist CC12–15, chorus CC16–19, phaser CC20–23; values clamped, e.g. cutoff 0–107). PC offset by one. Clock 24 ppqn + Start/Stop (SPP/MTC discarded). DX7 single and 32-voice bank SysEx accepted any time. Transmits notes only on ch 1 over USB/BLE; no CC/PC/PB/clock out; TRS is input only, no thru. [midi-guide]
- **Felucca:** CC5 glide, CC7 level, CC10 pan, CC71 resonance, CC72/73/75 release/attack/decay, CC74 brightness, CC91/93/94 reverb/chorus/delay; MIDI LEARN ×16; per-track channels or channel groups; PC loads sounds; PB, sustain, panic; clock from internal/USB/TRS. [felucca-feat]
- **Clipper:** CC1, 11, 64, AT, CC16–19 macros, CC20 morph, CC74, CC76–78, CC120–123, RPN0 bend range, identity request, clock out, PC with bank select; EWI mode (breath CC2). [clipper]
- **Official SDK limits:** SysEx max 1024 bytes; MIDI messages ≤128 B, control task only. [fm1_open]

---

## 9. Musician feedback

- Praised: price, size, battery + speaker, build quality, 12-voice FM, "a synth like a Volca FM, but with a different flavour". [synthanatomy][elektronauts]
- Criticised: mono output, weak FX, basic arp/seq, no chords, unclear patch bank navigation, no CC control of synth parameters, cheap-feeling encoders, "small and fiddly with few controls". [synthanatomy][elektronauts]
- Wished: chords, probability, ratchets, parameter automation, full CC map, better patch browsing, Elektron-style UI, program change. [synthanatomy][synthtopia-intro][elektronauts]
- Usage reality: most expect to pair it with an external sequencer or DAW; it is a sound source, not a track-finishing tool. USB audio capture (Felucca/Melodee) is highly valued. [synthtopia-intro][elektronauts][felucca-feat]
- Scene temperature: Synthtopia reader calls Groove OS ($29) "AI slop vibe coded garbage"; the community values open, reproducible builds and a working "return to V15". [synthtopia-alt][hub-kb]

---

## 10. Design-relevant numbers at a glance

Summary of sections 2–3; sources are tagged there.

| Budget | Value |
|---|---|
| Audio block (Felucca platform) | 256 frames @ 48 kHz = 5.33 ms (GATOR quotes 5.8 ms) |
| Audio block (official SDK) | 128 frames @ 44.1 kHz = 2.9 ms |
| CPU cost reference | 8 fixed-point VA voices ≈ 17 % of one core |
| RAM for app | ~336 KB static pool (Felucca platform); SDRAM unknown |
| Flash for app + data | ~1 MiB minus 16 KB boot minus VM area; stock app ≈718 KB |
| Flash sector | 4096 B; sector erase stops audio |
| Preset store (GATOR) | 2 × 3840 B, A/B + CRC |
| Delay memory reference | Felucca 1.49 s; Melodee reverb ≈19 KB |
| UI loop | ~66 Hz; canvas ≤ 240×124; screen 240×240 |
| Key scan | ~10 kHz column tick; press 2 frames, release 8 |
| Encoders / pots | 7 relative detent encoders; 1 absolute pot (MASTER) |
| Keys | 27, F3–G5 (MIDI 53–79) |
| LEDs | up to 44 |
| Watchdog | ~8 s |

---

## 11. Open questions (verify before architecture lock)

1. ~~**SDRAM**~~ **Resolved:** none usable; 512 KB internal SRAM, 336 KB pool (`platform-decision.md`).
2. **Codec**: part number, measured noise floor, THD, output level (line vs headphone), speaker amp path. Needs a unit.
3. ~~**Block/rate default**~~ **Resolved:** fm1-base 256 frames @ 44.1 k family (44,117.6 Hz actual); 48 k exists only in GATOR's HAL.
4. ~~**`fm1-base`**~~ **Resolved:** https://github.com/charlesvestal/fm1-base, read at `d8ff8f4`.
5. **Velocity** via analog rows (`lib/touch.h`): usable dynamic range and consistency across keys. Needs a unit.
6. **CPU1** stability on our unit and battery cost; whether the 320 MHz SDK clock avoids the multiply bug. fm1-base's `cpu2_fallback` makes it optional.
7. ~~**BLE MIDI**~~ **Resolved:** fm1-base provides it (`ble = true`, JieLi or own stack; costs in `platform-decision.md`).
8. **RAM code placement**: fm1-base has a 32 KB RAMTEXT region and the `APP_QUIET` hook for flash-busy periods; whether the render itself should sit in RAMTEXT is checked when the first engine exists.
9. ~~**Preset/data partition size**~~ **Resolved:** 581 KB app slot, ≈400 KB data windows.
10. Firmware Hub **wish list** (sign-in required) and r/MVaveFM1 — not read; worth a manual pass for user demand.

---

## 12. Not pursued (recorded so nobody re-researches)

- Groove OS and Baud Girl are closed; no technical detail beyond marketing.
- Elektronauts page 1 predates deliveries; little hands-on feedback.
- `old.reddit.com` is blocked for fetch; r/MVaveFM1 unread.
- Firmware Hub wishes page requires sign-in.
- Felucca wiki Controls/Menu pages partly failed to load; Controls summary captured (tap/hold table), Menu not.

---

## 13. Sources

| Tag | URL |
|---|---|
| [hub] | https://thefirmwarehub.com/ |
| [hub-kb] | https://thefirmwarehub.com/kb/ |
| [aroum] | https://github.com/aroum/fm1-custom-fw (README) |
| [aroum-RE] | https://raw.githubusercontent.com/aroum/fm1-custom-fw/main/REVERSE_ENGINEERING.md |
| [fm1_open] | https://github.com/m-vave/fm1_open — README, `sdk/include/fm1.h`, `fm1_block.h`, `fm1_audio_io.h`, `fm1_engine_control.h`, `app/` |
| [felucca] | https://github.com/hugelton/Felucca (README, `firmware/src`) |
| [felucca-build] | https://raw.githubusercontent.com/hugelton/Felucca/main/BUILDING.md |
| [felucca-feat] | https://github.com/hugelton/Felucca/wiki/Features |
| [felucca-eng] | https://github.com/hugelton/Felucca/wiki/Engines |
| [felucca-ctl] | https://github.com/hugelton/Felucca/wiki/Controls |
| [felucca-rn] | https://github.com/hugelton/Felucca/wiki/Release-Notes |
| [felucca-bsp] | https://github.com/hugelton/felucca-bsp (README) |
| [felucca-bsp-cpu] | https://raw.githubusercontent.com/hugelton/felucca-bsp/main/docs/CPU.md |
| [felucca-bsp-exp] | https://raw.githubusercontent.com/hugelton/felucca-bsp/main/docs/EXPERIMENTAL.md |
| [felucca-bsp-audio] | https://raw.githubusercontent.com/hugelton/felucca-bsp/main/boards/fm1/fm1_audio.h |
| [felucca-bsp-input] | https://raw.githubusercontent.com/hugelton/felucca-bsp/main/boards/fm1/fm1_input.h |
| [fe-engines] | https://github.com/hugelton/Felucca-Engines , `src/felucca_engine.h` |
| [gator] | https://github.com/npi3pak/fm1-gator , `docs/DEVELOPING.md` |
| [clipper] | https://github.com/isod89/clipper-fm1 |
| [melodee] | https://github.com/keremimo/melodee/wiki |
| [amb1] | https://github.com/charlesvestal/fm1-amb |
| [x0x] | https://github.com/charlesvestal/fm1-x0x/wiki |
| [mycelium] | https://github.com/megagott/mycelium-fm1 |
| [midi-guide] | https://m-vave-fm1-midi-guide.up.railway.app |
| [synthanatomy] | https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html |
| [synthtopia-intro] | https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/ |
| [synthtopia-sdk] | https://synthtopia.com/content/2026/10/20/m-vave-officially-opens-fm-1-firmware-to-developers-with-new-workbench-sdk |
| [synthtopia-alt] | https://www.synthtopia.com/?p=155069 |
| [elektronauts] | https://www.elektronauts.com/t/m-vave-fm-1/252170 |
| [tb3po] | https://github.com/Logarhythm1/O_C-HemisphereSuite/wiki/TB-3PO |
| [fm1-base] | https://github.com/charlesvestal/fm1-base — README, DESIGN.md, LICENSING.md, `include/plat.h`, `include/fm1b_config.h`, `device/app.ld`, `device/main_fm1.c`, `template/` (commit `d8ff8f4`, 2026-10-29) |
| [fm1_open-ld] | m-vave/fm1_open `sdk/config/linker.ld.in` (`SDRAM_SIZE = 0`) |
| [felucca-bsp-clock] | felucca-bsp `chip/fm1_clock.h` |
| [gator-rate] | fm1-gator `firmware/sys/fw_rate.h`, `docs/MANUAL.md`, `BUILDING.md` |
| Tools | https://github.com/Quixotic7/MvaveFM1Unbricker · https://github.com/kagaimiq/jl-uboot-tool · https://github.com/kurogedelic/FM-1-transporter · https://github.com/zvenson/fm1-build · https://github.com/simonjohansson/fm1-emulator · https://joakimeriksson.github.io/fm1-emu/ · https://open.m-vave.com/fm1-workbench · https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK |
