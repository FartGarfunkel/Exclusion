#!/usr/bin/env python3
# tools/zone/paint_skins.py — paints the nine Zone NPC skins (original art, 64x64, Minecraft layout, classic 4px arms) to
# data/textures/entity/zone/z_npc_<faction>.png. data/mobs.json names them ("skin": "textures/entity/zone/z_npc_x.png");
# the engine finds a bare skin path under data/ (assets/char_creator.cpp resolveCharacter) and builds the voxel character
# from it with the base layer + the overlay layer (hoods, helmets, vests, packs stand proud of the body).
# Run from the repo root:  python3 tools/zone/paint_skins.py [--preview /path/skins.png]   (idempotent, deterministic noise)
import os, random, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zonepix import sh, mix, write_png

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'data', 'textures', 'entity', 'zone')

PARTS = {'head': ((0, 0), (32, 0), 8, 8, 8), 'body': ((16, 16), (16, 32), 8, 12, 4), 'armR': ((40, 16), (40, 32), 4, 12, 4),
         'armL': ((32, 48), (48, 48), 4, 12, 4), 'legR': ((0, 16), (0, 32), 4, 12, 4), 'legL': ((16, 48), (0, 48), 4, 12, 4)}
FACES = ('top', 'bottom', 'right', 'front', 'left', 'back')

def face_rect(part, face, ov):
    (bu, bv), (ou, ovv), w, h, d = PARTS[part]; u, v = (ou, ovv) if ov else (bu, bv)
    return {'top': (u + d, v, w, d), 'bottom': (u + d + w, v, w, d), 'right': (u, v + d, d, h), 'front': (u + d, v + d, w, h),
            'left': (u + d + w, v + d, d, h), 'back': (u + 2 * d + w, v + d, w, h)}[face]

class Skin:
    def __init__(s): s.p = [None] * 4096
    def rect(s, part, face, x0, y0, x1, y1, c, ov=False):          # face-local, inclusive, clipped to the face
        fx, fy, fw, fh = face_rect(part, face, ov)
        for y in range(max(0, y0), min(fh - 1, y1) + 1):
            for x in range(max(0, x0), min(fw - 1, x1) + 1): s.p[(fy + y) * 64 + fx + x] = c
    def px(s, part, face, x, y, c, ov=False): s.rect(part, face, x, y, x, y, c, ov)
    def all(s, part, c, ov=False, faces=FACES):
        for f in faces: s.rect(part, f, 0, 0, 99, 99, c, ov)
    def sides(s, part, c, ov=False, y0=0, y1=99):                   # every side face (not top/bottom) over some rows
        for f in ('right', 'front', 'left', 'back'): s.rect(part, f, 0, y0, 99, y1, c, ov)

def figure(s, tone, hair, jac, pants, boots, gloves, cuff=None, sleeve=None):
    s.all('head', tone); s.all('head', hair, faces=('top',)); s.sides('head', hair, y1=1); s.rect('head', 'back', 0, 0, 7, 5, hair)
    s.rect('head', 'right', 0, 0, 4, 4, hair); s.rect('head', 'left', 3, 0, 7, 4, hair)
    d = sh(tone, 0.84)
    for x in (1, 2, 5, 6): s.px('head', 'front', x, 3, sh(hair, 0.9))
    for x in (2, 5): s.px('head', 'front', x, 4, (34, 28, 26))
    s.rect('head', 'front', 3, 5, 4, 5, d); s.rect('head', 'front', 3, 6, 4, 6, mix(tone, (130, 70, 64), 0.5))
    s.all('body', jac); s.rect('body', 'front', 3, 0, 4, 11, sh(jac, 0.8)); s.rect('body', 'front', 0, 9, 7, 10, sh(jac, 0.72))
    s.rect('body', 'front', 2, 0, 5, 1, sh(jac, 1.12))
    for part in ('armR', 'armL'):
        s.all(part, sleeve or jac); s.rect(part, 'front', 0, 10, 3, 11, gloves, ); [s.rect(part, f, 0, 10, 3, 11, gloves) for f in FACES[2:]]; s.all(part, gloves, faces=('bottom',))
        if cuff: s.rect(part, 'front', 0, 8, 3, 9, cuff); [s.rect(part, f, 0, 8, 3, 9, cuff) for f in ('right', 'left', 'back')]
    for part in ('legR', 'legL'):
        s.all(part, pants); s.rect(part, 'front', 3, 0, 3, 8, sh(pants, 0.85)) if part == 'legR' else s.rect(part, 'front', 0, 0, 0, 8, sh(pants, 0.85))
        for f in ('right', 'front', 'left', 'back'): s.rect(part, f, 0, 9, 3, 11, boots)
        s.all(part, boots, faces=('bottom',))

