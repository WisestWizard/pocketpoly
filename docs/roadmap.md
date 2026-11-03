# Pocket Poly — state of the build and the plan to finish it

Written 2026-10-31 after a full debug and test pass over everything built so far. Facts here were measured or run,
not assumed; the sections at the end are the plan.

## 1. What exists

| Area | Built | Where |
| --- | --- | --- |
| Platform | fm1-base over felucca-bsp, MPL-2.0 app, dual core (voices 0–3 on the second core), OTA identity FM-1_1300, installer, host simulator, device remote | `app/fm1.toml`, `base/` |
| Engine | 8 stereo voices; oscillators SAW, PULSE, TRI, SINE, PD, three wavetables (built at boot), FM6; sub, noise, XMOD, RING, SYNC; Simper SVF LP/BP/HP with drive; amp/filter/mod envelopes (mod loopable); two LFOs; 8-slot mod matrix with 4 named macros; drift, glide, velocity, poly and channel pressure, wheel, bend; per-voice DC blocker; cubic soft clip; load governor | `app/engine/engine.c` |
| FM | Six operators, 32 algorithms, DX7 envelopes/levels/rates, feedback, velocity, a 32-voice bank + LIVE, 8 factory voices, DX7 single-voice SysEx import and a bank tool | `app/engine/fm.c`, `tools/fm1_dx7.py` |
| FX | Juno ensemble (3 modes), tape delay (wow/flutter, dark feedback), Dattorro plate, limiter; stages rest when silent; FX hold-layer on the keys with live bypass | `app/engine/fx.c`, `ui.c` |
| Arpeggiator | 12 modes incl. CHORD and TURING; divisions with triplets; octaves; gate/tie; swing; Euclid (hits/steps/rotate); chance; ratchets; latch; seeds; MIDI clock follow (2 s timeout); clock/start/stop out; notes out | `engine.c`, ARP pages |
| Discovery | Seeded DICE (whole or per section), seed browsing, MUTATE, MORPH A/B, 16-deep history | `app/discover.c` |
| MIDI | USB, TRS in, Bluetooth both roles; channel filter; send channel; CC map (1, 7, 16–19, 64, 71–74, 91/93/94, 120/121/123); program change; sustain; LOCAL off; MIDI settings page | `app/app.c`, `ui.c` |
| Storage | Settings (PPL4), 16 user patches (BNK5, migrates BNK4), FM bank (FMB1); 8 factory patches | `app/project.c` |
| UI | 22 pages, 7 encoders with stepped/continuous kinds, save dialog, LEDs, screen reader (`speak`), manual with screenshots | `app/ui.c`, `app/docs/MANUAL.md` |
| Tooling | `tools/render_all.py` + `tests/golden.txt` (16 renders, SHA-256), `tools/cpu_probe.py`, `tools/fm1_dx7.py`, simulator scripts `app/tests/basic.txt`, `screens.txt` | `tools/`, `app/tests/` |

## 2. Debug and test pass (2026-10-31)

What was run, and the result:

- **Simulator checks:** `app/tests/basic.txt` grew from 231 to 310 checks this pass (added: every arp mode family
  — CHORD, RANDOM, TURING with its page — LATCH, CHANCE 0, RATCHET, MIDI-clock timeout and continuation, a program
  change under the arp, CC 7 volume, CC 120 with the arp's note-offs, PLAY and OCT± with keys held, history undo
  and redo, the plate ringing out with its send closed, OSC B's model range, the LIVE voice saved with a patch).
  310/310 pass.
- **Sanitizers:** the whole simulator rebuilt with AddressSanitizer + UndefinedBehaviorSanitizer; `basic.txt`,
  an arp stress script (every ARP knob, clock out, latch, MIDI clock in and out, the channel filter) and
  `screens.txt` ran clean. One UBSan report, in fm1-base's `fastmath.h:58` (`n << 23` with negative `n` in
  `fm_exp2f`): undefined in C99, well-defined on the target's two's-complement shift; not ours, noted upstream.
