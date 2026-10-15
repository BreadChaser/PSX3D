#!/usr/bin/env python3
# PSX3D art: synthwave sunset with CC0 palm silhouettes (tools/art), a grid
# floor that rises into wireframe hills, and the Michroma wordmark.
#
#   python3 tools/make_art.py <out dir>        (needs Pillow)
#
# Writes splash_top.png (400x240), banner.png (256x128), icon.png (48x48) and
# library_bg.png (400x240, no floor mesh: the app draws that live, see
# psx3d/source/libui.c, which mirrors height()/colour() below).
import math, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "art")
OUT = sys.argv[1]
FONT = os.path.join(ART, "Michroma-Regular.ttf")

PINK, VIOLET, BLUE = (255, 70, 200), (150, 60, 255), (40, 90, 255)
WHITE = [(0, (255, 255, 255, 255)), (1, (230, 225, 250, 255))]
PINKG = [(0, (255, 150, 225, 255)), (1, (255, 70, 175, 255))]


def vgrad(w, h, stops):
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        for i in range(len(stops) - 1):
            if stops[i][0] <= t <= stops[i + 1][0]:
                u = (t - stops[i][0]) / (stops[i + 1][0] - stops[i][0] + 1e-9)
                c = tuple(int(stops[i][1][k] + (stops[i + 1][1][k] - stops[i][1][k]) * u) for k in range(4))
                break
        for x in range(w):
            px[x, y] = c
    return img


def mix(c1, c2, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(c1[k] + (c2[k] - c1[k]) * t) for k in range(3))


# ---- the floor/hills mesh (libui.c draws the same thing live)
def height(x, z):
    rise = max(0.0, abs(x) - 2.0) ** 1.15 * 0.32
    n = 0.8 + 0.22 * math.sin(x * 0.9 + z * .35) + 0.12 * math.sin(x * 1.7 - z * .5)
    near = min(1.0, (z - 1.0) / 6.0)
    return rise * n * (0.35 + 0.65 * near)


def colour(x, z):
    side = min(1.0, abs(x) / 9.0)
    depth = min(1.0, (z - 1.0) / 25.0)
    c = mix(VIOLET, BLUE, side ** 0.8)
    return mix(c, PINK, depth ** 0.7 * (1 - side) * 0.85)


