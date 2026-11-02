#!/usr/bin/env python3
"""Draws the Exclusion app icon: assets/icon.png (256x256) and assets/icon.ico (16..256).

An original mark, not the official radiation symbol: a three-blade pinwheel (each blade a 48 degree arc
segment, turned 14 degrees) in amber around a dark hub holding a blocky letter E, on a scratched, rusting,
dark green-grey metal plate with rivets. Needs Pillow. Deterministic (fixed seed).

    python3 tools/zone/make_icon.py            # from the repo root
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, "..", "..", "assets"))
S = 1024            # drawn at 4x, shrunk to 256 for clean edges
OUT = 256
AMBER = (226, 184, 76)
AMBER_DARK = (122, 90, 28)
PLATE = (43, 50, 44)
PLATE_DARK = (24, 29, 25)
HUB = (22, 26, 22)
RUST = [(107, 74, 46), (122, 80, 50)]


def noise_layer(size, scale, seed, lo=0, hi=255):
    """smooth value noise (random small grid, upscaled bicubic)"""
    rnd = random.Random(seed)
    g = max(2, size // scale)
    small = Image.new("L", (g, g))
    small.putdata([rnd.randint(lo, hi) for _ in range(g * g)])
    return small.resize((size, size), Image.BICUBIC)


def plate():
    rnd = random.Random(1986)
    m = 28
    r = 150
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((m, m, S - m, S - m), r, fill=255)
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    base = Image.new("RGB", (S, S), PLATE)
    # vertical shading and blotchy grime
    shade = Image.linear_gradient("L").resize((S, S))                   # 0 top .. 255 bottom
    base = Image.composite(Image.new("RGB", (S, S), PLATE_DARK), base, shade.point(lambda v: int(v * 0.45)))
    grime = noise_layer(S, 64, 7, 60, 200)
    base = Image.composite(Image.new("RGB", (S, S), (70, 78, 66)), base, grime.point(lambda v: max(0, v - 120)))
    grain = noise_layer(S, 3, 11, 90, 165).filter(ImageFilter.GaussianBlur(0.6))
    base = Image.blend(base, ImageChops.multiply(base, Image.merge("RGB", (grain, grain, grain))), 0.35)
    d = ImageDraw.Draw(base)
    # rust: streaks running down from the top and the rivets
    for _ in range(46):
        x = rnd.randint(m + 40, S - m - 40)
        y0 = rnd.randint(m + 20, S - m - 120)
        ln = rnd.randint(40, 230)
        w = rnd.randint(2, 7)
        col = rnd.choice(RUST)
        for k in range(ln):
            if rnd.random() < 0.9 - 0.7 * k / ln:
                d.line((x + rnd.randint(-1, 1), y0 + k, x + w, y0 + k), fill=col)
    # scratches: thin light and dark hairlines at shallow angles
    for _ in range(95):
        x, y = rnd.randint(m, S - m), rnd.randint(m, S - m)
        a = rnd.uniform(-0.5, 0.5) + (math.pi / 2 if rnd.random() < 0.25 else 0)
        ln = rnd.randint(30, 220)
        light = rnd.random() < 0.7
        c = (150, 158, 142) if light else (14, 17, 14)
        d.line((x, y, x + math.cos(a) * ln, y + math.sin(a) * ln), fill=c, width=rnd.choice((1, 1, 2)))
    img.paste(base, (0, 0), mask)
    # bevel: light top/left rim, dark outline and bottom/right rim
    rim = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    rd = ImageDraw.Draw(rim)
    rd.rounded_rectangle((m, m, S - m, S - m), r, outline=(12, 14, 12, 255), width=10)
    rd.arc((m + 8, m + 8, S - m - 8, S - m - 8), 180, 270, fill=(120, 130, 112, 200), width=7)
    rd.line((m + r, m + 10, S - m - r, m + 10), fill=(120, 130, 112, 200), width=7)
    rd.line((m + 10, m + r, m + 10, S - m - r), fill=(120, 130, 112, 170), width=7)
    rd.arc((m + 8, m + 8, S - m - 8, S - m - 8), 0, 90, fill=(8, 10, 8, 220), width=7)
    img = Image.alpha_composite(img, rim)
    # rivets
    dd = ImageDraw.Draw(img)
    for cx, cy in ((m + 78, m + 78), (S - m - 78, m + 78), (m + 78, S - m - 78), (S - m - 78, S - m - 78)):
        dd.ellipse((cx - 22, cy - 22, cx + 22, cy + 22), fill=(16, 19, 16, 255))
        dd.ellipse((cx - 17, cy - 17, cx + 17, cy + 17), fill=(78, 84, 72, 255))
        dd.ellipse((cx - 10, cy - 14, cx + 6, cy + 2), fill=(128, 136, 118, 255))
        dd.ellipse((cx - 17, cy + 2, cx + 17, cy + 22), fill=(60, 52, 40, 70))   # rust under the rivet
    return img, mask


def sector(cx, cy, r0, r1, a0, a1, steps=40):
    pts = []
    for i in range(steps + 1):
        a = math.radians(a0 + (a1 - a0) * i / steps)
        pts.append((cx + r1 * math.cos(a), cy + r1 * math.sin(a)))
    for i in range(steps, -1, -1):
        a = math.radians(a0 + (a1 - a0) * i / steps)
        pts.append((cx + r0 * math.cos(a), cy + r0 * math.sin(a)))
    return pts


def mark(img):
    cx = cy = S // 2
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ld = ImageDraw.Draw(layer)
    r0, r1 = 215, 395
    turn = 14                                    # pinwheel turn, so it is not the official symbol
    for k in range(3):
        a = -90 + 120 * k + turn                 # blade centres at 12, 4 and 8 o'clock, turned
        ld.polygon(sector(cx + 9, cy + 11, r0, r1, a - 24, a + 24), fill=(0, 0, 0, 150))          # drop shadow
    for k in range(3):
        a = -90 + 120 * k + turn
        ld.polygon(sector(cx, cy, r0, r1, a - 24, a + 24), fill=AMBER_DARK + (255,))
        ld.polygon(sector(cx, cy, r0 + 8, r1 - 10, a - 21, a + 21), fill=AMBER + (255,))
        ld.polygon(sector(cx, cy, r1 - 30, r1 - 10, a - 21, a + 21), fill=(240, 205, 112, 255))   # lighter outer lip
    ld.ellipse((cx - 195, cy - 195, cx + 195, cy + 195), fill=(0, 0, 0, 150))
    ld.ellipse((cx - 186, cy - 186, cx + 186, cy + 186), fill=AMBER_DARK + (255,))
    ld.ellipse((cx - 172, cy - 172, cx + 172, cy + 172), fill=HUB + (255,))
    ld.ellipse((cx - 150, cy - 150, cx + 150, cy + 150), outline=(70, 80, 64, 255), width=5)
    # the letter E, built from bars (spine + three arms)
    ex, ey, h, t = cx - 70, cy - 105, 210, 46
    w = 150
    for box in ((ex, ey, ex + t, ey + h), (ex, ey, ex + w, ey + t),
                (ex, ey + (h - t) // 2, ex + w - 22, ey + (h - t) // 2 + t), (ex, ey + h - t, ex + w, ey + h)):
        ld.rectangle(box, fill=AMBER + (255,))
    # wear: knock flecks and scratches out of the paint so the plate shows through
    rnd = random.Random(420)
    wear = noise_layer(S, 5, 3, 0, 255).point(lambda v: 255 if v > 232 else 0)
    wd = ImageDraw.Draw(wear)
    for _ in range(70):
        x, y = rnd.randint(cx - 400, cx + 400), rnd.randint(cy - 400, cy + 400)
        a = rnd.uniform(-0.6, 0.6)
        ln = rnd.randint(25, 140)
        wd.line((x, y, x + math.cos(a) * ln, y + math.sin(a) * ln), fill=255, width=rnd.choice((2, 3, 4)))
    keep = wear.point(lambda v: 150 if v > 128 else 255)
    layer.putalpha(ImageChops.multiply(layer.getchannel("A"), keep))
    return Image.alpha_composite(img, layer)


def main():
    img, mask = plate()
    img = mark(img)
    img.putalpha(ImageChops.multiply(img.getchannel("A"), mask))
    out = img.resize((OUT, OUT), Image.LANCZOS)
    os.makedirs(ASSETS, exist_ok=True)
    png = os.path.join(ASSETS, "icon.png")
    ico = os.path.join(ASSETS, "icon.ico")
    out.save(png, optimize=True)
    out.save(ico, sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("wrote", png, "and", ico)


if __name__ == "__main__":
    sys.exit(main())
