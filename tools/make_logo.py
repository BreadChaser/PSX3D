#!/usr/bin/env python3
# PSX3D synthwave logo: midnight sky, striped sun, neon grid, wordmark with
# extrusions in PlayStation-era colours. Renders psx3d/meta's icon.png,
# banner.png and splash_top.png.
#
#   python3 tools/make_logo.py <fonts dir with RussoOne-Regular.ttf> <out dir>
#
# Needs Pillow. Russo One is OFL-licensed: https://fonts.google.com/specimen/Russo+One
import math, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

FONTS = sys.argv[1]
OUT = sys.argv[2]
FONT = f"{FONTS}/RussoOne-Regular.ttf"

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))

def vgrad(w, h, stops):
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        for i in range(len(stops) - 1):
            if stops[i][0] <= t <= stops[i + 1][0]:
                u = (t - stops[i][0]) / (stops[i + 1][0] - stops[i][0])
                c = lerp(stops[i][1], stops[i + 1][1], u)
                break
        for x in range(w):
            px[x, y] = c
    return img

def glow(layer, radius, strength=1.0):
    g = layer.filter(ImageFilter.GaussianBlur(radius))
    if strength != 1.0:
        a = g.getchannel("A").point(lambda v: min(255, int(v * strength)))
        g.putalpha(a)
    return g