# --- overlay layer pieces
def hood(s, col, open_face=True, depth=2):
    s.all('head', col, True, faces=('top', 'right', 'left', 'back')); s.rect('head', 'front', 0, 0, 7, 7, col, True)
    if open_face: s.rect('head', 'front', 1, 2, 6, 7, None, True)
    s.rect('head', 'front', 0, 0, 7, 1, sh(col, 1.1), True)
def helmet(s, col, rim=None, visor=None, strap=None):
    s.all('head', col, True, faces=('top',)); s.rect('head', 'front', 0, 0, 7, 2, col, True)
    for f in ('right', 'left', 'back'): s.rect('head', f, 0, 0, 7, 3, col, True)
    if rim: s.rect('head', 'front', 0, 3, 7, 3, rim, True)
    if visor: s.rect('head', 'front', 0, 3, 7, 5, visor, True)
    if strap: [s.rect('head', f, 2, 4, 2, 7, strap, True) for f in ('right', 'left')]
def lowermask(s, col, rows=(5, 7)):
    s.rect('head', 'front', 0, rows[0], 7, rows[1], col, True)
    for f in ('right', 'left'): s.rect('head', f, 0, rows[0], 7, rows[1], col, True)
def vest(s, col, pouch=None, back=True):
    s.rect('body', 'front', 0, 0, 7, 9, col, True); s.rect('body', 'right', 0, 0, 3, 9, col, True); s.rect('body', 'left', 0, 0, 3, 9, col, True)
    if back: s.rect('body', 'back', 0, 0, 7, 9, col, True)
    s.rect('body', 'front', 3, 0, 4, 9, sh(col, 0.75), True)
    if pouch:
        for x in (0, 2, 5): s.rect('body', 'front', x, 5, x + 1, 8, pouch, True); s.rect('body', 'front', x, 5, x + 1, 5, sh(pouch, 1.25), True)
def pack(s, col, strap):
    s.rect('body', 'back', 0, 0, 7, 11, col, True); s.rect('body', 'back', 1, 3, 6, 6, sh(col, 0.8), True); s.rect('body', 'back', 3, 0, 4, 2, sh(col, 1.2), True)
    s.rect('body', 'front', 1, 0, 1, 4, strap, True); s.rect('body', 'front', 6, 0, 6, 4, strap, True)

