#!/usr/bin/env python3
# PSX3D UI sounds and library music, synthesized from scratch (original,
# no samples): soft FM glass bells and slow pads in a big airy reverb, in the
# spirit of late-90s console menus. Writes raw signed 16-bit LE PCM, 32000 Hz:
#   move.pcm select.pcm back.pcm launch.pcm  (mono)
#   music.pcm                                 (stereo, seamless loop)
#
#   python3 tools/make_sounds.py <out dir>     (needs numpy)
import sys
import numpy as np

SR = 32000
OUT = sys.argv[1]
rng = np.random.default_rng(7)


def t(sec):
    return np.arange(int(SR * sec)) / SR


def midi(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def bell(f, dur, vol, ratio=3.5, index=1.6, decay=3.0):
    """FM glass bell: bright attack that mellows as the modulation decays."""
    tt = t(dur)
    mod_env = np.exp(-tt * decay * 2.2)
    amp_env = np.exp(-tt * decay) * np.minimum(1, tt / 0.004)
    mod = index * mod_env * np.sin(2 * np.pi * f * ratio * tt)
    return vol * amp_env * np.sin(2 * np.pi * f * tt + mod)


def pad(freqs, dur, vol, attack, release):
    tt = t(dur)
    x = np.zeros(len(tt))
    for f in freqs:
        for d in (-0.003, 0.0, 0.0031):
            # sine + a little 2nd harmonic, slow chorus wobble
            ph = 2 * np.pi * f * (1 + d) * tt + 0.3 * np.sin(2 * np.pi * 0.2 * tt + f)
            x += np.sin(ph) + 0.15 * np.sin(2 * ph)
    x /= 3 * len(freqs)
    e = np.ones(len(tt))
    a, r = int(SR * attack), int(SR * release)
    e[:a] = np.sin(np.linspace(0, np.pi / 2, a)) ** 2
    e[-r:] *= np.cos(np.linspace(0, np.pi / 2, r)) ** 2
    return vol * x * e


def reverb_ir(seconds, damp_hz=5000, predelay=0.012):
    n = int(SR * seconds)
    tt = np.arange(n) / SR
    ir = rng.normal(0, 1, (2, n)) * np.exp(-tt * 6.9 / seconds)   # -60 dB at the end
    # darken the tail over time (air absorption)
    spec = np.fft.rfft(ir, axis=1)
    f = np.fft.rfftfreq(n, 1 / SR)
    spec *= 1 / (1 + (f / damp_hz) ** 2)
    ir = np.fft.irfft(spec, n, axis=1)
    pd = int(SR * predelay)
    ir = np.pad(ir, ((0, 0), (pd, 0)))[:, :n]
    return ir / np.sqrt((ir ** 2).sum(axis=1, keepdims=True))


def wet(x, ir, mix, circular=False):
    """stereo reverb of mono x; circular=True wraps the tail (seamless loops)"""
    out = []
    for ch in range(2):
        if circular:
            n = len(x)
            h = ir[ch][:n] if len(ir[ch]) >= n else np.pad(ir[ch], (0, n - len(ir[ch])))
            y = np.fft.irfft(np.fft.rfft(x) * np.fft.rfft(h), n)
        else:
            n = len(x) + len(ir[ch])
            y = np.fft.irfft(np.fft.rfft(x, n) * np.fft.rfft(ir[ch], n), n)
        out.append(y)
    dry = np.pad(x, (0, len(out[0]) - len(x)))
    return np.stack([dry * (1 - mix) + out[0] * mix, dry * (1 - mix) + out[1] * mix])


def save_mono(name, st, vol, max_s=None):
    x = st.mean(axis=0)
    if max_s:
        x = x[: int(SR * max_s)]
    # trim the silent end of the tail
    nz = np.nonzero(np.abs(x) > 1e-4 * np.abs(x).max())[0]
    x = x[: nz[-1] + 1] if len(nz) else x
    x = x / np.abs(x).max() * vol
    fade = min(len(x), int(SR * (0.15 if max_s else 0.05)))
    x[-fade:] *= np.linspace(1, 0, fade)
    (np.clip(x, -1, 1) * 32000).astype("<i2").tofile(f"{OUT}/{name}.pcm")


small = reverb_ir(1.2, damp_hz=6000)
big = reverb_ir(3.5, damp_hz=4000, predelay=0.03)

# move: tiny soft glass tick
save_mono("move", wet(bell(midi(96), 0.12, 1.0, ratio=2.0, index=0.6, decay=40), small, 0.35), 0.16, max_s=0.4)

# select: two airy bell notes a fifth apart
x = np.zeros(int(SR * 0.6))
for i, (m, d) in enumerate(((84, 0.0), (91, 0.07))):
    b = bell(midi(m), 0.5, 1.0 if i == 0 else 0.8, ratio=3.5, index=1.2, decay=7)
    s = int(SR * d)
    x[s:s + len(b)] += b[: len(x) - s]
save_mono("select", wet(x, small, 0.45), 0.30)

# back: one soft lower bell
save_mono("back", wet(bell(midi(76), 0.4, 1.0, ratio=2.0, index=0.9, decay=9), small, 0.45), 0.24)

# launch: a slow shimmering swell (Cmaj9) with rising bell sparkles
dur = 2.2
x = pad([midi(m) for m in (60, 64, 67, 71, 74)], dur, 1.0, attack=0.6, release=1.2)
for k, m in enumerate((79, 83, 86, 91)):
    b = bell(midi(m), 1.2, 0.35, ratio=3.5, index=1.0, decay=3)
    s = int(SR * (0.25 + k * 0.12))
    x[s:s + len(b)] += b[: len(x) - s]
save_mono("launch", wet(x, big, 0.55), 0.32)

# ---------------------------------------------------------------- music
# Slow ambient loop: Cmaj7 - Am9 - Fmaj7 - G6/9, 4 s per chord, soft pad,
# sparse bell melody, big reverb wrapped around the loop.
chords = [
    (48, (60, 64, 67, 71)),       # Cmaj7
    (45, (60, 64, 67, 71, 74)),   # Am9
    (41, (60, 64, 65, 69, 72)),   # Fmaj7
    (43, (59, 62, 64, 67, 69)),   # G6/9
]
seg = 4.0
total = seg * len(chords)
n = int(SR * total)
mono = np.zeros(n)
for ci, (root, notes) in enumerate(chords):
    s = int(SR * ci * seg)
    # pads overlap into the next chord a little (crossfade)
    p = pad([midi(m) for m in notes], seg + 1.0, 0.5, attack=1.0, release=1.4)
    p += pad([midi(root)], seg + 1.0, 0.35, attack=0.8, release=1.2)
    idx = (s + np.arange(len(p))) % n
    np.add.at(mono, idx, p)
# bell melody: a few notes per chord, pentatonic, unhurried
mel = [(0.5, 79), (1.75, 76), (3.0, 74),
       (4.5, 76), (5.25, 72), (6.5, 69),
       (8.5, 72), (9.75, 77), (11.0, 76),
       (12.5, 74), (13.5, 71), (14.75, 74)]
for when, m in mel:
    b = bell(midi(m), 2.0, 0.22, ratio=3.5, index=1.1, decay=2.2)
    idx = (int(SR * when) + np.arange(len(b))) % n
    np.add.at(mono, idx, b)
st = wet(mono, big, 0.6, circular=True)
st = st / np.abs(st).max() * 0.5
out = np.empty(n * 2)
out[0::2] = st[0]
out[1::2] = st[1]
(np.clip(out, -1, 1) * 32000).astype("<i2").tofile(f"{OUT}/music.pcm")
print("ok", total, "s loop")