def scene(W, H, horizon, show_shapes=True, show_sun=True):
    sky = vgrad(W, H, [(0, (3, 6, 22, 255)), (0.45, (10, 24, 64, 255)),
                       (horizon / H, (30, 90, 170, 255)), (horizon / H + 0.001, (3, 6, 20, 255)),
                       (1, (1, 2, 10, 255))])
    d = ImageDraw.Draw(sky)
    rnd = random.Random(7)
    for _ in range(int(W * H / 18000)):
        x, y = rnd.randrange(W), rnd.randrange(int(horizon * 0.8))
        b = rnd.randrange(150, 255)
        r = max(1, H // 220) * rnd.choice([1, 1, 2])
        d.ellipse([x - r, y - r, x + r, y + r], fill=(b, b, 255, 255))

    # a few faint controller shapes among the stars
    sd = ImageDraw.Draw(sky)
    shapes = [("tri", (60, 210, 140)), ("circ", (235, 60, 70)), ("cross", (80, 140, 255)), ("sq", (240, 110, 200))]
    rnd2 = random.Random(11)
    spots = [(0.09, 0.20), (0.91, 0.16)]
    for i in range(2 if show_shapes else 0):
        kind, c = shapes[i % 4]
        x, y = int(W * spots[i][0]), int(horizon * spots[i][1])
        r = int(H * 0.035)
        col = c + (190,)
        w = max(2, r // 3)
        if kind == "tri":
            sd.polygon([(x, y - r), (x - r, y + r * 0.8), (x + r, y + r * 0.8)], outline=col, width=w)
        elif kind == "circ":
            sd.ellipse([x - r, y - r, x + r, y + r], outline=col, width=w)
        elif kind == "cross":
            sd.line([(x - r, y - r), (x + r, y + r)], fill=col, width=w)
            sd.line([(x - r, y + r), (x + r, y - r)], fill=col, width=w)
        else:
            sd.rectangle([x - r, y - r, x + r, y + r], outline=col, width=w)

    # striped sun
    R = int(H * 0.30)
    cx, cy = W // 2, horizon - int(R * 0.35)
    sun = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sg = vgrad(2 * R, 2 * R, [(0, (255, 230, 60, 255)), (0.55, (255, 120, 40, 255)), (1, (230, 20, 50, 255))])
    mask = Image.new("L", (2 * R, 2 * R), 0)
    md = ImageDraw.Draw(mask)
    md.ellipse([0, 0, 2 * R - 1, 2 * R - 1], fill=255)
    # horizontal cut stripes, thicker toward the bottom
    y, gap = int(R * 0.55), max(2, R // 22)
    while y < 2 * R:
        md.rectangle([0, y, 2 * R, y + gap], fill=0)
        y += gap * 3 + int((2 * R - y) * 0.05)
        gap += max(1, R // 60)
    sun.paste(sg, (cx - R, cy - R), mask)
    # the sun sets behind the horizon: nothing of it below
    ImageDraw.Draw(sun).rectangle([0, horizon, W, H], fill=(0, 0, 0, 0))
    if show_sun:
        sky = Image.alpha_composite(sky, glow(sun, R * 0.25, 1.6))
        sky = Image.alpha_composite(sky, sun)

    # mountains with a neon rim
    mtn = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    md = ImageDraw.Draw(mtn)
    pts = [(0, horizon)]
    rnd = random.Random(3)
    x = 0
    while x < W:
        x += rnd.randrange(W // 16, W // 7)
        far_from_sun = abs(x - cx) / (W / 2)
        pts.append((x, horizon - int(H * (0.03 + 0.22 * far_from_sun ** 1.5) * rnd.uniform(0.55, 1.0))))
    pts.append((W, horizon))
    md.polygon(pts, fill=(4, 10, 32, 255))
    md.line(pts, fill=(60, 170, 255, 255), width=max(1, H // 160))
    sky = Image.alpha_composite(sky, glow(mtn, H / 200, 1.0))
    sky = Image.alpha_composite(sky, mtn)

    # perspective grid floor
    grid = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grid)
    lw = max(2, H // 110)
    col = (40, 140, 255, 255)
    for i in range(-12, 13):
        gd.line([(cx + i * W * 0.04, horizon), (cx + i * W * 0.42, H)], fill=col, width=lw)
    k = 0
    while True:
        t = (k / 6.0) ** 2.2
        y = horizon + t * (H - horizon)
        if y > H:
            break
        gd.line([(0, y), (W, y)], fill=col, width=lw)
        k += 1
    sky = Image.alpha_composite(sky, glow(grid, H / 120, 1.1))
    sky = Image.alpha_composite(sky, grid)
    return sky

PS = {"P": (230, 30, 50), "S": (250, 205, 20), "X": (30, 190, 120), "3": (40, 110, 230), "D": (230, 30, 50)}

def wordmark(text, size, colors=PS):
    font = ImageFont.truetype(FONT, size)
    l, t, r, b = font.getbbox(text)
    w, h = r - l, b - t
    depth = int(size * 0.22)
    pad = int(size * 0.35)
    W, H = w + int(depth * 0.75 * (len(text) - 1)) + depth + 2 * pad, h + depth + 2 * pad
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ox, oy = pad - l, pad - t

    # letter x positions, tracked apart so each extrusion stays clear of
    # the next letter
    track = depth * 0.75
    xs, x = [], 0.0
    for ch in text:
        xs.append(x)
        x += font.getlength(ch) + track
    extra = track * (len(text) - 1)

    def letter_mask(i, dx, dy):
        m = Image.new("L", (W, H), 0)
        ImageDraw.Draw(m).text((ox + xs[i] + dx, oy + dy), text[i], font=font, fill=255)
        return m

    def all_mask(dx, dy):
        m = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(m)
        for i, ch in enumerate(text):
            d.text((ox + xs[i] + dx, oy + dy), ch, font=font, fill=255)
        return m

    # coloured extrusion per letter, darker toward the back
    for i in range(depth, 0, -1):
        for k, ch in enumerate(text):
            c = colors.get(ch, (200, 200, 200))
            shade = lerp(c + (255,), (c[0] // 4, c[1] // 4, c[2] // 4, 255), i / depth)
            out.paste(Image.new("RGBA", (W, H), shade), (0, 0), letter_mask(k, i, i))
    # silver-white face
    face = vgrad(W, H, [(0, (255, 255, 255, 255)), ((oy + t + h * 0.5) / H, (225, 232, 245, 255)),
                        ((oy + t + h * 0.52) / H, (170, 185, 210, 255)), (1, (235, 240, 250, 255))])
    fm = all_mask(0, 0)
    out.paste(face, (0, 0), fm)
    # thin coloured rim on the face edge for each letter
    for k, ch in enumerate(text):
        c = colors.get(ch, (200, 200, 200))
        rim = ImageChops.subtract(letter_mask(k, 0, 0), letter_mask(k, 0, 0).filter(ImageFilter.MinFilter(3)))
        out.paste(Image.new("RGBA", (W, H), c + (255,)), (0, 0), rim)
    # soft white-blue glow
    g = Image.new("RGBA", (W, H), (120, 180, 255, 0))
    g.putalpha(all_mask(depth // 2, depth // 2).filter(ImageFilter.GaussianBlur(size * 0.12)).point(lambda v: v * 3 // 4))
    return Image.alpha_composite(g, out)

def halo(img, wm, pos, size):
    # soft dark backing so the grid doesn't run through the lettering
    a = wm.getchannel("A").filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(size * 0.18))
    a = a.point(lambda v: min(255, v * 2) * 220 // 255)
    dark = Image.new("RGBA", wm.size, (2, 4, 16, 0))
    dark.putalpha(a)
    img.alpha_composite(dark, pos)

def compose(W, H, horizon, size, ty, show_shapes=True):
    bg = scene(W, H, horizon, show_shapes)
    wm = wordmark("PSX3D", size)
    pos = ((W - wm.width) // 2, ty - wm.height // 2)
    halo(bg, wm, pos, size)
    bg.alpha_composite(wm, pos)
    return bg

def rounded(img, r):
    m = Image.new("L", img.size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, img.width - 1, img.height - 1], r, fill=255)
    img.putalpha(ImageChops.multiply(img.getchannel("A"), m))
    return img

hero = compose(1600, 960, 500, 300, 715)
hero.save(f"{OUT}/hero.png")
hero.resize((400, 240), Image.LANCZOS).save(f"{OUT}/splash_top.png")

banner = compose(1024, 512, 240, 215, 365, show_shapes=False)
banner.resize((256, 128), Image.LANCZOS).save(f"{OUT}/banner.png")

# icon: the whole name, stacked PSX over 3D, chunky enough for 48x48
ic = scene(384, 384, 300, show_shapes=False, show_sun=False)
top = wordmark("PSX", 126)
bot = wordmark("3D", 158)
for wm_, cy_ in ((top, 116), (bot, 262)):
    pos_ = ((384 - wm_.width) // 2 + 6, cy_ - wm_.height // 2)
    halo(ic, wm_, pos_, 150)
    ic.alpha_composite(wm_, pos_)
rounded(ic, 60).resize((48, 48), Image.LANCZOS).save(f"{OUT}/icon.png")
rounded(ic.copy(), 60).save(f"{OUT}/icon_big.png")
print("ok")
