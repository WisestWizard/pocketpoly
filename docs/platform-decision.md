# Platform Decision

Date 2026-10-30. Resolves the platform questions left open in `fm1-research-findings.md` §11 and `persona-evaluation.md` §6. Everything below was read from source in shallow clones of `charlesvestal/fm1-base` (via `fm1-amb/base`, commit `d8ff8f4`, 2026-10-29 — identical to upstream HEAD), `hugelton/felucca-bsp` (`305c372`, 2026-10-26), `npi3pak/fm1-gator` and `m-vave/fm1_open`; file paths are given so the facts can be re-checked.

## Decision

Build the firmware as an **app on fm1-base** (MPL-2.0) over **felucca-bsp** (MPL-2.0). Our own code lives in its own files under our own licence. No Felucca *application* code (GPL-3.0) is copied in.

MIDI is a first-class requirement (Kyle, 2026-10-30): USB-MIDI in/out, TRS MIDI in, and **Bluetooth MIDI in both roles** — peripheral to a computer, phone or tablet, and central to a Bluetooth MIDI keyboard or controller — stay in every build. fm1-base provides all of it (`ble = true`, `ble_central = true`, JieLi's stack); its cost in flash and RAM is budgeted below, never traded away for voices or FX.

Why fm1-base and not the alternatives:

| Route | Verdict |
|---|---|
| Fork the Felucca app | Inherits a groovebox UI, sequencer and storage format we do not want, and GPL-3.0 for everything. The parts we would keep (HAL, loader, USB, installer) are exactly what fm1-base already packages under MPL. |
| GATOR-style private copy of the platform layer | Works (GATOR, Clipper, X0X did it) but every firmware then carries its own copy of USB, OTA and panel code and misses fixes. fm1-base exists precisely to end that. |
| Official `fm1_open` SDK | Closed binary engine; Windows-only build. Not viable for our own DSP. |
| **fm1-base** | One maintained platform shared by CTL-1, Radio, AMB-1 (X0X and FoMni are moving to it). Host simulator and browser emulator run the same app code; scripted tests; packaging, installer, site and backup tools included. Beta, but its author is the most active platform developer on the FM-1 and the interface is already the union of what 13 firmwares use. |

## Verified facts

### Memory — no external RAM

- fm1-base README: "the chip has no external RAM". M-VAVE's own linker: `sdk/config/linker.ld.in` sets `SDRAM_SIZE = 0`. The RE doc's "8 MB SDRAM" is a package option the FM-1 does not expose. **Closed: design for internal SRAM only.**
- Internal SRAM map (`fm1-base/device/app.ld`), total ≈ 512 KB:

| Region | Origin | Size | Use |
|---|---|---|---|
| RAMTEXT | `0x01C00000` | 32 KB | code that must run from RAM |
| RAM | `0x01C08000` | 96 KB (128 KB in WiFi builds) | `.data` / `.bss` |
| POOL | `0x01C20000` | **336 KB** (304 KB WiFi) | big buffers, `PLAT_POOL` section (`.pool`) |
| NOINIT | `0x01C7C000` | 15.6 KB | survives reset (crash record etc.) |

- Measured costs (fm1-base README, Oct 2026): template without radio 27 KB RAM + 68 KB pool; Bluetooth MIDI on JieLi's stack +45 KB RAM +24 KB pool; on fm1-base's own BLE stack +10 KB RAM +1 KB pool. The screen canvas is inside the pool (240×124×2 B ≈ 58 KB by default; WiFi builds shrink it to 240×48).
- Consequence for FX: a 1 s stereo delay in `int32` at 44.1 kHz is 353 KB — impossible. A 1.5 s **mono `int16`** delay line is 132 KB; a Dattorro plate is ~20–30 KB. With Bluetooth MIDI built in (see the scaffold numbers below), the pool left for us is ~250 KB: voices ≤ 40 KB, delay ≤ 100 KB, reverb ≤ 30 KB, chorus ≤ 8 KB, headroom ~70 KB.

### Flash — 581 KB app slot, ≈400 KB data

From `fm1-base/device/app.ld` and `include/plat.h`:

| Window | Range | Size | Purpose |
|---|---|---|---|
| App (XIP) | `0x02000120` + `0x8DFBC` | **581 KB** | the firmware image; the build rejects a larger one |
| XDATA0 | `0x93000–0x97000` | 16 KB | app big data |
| DATA | `0x97000–0xE0000` | **292 KB** | app big data + store objects (A/B 4 KiB sector pairs, 3840 B payload each, CRC, torn-write safe) |
| update staging | `0xE0000–0xE5000` | 20 KB | loader's |
| XDATA1 | `0xE5000–0xFC000` | 92 KB | app big data (keep bytes `0xF00–0xF07` of each sector erased — loader record rule) |
| settings pair | `0xFC000` | 8 KB | Felucca-compatible settings slot |

Reference sizes: template 77 KB flash; template + JieLi BLE 214 KB; CTL-1 (General MIDI synth + BLE) 491 KB; LOOM-8 506 KB. **An 8–12-voice three-engine synth with FX fits; sample banks do not.** A flash erase/write stops audio (`plat_data_*` and `plat_store_save` are main-loop only, "the sound too") — save on explicit action, never during playback.

### Clock

`felucca-bsp/chip/fm1_clock.h`: the stock boot path runs the core at **360 MHz** (PLL1 = 24 MHz / 12 × 270 = 540 MHz; system clock = PLL1 × 2/3). Every community firmware runs there; the SDK's `clk_set` ceiling of 320 MHz is irrelevant to us.

### Sample rate and block

- fm1-base is **44.1 kHz family, fixed**: `device/main_fm1.c` `#define FS 44100u`; `APP_BLOCK 256u` → 5.8 ms per render.
- The codec rate is an ALNK0 MCLK divider (GATOR `firmware/sys/fw_rate.h`): **192 MHz / 17 / 256 = 44,117.6 Hz** actual (felucca-bsp `usb.c` resamples USB audio by exactly this ratio), or **160 MHz / 13 / 256 = 48,076.9 Hz** for GATOR's 48 k build. 44,117.6 Hz is 0.7 cents sharp of nominal — inaudible, but compute tuning from the real rate as GATOR does.
- **Decision: 44.1 k family for v0.x.** 48 k would mean adding GATOR's rate option to fm1-base (an MPL contribution, later if ever).
- Audio format: `app_render(int32_t *lr, uint32_t n, uint32_t master_q12)` — interleaved L/R, 24-bit in `int32` (template scales full-scale to ≈ 8.3 M = 2²³), MASTER gain in Q12 applied by the app (`APP_GAIN` default is the square of the pot position).
- Latency: 5.8 ms block + ≤ 1 ms key scan — fine.

### The app contract (`fm1-base/include/plat.h`, `template/app.h`)

Required: `app_init`, `app_render`, `app_panic` (silence before flash rewrite), `app_ui_init`, `app_frame` (~60 Hz), `app_input_only`, `app_say` (screen + screen reader).

Optional hooks we will use:

| Hook | Use for us |
|---|---|
| `APP_TICK` — `app_tick()` at 1 kHz in the timer IRQ | arpeggiator / clock engine with 1 ms resolution, independent of the 5.8 ms audio block |
| `APP_OVERLOAD` — render took > 90 % | load governor: shed voices, then oversampling, then reverb quality |
| `APP_QUIET` — render while flash must not be read | keep arp time, output silence |
| `APP_KEY_SENSE` + `lib/touch.h` | opt-in velocity/pressure |
| `APP_SYSEX`, `APP_USB_CMD`, `plat_host_read` | DX7 `.syx` import, editor protocol, backups |
| `APP_BACKUP_*` | whole-store backup/restore over USB (`tools/fm1_backup.py`, `web/backup`) |
| `plat_cpu2_run/wait/sync` (`cpu2 = true`, `cpu2_fallback = true`) | second-core voice rendering as a job; automatic single-core fallback |

Platform services already there: buttons/keys/encoders/MASTER/battery; LEDs with lit/mid/dim/glow levels; MIDI in from USB, TRS and BLE with per-port routing, out-queue room queries and counters; USB-MIDI host (opt-in); BLE MIDI on JieLi's or fm1-base's own stack (central mode too); Bluetooth A2DP out; WiFi (mutually exclusive with BLE); store objects; three flash data windows; SysEx send; `plat_cpu_pct`, `plat_xruns`, `plat_ticks24` profiling; `plat_lock/unlock/yield`.

Drawing (`device/gfx.c`, `lcd.c`): `cv_begin/cv_rect/cv_line/cv_pset/cv_text/cv_blit`, `text_w`, `lcd_fill/lcd_blit`, fonts `FONT_XS … FONT_B/M/L`, `RGB()`, palettes (`APP_PALETTE`). Strip-drawn, no framebuffer.

DSP helpers: `include/fastmath.h` — float `fm_exp2f/log2f/powf/tanhf/sinf/cosf/sqrtf/rsqrtf/db2lin` for the single-precision FPU. The build **fails if `double` is linked**, if registers are touched outside the BSP, or if image/RAM/pool exceed their limits.

### Tooling

- `tools/build.py APP_DIR` driven by `APP_DIR/fm1.toml` (name, `id_prefix`, USB names, `include`, `o2` list for -O2 units, radio options, `usb_audio_in/out`). Produces `NAME.bin`, `loader/ota.bin`, `NAME.fwsc`; `--release X.Y` sets identity `FM-1_<prefix>XXYYZZ`.
- `--host`: the simulator, built with the system `cc` — **no JieLi toolchain needed**. Scripted tests (`template/tests/basic.txt`): `key/tap/press/turn/master/midi` input, `expect` on app-defined probes, `wav`/`wavstop` audio capture, `shot` screenshots, `reboot`, `listen/said` for the screen reader. The DSP can be iterated and regression-tested entirely on the Mac.
- `--web`: browser emulator (Emscripten). `--rfm1`: the app as an AUv3/instrument inside RemoteFM1 for iOS.
- `tools/fm1_install.py` (Web-MIDI-free install over `mido`), `tools/fm1_remote.py` (drive a real unit, read its screen, pull crash logs), `tools/make_site.py` (installer + manual + emulator site), `tools/fm1_backup.py`.
- Package identity: Hub convention `FM-1_9NN`-style unique number; fm1-base examples use `FM-1_11` (template) and `FM-1_7` (AMB-1). Pick an unused prefix against the Hub catalogue before the first release.

### Reference app: AMB-1 (`charlesvestal/fm1-amb`)

3.4 k lines total: `app/dsp/amb.c` 1784 (engine, built at -O2 via `o2 = ["dsp/amb.c"]`), `app/ui.c` 1423, `app/project.c` 160, `app/app.c` 32, plus `host.c` (simulator probes), `words.tsv` (screen reader), `web/`. `ble = false` (AMB-1 never uses the radio; ours keeps Bluetooth MIDI), `usb_audio_in = true`. This is the shape our repo takes.

## Licence posture (recommendation — Kyle's call)

- fm1-base and felucca-bsp are MPL-2.0 file by file: changes to *their* files are published; *our* files may be anything.
- Recommendation: **MPL-2.0 for our app too.** It is Hub-friendly, lets others build on our engines the way we build on fm1-base, and keeps the option of linking JieLi's binary BLE library (impossible under GPL). Choosing GPL-3.0 instead would only buy the right to copy Felucca app code — and we are deliberately rewriting arp, chords and storage.
- Third-party DSP we may pull in and its terms: Felucca-Engines (MIT), Plaits/stmlib (MIT), Dexed `msfa` (Apache-2.0), Simper SVF (public design), Dattorro plate (published algorithm). All MPL-compatible.

## Environment on this Mac (set up 2026-10-30)

| Need | Status |
|---|---|
| Apple silicon, Apple clang 21 | present — builds the host simulator |
| Python for `tools/build.py` | **Homebrew `python@3.14` + `pillow` 13** (`/opt/homebrew/bin/python3.14`; no unversioned `python3` symlink). The font generator calls Pillow with `features=` and needs **libraqm**: Xcode's Python 3.9 with pip's Pillow wheel fails (`KeyError: 'setting text direction … without libraqm'`). `build.sh` defaults `PYTHON` to the 3.14 binary. |
| `mido`, `python-rtmidi` | installed for Xcode's Python 3.9 (`python3 -m pip install --user`), for `base/tools/fm1_install.py` |
| Node 24 | present (web tests); Emscripten not installed (browser emulator and `.rfm1` builds need it) |
| Container runtime | **colima 0.x + docker CLI** via Homebrew, not Docker Desktop: `colima start --vm-type vz --vz-rosetta --cpu 4 --memory 4 --disk 20`. `docker run --platform linux/amd64 debian:bookworm-slim uname -m` → `x86_64`. Must be running (`colima start`) before a device build. |
| JieLi toolchain | `~/.jieli/toolchain` → `jieli-linux-toolchains-20260730.1` (135 MB) from `base/tools/get_toolchain.sh` |
| AC79 SDK | `~/fw-AC79_AIoT_SDK` at tag `AC79NN_SDK_V1.2.1_2023-12-13` (1.0 GB shallow clone; the build reads `cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`) |
| An FM-1 on USB-C | not verified; nothing has been flashed |

`./build.sh --host` and the scripted tests run with no container. `./build.sh` (device) needs colima running; `JIELI_TOOLCHAIN` and `AC79_SDK` default to the paths above.

First builds of the scaffold (2026-10-30). Host simulator built and `app/tests/basic.txt` passed in full: keys, MIDI in/out, settings across a reboot, audio peak, screen reader, velocity/pressure, port counters, and the Bluetooth script (off/on with REC, device scan, connect, disconnect, routing to the computer's and the device's links, MIDI thru source). Device build passed every check (no inlined 64-bit multiply beside a load, no soft-double, register access in felucca-bsp only; the radio's own RAM code is the one allowed `.ram_text` exception). Package `app/build/pocketpoly.fwsc` 611,496 B, identity `FM-1_1300`. Not flashed.

| Scaffold build | Image (of 581 KB) | RAM `.data`+`.bss` (of 96 KB) | Pool (of 336 KB) |
|---|---|---|---|
| without a radio (`--no-ble`) | 81,064 B | 19,364 B | 69,384 B |
| **with Bluetooth MIDI, both roles (the build we ship)** | **218,744 B** | **67,956 B** | **93,960 B** |

So the app proper has about **360 KB of flash, 30 KB of static RAM and 250 KB of pool** to grow into. Voice state, delay lines and reverb memory go in the pool (`PLAT_POOL`), not in `.bss`. If that ever binds, fm1-base's own Bluetooth stack (`ble_stack = "own"`) returns roughly 80 KB of flash and 35 KB of RAM at the price of one Bluetooth link at a time — a trade to put to Kyle, not to make quietly.

## Proposed repository shape

```
fm1-<name>/
  base/          fm1-base as a git submodule (felucca-bsp inside it)
  app/
    fm1.toml     name, id_prefix, USB names, include/o2 lists, usb_audio_in = true, ble = true, ble_central = true
    app.h app.c  the contract, store objects, APP_BLOCK, APP_TICK, APP_OVERLOAD
    engine/      voice allocator, VA, FM6, macro osc (float .c at -O2)
    fx/          chorus, delay, plate, comp/limiter
    arp/         clock, modes, Turing, Euclid, generators
    discover/    dice / mutate / seed / morph / breed / favourites
    ui/          pages, cards, LEDs
    host.c       simulator probes and commands
    words.tsv docs/MANUAL.md site/
  tests/         scenarios/*.txt (simulator), golden-hash DSP tests
  build.sh       → base/tools/build.py app
```

## Engine v0.1 (2026-10-30)

`app/engine/engine.c` (its own -O2 unit, float on the FPU, no platform calls) and the app around it are in:

- 8 voices: 2 oscillators (polyBLEP saw and pulse, triangle integrated from the BLEP pulse, sine), square sub, noise, drive (`tanh`) into a Simper trapezoidal SVF (LP/BP/HP), amp and filter envelopes (one-pole, attack overshoot, exponential decay/release, 1 ms–10 s), one LFO (tri/sine/square/saw/S&H, 0.05–30 Hz → pitch ±2 st, cutoff ±4 oct, PW), per-voice fixed detune + random-walk drift, voice-indexed constant-power pan under SPREAD, glide, velocity → amp and filter, key tracking, pitch bend ±2 st.
- Control tick 32 samples for envelopes, pitch and filter coefficients; amplitude ramps across the tick; cutoff/resonance knob values glide per block. Voice stealing: same key → retrigger; free; oldest released; oldest. Sum → `tanh` soft clip → 24-bit.
- Note events: MIDI handled in the render (no queue); the keys go through a 64-entry ring from the main loop. `APP_OVERLOAD` lowers the voice cap (min 3) and the footer shows it.
- 34 parameters in `patch_t` (46 B), 8 factory patches, 16 user slots in one store object (744 B) at `0xDA000`; settings at `0xFC000`.
- UI: nine pages of four knobs (HOME, OSC A/B, VOICE, FILTER, AMP/FLT ENV, LFO, MOTION), SELECT pages, ALGO filter type, PRESET browse, save dialog, Bluetooth on REC as before. 81 simulator checks pass (`app/tests/basic.txt`), WAVs render without clipping or DC.
- Design notes for later: pan is per voice, so a lone note sits where its voice does (alternative: pan by key position); single-voice level at full MASTER is about −15 dBFS (headroom for 8 voices before the soft clip); the real codec rate (44,117.6 Hz) is used for tuning, the host renders at 44,100.
- Device build with the engine: **243,620 B image, 71,668 B RAM of 98,304, 93,960 B pool of 344,064**, every platform check passing; the engine and its UI cost 25 KB of flash and 3.7 KB of RAM over the scaffold. The engine's mix buffers (2 KB) and voices live in `.bss`; move big FX buffers to `PLAT_POOL`.
- **Device CPU, measured 2026-10-30** on Kyle's FM-1 (flashed with `base/tools/fm1_install.py`, chords sent over USB-MIDI, `plat_cpu_pct` read back through the screen-reader channel with `tools/cpu_probe.py`):

| Held | Patch | CPU |
|---|---|---|
| nothing | — | 1 % |
| 1 voice | INIT SAW | 7 % |
| 8 voices | INIT SAW (one saw, no drive) | 43 % |
| 8 voices | STRINGS (two saws) | 43 % |
| 8 voices | WARM PAD (two saws, sub, drive) | 53 % |
| 8 voices | BRIGHT LEAD (two saws, drive 60) | 54 % |
| 8 voices | FAT BASS (saw + pulse, sub, drive 50) | 57 % |
| 8 voices | E KEYS (sine + triangle, drive 30) | 60 % |

  So a voice costs 5–7 % of the core; the full 8 leave ~40 % for the FX chain, modulation and UI. The overload governor (`APP_OVERLOAD`, > 90 %) never engaged. Cheapest cuts if v0.2's FX need room: a table sine instead of the polynomial, the triangle's second BLEP, and `tanh` drive only on voices whose patch asks for it (already) — or the second core for the voice loop.
- First flash notes: the install from AMB-1 (`FM-1_930`) took about a minute and came back as `FM-1_1300`; macOS keeps showing the old USB name ("AMB-1 FM-1") for the same VID:PID until the cable is re-plugged — the identity query is the truth. AMB-1's saved project lives at `0xFC000`, where our settings object also lives: our first SAVE replaces it (nothing is written until then).
- `tools/cpu_probe.py` repeats the measurement: `python3 tools/cpu_probe.py` with the FM-1 on USB.

## Engine v0.2 (2026-10-30) — the digital-hybrid pass

What changed, all in `app/engine/` (three -O2 units now: `engine.c`, `wt.c`, `fx.c`) and the app around it:

- **Oscillator models** per oscillator: SAW, PULSE, TRI, SINE, **PD** (Casio-style phase distortion, bend scaled down above 2.5 kHz), **WT A/B/C** (16-frame, 128-sample wavetables in five band-limited mip levels — 64/32/16/8/4 harmonics — chosen per voice from the pitch; linear interpolation within and between frames). The tables are **synthesised at boot** by inverse DFT from a 128-point sine table into the pool: A sweeps sine→triangle→saw→square→12 % pulse, B moves two vocal formants A-E-I-O-U, C is sparse partials plus a climbing cluster; every frame normalised to the same peak. 61,440 B, no flash.
- **Between the oscillators**: hard sync (B restarts with A), ring (A×B), **XMOD** (B's output modulates A's phase: two-operator FM with any carrier model; index = amount² × 0.5 turns).
- **Modulation**: LFO 2; a loopable **MOD ENV** (attack/decay, LOOP); an **8-slot matrix** (sources LFO1/2, MOD ENV, velocity, key, pressure, wheel, random-per-note, MACRO 1–4; destinations pitch, cutoff, reso, shape A/B, pitch B, XMOD, RING, level, pan, LFO1 rate, delay send, reverb send, ensemble mix, drive). Per-voice destinations are evaluated per voice per tick; global destinations once per block from global sources. **Four macros** are patch values on the HOME knobs with a name each (14 names); INIT routes them BRITE→cutoff, TIMBRE→shape A, MOTION→LFO1 rate, SPACE→reverb+delay sends. Pressure from the keys (poly AT) and channel pressure feed MS_PRESS; CC 1 feeds MS_WHEEL.
- **FX chain** (`fx.c`, in place on the float bus after the `tanh` voice-bus clip): Juno-style ensemble (0.513 / 0.863 Hz triangles, antiphase L/R, modes CHOR I / CHOR II / ENSEMBLE); tape delay (one 0.836 s `int16` line, 20 ms–830 ms with pitch-bending time slew, 0.35 Hz wow + 6.3 Hz flutter, one-pole dark feedback with `tanh` and a DC blocker, mono in/out); Dattorro plate (lengths ×1.482 for our rate, `int16` lines, 67.6 KB, output normalised against decay); a peak limiter (instant attack, 80 ms release, −0.45 dBFS).
- **Memory**: the app hands the engine a **220 KB arena** from the pool (`PLAT_POOL` in `app.c`): wavetables 61,440 + ensemble 4,096 + delay 73,728 + reverb ~67,600 B. Device build: **257,656 B image, 74,644 B RAM of 98,304, 319,240 B pool of 344,064** (24.8 KB spare), all platform checks passing.
- **Patch format v2**: 82 parameters, 94 B per patch, bank magic `BNK2` (16 user slots, 1,512 B object). Factory patches are built at boot as change-lists over INIT (`project.c`): INIT, GLASS KEYS, PD BASS, WIRE PLUCK, EVOLVE PAD, FM BELL, SYNC LEAD, VOX.
- **UI**: 15 pages (HOME = macros; EDIT: OSC A/B, MIX, VOICE; FX: FILTER, FX 1, FX 2; ENV: AMP/FLT/MOD; LFO: 1/2; SEL: MOD slot editor, MACROS names).
- **Found and fixed by measurement**: (1) the plate's output taps ran ~+22 dB over unity at full send, pinning every chord to the limiter — now scaled to about the dry level at full send and normalised so REV SIZE barely changes loudness; (2) hard sync left +10 % DC on an 8-note SYNC LEAD chord — a per-voice DC blocker (15 Hz) after the filter removes it. 103 simulator checks pass; renders show no clipping, no DC, stereo correlation 0.3–0.7 on pads.
- **Measured on the FM-1, first flash of v0.2**: idle 5 %, INIT 1 voice 13 %, 8 voices 57 %, GLASS KEYS 82 %, PD BASS over 90 % — and the governor was wrong: `en_overload` fired every block, so the cap fell 8 → 3 within a second and stayed there. Causes: the delay ran whenever feedback > 0 (idle cost), `tanh` per sample for the drive and the voice bus, the polynomial sine per sample for PD/SINE, oscillator B rendered even at level 0, and ~+30 % per-voice overhead over v0.1 (matrix, DC blocker, shape smoothing, per-tick pan).
- **Fixed the same day**: governor sheds one voice per ~0.5 s and gives one back after ~7 calm seconds; delay and reverb run only while their send is open or their tail rings (7 / 8 s); cubic soft clip replaces `tanh` for drive, bus and delay feedback; a 1024-point sine table replaces the polynomial per sample; oscillator B, sub and noise are skipped when silent; and **the second core renders voices 1–4** (`cpu2 = true`, `cpu2_fallback = true`; `plat_cpu2_run/wait/sync`; LFO values precomputed per tick into the shared block; separate mix buffers summed after `wait`; on a dropped job CPU0 renders that half itself).
- **Measured again (2026-10-30, second core on, no crash or give-up in the log)**:

| Held | Patch | CPU |
|---|---|---|
| nothing | — | 2 % |
| 1 voice | INIT | 7 % |
| 8 voices | INIT | 21 % |
| 8 voices | GLASS KEYS / WIRE PLUCK / FM BELL / SYNC LEAD | 48–49 % |
| 8 voices | VOX | 51 % |
| 8 voices | PD BASS / EVOLVE PAD | 53 % |
| nothing, FX tails still ringing | VOX just released | 22 % |

  The FX chain alone is about 20 % of CPU0 (the plate most of it); the voices now cost ~2.5 % each on CPU0's side. Headroom for v0.3 (arp, discovery) is ample; if FX grow, they are the next thing to move to the second core. Build with the second core: **258,548 B image, 84,308 B RAM of 98,304 (14 KB left — watch this), 319,240 B pool of 344,064**.

## Engine v0.3 (2026-10-30) — arpeggiator and discovery

- **Arpeggiator** (`engine.c`, runs in the render at control-tick resolution, so MIDI clock sync has no third thread): held-note list with latch; 12 modes (UP, DOWN, UP DN, ORDER, RANDOM, WALK, CONV, DIVERGE, PINKY, THUMB, CHORD, TURING); 1–4 octaves; divisions 1/4 … 1/32 with triplets; swing (odd steps late, up to a triplet); gate 5–200 % (ties above 100 %); Euclidean mask by Bresenham (HITS over STEPS, ROTATE); per-step CHANCE; RATCHET (×2/×3); Turing shift register (LENGTH 2–16, LOCK probability). Its randomness comes from a 14-bit SEED in the patch; REPEAT re-seeds every cycle so a random pattern loops exactly. Internal tempo in settings (40–240); MIDI clock (24 ppqn) is followed when present, with start/continue/stop, and dropped 2 s after it stops. Every arp note goes out over USB and Bluetooth MIDI; the keys' own MIDI out is suppressed while the arp plays them.
- **Discovery** (`discover.c`, main loop): `dice_param()` holds a sane range or weighted choice per parameter; DICE derives the whole patch (or one section: OSC, FILTER+FX, ENV, LFO, MOD, ARP) deterministically from a 16-bit seed and names it `DICE xxxx`; turning SEED browses neighbouring seeds; MUTATE nudges a third of the continuous parameters by the turn's size (a burst of turns is one history step); MORPH blends stored A and B (enums switch at the midpoint); a 16-deep history goes back and forward. Matrix slots 1–5 (the macro routings) and the macros themselves are left alone by dice so a diced patch still has working HOME knobs.
- **UI**: ARP 1–3 + TURING pages (ARP button), DISCOVER (SEQ held), PLAY toggles the arp (lit), SEQ taps dice (with a page button held, that section), OCT−/OCT+ on DISCOVER set A/B. The footer shows ARP / ARP> in place of the filter type while the arp is on. 17 new patch parameters (99 total, 111 B per patch, bank magic `BNK3`), settings magic `PPL3` (adds BPM).
- **Verified in the simulator**: 153 checks — arp timing (8 steps/s at 120 bpm 1/16), MIDI out of arp notes, held/latch bookkeeping, mode and stepped-value editing (an exact round trip between display, 0..127 and the engine's truncation — the first version skipped steps), Euclid 3-of-8 firing three notes a cycle, MIDI clock follow and release, dice/history/mutate/morph behaviour, section dice leaving the filter alone.
- **Device build**: **267,776 B image, 87,108 B RAM of 98,304 (11 KB left), pool unchanged (319,240 B)**. RAM is the constraint now: the next additions should move the history ring (1.8 KB) and the arp tables to the pool, or trim `.bss`.
- **Flashed and measured 2026-10-30 (late)**: CPU identical to v0.2 — idle 2 %, INIT 1 voice 7 %, 8 voices 21 % (INIT) to 53 % (PD BASS, EVOLVE PAD), FX tails 22 %. The arp ran on the device: WIRE PLUCK with PLAY on and a three-note chord held over USB sent **16 note-ons and 16 note-offs in 2 s** (8 steps/s at 120 bpm 1/16, ratchet repeats visible), no crash, second core fine. One thing the capture showed: the factory value 42 for OCTAVES reads as one octave in the engine (43 is the first two-octave value) — fixed in `project.c` for WIRE PLUCK and FM BELL; the build on the unit still has the one-octave versions until the next flash.

## Incident 2026-10-30: the constant tone (fixed)

Kyle heard a constant tone on every patch after the v0.3 flash: 0 voices reported, deaf to all-notes-off, started the instant PRESET landed on GLASS KEYS, outlived patch changes, died on INIT once the FX tails ran out. It reproduced on a single-core build (so not the second core) and not in the simulator's identical sequence.

Found by instrumenting: `speak()` now reports per-stage peaks (BUS, OUT, ENS, DLY, REV in dB) so `fm1_remote.py screen` can read them. On the unit, after a released GLASS KEYS chord with 0 voices: REV −21 dB at 1.5 s, **−15 dB at 5.5 s** — the plate's output *grew* with no input, and the limiter turned the runaway into a tone. Root cause in `fx.c`: the allpass returned `w[n-L] − g·x` instead of the Schroeder form `w[n-L] − g·w[n]` with `w[n] = x + g·w[n-L]`; that is not an allpass (it peaks at 1/(1−g), 2.6× in the tank at g = 0.7), so the tank's loop gain exceeded unity at some frequencies. The host had the same bug — my renders never captured a tail long enough after a release to see it (before the fix a released chord's tail went −35.8 → −24.8 dBFS; after, −87 → silence). A tail-decay test is now in `app/tests/basic.txt` (156 checks). Also hardened on the way: the pool arena is word-aligned (`uint32_t`, `aligned(8)`) instead of a `uint8_t` array cast to `int16_t*`/`float*`.

Lessons recorded: every feedback structure gets a decay test after release, not just a held-note peak; and the stage meters stay in the screen-reader text — they cost nothing and turned a guess into a measurement in one read.

## Engine v0.4 (2026-10-30/31) — RAM, six-operator FM, DX7 import

- **RAM first**: the mix buffers, sine table, block settings, patch bank, factory patches and the discovery history moved to the pool (`PLAT_POOL`); static RAM fell from 87.1 to 74.0 KB before the FM work. The pool is now the full one (332.5 of 344 KB, 11.5 KB over the 8 KB spare the build insists on): nothing more goes there.
- **FM6** (`app/engine/fm.c`, its own -O2 unit) is oscillator model 9: DX7-shaped — 32 algorithms as a table of modulator masks, carrier masks and feedback operator (two exotic two-operator feedback loops drawn as self-feedback), ratio or fixed-frequency operators with fine and detune, four-rate four-level envelopes in the DX7's 0..99 units (level → 0.75 dB a step; rate 99 ≈ 4 ms across the range, 0 ≈ 40 s), velocity and rate scaling, a one-sample-averaged feedback path. A voice is 104 B; the FM bank is 32 voices in one store object (`OBJ_FMBANK` at `0xD8000`, 3,336 B) plus a LIVE voice the dice writes. Eight factory voices (E.PIANO, FM BASS, BRASS, BELL, GLASS PAD, MARIMBA, ORGAN, FM LEAD). The FM page shapes a voice without operator editing: VOICE, FEEDBACK and RATE offsets, BRIGHT (SHAPE A) on the modulators. Oscillator B, sub and noise still mix; XMOD/sync do not apply to an FM6 A. FM BELL is a true FM6 patch now.
- **FM-aware dice**: dicing the oscillator section of an FM6 patch also dices a LIVE voice (`fm_dice`: carrier ratios from a harmonic set, modulator ratios including 1.41, 3.5, 7.01 and 14, five envelope templates with jitter, feedback, velocity on the modulators).
- **DX7 SysEx import**: `APP_SYSEX` → `app_sysex()`. fm1-base passes frames of at most **640 bytes** (`OTA_FRAME_MAX`), so a 32-voice bulk dump (4,104 B) cannot arrive; single-voice dumps (163 B) do, into the patch's FM slot, and `tools/fm1_dx7.py` unpacks a bank `.syx` into 32 single-voice dumps, each preceded by a slot message (`F0 7D 50 50 01 slot F7`), saving once at the end. The importer drops the DX7 LFO, pitch envelope and key-level scaling.
- **Checks**: 165 simulator checks (model FM6, the FM page as EDIT's fifth page, voice select up to LIVE, FM BELL render, FM dice → LIVE); renders have level and harmonic partials, no DC. Patch format BNK4 (102 parameters, 114 B); the mod-slot parameter indices moved to 78+.
- **Flashed and measured 2026-10-31, first pass**: VA patches unchanged (idle 3 %, 8 voices 21–54 %) but **FM BELL at 8 voices read 102 %** and the governor cut to 5 voices; three FM voices alone read 79 %. The first FM voice cost ~17–20 % of a core: a `floor` and an interpolated sine per operator per sample, a bit-mask scan for modulators, and three or four `exp2` plus a division per operator per tick.
- **Optimised the same hour**: per-note precomputation (operator ratios, modulator index lists, velocity-scaled levels, active-operator mask that drops level-0 operators and their links), a 100-entry level→amplitude table and a 100-entry rate→units table instead of per-tick `exp2`, one `exp2` per voice per tick for the base pitch, a 2048-entry `int16` sine without interpolation for FM, truncation instead of `floor` for the phase wrap. **Measured again: FM BELL 8 voices 79 %** (1 voice 38 % incl. the patch's FX, 4 voices 63 %, 8 voices 79 % with the second core taking four); every other patch unchanged. An FM voice now costs ~8 % of a core, a VA voice 5–7 %.
- **DX7 import verified on the unit**: `tools/fm1_dx7.py /tmp/testvoice.syx --slot 32` → the FM-1 said "DX7 VOICE 32 TESTVOICE", the bank saved, and the voice played when selected on the FM page.
- Device build after the optimisation: **275,184 B image, 87,576 B RAM of 98,304 (10.7 KB left), 332,520 B pool (11.5 KB left)**. Both memories are now the constraint for anything further.

## v0.4.1 (2026-10-31) — the FX layer

Kyle's request while evaluating: hold **FX** and the keys show and switch the effects. Implemented as live bypass bits in the engine (`en_fx_bypass`, `EN_FX_ENS/DLY/REV/DRIVE`; `fx_params_t.bypass` skips a stage and leaves its lines frozen) with the four lowest white keys as switches, LEDs showing what is heard (`en_fx_active`: on in the patch and not bypassed), a body overlay listing the states while FX is held, and FX's tap kept for the page cycle (a key press while held marks the gesture as used). Not saved with the patch. 175 simulator checks; device build 275,984 B image, 87,592 B RAM, pool unchanged. Flashed and verified on the unit with the meters: a GLASS KEYS chord read ENS/DLY/REV −22 dB at 43 % CPU; FX held + A3 + G3 → DLY and REV −99 and CPU 22 % (the stages are skipped, not just muted); pressed again → back to −21 and 43 %; the switching keys started no notes; a tap of FX still opened FILTER.

### Hardware lesson: buttons jolt the analog key rows

The first FX layer read the keys through the velocity/pressure path (`touch_keys()`, analog rows PA7/PA8 — the 20 lowest keys). On the unit, pressing the FX button itself produced phantom key edges on those rows, so the switches flipped before Kyle touched a key (every effect showed OFF) and flipped again on release. Over USB the panel is driven digitally, so the simulator and my remote tests never saw it. Fix (verified by Kyle): the layer reads the plain digital matrix (`plat_keys()`), ignores key edges for 80 ms after FX goes down and for 120 ms after it comes up, and the screen-reader text carries the bypass bits (`FXB`). Rule from now on: anything that uses keys as switches while a button is held reads `plat_keys()`, never the sensed rows; and every hold-layer gets a real finger test, since the remote cannot reproduce the jolt.

## v0.5 (2026-10-30/31) — background work while the FX layer was tested

Simulator-only; not flashed yet.

- **RAM:** the FM bank was held twice (project.c's store copy and a 33-voice array handed to the engine). One
  struct now holds the bank and the LIVE voice contiguously and the engine reads it in place: RAM .data+.bss
  87,592 → 84,216 B of 98,304 (device build). Pool unchanged at 332,520 of 344,064.
- **Render-all and golden hashes:** `tools/render_all.py` renders every factory patch and every factory FM voice
  (a C-major chord, 1.8 s held, 1.7 s tail) to WAV from the host simulator, 16 files in `app/build/renders/`, for
  auditioning on the Mac; without `--golden` it compares the SHA-256 of each render with `tests/golden.txt` and
  fails on any change. The simulator is deterministic, so a hash moves only when the sound does: an intended
  change rewrites the goldens with `--golden`, an unintended one shows up in the next run. The MIDI work below
  passed it unchanged.
- **MIDI completeness:** CC 64 sustain (voices wait for the pedal; with the arp on it latches the chord, and the
  arp's own step note-offs never wait), CC 7 volume, CC 121 reset controllers, a CC map (16–19 macros, 74/71
  cutoff/resonance, 73/72 amp attack/release, 91/93/94 reverb/ensemble/delay sends — each edits the patch and
  lights SAVE), a receive-channel filter (OMNI or 1–16), a send channel for the keys and the arp, MIDI clock +
  start/stop out (24 ppqn from the arp's tempo while it runs on the internal clock), and LOCAL off. The settings
  live on a MIDI page (SEL after MOD and MACROS), in the settings record (magic PPL3 → PPL4: old records load
  defaults) and are kept with SAVE. Tests: basic.txt 175 → 226 checks.
- Image 276,192 → 277,588 B.

## Sound pass 1 (2026-10-31) — Kyle's notes on the FM voices

Notes, by FM voice: E.PIANO pans hard left/right per key press ("janky", many patches do it) and is too percussive
low down; FM BASS too quiet and over before a bass note should be ("drum like"); BRASS slow attack, reedy, quiet;
BELL fine; GLASS PAD too quiet, slow attack, chords don't sit; FM LEAD quiet and dull.

What the renders showed (tools/render_all.py + a level script): FM voices peaked 6–12 dB under the VA patches;
FM BASS was at −68 dB half a second into a held chord (every operator's sustain level was 0, so it decayed to
nothing under the key); BRASS and GLASS PAD took 0.15 s and 0.9 s to arrive; the host patch FM BELL had AMP SUS
90/127 and LEVEL 100 on top of the voices' own envelopes; SPREAD placed voices at fixed alternating positions
(−1, +1, −0.5, +0.5 …) × SPREAD, so each new note jumped to a different side.

Changes:
- **Pan:** Kyle: a full sound by default, panning a choice. SPREAD is bipolar and OFF (64) in INIT and every
  factory patch: the stereo image comes from the ensemble/chorus, delay and reverb, so a chord sits full and
  centred. Turned up (KEYS n), the keyboard spreads across the stereo field (middle C centre, ±30 semitones to
  the edges); turned down (VOICES n), voices take alternating places, the old behaviour. v0.4 banks (BNK4) load
  with SPREAD off and save as BNK5; the dice keep SPREAD within half either way. (A first version panned by key
  position by default; Kyle rejected that.)
- **FM level:** carrier sum gain 0.8/√n → 2/(1+√n) (1, .83, .73 … .58 for 1–6 carriers); FM BELL AMP SUS 127,
  LEVEL 127 (an open gate: the DX7 envelopes shape the note). FM voices now peak −17 to −26 dB, in the VA
  patches' range (−15 to −25).
- **Voices:** FM BASS carriers sustain at 88–90 with the modulators decaying into a round body (−37 dB held,
  steady); BRASS attack rates 72 (carriers, ~50 ms) and 62–66 (modulators bloom just after), the plain sine
  carrier under the stacks at 88, sustain 92; GLASS PAD rebuilt as three harmonic pairs (1:1, 1:3, 2:1, algorithm
  5) with a 0.25 s attack, sustain 88–92; FM LEAD a biting 3-op stack over a detuned pair plus an octave,
  feedback 7, sustain 92; E.PIANO tine 58 → 52 and the ratio-1 modulators −8, a slightly longer body.
- BELL, MARIMBA, ORGAN unchanged. Goldens rewritten.

## FX at rest (2026-10-31)

On the device, a patch with a reverb or delay send open cost ~20 % CPU with nothing playing: each stage ran whenever
its send was open, with a 7–8 s run-out only after the send closed. Now a stage rests when nothing passes through
it: the ensemble after its 11.6 ms line has been silent, the delay after its full line length (0.85 s) of silent
input and silent reads/writes, the plate after 80 blocks (0.46 s, past the tank's 62-block loop) of silent input,
diffuser and output taps. The int16 lines reach exact zero as a tail dies, so a resting stage holds only zeros and
restarts with no click and no ghost. While resting, the chorus LFOs, the tape wow/flutter, the plate's modulation
and the delay-time slew still step sample by sample, so the output is identical to running on silence: renders with
every stage forced on matched the resting renders sample for sample, all 16. (The goldens changed once because
those phases now also run while a stage is off or bypassed; a switched-to patch meets the LFOs at a different
phase. Side effect: the delay time is already settled when a send opens — no pitch glide.) `expect fxrun` in the
simulator: GLASS KEYS idle 0, a note 7, 9 s after it 0. Measured on the device after flashing: INIT, GLASS KEYS
and FM BELL all idle at 4 % (GLASS KEYS was 25 %, INIT 23 % while a tail timer ran).

## Incident 2026-10-31: "none of the arp works", then no answer over USB (not reproduced)

After the FX-at-rest flash and the idle-CPU readings, Kyle reported the arp not working. By then the FM-1 no
longer answered the screen-reader or log SysEx, though it still enumerated on USB. After a power cycle: "no crash
recorded", "last watchdog reset in ?", and the arp works — driven from the key matrix over USB (PLAY, one key then
three: two voices cycling at −17 dB; keys up: silence; the four ARP pages open), and by Kyle at the keys. The
simulator passes an arp stress script (every ARP page knob, clock out, latch, MIDI clock in and out, the channel
filter). Not reproduced; cause unknown. If it recurs: note whether the footer's CPU figure and the voice blocks
still move (a live UI with dead MIDI points at the USB/MIDI path; a frozen screen at the audio interrupt), and read
`fm1_remote.py log` before power-cycling if it answers. Lesson for me: don't drive the device over USB while Kyle
is playing it — the remote's injected keys and PLAY taps collide with his.

## Review pass (2026-10-31) — full debug and test

Three independent code reviews (engine + FM; FX + wavetables; app/UI/storage/discovery), each confirming its
findings by reading the call paths and, where it could, by running the simulator; 37 findings, 11 confirmed by
runs. Fixed in this pass, each with a simulator check in `basic.txt` where one could be written (272 → 307
checks):

- Engine: LFO1's phase could leave [0, 1) when the matrix pushed its rate past a cycle a tick, and the sine table
  read past its end with a growing index (a bus fault on the device) — the phase wraps by `floor`, the rate is
  clamped. Keys gated with the arp off hung when the arp turned on (their note-offs went to the arp) — the UI now
  ends and restarts held keys around PLAY and the engine releases gated voices on the arp's on-edge. When MIDI
  clock stopped, the fallback to the internal tempo recomputed the schedule from the external step count — silence
  for seconds or a burst of steps; it now continues from "now". Panic (CC 120) sent no note-offs for the arp's
  notes. Ties (GATE ≥ 100 %) re-sent and re-attacked notes that continued. Ratchet repeats were spaced by the gate,
  at velocity 100, and could spill into the next step; they now land at even parts of the step at the step's
  velocity and a new step cancels them. The step's velocity lookup never matched (an identity expression). The
  pedal lifting with nothing latched stopped without MIDI Stop. CC 121 reset the volume (RP-015 says not to) and
  left poly pressure. The note event queue got a compiler barrier between payload and index.
- FX: the delay and the plate rested while their lines still rang if the send was turned to 0 (a cut, then a
  ghost when it reopened) — a ringing line now keeps its stage awake. The plate's 16-bit lines clipped inside the
  allpass nodes at pad-like settings (11 k clipped writes in 2.3 s measured): the input is scaled by 1/4 and the
  output by 4. The ensemble's line was not emptied when the stage was off, so switching it on replayed 11.6 ms of
  old signal. The delay-time clamp left no room for wow and flutter at the longest time. The delay's filter
  states are flushed out of denormals once a block. `fxrun` reports the ensemble only when it is on.
- Wavetables: the top frame of ANALOG and VOX snapped back to the 0.75 keyframe (12 % pulse and 'U' were never
  reached). Each mip was normalised to its own peak, so a note crossing a mip jumped up to +11.5 dB (GLASS); one
  gain per frame now.
- App: a stack overflow in the DX7 import's message text (`char t[8]` for "32 " + a ten-letter name). The first
  undo after a single DICE was impossible and bouncing off the end of the history duplicated entries (`hist_live`
  tracks whether the present is in the history). A DX7 import saved settings and the patch bank and cleared the
  patch's dirty flag. Dice values: ARP OCT 42 meant one octave (43 is two), STEPS 59 meant 7 (60 is 8).
  Oscillator B could be set or diced to FM6, which it rendered as a saw. Changing IN CH stranded held notes. SEMI
  was 24/63 of a semitone per step (an "octave" 20 cents flat); it is whole semitones now, shown rounded. A
  one-frame hole in the FX-release note suppression. A patch saved while playing the LIVE FM voice lost its sound
  at reboot; LIVE is now copied into the slot's own bank voice (9 + u). `project_save` wrote the patch bank even for
  a settings save. OCT± while a key was held sent the note-off for the wrong note; keys now remember the note and
  channel they started.
- Deferred to the roadmap: wavetable aliasing above ~5 kHz on the lowest mip (needs two more mips or a sine
  fallback), the arp's float step schedule after hours of running, denormal costs in a few smoothers (FPU behaviour
  unknown), `fastmath.h:58`'s negative shift (fm1-base).
- Verification: sanitizer build clean, clang analyzer clean, 307/307, the reviewers' repro scripts now pass (one
  asserted the bug and fails for the right reason), goldens rewritten for the intended sound changes (plate
  headroom, SEMI, wavetable frames and mips).

## Still open (hardware or later)

- Codec part number and measured noise floor / headroom — needs a unit and an interface.
- Velocity/pressure quality via `lib/touch.h` — needs a unit.
- CPU2 behaviour on this specific unit — the platform's fallback makes it an optimisation, not a dependency.
- Whether our render should live in RAMTEXT (32 KB) — `APP_QUIET` already covers the flash-busy case; check `device/unity.c` placement when the first engine exists.
- Hub identity prefix.