- **Static analysis:** clang's analyzer over every compile unit: no reports in `app/`.
- **Renders:** 16 factory sounds byte-identical to `tests/golden.txt`; the at-rest FX path proven identical to
  running (forced-on vs resting renders, sample for sample).
- **Device (measured this pass):** image 279,040 B of the 581 KB slot; RAM 84,296 of 98,304 B; pool 332,512 of
  344,064 B (8 KB spare required, 11.5 KB left). Idle CPU 4 % on INIT, GLASS KEYS and FM BELL (was 25 % before FX
  rested). Eight held notes: INIT 22 %, GLASS KEYS 52 %, PD BASS 51 %, WIRE PLUCK 50 %, EVOLVE PAD 55 %, SYNC LEAD
  50 %, VOX 54 %, FM BELL 80 %; every factory FM voice on FM BELL 81 % (the guardrail is 85 %). Output peaks with
  eight notes: VA patches −1 to −14 dB, FM voices −14 to −19 dB (the meters read peak after 1.6 s).
- **Code review:** three independent reviews (engine + FM; FX + wavetables; app/UI/project/discover): 37 findings,
  11 confirmed by runs, 33 fixed in this pass with checks, 4 deferred (§3). The full list is in
  `platform-decision.md`, "Review pass".

## 3. Known issues and open items

- **Fixed this pass (highlights):** LFO1 could index the sine table out of bounds when the matrix pushed its rate
  (a bus fault on the device); keys held while PLAY toggled hung voices and external notes; the arp's fallback
  after MIDI clock stopped went silent or burst; panic sent no arp note-offs; ties re-attacked; ratchets were
  mis-spaced; the plate clipped inside its 16-bit allpasses at pad settings; the delay and the plate were cut
  (then ghosted) when a send closed; the ensemble replayed stale signal when switched on; wavetable top frames
  and mip levels were wrong (+11.5 dB jumps on GLASS); a stack overflow in the DX7 import message; history undo
  off by one and duplicating; OSC B could be FM6 (a saw); SEMI steps were 0.38 semitones; a LIVE FM voice was lost
  on save. Details in `platform-decision.md`.
- **Deferred from the review:** wavetable aliasing above ~5 kHz on the lowest mip (two more mips cost 24 KB of
  arena; a sine fallback is the cheap route) — v0.6; the arp's float step schedule drifts after hours of
  continuous running — v0.9; denormal costs in a few smoothers depend on the pi32v2 FPU — measure in v0.9;
  `fastmath.h:58` shifts a negative int (fm1-base, benign on target) — report upstream.
- **Unreproduced incident (2026-10-31):** the arp seemed dead and the unit stopped answering USB SysEx until a
  power cycle; no crash recorded. What to capture if it recurs is in `platform-decision.md`.
- **Bank migration BNK4 → BNK5** is tested in code and by reading, not yet on a device that held real user saves.
- **FM BELL 8 voices at 79 %:** the heaviest patch; a sixth-operator voice on eight voices leaves little headroom
  for FX. Watch it when adding anything to the render.
- **Codec noise floor / headroom** never measured (needs an audio interface); gain staging is by ear and meters.
- **CPU2 on this unit:** works; the platform's fallback to one core exists but has not been exercised on purpose.
- **Hub identity:** FM-1_1300 is a development prefix; a final prefix is needed before any public package.
- **Stepped parameters move one step per encoder event**, not per detent, so a flicked MODEL/MODE knob moves
  slower than the detents. Deliberate (no overshoot through enums); revisit if it annoys.

## 4. The plan

Each phase ends with: simulator checks for everything new, goldens rewritten only for intended sound changes,
a device build measured (image/RAM/pool, CPU on the worst patch), and the manual updated. Guardrails: RAM ≥ 8 KB
and pool ≥ 8 KB spare; the worst factory patch at 8 voices ≤ 85 % CPU with every effect on.

