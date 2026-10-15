#!/usr/bin/env python3
# PSX3D UI sounds and library music, synthesized from scratch (original,
# no samples). Writes raw signed 16-bit little-endian PCM:
#   move.pcm select.pcm back.pcm launch.pcm  (mono, 32000 Hz)
#   music.pcm                                 (stereo, 32000 Hz, seamless loop)
#
#   python3 tools/make_sounds.py <out dir>     (needs numpy)
import sys
import numpy as np

SR = 32000
OUT = sys.argv[1]
rng = np.random.default_rng(3)


def t(sec):
    return np.arange(int(SR * sec)) / SR


def env(n, attack, release, sustain=1.0):
    e = np.ones(n) * sustain
    a = max(1, int(SR * attack))
    r = max(1, int(SR * release))
    e[:a] = np.linspace(0, 1, a)
    e[-r:] *= np.linspace(1, 0, r) ** 2
    return e


def saw(freq, tt, detune=0.0):
    ph = (freq * (1 + detune)) * tt
    return 2.0 * (ph - np.floor(ph + 0.5))


def lowpass(x, cutoff):
    # one-pole low-pass, cutoff in Hz (array or scalar)
    a = np.exp(-2 * np.pi * np.broadcast_to(cutoff, x.shape) / SR)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc = (1 - a[i]) * x[i] + a[i] * acc
        y[i] = acc
    return y


def save(name, x, stereo=False):
    x = np.clip(x, -1, 1)
    pcm = (x * 32000).astype("<i2")
    pcm.tofile(f"{OUT}/{name}.pcm")


def blip(f0, f1, dur, vol):
    tt = t(dur)
    f = np.linspace(f0, f1, len(tt))
    ph = np.cumsum(f) / SR
    x = np.sin(2 * np.pi * ph) + 0.25 * np.sin(4 * np.pi * ph)
    return x * env(len(tt), 0.002, dur * 0.9) * vol


# move: short soft tick
save("move", blip(1400, 1100, 0.045, 0.35))

# select: two-note rising chime (E6 -> B6)
a = blip(1319, 1319, 0.16, 0.35)
b = blip(1976, 1976, 0.22, 0.3)
sel = np.zeros(int(SR * 0.3))
sel[: len(a)] += a
sel[int(SR * 0.07): int(SR * 0.07) + len(b)] += b
save("select", sel)

# back: falling blip
save("back", blip(900, 520, 0.09, 0.35))

# launch: filtered saw sweep up into a bright chord, with a bit of air
dur = 0.9
tt = t(dur)
chord = sum(saw(f, tt, d) for f in (220, 277.2, 329.6, 440) for d in (-0.004, 0.004)) / 6
cut = np.geomspace(300, 6000, len(tt))
x = lowpass(chord, cut) * env(len(tt), 0.01, 0.5) * 0.6
x += rng.normal(0, 1, len(tt)) * np.linspace(0.12, 0, len(tt)) * env(len(tt), 0.2, 0.3)
save("launch", x)

# ---------------------------------------------------------------- music
# 4 bars of Am - F - C - G at 96 bpm, 2 beats per chord, pad + arpeggio +
# soft kick, looped seamlessly (the reverb tail wraps around).
bpm = 96
beat = 60 / bpm
chords = [(57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62)]  # MIDI
bar = beat * 4
total = bar * 4
n = int(SR * total)
L = np.zeros(n)
R = np.zeros(n)


def midi(m):
    return 440.0 * 2 ** ((m - 69) / 12)


for ci, ch in enumerate(chords):
    start = int(SR * ci * bar)
    tt = t(bar)
    pad = np.zeros(len(tt))
    for m in ch:
        for d in (-0.006, 0.0, 0.006):
            pad += saw(midi(m), tt, d)
    pad = lowpass(pad / 9, 900 + 500 * np.sin(np.pi * tt / bar)) * env(len(tt), 0.25, 0.4)
    L[start:start + len(tt)] += pad * 0.35
    R[start:start + len(tt)] += pad * 0.33
    # bass
    bass = np.sin(2 * np.pi * midi(ch[0] - 24) * tt) * env(len(tt), 0.01, 0.2)
    L[start:start + len(tt)] += bass * 0.22
    R[start:start + len(tt)] += bass * 0.22
    # arpeggio: 8th notes up through the chord
    step = beat / 2
    notes = [ch[0] + 12, ch[1] + 12, ch[2] + 12, ch[1] + 24]
    for k in range(8):
        s = start + int(SR * k * step)
        tn = t(step * 0.9)
        f = midi(notes[k % 4])
        tone = (saw(f, tn) * 0.5 + np.sin(2 * np.pi * f * tn)) * env(len(tn), 0.003, step * 0.8)
        tone = lowpass(tone, 2500)
        pan = 0.5 + 0.35 * np.sin(k)
        L[s:s + len(tn)] += tone * 0.13 * (1 - pan)
        R[s:s + len(tn)] += tone * 0.13 * pan
    # soft kick on beats
    for k in range(4):
        s = start + int(SR * k * beat)
        tk = t(0.25)
        kick = np.sin(2 * np.pi * np.cumsum(np.geomspace(120, 45, len(tk))) / SR) * env(len(tk), 0.002, 0.2)
        L[s:s + len(tk)] += kick * 0.3
        R[s:s + len(tk)] += kick * 0.3

# simple stereo echo, wrapping around so the loop point is seamless
d = int(SR * beat * 0.75)
for src, dst, g in ((L, R, 0.28), (R, L, 0.28)):
    dst += np.roll(src, d) * g
L = L / max(1e-6, np.max(np.abs(L))) * 0.55
R = R / max(1e-6, np.max(np.abs(R))) * 0.55
st = np.empty(n * 2)
st[0::2] = L
st[1::2] = R
save("music", st)
print("ok", total, "s loop")