def camo(s, parts, cols, rnd, cover=0.45):
    for part in parts:
        for f in FACES:
            fx, fy, fw, fh = face_rect(part, f, False)
            for y in range(fh):
                for x in range(fw):
                    if s.p[(fy + y) * 64 + fx + x] is not None and rnd.random() < cover and ((x // 2 + y // 2) % 3 != 0 or rnd.random() < 0.3): s.p[(fy + y) * 64 + fx + x] = rnd.choice(cols)

# --- the nine
def drifter(s, r):
    figure(s, (190, 150, 120), (70, 52, 38), (98, 104, 70), (72, 68, 50), (54, 42, 32), (66, 60, 46), cuff=(74, 80, 54))
    hood(s, (92, 98, 66)); lowermask(s, (96, 82, 58), (6, 7)); pack(s, (86, 74, 52), (60, 52, 38))
    for x in (1, 5): s.rect('body', 'front', x, 3, x + 1, 5, (78, 84, 54), True)
    s.rect('body', 'front', 0, 9, 7, 9, (70, 58, 42), True); s.rect('body', 'front', 5, 9, 6, 11, (62, 52, 38), True)
def bandit(s, r):
    figure(s, (176, 138, 112), (34, 30, 28), (34, 32, 33), (40, 44, 56), (28, 26, 26), (30, 28, 28))
    s.rect('body', 'front', 3, 0, 3, 11, (140, 140, 136)); s.rect('body', 'front', 0, 2, 7, 2, (56, 52, 50), True)
    helmet(s, (44, 44, 46), rim=(30, 30, 30)); lowermask(s, (38, 36, 36), (5, 7))
    for part in ('legR', 'legL'):
        s.rect(part, 'right', 1, 0, 1, 8, (170, 170, 160)); s.rect(part, 'left', 2, 0, 2, 8, (170, 170, 160))
    s.rect('armR', 'front', 0, 4, 3, 5, (120, 36, 32), True); s.rect('armR', 'right', 0, 4, 3, 5, (120, 36, 32), True); s.rect('armR', 'left', 0, 4, 3, 5, (120, 36, 32), True); s.rect('armR', 'back', 0, 4, 3, 5, (120, 36, 32), True)
    s.rect('body', 'front', 0, 0, 7, 0, (24, 22, 22), True)
def army(s, r):
    figure(s, (186, 146, 118), (60, 46, 36), (78, 86, 58), (72, 80, 54), (30, 28, 26), (34, 32, 30), cuff=(66, 74, 48))
    camo(s, ('body', 'armR', 'armL', 'legR', 'legL'), [(60, 68, 46), (96, 100, 70), (52, 56, 38)], r)
    vest(s, (62, 68, 50), pouch=(70, 76, 54)); helmet(s, (70, 78, 52), rim=(48, 54, 36), strap=(40, 40, 34)); s.rect('head', 'front', 2, 8, 5, 8, (0, 0, 0), True)
    s.rect('head', 'front', 0, 3, 7, 3, (50, 56, 38), True)
    for part in ('legR', 'legL'): s.rect(part, 'front', 0, 5, 3, 6, (50, 56, 38), True); s.rect(part, 'front', 0, 5, 3, 6, (50, 56, 38), True)
def bastion(s, r):
    figure(s, (170, 134, 110), (30, 26, 26), (44, 38, 36), (46, 40, 38), (30, 26, 24), (34, 30, 28))
    plate = (104, 46, 40); dk = (66, 28, 26)
    s.rect('body', 'front', 0, 0, 7, 9, plate, True); s.rect('body', 'back', 0, 0, 7, 9, plate, True); s.rect('body', 'right', 0, 0, 3, 9, dk, True); s.rect('body', 'left', 0, 0, 3, 9, dk, True)
    s.rect('body', 'front', 3, 0, 4, 9, dk, True); s.rect('body', 'front', 0, 5, 7, 5, dk, True)
    for part in ('armR', 'armL'):
        for f in ('front', 'right', 'left', 'back'): s.rect(part, f, 0, 0, 3, 3, plate, True); s.rect(part, f, 0, 4, 3, 4, dk, True)
        s.all(part, plate, True, faces=('top',))
    for part in ('legR', 'legL'): [s.rect(part, f, 0, 4, 3, 8, dk, True) for f in ('front', 'right', 'left', 'back')]; s.rect(part, 'front', 0, 5, 3, 6, plate, True)
    helmet(s, plate, rim=dk, visor=(24, 20, 20)); s.rect('head', 'front', 0, 6, 7, 7, dk, True); s.rect('head', 'front', 1, 4, 2, 4, (150, 60, 40), True); s.rect('head', 'front', 5, 4, 6, 4, (150, 60, 40), True)
def opensky(s, r):
    figure(s, (186, 148, 122), (96, 70, 44), (130, 136, 62), (92, 100, 54), (52, 50, 40), (60, 66, 44), cuff=(70, 84, 48))
    for part in ('armR', 'armL'):
        for f in ('front', 'right', 'left', 'back'): s.rect(part, f, 1, 0, 1, 7, (204, 204, 176)); s.rect(part, f, 0, 0, 0, 7, (68, 84, 50)) if False else None
    s.rect('body', 'front', 0, 0, 1, 11, (66, 82, 48)); s.rect('body', 'front', 6, 0, 7, 11, (66, 82, 48)); s.rect('body', 'front', 0, 6, 7, 6, (204, 204, 176))
    hood(s, (126, 132, 60)); s.rect('head', 'front', 1, 3, 6, 4, (34, 38, 30), True); s.rect('head', 'front', 2, 3, 3, 3, (206, 160, 60), True); s.rect('head', 'front', 4, 3, 5, 3, (206, 160, 60), True)
    for part in ('legR', 'legL'): [s.rect(part, f, 1, 0, 1, 8, (204, 204, 176)) for f in ('right', 'left')]
def merc(s, r):
    figure(s, (180, 142, 116), (40, 36, 32), (84, 98, 116), (80, 94, 112), (118, 98, 74), (36, 38, 42), cuff=(60, 70, 86))
    camo(s, ('body', 'armR', 'armL', 'legR', 'legL'), [(58, 70, 88), (122, 132, 142), (68, 80, 96)], r, 0.6)
    vest(s, (44, 48, 54), pouch=(54, 58, 64)); helmet(s, (88, 100, 112), rim=(54, 62, 72)); lowermask(s, (34, 36, 40), (4, 7))
    s.rect('head', 'front', 3, 1, 4, 2, (30, 34, 38), True)
    for part in ('legR', 'legL'): s.rect(part, 'front', 0, 5, 3, 6, (40, 44, 50), True)
def choir(s, r):
    robe = (158, 158, 148)
    figure(s, (200, 196, 182), (150, 150, 140), robe, robe, (130, 130, 122), (176, 172, 160), cuff=(140, 140, 132))
    for f in FACES[2:]: s.rect('legR', f, 0, 0, 3, 11, sh(robe, 0.94)); s.rect('legL', f, 0, 0, 3, 11, sh(robe, 0.94))
    s.rect('body', 'front', 3, 0, 4, 11, (136, 136, 128)); s.rect('body', 'front', 3, 2, 4, 8, (44, 42, 42)); s.rect('body', 'front', 1, 4, 6, 5, (44, 42, 42))
    hood(s, (146, 146, 136), open_face=False); s.rect('head', 'front', 1, 2, 6, 7, (22, 20, 20), True); s.rect('head', 'front', 2, 3, 5, 6, (196, 192, 178), True)
    s.rect('head', 'front', 2, 4, 2, 4, (20, 18, 18), True); s.rect('head', 'front', 5, 4, 5, 4, (20, 18, 18), True); s.rect('head', 'front', 3, 6, 4, 6, (120, 116, 108), True)
    s.rect('body', 'front', 0, 9, 7, 9, (96, 94, 88), True)
    for part in ('legR', 'legL'): [s.rect(part, f, 0, 0, 3, 8, robe, True) for f in ('front', 'right', 'left', 'back')]
def scientist(s, r):
    coat = (176, 176, 168)
    figure(s, (190, 152, 128), (128, 124, 118), (84, 88, 96), (56, 58, 64), (36, 34, 34), (150, 156, 70))
    s.rect('body', 'front', 0, 0, 7, 11, coat, True); s.rect('body', 'back', 0, 0, 7, 11, coat, True); s.rect('body', 'right', 0, 0, 3, 11, coat, True); s.rect('body', 'left', 0, 0, 3, 11, coat, True)
    s.rect('body', 'front', 3, 0, 4, 11, (140, 140, 134), True); s.rect('body', 'front', 0, 7, 2, 9, (150, 150, 144), True); s.rect('body', 'front', 5, 7, 7, 9, (150, 150, 144), True); s.rect('body', 'front', 1, 4, 2, 5, (60, 110, 120), True)
    for part in ('armR', 'armL'): [s.rect(part, f, 0, 0, 3, 8, coat, True) for f in FACES[2:]]
    for part in ('legR', 'legL'): [s.rect(part, f, 0, 0, 3, 4, coat, True) for f in ('front', 'right', 'left', 'back')]
    s.rect('head', 'front', 1, 4, 3, 4, (50, 50, 56), True); s.rect('head', 'front', 4, 4, 6, 4, (50, 50, 56), True); s.rect('head', 'front', 1, 5, 3, 5, (150, 170, 176), True); s.rect('head', 'front', 4, 5, 6, 5, (150, 170, 176), True)
    s.rect('head', 'front', 0, 4, 0, 4, (50, 50, 56), True); s.rect('head', 'front', 7, 4, 7, 4, (50, 50, 56), True)
def trader(s, r):
    wool = (118, 84, 62)
    figure(s, (200, 146, 118), (96, 80, 66), wool, (54, 50, 46), (42, 34, 28), (96, 74, 54), sleeve=wool)
    for y in range(0, 12, 3):
        for part in ('body', 'armR', 'armL'): [s.rect(part, f, 0, y, 7, y, (146, 108, 80)) for f in FACES[2:]]
    s.rect('body', 'front', 0, 5, 7, 9, sh(wool, 1.12)); s.rect('body', 'front', 1, 6, 6, 8, sh(wool, 1.2)); s.rect('body', 'front', 0, 10, 7, 11, sh(wool, 0.78)); s.rect('body', 'front', 3, 0, 4, 0, (170, 150, 120))
    s.rect('body', 'front', 1, 3, 6, 11, (78, 62, 46), True); s.rect('body', 'front', 1, 3, 6, 3, (60, 50, 38), True); s.rect('body', 'front', 2, 8, 5, 10, (68, 54, 40), True)
    s.rect('head', 'front', 0, 0, 7, 7, (96, 80, 66), True); s.rect('head', 'front', 1, 2, 6, 4, None, True); s.rect('head', 'front', 0, 5, 7, 7, (110, 92, 76), True); s.rect('head', 'front', 3, 5, 4, 5, (150, 90, 80), True)
    for f in ('right', 'left', 'back'): s.rect('head', f, 0, 0, 7, 3, (72, 58, 46), True); s.rect('head', f, 0, 5, 7, 7, (110, 92, 76), True)
    s.all('head', (72, 58, 46), True, faces=('top',)); s.rect('head', 'front', 0, 0, 7, 1, (72, 58, 46), True)
    s.rect('head', 'front', 0, 4, 7, 4, (200, 146, 118), True) if False else None
    for x in (2, 5): s.px('head', 'front', x, 3, (34, 28, 26), True)
# --- the player's looks (original): the default (and the leather jacket) is a lone stalker in a worn leather jacket,
# hood up over a balaclava, olive cargo trousers, fingerless gloves, a small rucksack and a strap across the chest;
# the hazmat and exoskeleton suits have their own (the others wear the factions' suits: sim/zone_core.cpp suitSkin)
def player(s, r):
    jac = (70, 54, 42)
    figure(s, (184, 146, 120), (52, 42, 34), jac, (78, 80, 62), (38, 32, 26), (34, 32, 30), cuff=(50, 40, 32), sleeve=jac)
    for part in ('body', 'armR', 'armL'):
        for f in FACES[2:]:
            for y in range(12):
                if r.random() < 0.18: s.rect(part, f, r.randrange(4), y, r.randrange(4, 8), y, sh(jac, 1.18))   # scuffed leather
    s.rect('body', 'front', 3, 0, 4, 10, (40, 32, 26)); s.rect('body', 'front', 3, 0, 4, 0, (150, 150, 140))           # zip
    s.rect('body', 'front', 1, 0, 6, 1, (58, 46, 36), True); s.rect('body', 'front', 0, 2, 0, 3, (58, 46, 36), True); s.rect('body', 'front', 7, 2, 7, 3, (58, 46, 36), True)  # collar
    for i in range(8): s.px('body', 'front', i, 1 + i, (44, 40, 34), True); s.px('body', 'back', 7 - i, 1 + i, (44, 40, 34), True)   # the strap
    s.rect('body', 'front', 0, 10, 7, 10, (46, 40, 32), True); s.rect('body', 'front', 1, 10, 2, 11, (60, 52, 40), True)            # belt, pouch
    for part in ('legR', 'legL'):
        s.rect(part, 'right' if part == 'legR' else 'left', 0, 4, 3, 7, (70, 72, 56), True); s.rect(part, 'right' if part == 'legR' else 'left', 0, 4, 3, 4, (88, 90, 70), True)  # cargo pockets
        s.rect(part, 'front', 0, 6, 3, 6, (64, 66, 52))
    hood(s, (76, 78, 66)); lowermask(s, (44, 42, 40), (5, 7)); s.rect('head', 'front', 1, 4, 6, 4, (44, 42, 40), True)
    s.rect('head', 'front', 1, 4, 6, 4, None, True)
    pack(s, (74, 70, 54), (46, 42, 34))
def suit_hazmat(s, r):
    suit = (188, 150, 44)
    figure(s, (190, 150, 126), (60, 50, 40), suit, suit, (30, 30, 30), (34, 34, 34), cuff=(40, 40, 40), sleeve=suit)
    for part in ('body', 'armR', 'armL', 'legR', 'legL'):
        for f in FACES[2:]: s.rect(part, f, 0, 5, 7, 5, sh(suit, 0.82))
    s.rect('body', 'front', 2, 2, 5, 4, (60, 62, 58)); s.rect('body', 'front', 3, 3, 4, 3, (200, 60, 40))
    hood(s, sh(suit, 0.96), open_face=False); s.rect('head', 'front', 1, 2, 6, 4, (52, 70, 72), True); s.rect('head', 'front', 1, 2, 2, 2, (120, 150, 150), True)
    s.rect('head', 'front', 2, 5, 5, 7, (36, 36, 36), True); s.rect('head', 'front', 3, 6, 4, 7, (70, 70, 66), True)
    pack(s, (120, 124, 120), (40, 40, 40))
def suit_exo(s, r):
    plate, dk, cloth = (78, 82, 76), (46, 48, 46), (70, 74, 56)
    figure(s, (176, 140, 116), (40, 36, 32), cloth, cloth, (34, 34, 32), (40, 40, 38), cuff=dk)
    s.rect('body', 'front', 0, 0, 7, 10, plate, True); s.rect('body', 'back', 0, 0, 7, 11, plate, True); s.rect('body', 'right', 0, 0, 3, 10, dk, True); s.rect('body', 'left', 0, 0, 3, 10, dk, True)
    s.rect('body', 'front', 0, 4, 7, 4, dk, True); s.rect('body', 'front', 3, 5, 4, 10, dk, True); s.rect('body', 'back', 2, 2, 5, 8, (60, 62, 58), True)
    for part in ('armR', 'armL'):
        for f in ('front', 'right', 'left', 'back'): s.rect(part, f, 0, 0, 3, 4, plate, True); s.rect(part, f, 0, 6, 3, 9, plate, True); s.rect(part, f, 0, 5, 3, 5, dk, True)
        s.all(part, plate, True, faces=('top',))
    for part in ('legR', 'legL'):
        for f in ('front', 'right', 'left', 'back'): s.rect(part, f, 0, 0, 3, 3, plate, True); s.rect(part, f, 0, 5, 3, 11, plate, True); s.rect(part, f, 0, 4, 3, 4, dk, True)
    helmet(s, plate, rim=dk, visor=(30, 34, 32)); s.rect('head', 'front', 0, 6, 7, 7, dk, True); s.rect('head', 'front', 1, 4, 6, 4, (110, 150, 120), True)
PLAYER = {'player': player, 'suit_hazmat': suit_hazmat, 'suit_exo': suit_exo}

SKINS = {'drifter': drifter, 'bandit': bandit, 'army': army, 'bastion': bastion, 'opensky': opensky, 'merc': merc, 'choir': choir, 'scientist': scientist, 'trader': trader}

def finish(s, rnd):
    for i, c in enumerate(s.p):
        if c is not None:
            f = 1 + rnd.uniform(-0.055, 0.055)
            c = sh(c, f)
            if rnd.random() < 0.03: c = sh(c, 0.78)                 # grime
            s.p[i] = mix(c, (112, 104, 90), 0.08)
    # the base layer is always solid: anything left empty there becomes the nearest part colour
    for part, ((bu, bv), _, w, h, d) in PARTS.items():
        for f in FACES:
            fx, fy, fw, fh = face_rect(part, f, False)
            for y in range(fh):
                for x in range(fw):
                    if s.p[(fy + y) * 64 + fx + x] is None: s.p[(fy + y) * 64 + fx + x] = (90, 90, 84)

def preview(skins, path):
    from zonepix import write_png as wp
    S = 6; W = 18 * len(skins) * S; H = 34 * S; px = [(70, 68, 64)] * (W * H)
    place = {'head': (4, 0, 8, 8), 'body': (4, 8, 8, 12), 'armR': (0, 8, 4, 12), 'armL': (12, 8, 4, 12), 'legR': (4, 20, 4, 12), 'legL': (8, 20, 4, 12)}
    for n, s in enumerate(skins):
        for part, (ox, oy, w, h) in place.items():
            for ov in (False, True):
                fx, fy, fw, fh = face_rect(part, 'front', ov)
                for y in range(fh):
                    for x in range(fw):
                        c = s.p[(fy + y) * 64 + fx + x]
                        if c is None: continue
                        for yy in range(S):
                            for xx in range(S): px[((oy + y) * S + yy + S) * W + (n * 18 + ox + x + 1) * S + xx] = c
    wp(path, W, H, px)

def main():
    os.makedirs(OUT, exist_ok=True); made = []
    for name, fn in SKINS.items():
        s = Skin(); rnd = random.Random(zlib.crc32(name.encode())); fn(s, rnd); finish(s, rnd)
        write_png(os.path.join(OUT, 'z_npc_%s.png' % name), 64, 64, s.p); made.append(s)
    for name, fn in PLAYER.items():
        s = Skin(); rnd = random.Random(zlib.crc32(name.encode())); fn(s, rnd); finish(s, rnd)
        write_png(os.path.join(OUT, 'z_%s.png' % name), 64, 64, s.p); made.append(s)
    print('painted %d skins -> data/textures/entity/zone/z_npc_*.png, z_player.png, z_suit_*.png' % len(made))
    if '--preview' in sys.argv: preview(made, sys.argv[sys.argv.index('--preview') + 1])

if __name__ == '__main__': main()
