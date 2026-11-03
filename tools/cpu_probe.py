#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kyle McCann
"""Measure Pocket Poly's audio load on a connected FM-1: hold chords of each factory patch over USB-MIDI and read
VOICES and CPU back from the screen reader's channel (base/tools/fm1_remote.py screen). Needs mido with
python-rtmidi. Run from the repository: python3 tools/cpu_probe.py"""
import os
import mido, time, subprocess, re, sys
REMOTE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "base", "tools", "fm1_remote.py")
out = mido.open_output([n for n in mido.get_output_names() if "FM-1" in n][0])
NOTES = [48, 55, 60, 64, 67, 72, 76, 79, 84, 88]
def screen():
    r = subprocess.run([sys.executable, REMOTE, "screen"], capture_output=True, text=True, timeout=30)
    return (r.stdout + r.stderr).strip().replace("\n", " | ")
def cpu_of(text):
    m = re.search(r"CPU (\d+)%", text); v = re.search(r"VOICES (\d+) OF (\d+)", text)
    return (int(m.group(1)) if m else None, v.group(0) if v else "?")
print("idle:", screen())
PATCHES = ["INIT", "GLASS KEYS", "PD BASS", "WIRE PLUCK", "EVOLVE PAD", "FM BELL", "SYNC LEAD", "VOX"]
runs = [(0, "INIT", 1)] + [(i, PATCHES[i], 8) for i in range(8)]
for preset, name, n in runs:
    out.send(mido.Message("program_change", program=preset)); time.sleep(0.2)
    for k in NOTES[:n]:
        out.send(mido.Message("note_on", note=k, velocity=100))
    time.sleep(1.6)
    txt = screen()
    cpu, voices = cpu_of(txt)
    print(f"{name:12s} {n} notes held -> {voices}, CPU {cpu}%   [{txt[:90]}]")
    out.send(mido.Message("control_change", control=123, value=0)); time.sleep(2.5)
out.send(mido.Message("program_change", program=0))
print("after:", screen())
