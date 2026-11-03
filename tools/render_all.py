#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kyle McCann
"""Render every factory patch and every factory FM voice to WAV in the host simulator, for auditioning on the Mac
and as a regression check: the simulator is deterministic, so the renders' SHA-256 hashes only change when the
sound does. Run from the repository after ./build.sh --host.
  python3 tools/render_all.py              renders into app/build/renders/ and compares with tests/golden.txt
  python3 tools/render_all.py --golden     renders and rewrites tests/golden.txt (after an intended sound change)"""
import hashlib, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
HOST = ROOT / "app/build/host/pocketpoly_host"
OUT = ROOT / "app/build/renders"
GOLDEN = ROOT / "tests/golden.txt"
PATCHES = ["INIT", "GLASS KEYS", "PD BASS", "WIRE PLUCK", "EVOLVE PAD", "FM BELL", "SYNC LEAD", "VOX"]
FMVOICES = ["E.PIANO", "FM BASS", "BRASS", "BELL", "GLASS PAD", "MARIMBA", "ORGAN", "FM LEAD"]
CHORD = ["90 30 64", "90 37 64", "90 3C 64"]

def script():
    s = ["wait 700"]
    for i, name in enumerate(PATCHES):
        fn = f"{i + 1:02d}-{name.replace(' ', '_')}.wav"
        s += [f"midi C0 {i:02X}", "wait 100", f"wav {fn}"] + [f"midi {c}" for c in CHORD] + ["wait 1800", "midi B0 7B 00", "wait 1700", "wavstop", "wait 300"]
    s += ["midi C0 05", "wait 100", "tap EDIT", "tap EDIT", "tap EDIT", "tap EDIT", "tap EDIT", "wait 100", "turn K1 -3", "wait 100"]   # FM BELL: the FM page, voice 1
    for i, name in enumerate(FMVOICES):
        fn = f"fm-{i + 1:02d}-{name.replace(' ', '_').replace('.', '')}.wav"
        if i:
            s += ["turn K1 1", "wait 100"]
        s += [f"wav {fn}"] + [f"midi {c}" for c in CHORD] + ["wait 1800", "midi B0 7B 00", "wait 1700", "wavstop", "wait 300"]
    s += ["tap HOME"]
    return "\n".join(s) + "\n"

def main():
    golden = "--golden" in sys.argv
    OUT.mkdir(parents=True, exist_ok=True)
    for f in OUT.glob("*.wav"):
        f.unlink()
    (OUT / "render.txt").write_text(script())
    r = subprocess.run([str(HOST), str(OUT / "render.txt"), str(OUT)], capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stdout[-2000:] + r.stderr[-2000:])
    hashes = {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(OUT.glob("*.wav"))}
    for name, h in hashes.items():
        print(f"{h[:12]}  {name}")
    if golden:
        GOLDEN.write_text("".join(f"{h}  {n}\n" for n, h in hashes.items()))
        print(f"golden: {len(hashes)} renders written to {GOLDEN.relative_to(ROOT)}")
        return
    if not GOLDEN.exists():
        sys.exit("no tests/golden.txt yet: run with --golden")
    old = dict(line.split()[::-1] for line in GOLDEN.read_text().splitlines() if line.strip())
    changed = [n for n, h in hashes.items() if old.get(n) != h]
    missing = [n for n in old if n not in hashes]
    if changed or missing:
        print("CHANGED:", ", ".join(changed + [m + " (missing)" for m in missing]))
        sys.exit(1)
    print(f"all {len(hashes)} renders match golden")

if __name__ == "__main__":
    main()