def scene(W, H, hz_frac=0.583, mesh=True):
    s = H / 960.0
    HZ = int(H * hz_frac)
    cx = W / 2
    img = vgrad(W, H, [(0, (40, 8, 80, 255)), (hz_frac * .4, (110, 20, 120, 255)),
                       (hz_frac * .78, (230, 40, 120, 255)), (hz_frac, (255, 120, 60, 255)),
                       (hz_frac + .001, (20, 2, 44, 255)), (1, (6, 0, 18, 255))])
    # sun with horizontal cuts
    R = int(300 * s)
    cy = HZ - int(40 * s)
    sg = vgrad(2 * R, 2 * R, [(0, (255, 236, 110, 255)), (.5, (255, 196, 60, 255)),
                             (.85, (255, 120, 60, 255)), (1, (255, 80, 100, 255))])
    m = Image.new("L", (2 * R, 2 * R))
    md = ImageDraw.Draw(m)
    md.ellipse([0, 0, 2 * R - 1, 2 * R - 1], fill=255)
    y, k = R * 0.62, 0
    while y < 2 * R:
        g = R * (.02 + .016 * k)
        md.rectangle([0, y, 2 * R, y + g], fill=0)
        y += g + R * .08
        k += 1
    sun = Image.new("RGBA", (W, H))
    sun.paste(sg, (int(cx - R), cy - R), m)
    halo = Image.new("RGBA", (W, H))
    ImageDraw.Draw(halo).ellipse([cx - R * 1.5, cy - R * 1.5, cx + R * 1.5, cy + R * 1.5], fill=(255, 90, 130, 130))
    halo = halo.filter(ImageFilter.GaussianBlur(R * .4))
    for L in (halo, sun):
        ImageDraw.Draw(L).rectangle([0, HZ, W, H], fill=(0, 0, 0, 0))
        img = Image.alpha_composite(img, L)

    # palms: dark navy silhouettes with a warm rim on the sun side
    def palm(name, x, base, h, flip):
        im = Image.open(os.path.join(ART, name)).convert("RGBA")
        alpha = im.getchannel("A")
        # transparent PNG: use its alpha; opaque (white background): dark = palm
        a = alpha if alpha.getextrema()[0] < 255 else ImageChops.invert(im.convert("L"))
        a = a.crop(a.getbbox())
        a = a.crop((0, 0, a.width, int(a.height * 0.94)))       # drop the little mound
        sc = h / a.height
        a = a.resize((max(1, int(a.width * sc)), max(1, int(h))), Image.LANCZOS)
        if flip:
            a = a.transpose(Image.FLIP_LEFT_RIGHT)
        layer = Image.new("RGBA", a.size, (18, 22, 52, 255))
        layer.putalpha(a)
        rim = Image.new("RGBA", a.size, (255, 120, 90, 255))
        sh = max(1, int(3 * s)) * (1 if x > cx else -1)
        rim.putalpha(ImageChops.subtract(a, ImageChops.offset(a, sh, 0)).point(lambda v: v * .8))
        pos = (int(x - a.width / 2), int(base - a.height))
        img.alpha_composite(layer, pos)
        img.alpha_composite(rim, pos)

    palm("palms_pair_a.png", W * .13, HZ + 60 * s, 620 * s, False)
    palm("palms_pair_b.png", W * .86, HZ + 60 * s, 640 * s, True)

    f = 900 * s

    def proj(x, z):
        return cx + x / z * f * (W / H) / (1600 / 960), HZ + (1.0 - height(x, z)) / z * f * 0.45

    floor = Image.new("RGBA", (W, H))
    ImageDraw.Draw(floor).rectangle([0, HZ, W, H], fill=(16, 2, 44, 255))
    lines = Image.new("RGBA", (W, H))
    if mesh:
        xs = [i * 0.5 for i in range(-36, 37)]
        zs = [1.0 + (i / 28.0) ** 1.7 * 40 for i in range(29)]
        fd = ImageDraw.Draw(floor)
        for j in range(len(zs) - 2, -1, -1):
            for i in range(len(xs) - 1):
                fd.polygon([proj(xs[i], zs[j]), proj(xs[i + 1], zs[j]), proj(xs[i + 1], zs[j + 1]),
                            proj(xs[i], zs[j + 1])], fill=(16, 2, 44, 255))
        ld = ImageDraw.Draw(lines)
        lw = max(1, int(2 * s))
        for z in zs:
            for i in range(len(xs) - 1):
                ld.line([proj(xs[i], z), proj(xs[i + 1], z)], fill=colour((xs[i] + xs[i + 1]) / 2, z) + (255,), width=lw)
        for x in xs:
            for j in range(len(zs) - 1):
                ld.line([proj(x, zs[j]), proj(x, zs[j + 1])], fill=colour(x, (zs[j] + zs[j + 1]) / 2) + (255,), width=lw)
    img = Image.alpha_composite(img, floor)
    haze = Image.new("RGBA", (W, H))
    ImageDraw.Draw(haze).rectangle([0, HZ - 6 * s, W, HZ + 30 * s], fill=(255, 110, 190, 120))
    haze = haze.filter(ImageFilter.GaussianBlur(18 * s))
    ImageDraw.Draw(haze).rectangle([0, 0, W, HZ - 8 * s], fill=(0, 0, 0, 0))
    img = Image.alpha_composite(img, haze)
    img = Image.alpha_composite(img, lines.filter(ImageFilter.GaussianBlur(5 * s)))
    return Image.alpha_composite(img, lines)