### v0.6 — the sound and the keys (what a player hears first)

- Factory bank to 16 patches with a level pass (every render within ±3 dB of the loudest, measured by
  `render_all.py` + the level script): bass ×3, pad ×3, keys ×3, lead ×3, pluck ×2, FM ×2 — Kyle's notes drive it.
- Velocity curve setting (soft / linear / hard) for the FM-1's keys; VEL>AMP default per category.
- Unison STACK (2 voices per note, detuned, stereo) with polyphony 4 — the "nice full sound" option without panning.
- Glide modes (always / legato) and a legato mono mode for leads and basses.
- Second filter model: a four-pole ladder with saturation (CPU-checked against the guardrail).
- LFO tempo sync (divisions) and LFO fade-in; one-shot LFO.
- Chord memory and scale lock on the keys: a key plays a stored voicing; scale lock snaps to a scale (musician
  wish list #1).
- Done when: 16 patches pass the level test; every new parameter has a page, a dice range, a manual row and a
  simulator check; CPU guardrail met with STACK on the worst patch.

### v0.7 — discovery, finished

- FAVOURITES: one press keeps the current seed + edits in a 16-deep favourites ring (separate from history).
- COMPARE: hold to hear the saved version of the slot; UNDO beyond the 16 steps is not needed.
- BREED: two favourites → a child (per-parameter pick with mutation); lock sections before breeding.
- Category-aware DICE (BASS / PAD / LEAD / KEYS / PLUCK / ANY): ranges sampled from the factory bank's statistics.
- Section LOCK: hold a section button while turning SEED to keep that section fixed.
- LEDs: arp step and Euclid pattern on the key LEDs while the arp runs; seed digits on DICE.
- ARP → PHRASE: capture one bar of the arp into a phrase the keys transpose (the only sequencing we will do).
- Done when: a 30-second "find a patch" session needs no menu (dice, lock, breed, favourite all on buttons);
  simulator checks for ring bounds and determinism.

### v0.8 — MIDI and integration

- MIDI LEARN: 16 CC → parameter assignments stored in the patch bank header; a LEARN page.
- Bank select + program change map; MIDI PANIC on a button combination.
- Patch dump/load over SysEx (single patch and bank) and a Mac/Web tool for it; this is also the editor's
  transport.
- Backup/restore of everything over USB with fm1-base's `backup = true` and `tools/fm1_backup.py`; manual steps.
- MIDI thru option (TRS in → USB/BLE out) and clock out always (not only while the arp runs).
- MPE is out of scope (no per-note pitch beyond the arp); poly aftertouch stays.
- Done when: a DAW can store and recall every patch and setting; a controller can drive any parameter.

### v0.9 — platform and performance

- ECO / PERF: second core and clock as a setting; battery figures measured in both.
- Governor polish: voice count shown, voices come back when the load drops (today: until power-off).
- Measure the codec noise floor and headroom through an interface; set the limiter threshold and output gain
  from the numbers.
- Screen mirroring (`mirror = true`) for the manual's screenshots from the real unit and for remote support.
- RAMTEXT placement of the hottest render loops if the FM worst case needs it.
- Fallback to one core exercised on purpose (`FM1B_CPU2=0` build measured).

### v1.0 — release

- Manual complete with a MIDI implementation chart; `app/site/` landing page built with `make_site.py`.
- `fmhub.json`, reproducible build with `fm1-build@v1`, licence headers, a public repository, a changelog.
- Final identity prefix; factory reset (hold on power-up); settings and bank versions documented for migration.
- Regression suite in CI: simulator checks, goldens, sanitizer build; device CPU probe as a manual gate.

### Not planned

Tracks, drum machine, song mode, sampler, per-step automation, a touch of anything that makes it a groovebox.
