#!/usr/bin/env python3
# tools/zone/install_skins.py — the user's faction skins (tools/zone/skins/<faction>_<k>.png) become the stalkers' looks:
# z_npc_<faction>.png is the first, z_npc_<faction>_1.. the rest, then mixes made from them (one skin's head on another's
# body, slight shifts of shade) so every faction has several; the suit looks z_suit_sealed / z_suit_assault come from
# theirs. Legacy 64x32 skins are widened to 64x64 first. zone_npc.cpp picks a variant per stalker
# (render/zone_creature_draw.cpp zoneNpcSkin). Run after paint_skins.py and before paint_looks.py:
#   python3 tools/zone/paint_skins.py && python3 tools/zone/install_skins.py && python3 tools/zone/paint_looks.py
import os, glob, colorsys, random
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'data', 'textures', 'entity', 'zone')
WANT = 8   # looks per faction (the first + up to 7 variants)

def wide(im):
    im = im.convert('RGBA')
    if im.size == (64, 64): return im
    o = Image.new('RGBA', (64, 64), (0, 0, 0, 0)); o.paste(im, (0, 0))
    def mirror(src, dst):   # the old format's one arm / leg, flipped for the other side, faces swapped like the game does
        x, y = src; part = im.crop((x, y, x + 16, y + 16)); m = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
        for (sx, w), dx in (((0, 4), 8), ((4, 4), 4), ((8, 4), 0), ((12, 4), 12)):
            m.paste(part.crop((sx, 4, sx + w, 16)).transpose(Image.FLIP_LEFT_RIGHT), (dx, 4))
        m.paste(part.crop((4, 0, 8, 4)).transpose(Image.FLIP_LEFT_RIGHT), (4, 0)); m.paste(part.crop((8, 0, 12, 4)).transpose(Image.FLIP_LEFT_RIGHT), (8, 0))
        o.paste(m, dst)
    mirror((40, 16), (32, 48)); mirror((0, 16), (16, 48))
    return o

def headswap(a, b):   # b's head (base + overlay) on a's body
    o = a.copy(); o.paste(b.crop((0, 0, 64, 16)), (0, 0)); return o

def shade(im, rnd):   # a slightly different dye lot and wear
    dh, ds, dv = rnd.uniform(-0.02, 0.02), rnd.uniform(-0.08, 0.06), rnd.uniform(-0.09, 0.07)
    px = im.load(); o = im.copy(); po = o.load()
    for y in range(64):
        for x in range(64):
            r, g, b, a = px[x, y]
            if a == 0 or (y < 16 and 8 <= x < 16 and 8 <= y): continue   # keep the face itself
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            r2, g2, b2 = colorsys.hsv_to_rgb((h + dh) % 1, min(1, max(0, s + ds)), min(1, max(0, v + dv)))
            po[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255), a)
    return o

def main():
    groups = {}
    for p in sorted(glob.glob(os.path.join(HERE, 'skins', '*_*.png'))):
        f = os.path.basename(p).rsplit('_', 1)[0]; groups.setdefault(f, []).append(wide(Image.open(p)))
    n = 0
    for f, skins in groups.items():
        if f.startswith('suit_'):
            skins[0].save(os.path.join(OUT, 'z_%s.png' % f)); n += 1; continue
        rnd = random.Random(f); looks = list(skins)
        for a in skins:                      # heads swapped between the faction's own
            for b in skins:
                if a is not b and len(looks) < WANT: looks.append(headswap(a, b))
        i = 0
        while len(looks) < WANT and f != 'trader':   # and shades of them
            looks.append(shade(looks[i % len(skins)], rnd)); i += 1
        for k, im in enumerate(looks[:WANT] if f != 'trader' else looks[:1]):
            im.save(os.path.join(OUT, 'z_npc_%s%s.png' % (f, '' if k == 0 else '_%d' % k))); n += 1
        for k in range(len(looks), 16):      # no stale variants from an earlier run
            q = os.path.join(OUT, 'z_npc_%s_%d.png' % (f, k))
            if os.path.exists(q): os.remove(q)
    print('installed %d looks' % n)

if __name__ == '__main__': main()