# ---- wordmark: Michroma, white PSX, pink 3D
def word(text, size, stops, track, glow_rgba, bold=0):
    f = ImageFont.truetype(FONT, size)
    xs, x = [], 0
    for ch in text:
        xs.append(x)
        x += f.getlength(ch) + track
    l, t, r, b = f.getbbox(text)
    pad = int(size * 0.4)
    w, h = int(x) + 2 * pad, (b - t) + 2 * pad
    m = Image.new("L", (w, h))
    d = ImageDraw.Draw(m)
    for i, ch in enumerate(text):
        d.text((pad + xs[i], pad - t), ch, font=f, fill=255)
    if bold:
        m = m.filter(ImageFilter.MaxFilter(bold))
    face = vgrad(w, h, stops)
    face.putalpha(m)
    g = Image.new("RGBA", (w, h), glow_rgba[:3] + (0,))
    g.putalpha(m.filter(ImageFilter.GaussianBlur(size * 0.1)).point(lambda v: v * glow_rgba[3] // 255))
    out = Image.alpha_composite(g, face)
    return out.crop(out.getbbox())


def wordmark(size):
    a = word("PSX", size, WHITE, size * 0.17, (255, 120, 220, 170))
    b = word("3D", size, PINKG, size * 0.17, (255, 70, 170, 210))
    gap = int(size * 0.35)
    h = max(a.height, b.height)
    out = Image.new("RGBA", (a.width + gap + b.width, h))
    out.alpha_composite(a, (0, (h - a.height) // 2))
    out.alpha_composite(b, (a.width + gap, (h - b.height) // 2))
    return out


def place(img, m, cy):
    W, H = img.size
    a = m.getchannel("A").filter(ImageFilter.GaussianBlur(H * 0.03)).point(lambda v: min(255, v * 2) * 150 // 255)
    sh = Image.new("RGBA", m.size, (8, 0, 24, 0))
    sh.putalpha(a)
    pos = ((W - m.width) // 2, cy - m.height // 2)
    img.alpha_composite(sh, pos)
    img.alpha_composite(m, pos)


title = scene(1600, 960)
place(title, wordmark(200), int(960 * 0.68))
title.convert("RGB").resize((400, 240), Image.LANCZOS).save(f"{OUT}/splash_top.png")

ban = scene(1024, 512, hz_frac=0.62)
place(ban, wordmark(130), int(512 * 0.74))
ban.convert("RGB").resize((256, 128), Image.LANCZOS).save(f"{OUT}/banner.png")

bg = scene(1600, 960, hz_frac=0.583, mesh=False)
bg.convert("RGB").resize((400, 240), Image.LANCZOS).save(f"{OUT}/library_bg.png")

# icon: sun peeking in at the top, PSX over 3D, strokes thickened for 48x48
src = scene(1600, 960)
H = 960
ic = src.crop((int(800 - H * 0.42), int(H * 0.25), int(800 + H * 0.42), int(H * 1.09))).resize((384, 384), Image.LANCZOS)
dark = Image.new("RGBA", (384, 384), (10, 0, 30, 0))
dm = Image.new("L", (384, 384))
ImageDraw.Draw(dm).rectangle([0, 150, 384, 384], fill=150)
dark.putalpha(dm.filter(ImageFilter.GaussianBlur(40)))
ic = Image.alpha_composite(ic, dark)
for txt, size, stops, cy in (("PSX", 112, WHITE, 210), ("3D", 132, PINKG, 316)):
    w_ = word(txt, size, stops, 4, (255, 90, 190, 220), bold=7)
    ic.alpha_composite(w_, ((384 - w_.width) // 2, cy - w_.height // 2))
rm = Image.new("L", (384, 384))
ImageDraw.Draw(rm).rounded_rectangle([0, 0, 383, 383], 60, fill=255)
ic.putalpha(rm)
ic.resize((48, 48), Image.LANCZOS).save(f"{OUT}/icon.png")
print("ok")
