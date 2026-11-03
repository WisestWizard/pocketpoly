# FM-1 Custom Firmware — Persona Evaluation

Companion to `fm1-research-findings.md` (facts and source tags live there). This document is opinion: what three different experts would conclude from those facts, what instruments the hardware can carry, and a recommended direction.

Brief we are evaluating against: **a synth, not a groovebox** — great sound, great arpeggiator, strong ways to find good random patterns and patches.

---

## 1. Synth designer

### 1.1 What the budget actually buys

- One pi32v2 core at ~360 MHz with a single-precision FPU, a second core that works on most units but must be treated as a bonus. 8 fixed-point VA voices cost ~17 % of a core in Felucca; Melodee gets 16 FM6 voices per track on the FPU. Stock V16 manages 20 voices of 6-op FM plus stereo FX with a closed engine.
- Realistic target: **8 deep voices (3-osc-class VA or 6-op FM, with per-voice filter and 2 envelopes) plus a full stereo FX chain on one core**, 12 voices with the second core enabled. 16 voices only for lean engines. Deliberately choosing 8 excellent voices over 16 thin ones is the right call for a 27-key instrument.
- RAM is the tighter constraint: ~336 KB pool, with reverb at ~19 KB and a 1.5 s mono delay precedent. A stereo plate + tape delay + chorus is affordable; multi-second stereo delays are not — there is no external RAM (verified, `platform-decision.md`), so delay lines are `int16`, mono-in/stereo-out and ≤ ~1.5 s, and wavetables stay small.
- Flash (verified): a 581 KB app slot and ≈400 KB of data windows. Presets can be plentiful if small (Felucca's are tens of bytes). Sample content is still out; small wavetables (a few KB each) are in.

### 1.2 Where sound quality is won on this hardware

Ranked by payoff per effort:

1. **Stereo.** Stock is mono mirrored; the codec is stereo. Per-voice pan/spread, alternating-voice spread in unison, stereo chorus/ensemble, true stereo reverb. This is the single largest audible difference from stock and most forks.
2. **Anti-aliasing where it matters.** polyBLEP oscillators exist in Felucca-Engines. Nonlinear stages (wavefolder, drive, FM feedback, hard sync) want 2× oversampling with a cheap half-band; at 48 kHz this is where cheap digital synths sound cheap.
3. **Envelopes and voice management.** Exponential multi-segment envelopes (not linear ADSR), loopable env, retrigger legato rules, release-stage-priority voice stealing with a short fade (Felucca's stage-4 "fading out" is the hook), sensible unison detune curves, glide that is pitch-rate not time-constant.
4. **Filters.** Simper trapezoidal SVF is already there (fixed-point). Add one ladder model (Moog-style with saturation) and one diode/MS-20-style model; Clipper's eight filter models are more than necessary — three good ones with drive tuned per model are enough.
5. **Per-voice imperfection.** Slow random pitch/cutoff drift, per-voice detune error, envelope time jitter (the "VINTAGE" idea). Cheap and transformative on FM and VA alike.
6. **Effects chain worth keeping on.** Juno-style chorus/ensemble (BBD-flavoured, 2 modes), tape delay with wow/flutter/saturation and tempo sync, Dattorro plate (AMB-1 precedent) or small FDN hall, soft clipper + bus compressor + look-ahead limiter at the end so the speaker never splats. Reviewers' number one complaint about stock is FX quality.
7. **Gain staging and tuning.** Fixed internal headroom, consistent preset loudness (a factory normalisation pass), master tune, per-voice ±100 cents (V16 parity), optional micro-tuning tables.

### 1.3 Engine strategy

Few deep engines sharing one modulation/FX/arp/discovery layer, with a real factory sound-design pass. Clipper shows what 23 engines without that pass feels like.

| # | Engine | Why | Source material |
|---|---|---|---|
| 1 | **VA hybrid** — 2 osc (saw/pulse/tri/sine, PWM, sync) + sub + noise, osc 2 can be a ring/FM pair, 3 filter models, 2 env, drive | The bread-and-butter polysynth voice; cheapest in CPU (17 % for 8) | Felucca-Engines ANALOG/TRIO (MIT, fixed-point) |
| 2 | **6-op FM** with DX7 `.syx` import, 32 algorithms, feedback on any op, op waveforms beyond sine, per-op velocity/key scaling | It is the FM-1; users have DX7 banks; V16 parity is table stakes | Dexed `msfa` core (Apache-2.0; used by sloopDX), Felucca FM6 (GPL) as UI reference |
| 3 | **Macro oscillator** — a curated subset of Plaits models (VA, waveshaper, 2-op FM, formant, additive, wavetable, chord, string/modal, grain, noise) through an LPG/VCF | Enormous timbral range from four parameters; perfect for the four knobs and for randomisation | Plaits (MIT) as ported by GATOR; float, FPU |
| 4 (optional) | **Pluck/physical** — Karplus-Strong + modal resonator | Fills the "keys and mallets" hole FM only partly covers; cheap | Felucca PHYS as reference (GPL), Plaits string/modal models (MIT) |

Everything else (granular, sampler, drums, organ, CZ) is explicitly out. If an engine from 1–3 covers a sound convincingly, there is no fourth.

### 1.4 Modulation architecture (shared by all engines)

- 2 LFOs: free or synced rates, shapes incl. S&H and smoothed random, one-shot/retrig, fade-in, optional chaos source (Lorenz/logistic) for organic motion.
- 2 envelopes beyond amp: one loopable (becomes a complex LFO), both with curve control.
- 8-slot mod matrix with sources LFO1/2, ENV1/2, velocity, key track, aftertouch (MIDI), mod wheel, random-per-note, macro 1–4; destinations include FX parameters.
- **Four macros = KNOB 1–4 on the home page, stored per patch**, each with up to 4 destinations and polarity. This is how four cheap encoders become a performance surface.
- Everything macro-reachable is CC-reachable (CC16–19 as Clipper does) and MIDI-learnable.

### 1.5 Patch discovery — the part most firmwares do badly

Principles: randomisation must be **musical by construction**, **reversible**, **repeatable**, and **nudgeable**.

| Feature | Behaviour |
|---|---|
| **DICE** | Full random within per-engine, per-parameter *sane ranges* (not 0–127). Categories (BASS/PAD/LEAD/KEYS/PLUCK/FX) bias the ranges and the mod routing. |
| **Section dice** | Randomise only OSC, FILTER, ENV, MOD, or FX — hold the page button and press DICE. |
| **MUTATE** | Small Gaussian steps around the current patch; knob sets the amount. Repeated presses walk the space. |
| **SEED** | Every DICE result derives from a visible 16-bit seed. Lock, scroll (SELECT), type (keys), recall. Same seed + same category = same patch — shareable as four hex digits. |
| **MORPH** | Two slots (A/B) and a knob; stores interpolated result. Morph across engines is interpolation of shared layer only. |
| **BREED** | Pick two favourites → child takes each section from one parent with a mutation rate. |
| **HISTORY / COMPARE** | 16-step undo of edits and dice results; A/B compare with the saved version. |
| **FAVOURITE** | One press stars the current state into a ring of 16 without the save dialog. |
| **FM-aware dice** | For engine 2: ratios drawn from harmonic sets, algorithms grouped by brightness, feedback limited, envelope shapes from a library — random FM that sounds like a patch rather than noise. |
| **Smart ranges from data** | Factory presets define per-parameter distributions per category; DICE samples from those rather than uniform. |

### 1.6 What to avoid

- Shallow "many engines" marketing (Clipper, Felucca) over depth.
- Mono summing anywhere in the voice path.
- Linear envelopes, 1-pole filters with no drive, uniform-random patch generators.
- A sequencer. The arp and the discovery tools *are* the composition surface.

---

## 2. Electronic instrument hardware designer

### 2.1 Controls dictate the UI grammar

- **7 relative encoders, no absolute pots except MASTER.** No knob pickup problems — good. Fast sweeps are tedious — bad. Use acceleration, a fine mode (hold SELECT), and put sweep-type gestures on LFOs, macros, or hold-layers rather than expecting a hand to spin a cheap encoder.
- Dedicated roles for the three named encoders: **SELECT** = navigate/value, **ALGO** = engine/algorithm/model, **PRESET** = patch browse. Reviewers found stock bank navigation opaque; PRESET must browse instantly with audition.
- **KNOB 1–4** = the four parameters of the current page; on HOME they are the four macros.
- **14 buttons** = pages (tap) and layers (hold), exactly the Felucca/Clipper grammar users already know. Keep OCT− as "back" and OCT+ as "confirm" in dialogs; keep OCT− at power-on for update mode (non-negotiable).
- **Keys**: 27 silicone, F3–G5, two and a quarter octaves. Not velocity-sensitive in stock; analog-row velocity is experimental — design for accent/latch and velocity from external MIDI, treat on-board velocity as a later opt-in.
- **LEDs under keys** (up to 44): scale display, chord tones, arp step position, Turing-machine bit state, generated-pattern preview. This is the only visual feedback that doesn't cost screen redraws.

### 2.2 Display

240×240 TFT, no framebuffer, strips of ≤124 rows at ~66 Hz. Card-based pages with four knob tiles map cleanly; a small live scope or spectrum is feasible (Clipper does a scope). Avoid full-screen animated redraws; redraw dirty tiles.

### 2.3 Memory and storage budgets

- Flash (verified): app slot 581 KB; data windows 292 + 16 + 92 KB ≈ 400 KB for user data. Preset format ≤ 256 B → thousands of slots is trivially possible; plan 128–256 user patches, 16 favourites, settings, FM bank (4 KB per 32-voice bank).
- Flash writes stop audio (sector erase with IRQs off). Save only on explicit SAVE or when the voice count is zero; RAM-shadow all data; A/B slots with CRC (GATOR pattern) so power loss never loses the previous good state.
- RAM: budget the ~336 KB pool explicitly — e.g. voices ≤ 40 KB, chorus 8 KB, tape delay 1 s stereo at 48 kHz = 384 KB (too big) → 0.75 s mono + stereo spread, or 16-bit storage, or decimated delay line; plate reverb ≈ 20–30 KB; UI canvas ≈ 58 KB. There is no SDRAM; this pool is the whole budget.

### 2.4 Audio path realities

- 24-bit stereo codec with headphone amp and speaker: measure the noise floor and headroom before tuning the limiter; the speaker path needs a safe peak limiter and probably a high-pass.
- Latency: 256-frame block ≈ 5.3 ms + ≤1 ms scan + codec. Fine for a keyboard instrument; do not shrink the block for its own sake.
- No hardware MIDI OUT. "Arp to external gear" means USB-MIDI (and possibly BLE). Say so in the manual rather than pretending TRS out exists.

### 2.5 Reliability engineering

- Preserve OTA entry (OCT− at power-on) and the Felucca loader marker; publish a unique `FM-1_9NN` identity and `fmhub.json`; build with `fm1-build@v1` for the Hub's "Built from source" badge.
- Watchdog ≈ 8 s: the frame loop never blocks; long operations (bank import) are chunked.
- CPU1: opt-in, with Melodee-style check words and automatic retirement to single-core; voice count degrades gracefully. Prefer X0X's "trade quality, keep playing" to dropouts: a CPU-load governor that reduces oversampling, then reverb quality, then voice count.
- Keep 64-bit multiplies in `noinline` helpers in fixed-point hot loops (silicon bug); prefer the FPU path for new DSP.
- Audio code and tables in RAM sections so flash programming cannot stall the ISR.

### 2.6 Power

CPU1 and 360 MHz cost battery. Offer ECO (one core, lighter FX) vs PERFORMANCE; show battery on screen (ADC exists).

### 2.7 Development infrastructure (hardware designer's checklist)

- Toolchain in Docker on macOS (Felucca BUILDING.md), host simulator rendering WAV/PNG for every DSP change, golden-hash regression tests (Felucca-Engines pattern), `fm1-emulator` for UI, web installer + `fm1_install.py`, CI via `fm1-build@v1`.
- A bench unit that stays on V15 for A/B, and a second unit for destructive testing. Buy the RP2040 Transporter parts before the first flash.

---

## 3. Electronic musician

### 3.1 Who this is and how it gets used

A $70 pocket synth bought by producers who already own gear. They will: jam on the couch through the speaker, record into a DAW over USB, drive it from a real keyboard or sequencer over USB/BLE, and judge it in the first minute by whether the presets sound expensive. Nobody will finish a track on its sequencer; many will finish a track with its *sound*.

### 3.2 The first sixty seconds

- Power on → last patch, playable immediately, no splash dialogs.
- PRESET encoder browses with audition, category filter, and a starred ring. Patch names on screen in a large font.
- Four knobs always do something musical (macros) without entering a page.
- Hold a scale on SCL and every key is in key; one-finger chords with a voicing choice.
- ARP on, latch, and the thing already sounds like a record.

### 3.3 The arpeggiator spec (must-haves)

| Area | Requirement |
|---|---|
| Modes | UP, DOWN, UP/DOWN (inclusive and exclusive), ORDER, RANDOM, WALK (random neighbour), CONVERGE, DIVERGE, PINKY, THUMB, CHORD (strum/stab), **TURING** (shift-register with LOCK probability and LENGTH 2–16), **PATTERN** (user step-mask) |
| Range | 1–4 octaves; octave order up / down / alternate / random |
| Timing | rate 1/1–1/32 incl. triplets and dotted; swing; **gate 5–200 %** (tie when >100); humanise amount |
| Rhythm | per-step probability; ratchets 1–4 with velocity ramp; **Euclidean rhythm** (hits/steps/rotation) and 16 rhythm presets; rests via step mask |
| Hold | latch; chord memory (freeze held notes, transpose from keys); sustain pedal = latch |
| Strum | CHORD mode with strum time and direction; velocity curve across notes |
| Variation | **DICE** (new rhythm/order from a seed), **MUTATE** (one step changes), **SEED LOCK**, 16 favourites per project |
| Sync | internal, USB and TRS MIDI clock in with Start/Stop; tempo tap on a layer |
| Output | arp notes to USB-MIDI and Bluetooth MIDI out with channel select, so it drives other gear |
| Capture | **ARP → PHRASE**: record the last bar of arp output into a short phrase loop playable from one key (this replaces "a sequencer" without becoming a groovebox) |
| Feedback | step and octave shown on key LEDs; Turing bits on the black keys |

### 3.4 Finding good random patterns (the X0X/TB-3PO lesson)

Deterministic generators seeded by a visible number, with a density/complexity knob and seed lock, are what makes random *useful*: users can dial density up and down on a locked seed and return to a favourite. Apply that to the arp (rhythm + order), to Turing mode, and to a **GENERATE** page that composes a one-bar phrase from scale, chord, density, syncopation and contour parameters. Keep a history of the last 16 seeds and a favourites ring. Show the result on the LEDs before committing.

### 3.5 Performance layer

- Hold FX → key-mapped momentary effects (repeat/stutter, reverse, tape stop, filter sweep up/down, freeze, shimmer throw, echo throw). Felucca proved this on the same panel.
- Macros on CC16–19, full CC map documented in the manual, MIDI learn for everything else, PC with bank select for patches.
- MIDI IN channel selectable, local off, transpose, pitch bend range, MPE optional later.

### 3.6 Sound module use

Receive on one channel (optionally a second part for a split/layer) with the full engine available from a controller keyboard over USB, TRS or Bluetooth (the FM-1 connects to a Bluetooth keyboard itself); USB audio out so the FM-1 records straight into the DAW; clock follower so the arp locks to the session.

### 3.7 What makes them put it down

Mono output through headphones, FX that sound like demos, a randomiser that produces noise, menu diving to change a filter, saving that glitches audio, a bricked unit after an update, and no way back to stock.

---

## 4. Instrument concepts

| | Concept | Engines | FX chain | Signature features | Risk / overlap |
|---|---|---|---|---|---|
| **A** | **Pocket Poly** — 8-voice (12 with CPU1) stereo polysynth with a discovery engine | VA hybrid, 6-op FM, macro osc (+ optional pluck) | Juno chorus, tape delay, plate/FDN reverb, soft clip + comp + limiter | 4 macros, arp per §3.3, DICE/MUTATE/SEED/MORPH/BREED, ARP→PHRASE, USB audio | Scope; CPU at 12 voices with full FX |
| B | **FM-1 Pro FM** — 6-op FM done properly | FM only (op waveforms, feedback anywhere, stereo spread) | as A | FM-aware dice (harmonic ratio sets), `.syx` import/export, bank manager | Competes head-on with stock V16 (20 voices); narrower appeal |
| C | **Macro Mono/Para** — Plaits-style bass/lead box | Plaits subset + LPG, 4-voice paraphonic | drive, delay, reverb | Fastest randomise, Turing arp, drone mode | Overlaps GATOR; mono/para limits the 27 keys |
| D | **Texture / Drone** — generative ambient | granular, physical, pad | long delays, shimmer | generative loops, "tend" UI | Overlaps AMB-1; long FX need SDRAM |
| E | **Chord / Arp performer** — Omnichord-like | chord engine + strum, one pad voice | as A | keys as chord pads, strum plate on KNOB 4 | Overlaps FoMni / fm1-chord; thin as a synth |

Effects and features common to all: stereo-first signal path, the modulation layer of §1.4, the discovery tools of §1.5, the arp of §3.3, scale/chord keys, hold-FX layer, documented MIDI, USB audio capture, OTA safety.

---

## 5. Recommendation

Build **A — Pocket Poly**, folding B and C in as engines rather than separate products: a stereo polysynth with three deep engines (VA hybrid, 6-op FM with `.syx` import, Plaits-derived macro oscillator), one shared modulation/macro layer, an FX chain tuned to sound good through the speaker *and* in a mix, the arpeggiator of §3.3 with Turing/Euclid/dice/seed, and the patch-discovery system of §1.5.

What it is **not**: no tracks, no drum machine, no song mode, no sampler. The only "sequencing" is the arp and the one-bar ARP→PHRASE capture.

Why this wins against what exists:

- Stock V16 has voices and parts but weak FX, no discovery tools, a basic arp, and a closed engine.
- Clipper has the feature list but no factory sound pass, CPU trouble at higher voice counts, and 23 engines nobody will learn.
- GATOR is mono; AMB-1 is a single piece; the groovebox forks are groove boxes.
- Nothing on the platform combines **stereo sound quality + deep arp + seed-based discovery** as the product.

Suggested phasing:

1. **v0.1 (platform proof):** Felucca-platform app skeleton, VA engine 8 voices stereo, home page with 4 macros, preset save/load, OTA/identity, host simulator + golden tests, installer.
2. **v0.2 (sound):** FX chain, modulation layer, VINTAGE, factory preset pass #1, USB audio out.
3. **v0.3 (arp + discovery):** full arp spec, Turing/Euclid/dice/seed, DICE/MUTATE/MORPH/FAVOURITES, LED feedback.
4. **v0.4 (FM):** 6-op FM engine with `.syx` import, FM-aware dice.
5. **v0.5 (macro osc):** Plaits subset on the FPU; CPU1 governor; ECO/PERF.
6. **v1.0:** manual, MIDI chart, web editor/backup, Hub listing.

---

## 6. Platform recommendation and first decisions

Decided 2026-10-30: build as an app on **fm1-base over felucca-bsp** (`app_init` / `app_frame` / `app_render`, `plat_*`, canvas, LEDs), writing our own application layer rather than forking Felucca's groovebox app. Reuse Felucca-Engines (MIT) DSP primitives and Plaits (MIT) where useful. Verified numbers, contract, tooling and repo shape: `platform-decision.md`.

Decisions to make at the start of the next phase:

1. **Licence posture:** fm1-base located and read; our app files may use any licence. Recommendation: MPL-2.0 and no Felucca app code copied (`platform-decision.md`). Kyle's call.
2. **Sample rate:** 44.1 kHz family (44,117.6 Hz actual) — the only rate fm1-base exposes; 48 kHz would mean contributing GATOR's MCLK option to fm1-base. Tune from the real rate.
3. **DSP numeric model:** FPU float in `.cc` for new engines (GATOR/Melodee precedent) vs fixed-point (Felucca-Engines). Likely both: fixed-point VA/FX primitives that exist, float for FM and Plaits.
4. **Second core:** design voice rendering as jobs from day one so CPU1 is an opt-in multiplier, never a dependency.
5. **Preset format:** fixed-size binary ≤ 256 B with version byte; define before the first factory patch.

---

## 7. Open questions that gate the design

From `fm1-research-findings.md` §11, the ones that still change decisions here: codec noise floor (limiter and gain staging), CPU1 stability (voice count claims), and velocity viability (keyboard expressiveness). SDRAM, sample rate and `fm1-base` are resolved in `platform-decision.md`.
