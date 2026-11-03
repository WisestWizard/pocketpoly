#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Kyle McCann
"""Send DX7 voices to Pocket Poly over USB-MIDI. fm1-base passes the app SysEx frames of at most 640 bytes, so a
32-voice bulk dump (4104 bytes) cannot arrive whole: this tool unpacks a bank .syx into 32 single-voice dumps and
sends each after a slot message (F0 7D 50 50 01 slot F7); the FM-1 saves the bank once, after the 32nd. A single-
voice .syx goes to --slot (1..32), or to the slot the patch uses.
  python3 tools/fm1_dx7.py BANK.syx               the 32 voices into slots 1..32
  python3 tools/fm1_dx7.py VOICE.syx --slot 5      one voice into slot 5
  python3 tools/fm1_dx7.py --list BANK.syx         the voice names, nothing sent
Needs mido with python-rtmidi."""
import argparse, sys, time

def unpack(packed):                       # 128 packed bytes -> 155 unpacked (the DX7 single-voice layout)
    out = []
    for op in range(6):
        q = packed[op * 17:(op + 1) * 17]
        out += list(q[0:11])
        out += [q[11] & 3, (q[11] >> 2) & 3]            # LC, RC
        out += [q[12] & 7, (q[13] & 3)]                  # RS, AMS
        out += [(q[13] >> 2) & 7, q[14]]                 # KVS, OL
        out += [q[15] & 1, (q[15] >> 1) & 31, q[16], (q[12] >> 3) & 15]   # MODE, FC, FF, DET
    g = packed[102:128]
    out += list(g[0:8])                                  # PR1-4, PL1-4
    out += [g[8] & 31, g[9] & 7, (g[9] >> 3) & 1]        # ALG, FB, OKS
    out += list(g[10:14])                                # LFS, LFD, LPMD, LAMD
    out += [g[14] & 1, (g[14] >> 1) & 7, (g[14] >> 4) & 7]   # LKS, LFW, LPMS
    out += [g[15]]                                       # transpose
    out += list(g[16:26])                                # name
    return [v & 0x7F for v in out]

def single_dump(voice155):
    body = voice155
    chk = (-sum(body)) & 0x7F
    return [0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B] + body + [chk, 0xF7]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("syx"); ap.add_argument("--slot", type=int); ap.add_argument("--list", action="store_true")
    ap.add_argument("--port", help="part of the MIDI port's name (default: FM-1)")
    a = ap.parse_args()
    data = open(a.syx, "rb").read()
    voices = []
    i = 0
    while i < len(data):
        if data[i] != 0xF0: i += 1; continue
        j = data.index(0xF7, i)
        msg = data[i:j + 1]; i = j + 1
        if len(msg) >= 6 and msg[1] == 0x43 and msg[3] == 0x09 and len(msg) == 4104:
            for k in range(32):
                voices.append(unpack(msg[6 + k * 128:6 + (k + 1) * 128]))
        elif len(msg) >= 6 and msg[1] == 0x43 and msg[3] == 0x00 and len(msg) == 163:
            voices.append(list(msg[6:161]))
    if not voices:
        sys.exit("no DX7 voices in that file")
    for n, v in enumerate(voices):
        print(f"{n + 1:2d} {bytes(v[145:155]).decode('ascii', 'replace')}")
    if a.list:
        return
    import mido
    want = a.port or "FM-1"
    names = [n for n in mido.get_output_names() if want in n]
    if not names:
        sys.exit("no FM-1 on USB-MIDI")
    out = mido.open_output(names[0])
    for n, v in enumerate(voices):
        slot = (a.slot - 1) if (a.slot and len(voices) == 1) else n
        if slot >= 32:
            break
        out.send(mido.Message("sysex", data=[0x7D, 0x50, 0x50, 0x01, slot]))
        time.sleep(0.05)
        out.send(mido.Message("sysex", data=single_dump(v)[1:-1]))
        time.sleep(0.25 if n < len(voices) - 1 else 1.0)
    print(f"sent {min(len(voices), 32)} voice(s)")

if __name__ == "__main__":
    main()
