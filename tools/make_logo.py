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

def palm(d, x0, y0, h, lean, scale, col):
    """palm tree silhouette: curved trunk and drooping fronds"""
    pts = []
    for i in range(21):
        t = i / 20
        pts.append((x0 + lean * h * t * t, y0 - h * t))
    w = max(2, int(scale * 7))
    for i in range(20):
        ww = int(w * (1.2 - 0.5 * i / 20))
        d.line([pts[i], pts[i + 1]], fill=col, width=max(2, ww))
    tx, ty = pts[-1]
    for k in range(9):
        ang = -math.pi * 0.95 + k * (math.pi * 1.9 / 8)
        L = h * 0.6 * (0.8 + 0.3 * math.sin(k * 2.3))
        fr = []
        for j in range(13):
            t = j / 12
            fx = tx + math.cos(ang) * L * t
            fy = ty - math.sin(ang) * L * t * 0.5 + (L * 0.55) * t * t   # droop
            fr.append((fx, fy))
        d.line(fr, fill=col, width=max(2, int(scale * 6)))
        for j in range(2, 12):
            fx, fy = fr[j]
            ll = L * 0.28 * (1 - j / 13)
            for sgn in (-1, 1):
                d.line([(fx, fy), (fx + sgn * ll * 0.45, fy + ll)], fill=col, width=max(2, int(scale * 3)))


def scene(W, H, horizon, show_shapes=True, show_sun=True, show_grid=True, floor=True):
    # purple -> magenta -> orange sunset sky
    sky = vgrad(W, H, [(0, (60, 14, 90, 255)), (0.25 * horizon / H, (110, 20, 110, 255)),
                       (0.65 * horizon / H, (225, 40, 120, 255)), (horizon / H, (255, 120, 60, 255)),
                       (horizon / H + 0.001, (20, 4, 50, 255)), (1, (8, 0, 24, 255))])

    # big striped sun
    R = int(H * 0.32)
    cx, cy = W // 2, horizon - int(R * 0.15)
    sun = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if show_sun:
        sg = vgrad(2 * R, 2 * R, [(0, (255, 225, 60, 255)), (0.6, (255, 190, 40, 255)), (1, (255, 120, 40, 255))])
        mask = Image.new("L", (2 * R, 2 * R), 0)
        md = ImageDraw.Draw(mask)
        md.ellipse([0, 0, 2 * R - 1, 2 * R - 1], fill=255)
        y, gap = int(R * 0.6), max(2, R // 45)
        while y < 2 * R:
            md.rectangle([0, y, 2 * R, y + gap], fill=0)
            gap += max(1, R // 70)
            y += gap * 3
        sun.paste(sg, (cx - R, cy - R), mask)
        ImageDraw.Draw(sun).rectangle([0, horizon, W, H], fill=(0, 0, 0, 0))
        sky = Image.alpha_composite(sky, glow(sun, R * 0.3, 1.4))
        sky = Image.alpha_composite(sky, sun)

    # palm silhouettes (dark teal), two per side
    pl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    pd = ImageDraw.Draw(pl)
    col = (20, 70, 90, 255)
    sc = H / 240
    for x, hh, lean in ((0.13, 0.46, 0.18), (0.25, 0.32, 0.12), (0.75, 0.32, -0.12), (0.87, 0.46, -0.18)):
        palm(pd, W * x, horizon + H * 0.02, H * hh, lean, sc, col)
    sky = Image.alpha_composite(sky, pl)

    if not show_grid:
        floor_grid = False
    else:
        floor_grid = True

    # wireframe mountains on the sides + flat grid floor, drawn as a mesh
    mesh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    md = ImageDraw.Draw(mesh)
    cols, rows = 48, 22
    lw = max(1, int(H / 200))

    def height(u, v):
        side = min(1.0, abs(u - 0.5) / 0.5)
        side = max(0.0, (side - 0.35) / 0.65)
        n = (math.sin(u * 23 + v * 3) * 0.5 + math.sin(u * 11 - v * 7) * 0.3 + math.sin(u * 37) * 0.2)
        return side * (0.75 + 0.45 * n) * (1 - v * 0.5)

    def proj(u, v):
        z = 0.08 + v ** 1.6 * 1.0               # v=0 far (horizon), v=1 near
        x = cx + (u - 0.5) * W * 3.0 * z
        y = horizon + z * (H - horizon) * 1.05 - height(u, v) * H * 0.34 * (0.4 + z)
        return x, y

    def cl(v):
        return lerp((60, 70, 255, 255), (190, 40, 255, 255), v)

    def seg_ok(u0, v0, u1, v1):
        return floor or height(u0, v0) > 0.03 or height(u1, v1) > 0.03

    for r in range(rows + 1):
        v = r / rows
        for c in range(cols):
            if seg_ok(c / cols, v, (c + 1) / cols, v):
                md.line([proj(c / cols, v), proj((c + 1) / cols, v)], fill=cl(v), width=lw)
    for c in range(cols + 1):
        u = c / cols
        for r in range(rows):
            if seg_ok(u, r / rows, u, (r + 1) / rows):
                md.line([proj(u, r / rows), proj(u, (r + 1) / rows)], fill=cl(0.5), width=lw)
    # dark fill under the mesh lines so it occludes the sky below the horizon
    floor = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    fd = ImageDraw.Draw(floor)
    for r in range(rows * 2):
        v0, v1 = r / (rows * 2), (r + 1) / (rows * 2)
        for c in range(cols * 2):
            u0, u1 = c / (cols * 2), (c + 1) / (cols * 2)
            fd.polygon([proj(u0, v0), proj(u1, v0), proj(u1, v1), proj(u0, v1)], fill=(14, 0, 40, 255))
    sky = Image.alpha_composite(sky, floor)
    # magenta glow down the centre of the floor
    g = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if floor:
        ImageDraw.Draw(g).polygon([(cx - W * 0.08, horizon), (cx + W * 0.08, horizon), (cx + W * 0.3, H), (cx - W * 0.3, H)],
                                  fill=(200, 40, 230, 110))
    sky = Image.alpha_composite(sky, g.filter(ImageFilter.GaussianBlur(H / 25)))
    sky = Image.alpha_composite(sky, glow(mesh, H / 140, 1.3))
    sky = Image.alpha_composite(sky, mesh)

    # faint scanlines
    sl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sl)
    step = max(2, H // 120)
    for y in range(0, H, step):
        sd.line([(0, y), (W, y)], fill=(0, 0, 0, 28))
    return Image.alpha_composite(sky, sl)

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
ic = scene(384, 384, 300, show_shapes=False)
top = wordmark("PSX", 126)
bot = wordmark("3D", 158)
for wm_, cy_ in ((top, 116), (bot, 262)):
    pos_ = ((384 - wm_.width) // 2 + 6, cy_ - wm_.height // 2)
    halo(ic, wm_, pos_, 150)
    ic.alpha_composite(wm_, pos_)
rounded(ic, 60).resize((48, 48), Image.LANCZOS).save(f"{OUT}/icon.png")
rounded(ic.copy(), 60).save(f"{OUT}/icon_big.png")
print("ok")

# library background: the scene without the logo, sun lower and dimmer so
# box art stands out in front of it
bg = scene(400, 240, 150, show_shapes=False, floor=False)   # the floor grid is drawn live
dim = Image.new("RGBA", bg.size, (0, 0, 0, 50))
Image.alpha_composite(bg, dim).convert("RGB").save(f"{OUT}/library_bg.png")
