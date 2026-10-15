#!/usr/bin/env python3
# PSX3D UI sounds and library music, synthesized from scratch (original,
# no samples): soft sine tones, breathy filtered noise and slow pads in a big
# airy reverb. No FM or bright transients, which read as chiptune. Writes raw signed 16-bit LE PCM, 32000 Hz:
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


def tone(f, dur, vol, attack=0.006, decay=8.0):
    """a plain sine with a soft attack and an exponential fade"""
    tt = t(dur)
    e = np.exp(-tt * decay) * np.sin(np.minimum(1, tt / attack) * np.pi / 2) ** 2
    return vol * e * (np.sin(2 * np.pi * f * tt) + 0.08 * np.sin(4 * np.pi * f * tt))


def lowpass(x, hz):
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    return np.fft.irfft(spec / (1 + (f / hz) ** 4), len(x))


def breath(dur, vol, lo, hi, attack, release):
    """band-limited noise swelling in and out: air rather than a note"""
    x = rng.normal(0, 1, int(SR * dur))
    x = lowpass(x, hi) - lowpass(x, lo)
    e = np.ones(len(x))
    a, r = int(SR * attack), int(SR * release)
    e[:a] = np.sin(np.linspace(0, np.pi / 2, a)) ** 2
    e[-r:] *= np.cos(np.linspace(0, np.pi / 2, r)) ** 2
    return vol * x / np.abs(x).max() * e


small = reverb_ir(1.2, damp_hz=4500)
big = reverb_ir(3.5, damp_hz=3500, predelay=0.03)

# move: a very soft, low "tup" with a breath of air
x = tone(midi(81), 0.12, 1.0, attack=0.004, decay=45)
x[: int(SR * 0.08)] += breath(0.08, 0.25, 1500, 5000, 0.01, 0.06)
save_mono("move", wet(x, small, 0.3), 0.13, max_s=0.35)

# select: two soft sines a fifth apart, slightly staggered, airy
x = np.zeros(int(SR * 0.7))
for i, (m, d) in enumerate(((72, 0.0), (79, 0.06))):
    b = tone(midi(m), 0.6, 1.0 if i == 0 else 0.7, attack=0.01, decay=7)
    s0 = int(SR * d)
    x[s0:s0 + len(b)] += b[: len(x) - s0]
x[: int(SR * 0.3)] += breath(0.3, 0.3, 800, 4000, 0.02, 0.25)
save_mono("select", wet(x, small, 0.45), 0.26)

# back: one soft lower tone
save_mono("back", wet(tone(midi(67), 0.45, 1.0, attack=0.01, decay=9), small, 0.45), 0.2)

# launch: a slow shimmering swell (Cmaj9) with a rising breath, no bells
dur = 2.2
x = pad([midi(m) for m in (60, 64, 67, 71, 74)], dur, 1.0, attack=0.6, release=1.2)
x += breath(dur, 0.35, 600, 6000, 1.0, 1.0)
save_mono("launch", wet(x, big, 0.55), 0.3)

# ---------------------------------------------------------------- music
# Ambience only: Cmaj7 - Am9 - Fmaj7 - G6/9, 6 s per chord, slow pads that
# overlap generously, a low root, and a faint wash of air, all in a big
# reverb wrapped around the loop. No melody.
chords = [
    (48, (60, 64, 67, 71)),       # Cmaj7
    (45, (60, 64, 67, 71, 74)),   # Am9
    (41, (60, 64, 65, 69, 72)),   # Fmaj7
    (43, (59, 62, 64, 67, 69)),   # G6/9
]
seg = 6.0
total = seg * len(chords)
n = int(SR * total)
mono = np.zeros(n)
for ci, (root, notes) in enumerate(chords):
    s0 = int(SR * ci * seg)
    p = pad([midi(m) for m in notes], seg + 2.5, 0.5, attack=2.2, release=2.6)
    p += pad([midi(root)], seg + 2.5, 0.3, attack=1.8, release=2.4)
    idx = (s0 + np.arange(len(p))) % n
    np.add.at(mono, idx, p)
# air: slowly breathing filtered noise across the whole loop (seamless)
air = rng.normal(0, 1, n)
air = lowpass(air, 3000) - lowpass(air, 400)
air /= np.abs(air).max()
tt = np.arange(n) / SR
mono += 0.05 * air * (0.6 + 0.4 * np.sin(2 * np.pi * tt / seg))
st = wet(mono, big, 0.6, circular=True)
st = st / np.abs(st).max() * 0.5
out = np.empty(n * 2)
out[0::2] = st[0]
out[1::2] = st[1]
(np.clip(out, -1, 1) * 32000).astype("<i2").tofile(f"{OUT}/music.pcm")
print("ok", total, "s loop")
