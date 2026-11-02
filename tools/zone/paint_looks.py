#!/usr/bin/env python3
# tools/zone/paint_looks.py — what you look like in the Zone with a helmet on: every suit look (z_player.png and the suit
# skins painted by paint_skins.py) with each helmet painted over its head (the overlay layer, which stands proud of the
# head): data/textures/entity/zone/z_look_<base>_<helmet>.png. sim/zone_core.cpp suitSkin() picks them.
# The default character (no suit / the starting leather jacket) is the user's own skin, tools/zone/player_skin.png: it
# is installed as z_player.png here (over paint_skins.py's painted stand-in).
# Run after paint_skins.py:  python3 tools/zone/paint_looks.py
import os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIR = os.path.join(ROOT, 'data', 'textures', 'entity', 'zone')
BASES = {'player': 'z_player.png', 'bandit': 'z_npc_bandit.png', 'drifter': 'z_npc_drifter.png', 'army': 'z_npc_army.png', 'merc': 'z_npc_merc.png',
         'opensky': 'z_npc_opensky.png', 'bastion': 'z_npc_bastion.png', 'choir': 'z_npc_choir.png', 'hazmat': 'z_suit_hazmat.png', 'exo': 'z_suit_exo.png'}
# the head's overlay faces (u, v, w, h) in a 64x64 skin
HO = {'top': (40, 0, 8, 8), 'bottom': (48, 0, 8, 8), 'right': (32, 8, 8, 8), 'front': (40, 8, 8, 8), 'left': (48, 8, 8, 8), 'back': (56, 8, 8, 8)}

def shade(c, k): return tuple(max(0, min(255, int(v * k))) for v in c[:3]) + (255,)

class Head:
    def __init__(s, im): s.im = im
    def rect(s, face, x0, y0, x1, y1, c):
        u, v, w, h = HO[face]
        for y in range(max(0, y0), min(h - 1, y1) + 1):
            for x in range(max(0, x0), min(w - 1, x1) + 1):
                s.im.putpixel((u + x, v + y), (0, 0, 0, 0) if c is None else (c if len(c) == 4 else tuple(c) + (255,)))
    def clear(s, faces=HO.keys()):
        for f in faces: s.rect(f, 0, 0, 7, 7, None)

def gasmask(h):
    rub, lens, can = (40, 42, 40), (96, 118, 108), (74, 78, 66)
    h.rect('front', 0, 2, 7, 7, rub)
    for x0 in (1, 5): h.rect('front', x0, 3, x0 + 1, 4, lens); h.rect('front', x0, 3, x0, 3, (150, 170, 160))
    h.rect('front', 3, 5, 4, 7, can); h.rect('front', 3, 6, 4, 6, shade(can, 0.7))
    for f in ('right', 'left'): h.rect(f, 0, 3, 7, 3, (30, 30, 30)); h.rect(f, 0, 5, 3, 7, rub)
    h.rect('back', 0, 3, 7, 3, (30, 30, 30))
def tactical(h):
    col, rim = (72, 78, 62), (48, 52, 42)
    h.clear()
    h.rect('top', 0, 0, 7, 7, col); h.rect('front', 0, 0, 7, 2, col); h.rect('front', 0, 2, 7, 2, rim)
    for f in ('right', 'left', 'back'): h.rect(f, 0, 0, 7, 3, col); h.rect(f, 0, 3, 7, 3, rim)
    h.rect('front', 1, 3, 6, 4, (36, 40, 38)); h.rect('front', 2, 3, 2, 3, (120, 140, 130)); h.rect('front', 5, 3, 5, 3, (120, 140, 130))   # goggles
    h.rect('front', 0, 5, 7, 7, (34, 32, 32))                                                                                         # balaclava
    for f in ('right', 'left'): h.rect(f, 2, 4, 2, 7, (40, 40, 36))
    h.rect('front', 3, 1, 4, 1, (210, 220, 200))                                                                                      # the helmet lamp
def sphere(h):
    col, rim, vis = (86, 92, 84), (58, 62, 56), (38, 52, 58)
    h.clear()
    h.rect('top', 0, 0, 7, 7, col)
    for f in ('right', 'left', 'back'): h.rect(f, 0, 0, 7, 5, col); h.rect(f, 0, 5, 7, 5, rim)
    h.rect('front', 0, 0, 7, 1, col); h.rect('front', 0, 2, 7, 5, vis); h.rect('front', 1, 2, 2, 2, (90, 120, 130)); h.rect('front', 0, 6, 7, 7, rim)
    h.rect('front', 3, 0, 4, 0, (210, 220, 200))
def psy(h):
    met, wire = (122, 122, 112), (40, 40, 38)
    h.clear()
    h.rect('top', 0, 0, 7, 7, met); h.rect('top', 2, 2, 2, 2, (200, 60, 40)); h.rect('top', 5, 5, 5, 5, (200, 60, 40))
    for f in ('right', 'left', 'back', 'front'): h.rect(f, 0, 0, 7, 1, met)
    for f in ('right', 'left'): h.rect(f, 2, 2, 3, 3, (150, 150, 140)); h.rect(f, 2, 2, 2, 2, (220, 70, 40))
    h.rect('back', 3, 2, 4, 7, wire); h.rect('front', 1, 2, 6, 2, (60, 60, 56))
HELMETS = {'gasmask': gasmask, 'helmet_tactical': tactical, 'helmet_sphere': sphere, 'helmet_psy': psy}

def main():
    n = 0
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'player_skin.png')
    if os.path.exists(src): Image.open(src).convert('RGBA').save(os.path.join(DIR, 'z_player.png'))
    for base, fn in BASES.items():
        src = os.path.join(DIR, fn)
        if not os.path.exists(src): print('missing', fn); continue
        for hk, paint in HELMETS.items():
            im = Image.open(src).convert('RGBA'); paint(Head(im))
            im.save(os.path.join(DIR, 'z_look_%s_%s.png' % (base, hk))); n += 1
    print('painted %d looks -> data/textures/entity/zone/z_look_*.png' % n)

if __name__ == '__main__': main()
