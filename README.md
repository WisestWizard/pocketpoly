# Pocket Poly — a stereo polysynth with a discovery engine for the M-VAVE FM-1

Eight stereo voices, each with two oscillators that can be analog shapes, phase-distorted sines, wavetables or a
six-operator FM voice, with sync, ring and cross modulation; a resonant filter with drive, three envelopes, two
LFOs, an eight-slot modulation matrix and four macros on the knobs; then a Juno ensemble, a tape delay, a Dattorro
plate and a limiter. An arpeggiator with twelve modes, Euclidean rhythms, chance, ratchets and a Turing machine
follows MIDI clock, and DICE, MUTATE and MORPH find new patches without a menu. MIDI over USB, Bluetooth (both
ways) and TRS; 8 factory patches, 16 user slots and a 32-voice FM bank that takes DX7 SysEx.

Built as an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) over
[felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0).

**Where it stands:** the engine, the FX, the arpeggiator, the discovery engine and the MIDI are built and pass the
simulator suite; the sound pass that follows them (`docs/roadmap.md`, v0.6: the 16-patch factory bank with matched
levels, STACK, chord memory and scale lock) has not been done, and there is no release and no published site yet.
It grew up beside [DEEP FIELD](https://github.com/WisestWizard/deepfield) in one repository until 2026-11-08; the
history up to that day is there, and Pocket Poly's own carries on here.

## Layout

| Path | What |
| --- | --- |
| `app/` | the firmware: `fm1.toml` (build settings), `app.c` / `ui.c` / `project.c` / `discover.c` / `engine/`, `tests/` (simulator scripts), `docs/MANUAL.md`, `site/` |
| `base/` | fm1-base, a git submodule (felucca-bsp inside it as `base/bsp`) |
| `docs/` | `roadmap.md` (what exists and the plan, read this first), the FM-1 research and the platform decision |
| `tools/` | `render_all.py` (the factory renders and their hashes), `cpu_probe.py`, `fm1_dx7.py` |
| `tests/` | `golden.txt`, the render hashes `render_all.py` checks against |
| `build.sh` | runs `base/tools/build.py app` |

## Build and test

```
./build.sh --host
app/build/host/pocketpoly_host app/tests/basic.txt app/build/host
./build.sh
```

The simulator runs the whole firmware on the Mac from a script (commands at the top of `base/host/plat_host.c`),
with WAV and PNG output; `app/tests/screens.txt` writes the manual's screenshots. Device builds run the JieLi
toolchain in a `linux/amd64` container.

```
python3 tools/render_all.py
```

renders every factory patch and FM voice to WAV and fails if a render's hash differs from `tests/golden.txt`;
after an intended change to the sound, `--golden` rewrites the file.

## Prerequisites (macOS, Apple silicon)

- Homebrew's Python 3.14 and its Pillow, built against libraqm: `brew install python@3.14 pillow` (the font
  generator needs raqm; Xcode's Python and pip's Pillow wheel do not have it).
- A container runtime with a `docker` CLI for the Linux-only JieLi toolchain: `brew install colima docker`, then
  `colima start --vm-type vz --vz-rosetta`.
- The JieLi toolchain: `sh base/tools/get_toolchain.sh` (→ `~/.jieli/toolchain`, 135 MB).
- The JieLi AC79 SDK, three files of which go into every package:
  `git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK` (1 GB).
- For installing over USB from a terminal: `python3 -m pip install --user mido python-rtmidi`.

`JIELI_TOOLCHAIN`, `AC79_SDK` and `PYTHON` override the defaults (`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`,
`/opt/homebrew/bin/python3.14`).

## Install

```
python3 base/tools/fm1_install.py app/build/pocketpoly.fwsc
```

At your own risk. Hold OCT− (or OCT− and OCT+ for five seconds) at power-on for update mode; the installer can put
M-VAVE's own firmware back. If the FM-1 only shows up as the chip's `WL82 UBOOT1.00` disk, see
`base/bsp/tools/fm1_rescue`.

## Licence

MPL-2.0 for this firmware's files (see `docs/platform-decision.md`). fm1-base and felucca-bsp keep their own
MPL-2.0 files; JieLi's SDK files used by the build are Apache-2.0 and are never committed here.
