#!/usr/bin/env python3
# tools/zone/paint_items.py — paints one 32x32 sprite per Zone item (data/zone_items.json) and registers it as the tile
# "zone_<id>" (the "icon" tools/zone/catalog.py already wrote into data/items.json).
#   * PNGs go to data/textures/item/zone_<id>.png (the engine loads every tile as a picture listed in data/tiles.json "files")
#   * tiles.json gets names["zone_<id>"] = index, files[index], res[index] = 32, count; itemshapes.json itemTiles gets the name
#   * idempotent: re-running repaints the PNGs (deterministic noise) and only touches the JSON if something is missing
# Run from the repo root:  python3 tools/zone/paint_items.py [--sheet /path/sprites.png] [--no-register]
# Look: muted, worn, Misery/STALCRAFT-ish. Light from the top left, 1px dark outline, a little noise and bare-metal wear.
# The engine sculpts each item into a 3D voxel model from its picture, so silhouettes matter more than inner detail.
import json, math, os, random, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zonepix import Cv, finish, sh, mix, bez, write_png

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, 'data')

# ---- palette (all muted) ----
BLK = (34, 35, 37); GM = (50, 53, 56); DS = (74, 78, 80); ST = (112, 116, 117); LS = (154, 156, 152)
WOOD = (134, 88, 50); DW = (90, 58, 36); PLUM = (104, 52, 52); TAN = (152, 132, 98); OD = (80, 86, 60); DOD = (56, 64, 46)
KHA = (130, 122, 82); BR = (160, 132, 74); CU = (150, 92, 56); RU = (124, 68, 42); CR = (198, 190, 166)
RD = (150, 50, 44); DR = (100, 36, 32); GR = (110, 110, 106); PAP = (176, 168, 140)

PAINT = {}
def sp(*ids, **kw):
    def d(f):
        for i in ids: PAINT[i] = (f, kw)
        return f
    return d

# ======================================================================= guns (side view, muzzle to the right)
def tguard(c, x0, x1, y, col=DS):
    c.line(x0, y, x0, y + 2, col); c.line(x0, y + 2, x1, y + 2, col); c.line(x1, y + 2, x1, y, col); c.put((x0 + x1) // 2, y, GM)
def grip(c, x, y, col, ln=8, w=4, rake=3): c.poly([(x, y), (x + w, y), (x + w - rake, y + ln), (x - rake, y + ln)], col, 0.14)
def magc(c, x, y, ln, w, col, fwd=4):
    c.poly([(x, y), (x + w, y), (x + w + fwd, y + ln), (x + fwd, y + ln)], col, 0.12)
    for i in range(3, ln, 3):
        xx = int(x + fwd * i / ln); c.line(xx, y + i, xx + w, y + i, sh(col, 0.72))
    c.line(x + fwd, y + ln, x + w + fwd, y + ln, sh(col, 0.55))
def bipod(c, x, y, L=7, col=DS):
    c.line(x, y, x - 3, y + L, col); c.line(x, y, x + 3, y + L, col); c.line(x - 1, y + 2, x + 1, y + 2, sh(col, 1.2))
def optic(c, kind, x, yb):                       # stands on the rail row yb, drawn upwards
    if kind == 'rd':
        c.rect(x, yb - 1, x + 8, yb - 1, DS, 0); c.rect(x + 1, yb - 4, x + 7, yb - 2, GM); c.rect(x + 5, yb - 6, x + 7, yb - 5, GM)
        c.put(x + 8, yb - 4, (150, 44, 38)); c.put(x + 8, yb - 3, (150, 44, 38))
    elif kind == 'pso':
        c.rect(x + 3, yb - 2, x + 9, yb - 1, DS, 0); c.cyl(x, yb - 7, x + 14, yb - 3, GM); c.rect(x - 1, yb - 8, x + 1, yb - 2, DS)
        c.rect(x + 11, yb - 8, x + 15, yb - 2, DS); c.put(x + 15, yb - 5, (96, 130, 140)); c.put(x + 6, yb - 8, DS)
    elif kind == 'acog':
        c.rect(x + 1, yb - 2, x + 9, yb - 1, DS, 0); c.cyl(x, yb - 7, x + 10, yb - 3, GM); c.rect(x + 8, yb - 8, x + 11, yb - 2, DS)
        c.rect(x + 3, yb - 9, x + 6, yb - 8, (190, 118, 44), 0); c.put(x + 11, yb - 5, (96, 130, 140))
    else:                                         # 8x scope
        c.rect(x + 2, yb - 2, x + 5, yb - 1, DS, 0); c.rect(x + 11, yb - 2, x + 14, yb - 1, DS, 0)
        c.cyl(x, yb - 8, x + 17, yb - 3, GM); c.rect(x + 13, yb - 9, x + 18, yb - 2, DS); c.rect(x - 1, yb - 8, x + 1, yb - 3, DS)
        c.rect(x + 7, yb - 11, x + 9, yb - 9, DS, 0); c.put(x + 18, yb - 6, (96, 130, 140))

def pistol(c, slide, frame, gripc, L=24, hh=6, y0=8, hammer=False, ring=False, wood=False, rail=False):
    x1 = 29; x0 = x1 - L; gy = y0 + hh
    c.rect(x0 + 3, gy, x1 - 4, gy + 2, frame)
    c.poly([(x0 + 1, gy), (x0 + 9, gy), (x0 + 7, gy + 13), (x0 - 1, gy + 12)], gripc, 0.12)
    if wood: c.poly([(x0 + 2, gy + 2), (x0 + 6, gy + 2), (x0 + 5, gy + 10), (x0 + 1, gy + 9)], sh(gripc, 1.2), 0.1)
    else:
        for i in range(3, 12, 2): c.put(x0 + 3 - i // 5, gy + i, sh(gripc, 0.7)); c.put(x0 + 5 - i // 5, gy + i, sh(gripc, 0.7))
    c.line(x0 - 1, gy + 12, x0 + 7, gy + 13, sh(frame, 0.6))
    tx = x0 + L // 2 - 1
    c.line(tx, gy + 3, tx, gy + 5, frame); c.line(tx, gy + 5, tx + 5, gy + 5, frame); c.line(tx + 5, gy + 5, tx + 5, gy + 2, frame); c.put(tx + 2, gy + 3, BLK)
    if rail: c.line(x1 - 10, gy + 2, x1 - 4, gy + 2, sh(frame, 0.55)); c.put(x1 - 7, gy + 1, BLK)
    c.rect(x0, y0, x1, y0 + hh - 1, slide, 0.2)
    c.rect(x1 - 1, y0 + 1, x1, y0 + hh - 2, sh(slide, 0.55), 0)
    for i in range(3): c.line(x0 + 2 + i, y0 + 1, x0 + 2 + i, y0 + hh - 2, sh(slide, 0.66))
    px = x0 + L // 2 - 1; c.rect(px, y0 + 1, px + 4, y0 + 2, sh(slide, 0.5), 0)
    c.put(x0, y0 - 1, DS); c.put(x0 + 1, y0 - 1, DS); c.put(x1 - 1, y0 - 1, DS)
    if hammer: c.rect(x0 - 2, y0, x0 - 1, y0 + 2, DS); c.put(x0 - 2, y0 - 1, DS)
    if ring: c.ring(x0 - 1, gy + 13, 2, 1, DS)

@sp('pm')
def _(c, k, r): pistol(c, (84, 86, 88), (80, 82, 84), (48, 34, 30), L=21, ring=True)
@sp('fort12')
def _(c, k, r): pistol(c, (52, 54, 56), (50, 52, 54), (44, 44, 44), L=23)
@sp('mp443')
def _(c, k, r): pistol(c, (72, 76, 78), (40, 42, 44), (40, 40, 42), L=24, hh=7, rail=True)
@sp('g17')
def _(c, k, r): pistol(c, (46, 47, 49), (62, 64, 58), (54, 56, 52), L=24, rail=True)
@sp('m1911')
def _(c, k, r): pistol(c, (62, 66, 68), (76, 78, 80), (100, 62, 38), L=26, hammer=True, wood=True)

def ak(c, fur, mcol, gcol, stock='wood', cy=12, hg1=26, brl=30, mlen=11, mw=5, mfwd=4, steel=DS, brake=True, box=False, tall=False, bip=False):
    rx0 = 10
    if stock == 'wood': c.poly([(1, cy + 1), (rx0, cy - 2), (rx0, cy + 3), (1, cy + 8)], fur, 0.12); c.rect(1, cy + 1, 2, cy + 8, sh(fur, 0.55), 0)
    elif stock == 'poly':
        c.poly([(1, cy + 1), (rx0, cy - 2), (rx0, cy + 3), (1, cy + 8)], fur, 0.12); c.hole(4, cy + 4, 7, cy + 4); c.rect(1, cy + 1, 2, cy + 8, sh(fur, 0.55), 0)
    else:                                         # side-folding wire stock
        c.line(1, cy + 5, rx0, cy - 1, DS); c.line(1, cy + 6, rx0, cy + 3, DS); c.rect(1, cy + 4, 2, cy + 8, DS, 0)
    if box: mags(c, 20, cy + 2, mlen, mw, mcol, mfwd)
    else: magc(c, 20, cy + 2, mlen, mw, mcol, mfwd)
    grip(c, 14, cy + 2, gcol, 8, 4, 3); tguard(c, 16, 19, cy + 3)
    c.rect(rx0, cy - 3, 19, cy + 2, steel, 0.18); c.line(rx0 + 1, cy - 1, 18, cy - 1, sh(steel, 0.7)); c.rect(17, cy - 5, 18, cy - 4, steel, 0)
    c.cyl(19, cy - 4, hg1 + 1, cy - 3, steel)
    c.rect(19, cy - 2, hg1, cy + (4 if tall else 3), fur, 0.15); c.line(20, cy + 1, hg1 - 1, cy + 1, sh(fur, 0.8))
    c.cyl(hg1, cy - 1, brl, cy, steel); c.rect(hg1 + 1, cy - 4, hg1 + 2, cy + 1, steel, 0); c.line(hg1 + 2, cy - 6, hg1 + 2, cy - 5, steel)
    if brake: c.rect(brl - 1, cy - 2, brl, cy + 1, sh(steel, 0.7), 0)
    if bip: bipod(c, brl - 5, cy + 1, 8)

def mags(c, x, y, ln, w, col, lean=1): magc(c, x, y, ln, w, col, lean)

@sp('akm')
def _(c, k, r): ak(c, WOOD, (78, 70, 62), (64, 44, 32))
@sp('ak74m')
def _(c, k, r): ak(c, PLUM, PLUM, (42, 38, 38), stock='poly', mlen=12)
@sp('aks74u')
def _(c, k, r):
    ak(c, WOOD, (78, 70, 62), (64, 44, 32), stock='fold', hg1=23, brl=26, brake=False)
    c.poly([(26, 10), (30, 10), (30, 13), (26, 13)], sh(DS, 0.8), 0.1); c.line(27, 11, 29, 11, sh(DS, 1.3))
@sp('an94')
def _(c, k, r): ak(c, (42, 46, 44), (50, 54, 52), (40, 42, 42), stock='poly', hg1=27, tall=True, steel=(66, 70, 70))
@sp('rpk')
def _(c, k, r): ak(c, WOOD, (90, 62, 44), (64, 44, 32), hg1=24, mlen=13, mw=5, mfwd=5, bip=True)
@sp('saiga')
def _(c, k, r): ak(c, (46, 48, 46), (54, 56, 54), (40, 42, 40), stock='poly', hg1=24, brl=28, mlen=9, mw=6, mfwd=2, box=True, steel=(58, 62, 62))

def m4(c, body, dark, magcol, cy=12, rx1=20, hg1=27, brl=30, stock='tele', opt=None, mlen=10, mw=5, fwd=1, hgcol=None):
    hgcol = hgcol or body
    if stock == 'tele': c.poly([(1, cy - 1), (8, cy - 2), (8, cy + 3), (1, cy + 5)], dark, 0.12); c.rect(8, cy - 1, 11, cy + 1, sh(dark, 0.9), 0.1)
    elif stock == 'box': c.poly([(1, cy - 2), (11, cy - 3), (11, cy + 3), (1, cy + 6)], dark, 0.12)
    elif stock == 'wire': c.line(1, cy + 4, 10, cy - 1, DS); c.line(1, cy + 5, 10, cy + 3, DS); c.rect(1, cy + 3, 2, cy + 7, DS, 0)
    grip(c, 13, cy + 3, dark, 8, 4, 3)
    mags(c, 21, cy + 3, mlen, mw, magcol, fwd)
    tguard(c, 17, 20, cy + 3)
    c.rect(11, cy + 2, 20, cy + 4, body); c.rect(11, cy - 3, rx1, cy + 2, body, 0.18)
    for x in range(11, hg1 + 1): c.put(x, cy - 4, DS if x % 2 == 0 else GM)
    c.line(14, cy - 1, 17, cy - 1, sh(body, 0.55))
    c.rect(rx1, cy - 2, hg1, cy + 2, hgcol, 0.15)
    for x in range(rx1 + 1, hg1, 2): c.line(x, cy - 2, x, cy + 2, sh(hgcol, 0.68))
    c.cyl(hg1, cy - 1, brl, cy, DS); c.rect(brl - 1, cy - 2, brl, cy + 1, GM, 0); c.line(hg1 - 2, cy - 6, hg1 - 2, cy - 5, DS)
    if opt: optic(c, opt, 13, cy - 4)

@sp('m416')
def _(c, k, r): m4(c, (44, 46, 48), (40, 42, 44), (52, 54, 54), cy=13)
@sp('scarl')
def _(c, k, r): m4(c, (148, 128, 94), (132, 114, 84), (60, 60, 56), cy=13, stock='box', rx1=22, hg1=28, mlen=11, fwd=2, hgcol=(140, 122, 90))
@sp('mk14')
def _(c, k, r): m4(c, (42, 44, 46), (136, 118, 86), (52, 54, 54), cy=14, stock='box', hg1=28, brl=30, opt='acog', mlen=9)
@sp('g36')
def _(c, k, r):
    cy = 12
    m4(c, (62, 64, 62), (52, 54, 52), (70, 72, 70), cy=cy, stock='wire', hg1=25, brl=29, mlen=11, fwd=2)
    c.rect(11, cy - 7, 20, cy - 5, (52, 54, 52), 0.1); c.hole(14, cy - 6, 17, cy - 6); c.line(21, cy - 6, 21, cy - 3, (52, 54, 52)); c.rect(10, cy - 5, 21, cy - 4, GM, 0)
@sp('ump45')
def _(c, k, r):
    cy = 12
    c.line(1, cy + 4, 9, cy - 1, DS); c.line(1, cy + 5, 9, cy + 3, DS); c.rect(1, cy + 3, 2, cy + 7, DS, 0)
    grip(c, 12, cy + 3, (52, 54, 54), 8, 4, 3); tguard(c, 16, 19, cy + 3)
    mags(c, 19, cy + 3, 11, 5, (74, 76, 74), 0)
    c.rect(9, cy - 3, 21, cy + 3, (58, 60, 62), 0.18); c.rect(21, cy - 2, 27, cy + 3, (50, 52, 54), 0.12)
    for x in range(9, 27): c.put(x, cy - 4, DS if x % 2 == 0 else GM)
    c.cyl(27, cy - 1, 30, cy + 1, DS); c.line(11, cy - 1, 18, cy - 1, (36, 38, 40))
@sp('mp5')
def _(c, k, r):
    cy = 12
    c.poly([(1, cy - 1), (9, cy - 2), (9, cy + 1), (1, cy + 5)], (52, 54, 56), 0.12); c.rect(1, cy - 1, 2, cy + 5, GM, 0)
    magc(c, 20, cy + 2, 11, 4, (58, 60, 60), 3)
    grip(c, 12, cy + 2, (40, 42, 44), 8, 4, 3); tguard(c, 16, 19, cy + 3)
    c.rect(9, cy - 3, 20, cy + 2, (42, 44, 46), 0.18); c.put(11, cy - 4, DS); c.put(12, cy - 4, DS)
    c.cyl(20, cy - 2, 27, cy + 3, (50, 52, 54));
    for x in (22, 24, 26): c.line(x, cy, x, cy + 2, GM)
    c.cyl(27, cy - 1, 30, cy + 1, DS); c.line(28, cy - 4, 28, cy - 2, DS); c.ring(28, cy - 1, 2, 1, GM)
@sp('pp19')
def _(c, k, r):
    cy = 11
    c.line(1, cy + 5, 8, cy - 1, DS); c.line(1, cy + 6, 8, cy + 2, DS); c.rect(1, cy + 4, 2, cy + 8, DS, 0)
    grip(c, 9, cy + 3, (44, 40, 38), 8, 4, 3)
    c.cyl(12, cy + 3, 27, cy + 8, (66, 68, 66))
    for x in range(13, 26, 3): c.line(x, cy + 8, x + 2, cy + 3, (122, 124, 120))
    c.rect(27, cy + 3, 28, cy + 8, DS, 0)
    c.rect(8, cy - 3, 21, cy + 2, (60, 56, 50), 0.18); c.rect(21, cy - 2, 27, cy + 2, (44, 44, 44), 0.12)
    c.cyl(27, cy - 1, 30, cy + 1, DS); c.line(29, cy - 4, 29, cy - 2, DS); c.line(11, cy - 1, 17, cy - 1, (36, 34, 32)); tguard(c, 13, 17, cy + 3)

def bullpup(c, body, dark, mcol, opt=None, curved=False, under=False, furn=None, cy=13):
    furn = furn or body
    c.poly([(1, cy - 2), (23, cy - 3), (23, cy + 4), (1, cy + 5)], body, 0.14); c.rect(1, cy - 2, 2, cy + 5, GM, 0)
    if curved: magc(c, 9, cy + 4, 10, 4, mcol, 3)
    else: mags(c, 9, cy + 4, 9, 4, mcol, 0)
    grip(c, 16, cy + 4, dark, 7, 4, 2); tguard(c, 20, 24, cy + 4)
    c.rect(14, cy - 1, 17, cy, sh(body, 0.5), 0)
    c.rect(22, cy - 2, 28, cy + 3, furn, 0.15)
    for x in (24, 26): c.line(x, cy - 1, x, cy + 2, sh(furn, 0.7))
    if under: c.cyl(21, cy + 4, 30, cy + 6, (86, 88, 84)); c.put(30, cy + 5, BLK)
    c.cyl(28, cy - 1, 30, cy, DS); c.rect(29, cy - 2, 30, cy + 1, GM, 0)
    if opt: optic(c, opt, 7, cy - 3)
    else: c.rect(8, cy - 5, 18, cy - 4, dark, 0.1); c.line(19, cy - 5, 19, cy - 3, dark); c.line(7, cy - 5, 7, cy - 3, dark)

@sp('l85a2')
def _(c, k, r): bullpup(c, (50, 54, 52), (40, 42, 42), (58, 60, 58), opt='acog')
@sp('groza')
def _(c, k, r): bullpup(c, (64, 66, 62), (42, 44, 42), (70, 64, 56), curved=True, under=True, furn=(86, 70, 50))

def val(c, scope):
    cy = 13
    if scope: c.poly([(1, cy + 1), (10, cy - 2), (10, cy), (1, cy + 3)], (96, 72, 48), 0.1); c.poly([(2, cy + 7), (10, cy + 2), (10, cy + 4), (2, cy + 9)], (96, 72, 48), 0.1); c.rect(1, cy + 1, 2, cy + 9, (60, 46, 34), 0)
    else: c.line(1, cy + 5, 10, cy - 1, DS); c.line(1, cy + 6, 10, cy + 3, DS); c.rect(1, cy + 4, 2, cy + 8, DS, 0)
    mags(c, 20, cy + 2, 9, 4, (52, 56, 52), 1)
    grip(c, 14, cy + 2, (40, 42, 40), 8, 4, 3); tguard(c, 16, 19, cy + 3)
    c.rect(10, cy - 3, 21, cy + 2, (56, 60, 56), 0.18); c.line(12, cy - 1, 18, cy - 1, (34, 36, 34))
    c.cyl(19, cy - 3, 30, cy + 2, (46, 50, 48))
    for x in (23, 26): c.line(x, cy - 3, x, cy + 2, (30, 32, 30))
    for x in range(21, 30, 2): c.put(x, cy - 1, BLK); c.put(x, cy + 1, BLK)
    c.put(30, cy - 1, BLK)
    if scope: optic(c, 'pso', 8, cy - 4)
    else: c.line(12, cy - 4, 18, cy - 4, DS)
@sp('asval')
def _(c, k, r): val(c, False)
@sp('vss')
def _(c, k, r): val(c, True)

@sp('svd')
def _(c, k, r):
    cy = 14
    c.poly([(1, cy), (11, cy - 2), (11, cy + 1), (1, cy + 3)], WOOD, 0.1); c.poly([(2, cy + 7), (11, cy + 2), (11, cy + 4), (2, cy + 9)], WOOD, 0.1); c.rect(1, cy, 2, cy + 9, sh(WOOD, 0.55), 0)
    magc(c, 19, cy + 2, 7, 4, (78, 72, 66), 2); grip(c, 13, cy + 2, DW, 7, 4, 3); tguard(c, 16, 19, cy + 3)
    c.rect(11, cy - 3, 20, cy + 2, DS, 0.18); c.line(12, cy - 1, 19, cy - 1, sh(DS, 0.7))
    c.cyl(20, cy - 4, 27, cy - 3, DS); c.rect(20, cy - 2, 26, cy + 2, WOOD, 0.15); c.cyl(26, cy - 1, 30, cy, DS); c.rect(29, cy - 2, 30, cy + 1, sh(DS, 0.7), 0)
    optic(c, 'pso', 10, cy - 5)
@sp('sks')
def _(c, k, r):
    cy = 13
    c.poly([(1, cy + 1), (12, cy - 2), (12, cy + 3), (1, cy + 8)], WOOD, 0.12); c.rect(1, cy + 1, 2, cy + 8, sh(WOOD, 0.55), 0)
    c.rect(12, cy - 1, 25, cy + 3, WOOD, 0.14); c.rect(25, cy - 1, 26, cy + 3, sh(WOOD, 0.6), 0)
    c.rect(12, cy - 3, 21, cy - 2, DS, 0.15); c.cyl(21, cy - 3, 25, cy - 2, DS); c.cyl(25, cy - 2, 30, cy - 1, DS)
    c.rect(14, cy + 3, 18, cy + 5, DS, 0.1); tguard(c, 11, 14, cy + 3)
    c.rect(28, cy - 5, 29, cy, DS, 0); c.line(25, cy + 2, 30, cy + 2, LS); c.line(25, cy + 3, 29, cy + 3, sh(LS, 0.6))
    c.line(15, cy - 1, 20, cy - 1, sh(WOOD, 0.7))
@sp('sv98')
def _(c, k, r):
    cy = 15; sk = (70, 78, 56)
    c.poly([(1, cy - 1), (12, cy - 3), (13, cy + 4), (9, cy + 10), (1, cy + 7)], sk, 0.12); c.hole(5, cy + 2, 8, cy + 6)
    c.rect(1, cy - 1, 2, cy + 7, GM, 0)
    c.rect(12, cy - 3, 21, cy + 2, BLK, 0.15); c.rect(10, cy - 3, 12, cy - 1, DS); c.put(9, cy - 2, DS)
    c.rect(14, cy + 2, 18, cy + 5, GM, 0.1); tguard(c, 14, 18, cy + 5)
    c.rect(21, cy - 2, 26, cy + 2, sk, 0.14); c.cyl(26, cy - 2, 30, cy, GM); c.rect(28, cy - 3, 30, cy + 1, DS, 0.1)
    optic(c, 'scope8', 11, cy - 4)
@sp('m82')
def _(c, k, r):
    cy = 16; ol = (66, 70, 58)
    c.rect(1, cy - 2, 4, cy + 5, (40, 40, 40), 0.1); c.poly([(4, cy - 2), (10, cy - 3), (10, cy + 4), (4, cy + 5)], ol, 0.1)
    c.rect(9, cy - 4, 21, cy + 3, ol, 0.18); c.rect(12, cy + 3, 19, cy + 8, (50, 52, 46), 0.12); c.line(12, cy + 5, 19, cy + 5, (30, 32, 30))
    c.line(10, cy + 1, 20, cy + 1, sh(ol, 0.6)); grip(c, 7, cy + 3, (40, 40, 40), 5, 3, 2)
    c.cyl(21, cy - 3, 30, cy + 1, (56, 58, 56)); c.rect(26, cy - 4, 30, cy + 2, DS, 0.12)
    for x in (27, 29): c.line(x, cy - 3, x, cy + 1, BLK)
    bipod(c, 23, cy + 2, 9)
    optic(c, 'scope8', 8, cy - 5)
@sp('pkm')
def _(c, k, r):
    cy = 12
    c.poly([(1, cy + 1), (10, cy - 2), (10, cy + 3), (1, cy + 8)], WOOD, 0.12); c.hole(4, cy + 4, 6, cy + 5); c.rect(1, cy + 1, 2, cy + 8, sh(WOOD, 0.55), 0)
    grip(c, 12, cy + 3, (44, 36, 30), 8, 4, 3); tguard(c, 16, 19, cy + 4)
    c.rect(11, cy + 4, 21, cy + 11, (66, 72, 54), 0.14); c.line(11, cy + 7, 21, cy + 7, (44, 48, 36))
    for i in range(3): c.put(14 + i * 3, cy + 5, BR)
    c.rect(9, cy - 3, 22, cy + 3, DS, 0.18); c.rect(12, cy - 5, 19, cy - 4, DS, 0.15); c.line(10, cy, 21, cy, sh(DS, 0.65))
    c.cyl(22, cy - 2, 27, cy + 1, (56, 58, 58)); c.put(24, cy - 1, BLK); c.put(26, cy - 1, BLK); c.cyl(27, cy - 1, 30, cy, DS); c.line(26, cy - 5, 26, cy - 2, DS)
    bipod(c, 25, cy + 1, 8)

# ---- shotguns
@sp('toz34')
def _(c, k, r):
    cy = 13
    c.poly([(1, cy + 1), (12, cy - 2), (12, cy + 3), (1, cy + 8)], WOOD, 0.12); c.rect(1, cy + 1, 2, cy + 8, sh(WOOD, 0.5), 0)
    c.cyl(15, cy - 3, 30, cy - 2, GM); c.cyl(15, cy, 30, cy + 1, GM); c.line(15, cy - 1, 29, cy - 1, DS)
    c.rect(12, cy - 3, 18, cy + 3, DS, 0.18); c.line(12, cy + 2, 18, cy + 2, GM); c.put(29, cy - 4, LS)
    c.rect(18, cy + 2, 25, cy + 4, WOOD, 0.14); tguard(c, 12, 16, cy + 3); c.line(13, cy - 3, 11, cy - 5, DS)
@sp('sawnoff')
def _(c, k, r):
    cy = 13
    c.poly([(1, cy - 1), (11, cy - 3), (11, cy + 2), (8, cy + 10), (2, cy + 10)], WOOD, 0.12); c.line(3, cy + 4, 9, cy + 4, sh(WOOD, 0.7))
    c.cyl(12, cy - 3, 30, cy - 2, GM); c.cyl(12, cy, 30, cy + 1, GM); c.line(12, cy - 1, 29, cy - 1, DS)
    c.rect(10, cy - 3, 16, cy + 3, DS, 0.18); c.rect(16, cy + 2, 22, cy + 4, WOOD, 0.14); tguard(c, 10, 14, cy + 3); c.put(29, cy - 4, LS)
def pump(c, stock, ccol, fcol, tube, shield=False, hook=False):
    cy = 13
    c.poly([(1, cy), (12, cy - 2), (12, cy + 3), (1, cy + 8)], stock, 0.12); c.rect(1, cy, 2, cy + 8, sh(stock, 0.55), 0)
    if hook: c.rect(1, cy - 2, 4, cy, stock, 0)
    c.rect(12, cy - 3, 20, cy + 2, ccol, 0.18); c.rect(14, cy - 1, 18, cy, sh(ccol, 0.5), 0); c.rect(14, cy + 2, 19, cy + 4, ccol, 0.1); tguard(c, 12, 16, cy + 3)
    c.cyl(20, cy - 3, 30, cy - 2, tube); c.cyl(20, cy, 29, cy + 1, tube)
    if shield:
        c.cyl(21, cy - 4, 29, cy - 1, (62, 64, 62))
        for x in range(22, 29, 2): c.put(x, cy - 3, BLK); c.put(x, cy - 2, BLK)
    c.rect(20, cy - 1, 27, cy + 4, fcol, 0.15)
    for x in range(21, 27, 2): c.line(x, cy, x, cy + 3, sh(fcol, 0.6))
    c.put(30, cy - 4, LS)
@sp('m870')
def _(c, k, r): pump(c, (46, 46, 46), (74, 78, 78), (50, 50, 50), (66, 70, 72))
@sp('spas12')
def _(c, k, r): pump(c, (40, 40, 42), (48, 50, 52), (40, 40, 42), (54, 56, 56), shield=True, hook=True)

@sp('rpg7')
def _(c, k, r):
    cy = 13
    c.poly([(1, cy - 4), (7, cy - 2), (7, cy + 3), (1, cy + 5)], (62, 66, 54), 0.1); c.hole(1, cy - 1, 2, cy + 2)
    c.cyl(6, cy - 2, 21, cy + 3, (70, 74, 58)); c.rect(10, cy - 4, 17, cy + 5, WOOD, 0.16)
    for x in (12, 15): c.line(x, cy - 4, x, cy + 5, sh(WOOD, 0.65))
    grip(c, 9, cy + 5, (50, 38, 30), 7, 4, 2); tguard(c, 13, 17, cy + 5); c.rect(15, cy + 4, 18, cy + 8, DW, 0.1)
    c.rect(6, cy - 6, 9, cy - 3, DS, 0); c.line(7, cy - 8, 7, cy - 6, DS)
    c.rect(19, cy - 1, 22, cy + 1, (56, 58, 50), 0); c.poly([(22, cy - 4), (26, cy - 4), (29, cy - 2), (30, cy), (29, cy + 2), (26, cy + 4), (22, cy + 4)], (92, 96, 66), 0.2)
    c.line(24, cy - 4, 24, cy + 4, (60, 64, 44)); c.put(30, cy, (40, 40, 36)); c.rect(21, cy - 2, 22, cy + 2, BR, 0)
@sp('knife')
def _(c, k, r):
    c.rect(3, 12, 13, 18, (44, 44, 46), 0.15)
    for x in range(4, 13, 3): c.line(x, 13, x, 17, (26, 26, 28))
    c.rect(1, 12, 2, 18, ST, 0.1); c.rect(13, 10, 14, 20, DS, 0.1)
    c.poly([(15, 12), (27, 12), (30, 15), (26, 18), (15, 18)], (150, 154, 154), 0.2); c.line(16, 14, 27, 14, (104, 108, 108)); c.line(26, 18, 29, 15, (190, 192, 188))

# ======================================================================= attachments (big, centred)
@sp('kobra')
def _(c, k, r):
    c.rect(4, 22, 27, 25, DS, 0.1); c.rect(8, 21, 11, 21, GM, 0); c.rect(19, 21, 22, 21, GM, 0)
    c.poly([(7, 21), (24, 21), (24, 9), (9, 9)], GM, 0.2); c.rect(18, 5, 24, 9, GM, 0.15)
    c.rect(24, 6, 25, 20, (84, 112, 122), 0); c.put(25, 12, (160, 44, 38)); c.put(25, 13, (170, 56, 44)); c.rect(10, 12, 16, 16, sh(GM, 0.7), 0); c.line(9, 10, 22, 10, DS)
@sp('holo')
def _(c, k, r):
    c.rect(4, 24, 27, 27, DS, 0.1); c.rect(9, 22, 22, 23, GM, 0)
    c.rect(7, 5, 24, 22, GM, 0.18); c.hole(10, 8, 21, 18); c.rect(10, 8, 21, 9, (58, 76, 82), 0); c.line(10, 8, 10, 18, (84, 108, 116)); c.put(16, 13, (176, 54, 44)); c.put(15, 13, (176, 54, 44)); c.put(16, 14, (176, 54, 44))
    c.rect(7, 19, 24, 21, DS, 0.1)
@sp('pso1')
def _(c, k, r):
    c.rect(11, 20, 21, 24, DS, 0.12); c.rect(9, 24, 23, 26, GM, 0)
    c.cyl(5, 10, 24, 19, GM); c.rect(1, 8, 6, 21, DS, 0.14); c.rect(23, 7, 30, 22, DS, 0.14); c.rect(14, 6, 17, 9, DS, 0.1)
    c.rect(29, 9, 30, 20, (76, 108, 116), 0); c.line(7, 12, 23, 12, (92, 94, 92)); c.put(3, 14, (30, 60, 66))
@sp('acog')
def _(c, k, r):
    c.rect(7, 22, 24, 26, DS, 0.12); c.rect(11, 20, 20, 21, GM, 0)
    c.poly([(3, 11), (24, 9), (28, 11), (28, 19), (3, 21)], (46, 48, 46), 0.2); c.rect(26, 7, 30, 22, DS, 0.14); c.rect(0 + 1, 9, 4, 23, DS, 0.14)
    c.rect(10, 6, 20, 8, (196, 122, 40), 0); c.line(10, 5, 20, 5, (230, 170, 70)); c.rect(9, 8, 21, 9, GM, 0)
    c.put(29, 14, (76, 108, 116)); c.put(29, 15, (76, 108, 116)); c.line(8, 13, 24, 13, (30, 32, 30))
@sp('scope8')
def _(c, k, r):
    c.rect(5, 22, 10, 25, DS, 0.12); c.rect(20, 22, 25, 25, DS, 0.12); c.rect(4, 25, 27, 27, GM, 0)
    c.cyl(4, 11, 26, 20, GM); c.rect(0 + 1, 10, 5, 21, DS, 0.14); c.rect(23, 7, 30, 24, DS, 0.14); c.rect(13, 4, 17, 11, DS, 0.1); c.rect(20, 7, 22, 10, DS, 0)
    c.rect(8, 21, 9, 22, GM, 0); c.line(29, 10, 29, 22, (72, 104, 112)); c.line(6, 13, 22, 13, (90, 92, 90)); c.put(15, 5, (170, 160, 100))
def supp(c, x0, x1, t, col, ports=False):
    y0 = 16 - t // 2; y1 = y0 + t - 1
    c.cyl(1, 13, x0 + 3, 18, sh(DS, 0.9))
    c.cyl(x0 + 2, y0, x1, y1, col); c.rect(x1 - 1, y0 + 1, x1, y1 - 1, sh(col, 0.55), 0); c.rect(x0 + 2, y0, x0 + 3, y1, sh(col, 0.7), 0)
    for x in range(x0 + 6, x1 - 2, 4): c.line(x, y0, x, y1, sh(col, 0.62))
    if ports:
        for x in range(x0 + 6, x1 - 3, 3): c.put(x, y0 + t // 3, BLK); c.put(x, y1 - t // 3, BLK)
    c.put(x1 - 1, 16, BLK)
@sp('supp_pistol')
def _(c, k, r): supp(c, 4, 27, 7, (60, 62, 64))
@sp('supp_545')
def _(c, k, r): supp(c, 3, 28, 9, (58, 60, 60))
@sp('supp_556')
def _(c, k, r): supp(c, 3, 29, 10, (52, 54, 54))
@sp('supp_762')
def _(c, k, r): supp(c, 3, 29, 12, (66, 68, 62))
@sp('supp_939')
def _(c, k, r): supp(c, 3, 29, 11, (56, 62, 54), ports=True)
@sp('brake')
def _(c, k, r):
    c.cyl(2, 12, 8, 19, DS); c.cyl(8, 9, 27, 22, (60, 62, 62)); c.rect(26, 10, 28, 21, sh(DS, 0.7), 0)
    for x in (12, 16, 20): c.rect(x, 10, x + 1, 21, BLK, 0)
    c.line(9, 10, 26, 10, (110, 112, 108)); c.rect(26, 13, 27, 18, BLK, 0)
@sp('vgrip')
def _(c, k, r):
    c.rect(5, 3, 26, 8, DS, 0.14)
    for x in range(6, 26, 3): c.line(x, 3, x, 4, BLK)
    c.rect(11, 8, 20, 10, GM, 0.1)
    c.poly([(11, 10), (20, 10), (19, 28), (12, 28)], (44, 46, 46), 0.12)
    for y in range(13, 27, 3): c.line(12, y, 19, y, (28, 30, 30))
    c.rect(11, 28, 20, 29, GM, 0)
@sp('laser')
def _(c, k, r):
    c.rect(6, 6, 24, 9, DS, 0.12); c.rect(8, 4, 12, 6, GM, 0); c.rect(18, 4, 22, 6, GM, 0)
    c.rect(5, 9, 23, 22, (54, 58, 54), 0.18); c.rect(23, 11, 26, 20, DS, 0.1); c.disc(26, 15, 2, (160, 44, 38)); c.put(25, 14, (220, 120, 100))
    c.line(27, 15, 30, 15, (190, 56, 48)); c.rect(8, 12, 18, 13, GM, 0); c.rect(8, 18, 12, 19, (150, 60, 50), 0)
@sp('light')
def _(c, k, r):
    c.rect(7, 5, 24, 8, DS, 0.12); c.rect(9, 3, 13, 5, GM, 0)
    c.cyl(5, 8, 20, 21, (56, 58, 56)); c.poly([(20, 6), (26, 5), (26, 23), (20, 22)], DS, 0.1); c.ball(27, 14, 3, 8, (224, 214, 160), 0.3)
    for y in (12, 16): c.line(7, y, 17, y, (36, 38, 36))
@sp('bipod')
def _(c, k, r):
    c.rect(8, 3, 23, 8, DS, 0.14); c.rect(12, 8, 19, 12, GM, 0.1)
    c.line(13, 12, 5, 27, ST); c.line(14, 12, 6, 27, DS); c.line(18, 12, 26, 27, ST); c.line(17, 12, 25, 27, DS)
    c.rect(3, 27, 8, 29, GM, 0.1); c.rect(23, 27, 28, 29, GM, 0.1)
    c.rect(14, 17, 17, 18, ST, 0); c.line(9, 20, 12, 20, GM); c.line(20, 20, 23, 20, GM)
@sp('gp25')
def _(c, k, r):
    c.rect(5, 6, 25, 8, DS, 0.1)
    c.cyl(2, 9, 23, 17, (70, 72, 66)); c.rect(23, 8, 29, 18, DS, 0.14); c.disc(29, 13, 3, BLK); c.line(8, 11, 20, 11, (46, 48, 44))
    c.rect(5, 17, 12, 19, GM, 0); grip(c, 12, 18, (88, 62, 40), 9, 5, 2); tguard(c, 8, 12, 18); c.rect(18, 5, 20, 8, LS, 0)
@sp('m203')
def _(c, k, r):
    c.rect(3, 5, 28, 7, DS, 0.1); c.cyl(1, 8, 29, 15, (40, 42, 42)); c.disc(29, 11, 3, (14, 14, 14)); c.line(4, 10, 25, 10, (60, 62, 60))
    c.rect(2, 15, 18, 22, (46, 48, 46), 0.14); c.rect(18, 15, 21, 18, DS, 0); tguard(c, 8, 13, 22); c.rect(21, 3, 22, 8, ST, 0)
    for y in (4, 5): c.put(23 + 0, y, ST)
    c.rect(25, 16, 27, 17, TAN, 0)
@sp('mag_ext')
def _(c, k, r):
    c.rect(7, 2, 16, 4, GM, 0.1); c.poly([(7, 4), (17, 4), (24, 29), (13, 29)], (62, 64, 60), 0.12)
    for i in range(7, 28, 3): c.line(int(7 + 6 * (i - 4) / 25), i, int(18 + 6 * (i - 4) / 25), i, (36, 38, 36))
    c.rect(13, 28, 24, 29, GM, 0); c.rect(8, 3, 15, 4, (150, 128, 70), 0)
    c.line(9, 6, 13, 24, (96, 98, 94))
@sp('drum12')
def _(c, k, r):
    c.rect(12, 1, 21, 6, GM, 0.14); c.ball(16.5, 18, 12, 12, (62, 66, 60), 0.5)
    c.ring(16.5, 18, 10, 8.4, (40, 42, 38)); c.ring(16.5, 18, 6, 5, (88, 90, 84)); c.disc(16.5, 18, 3, (50, 52, 48))
    for a in range(0, 360, 60): c.put(16.5 + 8.2 * math.cos(math.radians(a)), 18 + 8.2 * math.sin(math.radians(a)), (130, 128, 120))
    c.put(16, 17, ST)

# ======================================================================= ammo
CAL = {'9x18': (176, 150, 60), '9x19': (92, 112, 134), '45acp': (146, 94, 56), '545': (72, 108, 70), '556': (66, 96, 124),
       '762x39': (150, 84, 48), '762x51': (110, 112, 108), '762x54': (118, 82, 120), '9x39': (150, 148, 100)}
def ammobox(c, cal, ap=False):
    lab = CAL[cal]; card = (150, 124, 86)
    c.poly([(4, 13), (24, 13), (29, 8), (9, 8)], sh(card, 1.14), 0.1)
    c.poly([(24, 13), (29, 8), (29, 21), (24, 26)], sh(card, 0.72), 0.1)
    c.rect(4, 13, 24, 26, card, 0.14); c.line(4, 14, 24, 14, sh(card, 0.7)); c.rect(5, 17, 23, 23, lab, 0.12)
    c.line(5, 17, 23, 17, sh(lab, 1.2)); c.line(5, 23, 23, 23, sh(lab, 0.7))
    for i in range(3): c.rect(7 + i * 3, 19, 8 + i * 3, 21, (186, 150, 78), 0); c.put(7 + i * 3, 18, (60, 50, 40))
    c.rect(17, 19, 22, 19, (40, 38, 36), 0); c.rect(17, 21, 21, 21, (40, 38, 36), 0)
    if ap:
        for i in range(0, 20, 4): c.rect(5 + i, 24, 7 + i, 25, RD, 0); c.rect(7 + i, 24, 8 + i, 25, BLK, 0)
        c.rect(25, 9, 28, 11, RD, 0)
for _cal in ('9x18', '9x19', '45acp', '545', '556', '762x39', '762x51', '762x54', '9x39'):
    PAINT['ammo_' + _cal] = (lambda c, k, r, cal=_cal: ammobox(c, cal), {})
    PAINT['ammo_' + _cal + '_ap'] = (lambda c, k, r, cal=_cal: ammobox(c, cal, True), {})
PAINT['ammo_9x18_pbp'] = PAINT['ammo_9x18_ap']
def shells(c, tube, rim):
    for i, (x, h) in enumerate(((5, 17), (13, 20), (21, 16))):
        yb = 29; c.rect(x, yb - 5, x + 5, yb, rim, 0.2); c.rect(x + 1, yb - 6, x + 4, yb - 6, sh(rim, 0.7), 0)
        c.vcyl(x, yb - h, x + 5, yb - 6, tube); c.line(x, yb - h, x + 5, yb - h, sh(tube, 0.62)); c.rect(x + 1, yb - h - 1, x + 4, yb - h, sh(tube, 0.8), 0)
        c.rect(x + 1, yb - 8, x + 4, yb - 8, (36, 34, 32), 0)
@sp('ammo_12g_buck')
def _(c, k, r): shells(c, (150, 54, 44), BR)
@sp('ammo_12g_slug')
def _(c, k, r): shells(c, (170, 108, 44), (128, 126, 118))
@sp('ammo_12g_dart')
def _(c, k, r): shells(c, (80, 104, 126), (128, 126, 118))
@sp('ammo_50bmg')
def _(c, k, r):
    c.rect(2, 12, 29, 27, (68, 74, 56), 0.14); c.rect(2, 12, 29, 15, (78, 84, 62), 0.1); c.line(2, 16, 29, 16, (44, 48, 36))
    c.rect(11, 5, 20, 8, DS, 0); c.rect(10, 8, 11, 12, DS, 0); c.rect(20, 8, 21, 12, DS, 0)
    c.rect(13, 18, 18, 22, DS, 0.14); c.rect(6, 19, 8, 24, CR, 0); c.rect(22, 19, 27, 21, CR, 0); c.rect(22, 23, 25, 24, CR, 0)
@sp('ammo_pg7')
def _(c, k, r):
    c.poly([(1, 13), (6, 14), (6, 18), (1, 19)], (70, 72, 60), 0.1)
    c.cyl(6, 14, 16, 18, (62, 64, 54)); c.poly([(2, 10), (6, 13), (6, 14), (2, 14)], (74, 76, 60), 0.1); c.poly([(2, 22), (6, 19), (6, 18), (2, 18)], (74, 76, 60), 0.1)
    c.rect(16, 14, 19, 18, BR, 0.1); c.poly([(19, 11), (24, 10), (28, 13), (30, 16), (28, 19), (24, 22), (19, 21)], (84, 90, 62), 0.2); c.line(22, 10, 22, 22, (56, 62, 42)); c.rect(29, 15, 30, 17, BLK, 0)
@sp('vog25')
def _(c, k, r):
    c.cyl(2, 11, 10, 20, BR); c.rect(10, 12, 11, 19, sh(BR, 0.6), 0); c.rect(1, 13, 2, 18, sh(BR, 0.7), 0); c.disc(3, 15, 1, DS)
    c.poly([(11, 11), (22, 10), (27, 12), (30, 15), (27, 19), (22, 21), (11, 20)], (86, 92, 64), 0.2); c.line(20, 10, 20, 21, (58, 64, 44)); c.rect(14, 12, 15, 19, (130, 118, 60), 0)
@sp('m203_he')
def _(c, k, r):
    c.cyl(2, 11, 10, 20, BR); c.rect(10, 12, 11, 19, sh(BR, 0.6), 0); c.rect(1, 13, 2, 18, sh(BR, 0.7), 0); c.disc(3, 15, 1, DS)
    c.poly([(11, 12), (22, 11), (26, 12), (30, 15), (26, 19), (22, 20), (11, 19)], (78, 84, 60), 0.2)
    c.rect(18, 11, 20, 20, (172, 146, 56), 0); c.rect(24, 12, 25, 19, (172, 146, 56), 0); c.rect(13, 13, 15, 14, CR, 0)

# ======================================================================= outfits (front view)
def jacket(c, col, hood=False, long=False, sleeveL=None):
    c.sym([(10, 9), (6, 11), (2, 24), (7, 26), (10, 17)], col, 0.12)
    yb = 29 if long else 26
    c.poly([(9, 8), (22, 8), (23, yb), (8, yb)], col, 0.14)
    if hood:
        c.ball(15.5, 7, 7, 6, sh(col, 1.0), 0.45); c.ball(15.5, 9, 3.2, 3.6, (22, 20, 20), 0.2)
    else:
        c.poly([(11, 4), (20, 4), (23, 9), (8, 9)], sh(col, 1.08), 0.1); c.poly([(13, 4), (18, 4), (17, 9), (14, 9)], (28, 26, 24))
def cuffs(c, col):
    c.rect(2, 23, 6, 26, col, 0.1); c.rect(25, 23, 29, 26, col, 0.1)
@sp('jacket_leather')
def _(c, k, r):
    col = (74, 52, 38); jacket(c, col); c.line(15, 10, 15, 26, (30, 24, 20)); c.line(16, 10, 16, 26, (30, 24, 20)); c.put(15, 12, ST); c.put(15, 18, ST)
    c.poly([(11, 8), (15, 15), (14, 10)], sh(col, 1.25)); c.poly([(20, 8), (16, 15), (17, 10)], sh(col, 1.25)); cuffs(c, (50, 36, 28))
    c.rect(10, 19, 13, 21, sh(col, 0.75), 0); c.rect(18, 19, 21, 21, sh(col, 0.75), 0); c.line(9, 25, 22, 25, (52, 38, 30))
@sp('suit_bandit')
def _(c, k, r):
    col = (40, 38, 38); jacket(c, col, hood=True, long=True)
    c.poly([(15, 12), (16, 12), (18, 29), (13, 29)], (24, 22, 22)); c.line(15, 10, 15, 29, (20, 18, 18)); cuffs(c, (60, 36, 32))
    c.rect(9, 16, 12, 18, sh(col, 1.3), 0); c.rect(19, 16, 22, 18, sh(col, 1.3), 0); c.rect(3, 12, 5, 14, RD, 0); c.put(15, 14, ST); c.put(15, 20, ST); c.line(8, 28, 23, 28, (22, 20, 20))
@sp('suit_drifter')
def _(c, k, r):
    col = (100, 106, 70); jacket(c, col, hood=True); cuffs(c, (76, 80, 54))
    c.line(15, 11, 15, 26, (60, 64, 42)); c.rect(10, 15, 14, 18, sh(col, 0.8), 0); c.rect(17, 15, 21, 18, sh(col, 0.8), 0)
    c.line(10, 15, 14, 15, sh(col, 1.2)); c.line(17, 15, 21, 15, sh(col, 1.2)); c.rect(8, 21, 23, 22, (74, 62, 44), 0); c.rect(14, 21, 17, 22, ST, 0)
    c.line(5, 15, 6, 22, sh(col, 0.75)); c.line(26, 15, 25, 22, sh(col, 0.75))
@sp('suit_army')
def _(c, k, r):
    col = (78, 86, 60); jacket(c, col); cuffs(c, (64, 70, 48))
    c.poly([(10, 9), (21, 9), (21, 23), (10, 23)], (60, 68, 48), 0.12)
    for y in range(11, 23, 3): c.line(11, y, 20, y, (50, 56, 38))
    c.rect(11, 15, 14, 19, (84, 90, 64), 0.1); c.rect(17, 15, 20, 19, (84, 90, 64), 0.1)
    c.poly([(10, 9), (13, 9), (13, 12), (10, 12)], sh(col, 1.2)); c.rect(10, 24, 21, 26, (56, 62, 44), 0)
    for (x, y) in ((6, 15), (4, 19), (27, 14), (26, 18)): c.disc(x, y, 1.5, (60, 66, 44))
@sp('suit_merc')
def _(c, k, r):
    col = (88, 102, 120); jacket(c, col, hood=True); cuffs(c, (60, 72, 88))
    for (x, y, rr, cc) in ((12, 14, 3, (62, 74, 92)), (19, 19, 3, (124, 134, 144)), (11, 22, 2, (124, 134, 144)), (20, 13, 2, (62, 74, 92)), (4, 17, 2, (62, 74, 92)), (27, 20, 2, (124, 134, 144)), (6, 22, 2, (124, 134, 144)), (25, 14, 2, (62, 74, 92))):
        c.disc(x, y, rr, cc)
    c.rect(10, 16, 21, 19, (46, 52, 60), 0.1); c.line(10, 16, 21, 16, (70, 76, 84)); c.rect(11, 17, 13, 19, (60, 64, 72), 0); c.rect(18, 17, 20, 19, (60, 64, 72), 0)
@sp('suit_opensky')
def _(c, k, r):
    col = (136, 142, 64); jacket(c, col, hood=True); cuffs(c, (70, 82, 48))
    c.poly([(9, 8), (13, 8), (14, 26), (8, 26)], (72, 86, 50), 0.12); c.poly([(18, 8), (22, 8), (23, 26), (17, 26)], (72, 86, 50), 0.12)
    c.line(15, 10, 15, 26, (60, 70, 40)); c.line(4, 15, 8, 14, (200, 200, 170)); c.line(3, 19, 7, 18, (200, 200, 170)); c.line(27, 14, 23, 15, (200, 200, 170)); c.line(28, 18, 24, 19, (200, 200, 170))
    c.line(9, 22, 22, 22, (200, 200, 170))
@sp('suit_bastion')
def _(c, k, r):
    col = (100, 44, 38)
    c.sym([(10, 9), (4, 9), (1, 14), (3, 25), (8, 26), (10, 17)], (74, 32, 30), 0.12)
    c.ball(6, 11, 5, 4.5, (112, 50, 42), 0.5); c.ball(25, 11, 5, 4.5, (112, 50, 42), 0.5)
    c.poly([(9, 8), (22, 8), (23, 27), (8, 27)], col, 0.14); c.poly([(10, 4), (21, 4), (22, 9), (9, 9)], (60, 28, 26), 0.1); c.poly([(13, 4), (18, 4), (17, 8), (14, 8)], (26, 20, 18))
    c.poly([(11, 10), (20, 10), (20, 20), (15.5, 23), (11, 20)], (122, 56, 46), 0.2); c.line(15, 11, 15, 22, (70, 30, 26)); c.rect(9, 24, 22, 26, (40, 34, 32), 0); c.rect(14, 24, 17, 26, BR, 0)
    c.line(2, 20, 7, 20, (30, 26, 24)); c.line(24, 20, 29, 20, (30, 26, 24))
@sp('suit_choir')
def _(c, k, r):
    col = (160, 160, 150); jacket(c, col, hood=True, long=True); cuffs(c, (120, 120, 112))
    c.poly([(13, 12), (18, 12), (19, 29), (12, 29)], (126, 126, 118), 0.12); c.rect(14, 14, 17, 22, (46, 44, 44), 0); c.rect(12, 16, 19, 18, (46, 44, 44), 0)
    c.line(8, 27, 23, 27, (110, 110, 102)); c.ball(15.5, 9, 3.2, 3.6, (30, 28, 28), 0.2)
@sp('suit_hazmat')
def _(c, k, r):
    col = (186, 128, 44)
    c.sym([(10, 11), (6, 12), (3, 22), (7, 23), (10, 17)], col, 0.12)
    c.poly([(10, 10), (21, 10), (22, 20), (9, 20)], col, 0.14)
    c.poly([(9, 20), (15, 20), (14, 29), (8, 29)], col, 0.12); c.poly([(16, 20), (22, 20), (23, 29), (17, 29)], col, 0.12)
    c.rect(7, 28, 14, 30, (50, 46, 42), 0); c.rect(17, 28, 24, 30, (50, 46, 42), 0)
    c.ball(15.5, 6, 6, 5.5, (196, 138, 50), 0.45); c.rect(11, 4, 20, 8, (44, 52, 52), 0.2); c.line(12, 5, 15, 5, (130, 150, 150))
    c.rect(3, 21, 7, 22, (180, 180, 168), 0); c.rect(24, 21, 28, 22, (180, 180, 168), 0); c.rect(9, 26, 14, 26, (180, 180, 168), 0); c.rect(17, 26, 22, 26, (180, 180, 168), 0)
    c.rect(10, 14, 21, 15, (180, 180, 168), 0); c.line(15, 10, 15, 20, (140, 94, 30)); c.disc(15.5, 11, 1.5, (60, 60, 56))
@sp('suit_exo')
def _(c, k, r):
    dk = (62, 64, 68)
    c.sym([(9, 11), (5, 12), (4, 22), (8, 23), (9, 18)], (52, 54, 58), 0.12)
    c.rect(1, 8, 9, 14, (74, 76, 80), 0.2); c.rect(22, 8, 30, 14, (74, 76, 80), 0.2)
    c.rect(10, 8, 21, 20, dk, 0.16); c.rect(12, 9, 19, 15, (46, 48, 52), 0.15); c.rect(14, 11, 17, 13, (200, 120, 40), 0); c.put(15, 12, (240, 190, 90))
    c.rect(11, 20, 20, 23, (48, 50, 54), 0.1); c.poly([(10, 23), (15, 23), (14, 29), (9, 29)], dk, 0.12); c.poly([(16, 23), (21, 23), (22, 29), (17, 29)], dk, 0.12)
    c.rect(8, 27, 14, 30, (46, 48, 52), 0); c.rect(17, 27, 23, 30, (46, 48, 52), 0); c.rect(12, 4, 19, 8, (70, 72, 76), 0.2); c.rect(13, 5, 18, 6, (30, 30, 32), 0)
    c.line(4, 15, 4, 22, LS); c.line(27, 15, 27, 22, LS); c.rect(2, 9, 4, 10, (190, 150, 50), 0); c.rect(27, 9, 29, 10, (190, 150, 50), 0)

@sp('gasmask')
def _(c, k, r):
    rub = (58, 62, 54)
    c.ball(15.5, 13, 11, 12, rub, 0.5); c.poly([(8, 20), (23, 20), (20, 28), (11, 28)], rub, 0.12)
    for x in (12, 20):
        c.disc(x, 12, 4.6, (30, 30, 28)); c.disc(x, 12, 3.4, (70, 90, 84)); c.put(x - 1, 10, (170, 190, 184)); c.put(x - 1, 11, (130, 150, 144))
    c.ball(15.5, 22, 4.6, 4.6, (108, 112, 104), 0.5); c.ring(15.5, 22, 3, 2.2, (50, 52, 48)); c.disc(15.5, 22, 1.2, (50, 52, 48))
    c.line(6, 6, 25, 6, (40, 42, 38)); c.line(5, 11, 5, 15, (40, 42, 38)); c.line(26, 11, 26, 15, (40, 42, 38)); c.line(9, 17, 22, 17, sh(rub, 0.7))
@sp('helmet_tactical')
def _(c, k, r):
    col = (104, 102, 76)
    c.ball(15.5, 13, 12, 10.5, col, 0.5); c.rect(4, 16, 27, 20, col, 0.12)
    c.rect(3, 19, 28, 21, sh(col, 0.7), 0); c.rect(12, 7, 19, 12, DS, 0.14); c.disc(15.5, 9, 1.4, GM)
    c.rect(2, 13, 4, 20, DS, 0.1); c.rect(27, 13, 29, 20, DS, 0.1); c.rect(5, 21, 8, 27, (30, 30, 28), 0); c.rect(23, 21, 26, 27, (30, 30, 28), 0)
    c.line(8, 22, 10, 28, (66, 66, 56)); c.line(23, 22, 21, 28, (66, 66, 56)); c.line(9, 28, 22, 28, (66, 66, 56)); c.rect(12, 4, 19, 5, sh(col, 0.85), 0)
@sp('helmet_sphere')
def _(c, k, r):
    c.rect(5, 23, 26, 28, (112, 114, 110), 0.18); c.rect(5, 23, 26, 24, (150, 152, 146), 0)
    c.ball(15.5, 13, 12, 12, (50, 62, 62), 0.55); c.ball(15.5, 13, 9.5, 9.5, (36, 44, 46), 0.3)
    c.disc(10, 8, 2.4, (112, 134, 134)); c.put(13, 6, (170, 190, 186)); c.put(19, 18, (70, 88, 88))
    c.rect(3, 14, 5, 22, (92, 94, 90), 0.1); c.rect(26, 14, 28, 22, (92, 94, 90), 0.1)
@sp('helmet_psy')
def _(c, k, r):
    c.ball(15.5, 16, 12, 11, (92, 90, 100), 0.5); c.rect(4, 22, 27, 25, (70, 68, 78), 0.1)
    for i, y in enumerate((11, 15, 19)):
        c.line(5 + i, y, 26 - i, y, CU); c.line(5 + i, y + 1, 26 - i, y + 1, sh(CU, 0.7))
    for x, h in ((9, 8), (15, 12), (22, 8)): c.line(x, 8, x, 8 - min(h, 7), ST); c.put(x, 8 - min(h, 7), (150, 100, 190))
    c.disc(15.5, 22, 2, (134, 80, 170)); c.put(15, 21, (210, 170, 235))

# ======================================================================= medicine, food, drink
@sp('bandage', wear=0.05)
def _(c, k, r):
    c.poly([(19, 21), (28, 19), (30, 27), (21, 29)], sh(CR, 0.95), 0.1); c.line(21, 24, 29, 22, sh(CR, 0.75)); c.line(21, 27, 29, 25, sh(CR, 0.75))
    c.ball(13, 16, 11, 11, CR, 0.4); c.ring(13, 16, 8.5, 7.7, sh(CR, 0.78)); c.ring(13, 16, 6, 5.3, sh(CR, 0.78)); c.disc(13, 16, 3, (70, 62, 52)); c.disc(13, 16, 1.7, None)
    for (x, y) in ((7, 11), (19, 20), (10, 22)): c.put(x, y, (126, 96, 74))
    c.put(15, 6, (126, 126, 120)); c.put(16, 6, (126, 126, 120))
def medkit(c, box, cross, label=None):
    c.rect(10, 7, 21, 8, DS, 0); c.rect(10, 8, 11, 11, DS, 0); c.rect(20, 8, 21, 11, DS, 0)
    c.rect(4, 11, 27, 27, box, 0.14); c.rect(4, 11, 27, 15, sh(box, 1.1), 0.1); c.line(4, 15, 27, 15, sh(box, 0.55)); c.rect(14, 13, 17, 17, ST, 0.1); c.put(15, 15, GM)
    if label: c.rect(11, 18, 20, 25, label, 0)
    c.rect(12, 20, 19, 22, cross, 0); c.rect(14, 18, 17, 24, cross, 0)
@sp('medkit', wear=0.1)
def _(c, k, r): medkit(c, (146, 48, 42), CR)
@sp('medkit_army', wear=0.1)
def _(c, k, r): medkit(c, (76, 86, 58), CR)
@sp('medkit_sci', wear=0.1)
def _(c, k, r): medkit(c, (184, 146, 48), (60, 100, 66), label=CR)
@sp('antirad', wear=0.05)
def _(c, k, r):
    c.poly([(10, 12), (22, 12), (19, 8), (13, 8)], (150, 94, 38), 0.1); c.rect(9, 12, 22, 28, (150, 94, 38), 0.2); c.rect(11, 4, 20, 9, CR, 0.14)
    for x in (12, 14, 16, 18): c.line(x, 5, x, 8, sh(CR, 0.72))
    c.rect(10, 15, 21, 25, CR, 0); c.disc(15.5, 20, 2.2, (36, 34, 30)); c.disc(15.5, 20, 0.9, (200, 170, 60)); c.rect(11, 16, 20, 16, (200, 170, 60), 0); c.rect(11, 24, 20, 24, (200, 170, 60), 0); c.line(10, 13, 10, 27, (200, 140, 70))
@sp('psyblock', wear=0.1)
def _(c, k, r):
    c.rect(2, 11, 3, 19, ST, 0); c.line(4, 15, 9, 15, ST); c.rect(9, 9, 10, 21, DS, 0.1)
    c.cyl(10, 12, 23, 18, (172, 182, 184)); c.rect(11, 14, 22, 17, (112, 70, 142), 0.2); c.rect(23, 13, 25, 17, GM, 0); c.line(25, 15, 31, 15, LS)
    for x in (13, 16, 19): c.line(x, 12, x, 13, (60, 60, 60))
@sp('atlas', wear=0.1)
def _(c, k, r):
    c.rect(3, 8, 28, 25, (150, 152, 150), 0.18); c.rect(3, 8, 28, 10, (176, 178, 176), 0)
    for i in range(4):
        for j in range(2): c.ball(8 + i * 6, 14 + j * 6, 2.6, 2.6, (214, 200, 130), 0.5)
    c.line(3, 11, 28, 11, (104, 106, 104)); c.rect(24, 24, 28, 25, DS, 0)
@sp('vodka', wear=0.05)
def _(c, k, r):
    gl = (126, 150, 140)
    c.poly([(9, 13), (22, 13), (19, 9), (12, 9)], gl, 0.15); c.rect(9, 13, 22, 29, gl, 0.2); c.rect(13, 3, 18, 10, gl, 0.2)
    c.rect(13, 2, 18, 4, (150, 124, 60), 0.2); c.rect(10, 17, 21, 26, CR, 0); c.rect(10, 20, 21, 22, (120, 36, 34), 0); c.rect(12, 18, 19, 18, (40, 36, 34), 0); c.rect(12, 24, 17, 24, (40, 36, 34), 0)
    c.line(10, 14, 10, 28, (186, 206, 196)); c.line(14, 5, 14, 10, (186, 206, 196))
@sp('energy', wear=0.2)
def _(c, k, r):
    c.rect(10, 5, 21, 6, (160, 164, 160), 0.1); c.vcyl(10, 6, 21, 27, (44, 46, 52)); c.rect(10, 27, 21, 28, (150, 152, 148), 0.1); c.rect(13, 3, 17, 5, ST, 0); c.hole(14, 4, 16, 4)
    c.poly([(13, 10), (19, 10), (16, 16), (20, 16), (12, 25), (14, 18), (11, 18)], (168, 156, 50), 0.1); c.line(10, 8, 10, 26, (90, 92, 98))
@sp('bread', wear=0.05)
def _(c, k, r):
    c.ball(16, 18, 14, 9, (150, 98, 52), 0.55); c.ball(16, 16, 13, 7, (172, 120, 66), 0.5)
    for x in (9, 15, 21): c.line(x, 11, x + 3, 17, (118, 72, 36)); c.line(x + 1, 11, x + 4, 17, (200, 154, 96))
    for i in range(10): c.put(5 + (i * 7) % 22, 12 + (i * 5) % 8, (214, 190, 150))
@sp('sausage', wear=0.05)
def _(c, k, r):
    c.tube(bez((6, 25), (5, 5), (27, 5), (26, 25), 30), 3.6, 3.6, (152, 72, 54)); c.rect(4, 25, 8, 27, (90, 70, 46), 0); c.rect(23, 25, 27, 27, (90, 70, 46), 0); c.line(16, 3, 16, 1, (90, 70, 46))
@sp('canned_stew', wear=0.3)
def _(c, k, r):
    c.vcyl(8, 7, 23, 27, (126, 128, 122)); c.rect(8, 7, 23, 9, (170, 172, 166), 0.1); c.rect(8, 26, 23, 27, (150, 152, 146), 0)
    c.rect(8, 12, 23, 22, (124, 58, 40), 0.12); c.rect(12, 15, 19, 18, CR, 0); c.rect(11, 17, 20, 19, CR, 0); c.line(13, 14, 13, 13, CR); c.line(17, 14, 17, 12, CR); c.ring(16, 5, 3, 1.8, ST)
@sp('canned_fish', wear=0.3)
def _(c, k, r):
    c.rect(3, 10, 28, 23, (122, 124, 118), 0.16); c.rect(5, 12, 26, 21, (172, 140, 60), 0.14)
    c.ball(14, 16, 7, 2.8, (62, 44, 30), 0.3); c.poly([(20, 16), (24, 13), (24, 19)], (62, 44, 30)); c.put(9, 15, (210, 190, 120)); c.ring(26, 9, 3, 1.5, ST); c.line(26, 11, 25, 11, ST)
@sp('crackers', wear=0.05)
def _(c, k, r):
    c.rect(3, 9, 22, 25, (88, 94, 62), 0.14)
    for y in range(9, 26, 2): c.put(2, y, (88, 94, 62)); c.put(22, y + 1, (88, 94, 62))
    c.rect(6, 12, 15, 14, (170, 150, 90), 0); c.rect(6, 17, 15, 18, CR, 0); c.disc(10, 22, 1.5, (200, 190, 140))
    c.rect(14, 15, 28, 28, (192, 160, 96), 0.14)
    for x in (17, 20, 23, 26):
        for y in (18, 21, 24): c.put(x, y, (146, 114, 66))
@sp('water', wear=0.05)
def _(c, k, r):
    gl = (126, 156, 174)
    c.poly([(9, 12), (22, 12), (19, 8), (12, 8)], gl, 0.15); c.rect(9, 12, 22, 29, gl, 0.2); c.rect(13, 3, 18, 8, gl, 0.1); c.rect(12, 2, 19, 4, (60, 84, 116), 0.15)
    c.rect(9, 16, 22, 23, CR, 0); c.rect(9, 18, 22, 19, (70, 100, 130), 0)
    for y in (13, 26, 28): c.line(9, y, 22, y, sh(gl, 0.78))
    c.line(10, 13, 10, 28, (186, 210, 224))
def meat(c, raw):
    base = (120, 46, 54) if raw else (128, 80, 46)
    c.poly([(5, 13), (11, 7), (20, 6), (27, 11), (28, 19), (22, 26), (12, 27), (5, 21)], base, 0.22)
    c.line(24, 8, 29, 3, CR); c.line(25, 9, 30, 4, sh(CR, 0.8)); c.disc(29, 3, 1.6, CR); c.disc(30, 5, 1.4, CR)
    if raw:
        for (x1, y1, x2, y2) in ((8, 13, 14, 11), (12, 20, 20, 17), (18, 11, 24, 15), (9, 23, 17, 24)): c.line(x1, y1, x2, y2, (190, 146, 140))
        for (x, y) in ((22, 21), (10, 17), (17, 14)): c.disc(x, y, 1.6, (92, 104, 58))
        c.put(14, 9, (146, 60, 70))
    else:
        for (x1, y1, x2, y2) in ((8, 12, 12, 17), (14, 9, 19, 15), (19, 12, 24, 19), (11, 21, 17, 26)): c.line(x1, y1, x2, y2, (60, 36, 24))
        c.put(12, 12, (200, 150, 96)); c.put(13, 11, (200, 150, 96)); c.put(20, 10, (200, 150, 96))
@sp('mutant_meat', wear=0.05)
def _(c, k, r): meat(c, True)
@sp('mutant_meat_cooked', wear=0.05)
def _(c, k, r): meat(c, False)

# ======================================================================= detectors, tools, gear
@sp('detector_echo')
def _(c, k, r):
    c.line(20, 9, 20, 2, ST); c.disc(20, 2, 1.2, LS)
    c.rect(8, 9, 23, 29, (90, 94, 72), 0.16); c.rect(10, 11, 21, 18, (50, 54, 44), 0.1)
    for x in range(11, 21, 2):
        for y in (12, 14, 16): c.put(x, y, (110, 114, 98))
    c.put(11, 22, (168, 44, 38)); c.put(11, 25, (92, 160, 86)); c.disc(18, 24, 2.4, (120, 124, 118)); c.line(8, 20, 23, 20, (60, 64, 50))
@sp('detector_bear')
def _(c, k, r):
    c.line(9, 10, 6, 2, ST); c.line(22, 10, 25, 2, ST); c.rect(7, 10, 24, 29, (116, 84, 52), 0.16)
    c.disc(15.5, 18, 8, (118, 120, 116)); c.disc(15.5, 18, 6.6, (28, 34, 30)); c.line(15, 18, 20, 13, (118, 196, 120)); c.poly([(20, 13), (17, 14), (19, 16)], (118, 196, 120)); c.put(15, 18, (200, 230, 200))
    c.rect(10, 27, 13, 28, DS, 0); c.rect(18, 27, 21, 28, DS, 0)
@sp('detector_veles')
def _(c, k, r):
    c.line(27, 5, 29, 1, ST); c.rect(3, 5, 28, 28, (60, 62, 60), 0.16); c.rect(5, 7, 26, 20, (22, 40, 32), 0.1)
    c.ring(15.5, 13.5, 6.5, 5.7, (74, 142, 96)); c.ring(15.5, 13.5, 3.5, 2.8, (74, 142, 96)); c.line(15, 13, 21, 9, (132, 214, 150)); c.put(11, 15, (200, 230, 200)); c.put(19, 17, (200, 230, 200)); c.put(9, 9, (96, 168, 112))
    for x in (6, 11, 16, 21): c.rect(x, 23, x + 3, 25, (100, 104, 100), 0.1)
    c.rect(5, 26, 26, 26, DS, 0)
def grenade(c, body, seg):
    c.ball(15.5, 19, 9, 10, body, 0.55)
    if seg:
        for y in range(11, 29, 4): c.line(8, y, 23, y, sh(body, 0.62))
        for x in range(9, 23, 4): c.line(x, 10, x, 28, sh(body, 0.62))
    else: c.rect(8, 17, 23, 18, (150, 130, 60), 0)
    c.rect(12, 6, 19, 10, DS, 0.14); c.rect(13, 4, 18, 6, ST, 0.1); c.poly([(19, 6), (22, 8), (24, 14), (23, 22), (21, 22), (22, 14), (20, 9)], LS, 0.1)
    c.ring(9, 5, 3, 1.6, LS); c.line(12, 6, 11, 6, LS)
@sp('grenade_rgd5')
def _(c, k, r): grenade(c, (80, 88, 58), False)
@sp('grenade_f1')
def _(c, k, r): grenade(c, (88, 92, 66), True)
@sp('bolt', wear=0.4)
def _(c, k, r):
    pts = [(16 + 13 * math.cos(math.radians(a)), 16 + 13 * math.sin(math.radians(a))) for a in range(0, 360, 60)]
    c.poly(pts, (118, 120, 118), 0.2)
    c.poly([(16 + 10.5 * math.cos(math.radians(a)), 16 + 10.5 * math.sin(math.radians(a))) for a in range(0, 360, 60)], (136, 138, 134), 0.25)
    c.disc(16, 16, 6, (66, 66, 64)); c.disc(16, 16, 4.6, None); c.ring(16, 16, 6, 5, (90, 88, 84))
    for (x, y, rr) in ((9, 11, 2), (22, 21, 2.4), (20, 8, 1.5), (8, 20, 1.5)): c.disc(x, y, rr, (126, 74, 44))
def toolbox(c, col, tier):
    if tier == 1:
        c.rect(14, 3, 16, 12, (160, 40, 36), 0.1); c.rect(13, 2, 17, 4, (160, 40, 36), 0); c.line(21, 12, 22, 4, ST); c.disc(22, 3, 2.4, ST); c.disc(22, 3, 1, None)
        c.line(8, 12, 6, 5, DW); c.rect(5, 3, 8, 6, DS, 0)
    c.rect(3, 12, 28, 28, col, 0.16); c.rect(3, 12, 28, 15, sh(col, 1.12), 0.1); c.line(3, 16, 28, 16, sh(col, 0.55))
    if tier >= 2: c.poly([(9, 12), (9, 8), (22, 8), (22, 12)], DS, 0.1); c.rect(11, 9, 20, 11, GM, 0); c.rect(5, 17, 8, 22, DS, 0.1); c.rect(23, 17, 26, 22, DS, 0.1)
    if tier == 3: c.rect(3, 12, 6, 15, DS, 0); c.rect(25, 12, 28, 15, DS, 0); c.rect(3, 25, 6, 28, DS, 0); c.rect(25, 25, 28, 28, DS, 0); c.rect(12, 20, 19, 23, (186, 150, 60), 0); c.put(16, 21, GM)
    else: c.rect(13, 17, 18, 21, ST, 0.1)
@sp('toolkit_basic')
def _(c, k, r): toolbox(c, (122, 70, 46), 1)
@sp('toolkit_adv')
def _(c, k, r): toolbox(c, (74, 82, 58), 2)
@sp('toolkit_expert')
def _(c, k, r): toolbox(c, (42, 44, 48), 3)
@sp('repair_weapon')
def _(c, k, r):
    c.line(24, 10, 29, 3, ST); c.rect(28, 2, 30, 4, (130, 108, 60), 0)
    c.rect(4, 10, 27, 27, (62, 68, 52), 0.14); c.line(4, 14, 27, 14, (44, 50, 36)); c.rect(13, 12, 15, 16, ST, 0)
    c.rect(8, 17, 22, 18, CR, 0); c.rect(8, 18, 9, 21, CR, 0); c.rect(14, 18, 16, 21, CR, 0); c.rect(20, 17, 23, 19, CR, 0); c.rect(19, 24, 24, 25, (150, 128, 70), 0)
@sp('repair_armor')
def _(c, k, r):
    c.rect(4, 10, 27, 27, (140, 124, 90), 0.14); c.line(4, 14, 27, 14, (104, 90, 62)); c.rect(13, 12, 15, 16, ST, 0)
    c.poly([(10, 17), (14, 17), (16, 19), (18, 17), (22, 17), (21, 25), (11, 25)], CR, 0.1); c.line(25, 3, 20, 11, LS); c.disc(7, 22, 2, (120, 40, 36))
    c.disc(25, 23, 2.4, (80, 64, 46)); c.disc(25, 23, 1, None)
@sp('headlamp')
def _(c, k, r):
    ell = [(16 + 14 * math.cos(math.radians(a)), 19 + 10 * math.sin(math.radians(a))) for a in range(0, 361, 9)]
    c.tube(ell, 1.9, 1.9, (46, 48, 46)); c.line(3, 19, 3, 21, (110, 112, 106))
    c.rect(7, 2, 24, 14, (72, 74, 72), 0.2); c.rect(7, 2, 24, 3, (100, 102, 98), 0); c.rect(5, 6, 7, 11, (56, 58, 56), 0.1); c.rect(24, 6, 26, 11, (56, 58, 56), 0.1)
    c.disc(15.5, 8, 5.2, (34, 34, 32)); c.disc(15.5, 8, 4, (228, 220, 166)); c.disc(15.5, 8, 1.8, (252, 250, 226)); c.line(8, 13, 23, 13, (44, 46, 44))
@sp('pda')
def _(c, k, r):
    c.line(21, 4, 23, 1, ST); c.rect(8, 3, 23, 29, (58, 62, 54), 0.16); c.rect(10, 6, 21, 18, (24, 42, 34), 0.1)
    c.line(11, 9, 19, 9, (84, 160, 106)); c.line(11, 12, 16, 12, (84, 160, 106)); c.line(15, 7, 15, 17, (50, 100, 66)); c.disc(18, 14, 1.4, (200, 230, 200)); c.line(11, 16, 20, 16, (50, 100, 66))
    for i in range(3): c.rect(10 + i * 4, 21, 12 + i * 4, 23, (112, 116, 108), 0.1)
    c.rect(10, 25, 21, 26, (86, 90, 82), 0); c.rect(7, 2, 9, 4, (42, 44, 38), 0); c.rect(22, 27, 24, 29, (42, 44, 38), 0)
@sp('rubles', wear=0.05)
def _(c, k, r):
    cols = ((92, 112, 86), (112, 130, 96), (132, 140, 102), (104, 124, 94))
    for o in (3, 2, 1, 0): c.rect(4 + o, 11 + o * 2, 24 + o, 20 + o * 2, cols[o], 0.12); c.line(4 + o, 11 + o * 2, 24 + o, 11 + o * 2, sh(cols[o], 1.2))
    c.rect(5, 12, 23, 19, (104, 124, 94), 0); c.ball(12, 15.5, 3.6, 3.2, (172, 170, 128), 0.3)
    c.rect(6, 13, 8, 14, (56, 66, 48), 0); c.rect(17, 17, 21, 18, (56, 66, 48), 0); c.rect(16, 13, 21, 13, (56, 66, 48), 0)
    c.rect(13, 10, 16, 26, (182, 152, 96), 0.1); c.line(13, 14, 16, 14, (140, 114, 70))
@sp('scrap_metal')
def _(c, k, r):
    c.poly([(3, 26), (10, 13), (20, 11), (24, 18), (18, 28)], (112, 98, 90), 0.2); c.poly([(14, 28), (18, 17), (28, 16), (29, 27)], (98, 102, 104), 0.2)
    c.line(7, 22, 15, 16, (70, 62, 56)); c.line(20, 22, 26, 21, (128, 132, 132)); c.disc(22, 10, 4.2, (124, 78, 50)); c.ring(22, 10, 4.2, 3, (150, 96, 60)); c.disc(22, 10, 1.4, None)
    for a in range(0, 360, 60): c.put(22 + 5 * math.cos(math.radians(a)), 10 + 5 * math.sin(math.radians(a)), (124, 78, 50))
    c.disc(11, 21, 1, (60, 52, 46)); c.disc(24, 22, 1, (60, 62, 62))
@sp('electronics')
def _(c, k, r):
    c.rect(3, 7, 28, 25, (50, 88, 62), 0.16); c.rect(7, 11, 12, 16, (28, 28, 30), 0.1); c.rect(15, 10, 20, 13, (28, 28, 30), 0.1); c.vcyl(23, 11, 25, 18, (150, 108, 60)); c.rect(14, 17, 21, 19, (44, 44, 46), 0)
    for (x1, y1, x2, y2) in ((12, 13, 15, 12), (6, 18, 14, 18), (20, 11, 23, 11), (8, 21, 22, 21), (4, 9, 7, 9)): c.line(x1, y1, x2, y2, (150, 150, 80))
    for x in range(6, 26, 2): c.put(x, 25, (184, 156, 72)); c.put(x, 24, (184, 156, 72))
    for (x, y) in ((4, 8), (27, 8), (4, 22), (27, 22)): c.put(x, y, (24, 36, 28))
@sp('cloth', wear=0.05)
def _(c, k, r):
    c.rect(5, 19, 28, 27, (120, 116, 96), 0.14); c.rect(4, 14, 26, 22, (140, 134, 112), 0.14); c.rect(3, 9, 24, 17, (158, 150, 124), 0.14)
    for y in (10, 15, 20): c.line(5, y, 23, y, (108, 102, 84))
    for x in range(5, 23, 3): c.put(x, 11, (110, 100, 80))
    c.line(3, 17, 24, 17, (176, 168, 140)); c.line(27, 20, 27, 27, (90, 86, 70))
    for x in range(6, 28, 2): c.put(x, 28, (130, 124, 104))
@sp('springs')
def _(c, k, r):
    for (x0, yc, h, n) in ((2, 11, 6, 7), (5, 23, 5, 6)):
        c.line(x0, yc, x0 + 2, yc, ST); c.line(x0, yc + 1, x0 + 2, yc + 1, sh(ST, 0.7))
        for i in range(n):
            x = x0 + 2 + i * 3.8
            c.line(x, yc + h, x + 3, yc - h, LS); c.line(x + 1, yc + h, x + 4, yc - h, (118, 122, 124)); c.line(x + 2, yc + h, x + 5, yc - h, (76, 80, 82))
        xe = x0 + 2 + n * 3.8 + 1; c.line(xe, yc, xe + 2, yc, ST); c.line(xe, yc + 1, xe + 2, yc + 1, sh(ST, 0.7))
@sp('wires')
def _(c, k, r):
    c.ball(14, 16, 11, 11, CU, 0.45); c.disc(14, 16, 5, None)
    for rr in (9.4, 7.8, 6.4): c.ring(14, 16, rr, rr - 0.9, (96, 56, 32))
    c.tube(bez((22, 22), (28, 27), (30, 14), (27, 6), 12), 1.0, 0.9, (172, 108, 64)); c.rect(26, 4, 28, 6, (176, 180, 176), 0)
@sp('lead_sheet')
def _(c, k, r):
    c.poly([(4, 15), (22, 8), (29, 12), (11, 19)], (120, 126, 136), 0.2); c.poly([(4, 15), (11, 19), (11, 24), (4, 20)], (80, 84, 92), 0.1); c.poly([(11, 19), (29, 12), (29, 17), (11, 24)], (98, 102, 112), 0.1)
    c.line(8, 15, 20, 11, (150, 154, 162)); c.line(14, 20, 26, 15, (84, 88, 96)); c.disc(24, 12, 1, (84, 88, 96))
@sp('flash_drive')
def _(c, k, r):
    c.rect(3, 11, 19, 20, (50, 60, 84), 0.2); c.rect(19, 12, 21, 19, DS, 0.1); c.rect(21, 12, 30, 19, (150, 154, 152), 0.2); c.rect(23, 14, 28, 17, (30, 30, 30), 0); c.rect(23, 14, 24, 15, BR, 0); c.rect(23, 16, 24, 17, BR, 0)
    c.ring(3, 15.5, 3, 1.4, DS); c.disc(3, 15.5, 1, None); c.put(15, 13, (168, 44, 38)); c.line(6, 17, 16, 17, (34, 42, 62))
@sp('documents', wear=0.05)
def _(c, k, r):
    c.rect(8, 5, 23, 15, CR, 0.1)
    for y in (8, 11, 13): c.line(10, y, 21, y, (120, 114, 100))
    c.poly([(5, 13), (5, 10), (14, 10), (16, 13)], (160, 134, 86), 0.12); c.rect(5, 13, 27, 28, (176, 150, 100), 0.14); c.line(5, 14, 27, 14, (140, 114, 70))
    c.rect(15, 19, 24, 24, None or (120, 36, 34), 0); c.rect(16, 20, 23, 23, (176, 150, 100), 0); c.rect(18, 21, 21, 22, (120, 36, 34), 0); c.line(11, 13, 11, 28, (60, 54, 46)); c.line(12, 13, 12, 28, (60, 54, 46))
@sp('gun_oil')
def _(c, k, r):
    c.tube(bez((20, 14), (26, 10), (28, 7), (30, 2), 12), 1.2, 0.9, ST)
    c.poly([(7, 18), (22, 18), (19, 12), (10, 12)], (96, 100, 70), 0.14); c.rect(7, 18, 22, 29, (96, 100, 70), 0.2); c.rect(9, 10, 20, 12, DS, 0.1)
    c.rect(7, 21, 22, 26, (176, 144, 62), 0); c.rect(10, 22, 18, 22, (50, 44, 34), 0); c.rect(10, 24, 15, 24, (50, 44, 34), 0); c.line(8, 19, 8, 28, (130, 134, 100))

# ======================================================================= trophies (mutant parts)
@sp('dog_tail', wear=0, grade=0.04)
def _(c, k, r):
    pts = bez((4, 25), (9, 30), (15, 7), (28, 6), 30); c.tube(pts, 3.2, 1.1, (150, 118, 108))
    for i in range(4, 30, 4): x, y = pts[i]; c.disc(x + r.uniform(-1, 1), y + r.uniform(-1, 1), 1, (104, 66, 50))
    for i in range(0, 30, 6): x, y = pts[i]; c.put(x - 1, y - 2, (200, 170, 150))
    c.disc(4, 25, 2.6, (150, 52, 46)); c.disc(4, 25, 1.2, (214, 196, 170))
    for dx, dy in ((0, 0), (1, -1), (2, 0), (1, 1)): c.put(28 + dx, 6 + dy, (60, 46, 40))
@sp('boar_hoof', wear=0, grade=0.04)
def _(c, k, r):
    c.poly([(5, 2), (15, 2), (16, 13), (6, 14)], (104, 86, 70), 0.14)
    for x in range(6, 15, 2): c.line(x, 5, x + 1, 12, (70, 58, 46))
    c.ball(10, 3, 5, 2, (116, 36, 34), 0.3); c.disc(10, 3, 1.1, (206, 190, 164))
    c.poly([(5, 14), (16, 13), (27, 21), (29, 27), (5, 28)], (60, 50, 44), 0.22); c.poly([(5, 14), (16, 13), (18, 14), (6, 17)], (140, 124, 104))
    c.line(21, 18, 25, 28, (22, 18, 16)); c.line(22, 18, 26, 28, (22, 18, 16)); c.line(6, 27, 29, 27, (120, 104, 88)); c.line(8, 18, 8, 25, (96, 82, 70)); c.line(12, 17, 12, 25, (84, 72, 62))
    c.poly([(19, 12), (23, 15), (21, 17)], (78, 66, 56))
@sp('hog_eye', wear=0, grade=0.04)
def _(c, k, r):
    c.tube(bez((11, 21), (6, 25), (8, 28), (4, 30), 14), 3.2, 2.2, (150, 70, 74)); c.ball(17, 14, 11, 11, (200, 188, 164), 0.5)
    for (x1, y1, x2, y2) in ((9, 10, 12, 12), (10, 18, 13, 16), (24, 20, 21, 17), (23, 8, 20, 10)): c.line(x1, y1, x2, y2, (170, 70, 66))
    c.disc(17, 14, 5.4, (150, 150, 62)); c.ring(17, 14, 5.4, 4.2, (112, 112, 40)); c.rect(16, 10, 17, 18, (24, 22, 18), 0); c.put(14, 11, (240, 236, 210))
@sp('lurcher_tail', wear=0, grade=0.04)
def _(c, k, r):
    pts = bez((5, 26), (10, 31), (17, 11), (28, 5), 30); c.tube(pts, 3.8, 1.5, (86, 76, 88))
    for i in range(3, 28, 3): x, y = pts[i]; c.put(x - 1, y - 2, (136, 124, 142)); c.put(x, y - 3, (136, 124, 142))
    c.disc(5, 26, 3, (130, 44, 44)); c.disc(5, 26, 1.4, (214, 196, 170))
    for dx, dy in ((0, 0), (1, -2), (2, 1), (-1, 1), (2, -1)): c.put(28 + dx, 5 + dy, (40, 34, 44))
@sp('crawler_foot', wear=0, grade=0.04)
def _(c, k, r):
    c.poly([(3, 5), (11, 3), (14, 17), (6, 19)], (112, 118, 96), 0.15); c.ball(7, 4, 5, 2.5, (126, 40, 38), 0.3); c.disc(7, 4, 1.2, (206, 190, 164))
    c.tube(bez((9, 14), (10, 22), (16, 25), (24, 24), 14), 3.4, 2.2, (112, 118, 96))
    for (x, y) in ((30, 18), (30, 23), (29, 28)):
        c.tube(bez((24, 24), (26, 23), (27, y), (x - 1, y), 8), 1.7, 1.0, (112, 118, 96)); c.put(x, y, (214, 204, 176)); c.put(x - 1, y, (214, 204, 176))
    c.line(6, 12, 12, 14, (84, 90, 70)); c.line(7, 16, 13, 17, (84, 90, 70))
@sp('shroud_tentacle', wear=0, grade=0.03)
def _(c, k, r):
    for p in (bez((7, 29), (2, 19), (14, 17), (9, 4), 26), bez((15, 29), (21, 21), (8, 12), (17, 3), 26), bez((23, 29), (30, 19), (20, 14), (26, 5), 26)):
        c.tube(p, 3.1, 1.1, (54, 48, 66))
        for i in range(3, 24, 4): x, y = p[i]; c.put(x + 1, y, (104, 94, 124))
    c.rect(4, 28, 26, 30, (110, 44, 60), 0)
@sp('puppeteer_hand', wear=0, grade=0.04)
def _(c, k, r):
    skin = (134, 118, 130)
    c.tube([(16, 30), (16, 27), (16, 25)], 4, 4, skin); c.disc(16, 29, 3, (126, 44, 56))
    c.ball(16, 21, 7, 5.5, skin, 0.45)
    for (pts, rr) in (([(10, 21), (6, 18), (3, 14)], 1.6), ([(13, 17), (11, 10), (10, 3)], 1.5), ([(16, 16), (16, 8), (16, 1)], 1.5), ([(19, 17), (21, 10), (23, 4)], 1.5), ([(22, 19), (25, 15), (28, 10)], 1.4)):
        full = []
        for i in range(len(pts) - 1):
            for t in range(6): full.append((pts[i][0] + (pts[i + 1][0] - pts[i][0]) * t / 6, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t / 6))
        full.append(pts[-1]); c.tube(full, rr, rr * 0.6, skin); c.put(pts[-1][0], pts[-1][1], (56, 46, 62))
        for p in pts[1:-1]: c.put(p[0], p[1], sh(skin, 1.3))
    c.line(15, 20, 17, 25, (96, 84, 108))
@sp('stoneback_plate', wear=0, grade=0.05)
def _(c, k, r):
    c.poly([(4, 10), (14, 4), (27, 7), (29, 20), (20, 28), (7, 26), (3, 18)], (98, 96, 90), 0.2)
    for _i in range(9): c.disc(r.uniform(7, 25), r.uniform(8, 25), r.uniform(1, 2.4), (72, 70, 66))
    for (x1, y1, x2, y2) in ((8, 12, 15, 16), (17, 9, 22, 18), (11, 21, 21, 23)): c.line(x1, y1, x2, y2, (58, 56, 52))
    c.poly([(3, 18), (7, 26), (8, 29), (3, 24)], (126, 48, 46)); c.line(14, 5, 26, 8, (134, 130, 120))
@sp('hurler_gland', wear=0, grade=0.04)
def _(c, k, r):
    c.tube(bez((16, 29), (14, 26), (18, 24), (16, 21), 10), 2.4, 1.6, (126, 52, 60))
    c.ball(16, 14, 10, 8.5, (176, 128, 112), 0.5)
    for (x1, y1, x2, y2) in ((9, 11, 13, 15), (21, 9, 18, 14), (12, 19, 17, 17), (23, 17, 19, 15)): c.line(x1, y1, x2, y2, (120, 62, 84))
    c.disc(13, 11, 2.2, (214, 178, 150)); c.put(12, 10, (246, 228, 206))
@sp('ridgecat_claw', wear=0, grade=0.04)
def _(c, k, r):
    c.ball(10, 22, 7, 6, (110, 96, 78), 0.4); c.disc(6, 26, 2.4, (126, 44, 44)); c.disc(6, 26, 1.1, (214, 196, 170))
    for i in range(0, 20, 5): c.put(6 + i * 0.4, 18 + (i % 3), (60, 50, 42))
    pts = bez((13, 19), (17, 10), (24, 5), (29, 9), 20); c.tube(pts, 2.8, 0.6, (60, 54, 48))
    for i in range(0, 18, 3): x, y = pts[i]; c.put(x, y - 1, (150, 140, 124))

# ======================================================================= artifacts (organic / mineral, glowing; shapes all different)
def star_pts(cx, cy, ro, ri, n=5, rot=-90):
    return [(cx + (ro if i % 2 == 0 else ri) * math.cos(math.radians(rot + 180 * i / n)), cy + (ro if i % 2 == 0 else ri) * math.sin(math.radians(rot + 180 * i / n))) for i in range(2 * n)]
@sp('moth_wing', wear=0, grade=0)
def _(c, k, r):
    c.sym([(15, 13), (6, 3), (1, 9), (2, 17), (15, 20)], (178, 190, 204), 0.1); c.sym([(15, 19), (3, 19), (4, 27), (10, 29), (15, 24)], (160, 176, 196), 0.12)
    for (x2, y2) in ((6, 6), (3, 10), (4, 15), (6, 20), (6, 25)): c.line(15, 17, x2, y2, (116, 136, 160)); c.line(16, 17, 31 - x2, y2, (116, 136, 160))
    c.disc(8, 12, 2.2, (214, 232, 248)); c.disc(23, 12, 2.2, (214, 232, 248)); c.put(8, 12, (96, 120, 160)); c.put(23, 12, (96, 120, 160))
    c.ball(15.5, 16, 1.8, 7, (200, 208, 214), 0.4); c.line(14, 9, 11, 3, (170, 182, 196)); c.line(17, 9, 20, 3, (170, 182, 196)); c.put(12, 13, (240, 248, 255))
@sp('gravel_heart', wear=0, grade=0)
def _(c, k, r):
    t = Cv(); t.ball(10.5, 11, 7.5, 7.5, (150, 154, 160), 0.2); t.ball(21.5, 11, 7.5, 7.5, (150, 154, 160), 0.2); t.poly([(3.5, 13), (28.5, 13), (16, 29)], (150, 154, 160), 0.2)
    peb = Cv()
    for i in range(46):
        x, y = r.uniform(3, 29), r.uniform(4, 28); v = r.randint(112, 178); peb.ball(x, y, r.uniform(1.8, 3.2), r.uniform(1.6, 2.8), (v, v + 3, v + 8), 0.5)
    for i in range(1024):
        if t.p[i] is not None: c.p[i] = peb.p[i] if peb.p[i] is not None else t.p[i]
    c.ball(16, 14, 3.4, 3.4, (170, 210, 236), 0.3); c.put(15, 13, (240, 250, 255))
@sp('hollow_star', wear=0, grade=0)
def _(c, k, r):
    c.poly(star_pts(16, 17, 14.5, 6.5), (190, 194, 200), 0.18)
    inner = star_pts(16, 17, 6.2, 2.8)
    hole = Cv(); hole.poly(inner, (1, 1, 1))
    for i in range(1024):
        if hole.p[i] is not None: c.p[i] = None
    for y in range(32):
        for x in range(32):
            if c.get(x, y) is not None and any(hole.get(x + dx, y + dy) is not None for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))): c.put(x, y, (190, 224, 246))
    c.line(16, 4, 18, 9, (232, 236, 240)); c.put(15, 5, (255, 255, 255))
@sp('ember_knot', wear=0, grade=0)
def _(c, k, r):
    pts = [(16 + 4.4 * (math.sin(t) + 2 * math.sin(2 * t)), 16 + 4.4 * (math.cos(t) - 2 * math.cos(2 * t)) * 0.9) for t in [i * math.pi * 2 / 90 for i in range(91)]]
    c.tube(pts, 2.9, 2.9, (52, 44, 42))
    for i in range(0, 90, 2):
        if (i // 2) % 3: c.put(pts[i][0] - 0.5, pts[i][1] - 0.5, (216, 116, 36))
    for i in range(1, 90, 9): c.put(pts[i][0], pts[i][1], (250, 196, 90))
@sp('night_candle', wear=0, grade=0)
def _(c, k, r):
    c.rect(11, 13, 20, 27, (42, 36, 40), 0.18); c.line(13, 14, 13, 26, (78, 66, 70)); c.rect(9, 27, 22, 29, (110, 108, 104), 0.2)
    c.poly([(11, 13), (20, 13), (20, 20), (18, 17), (16, 22), (14, 16), (11, 19)], (66, 56, 60), 0.1); c.rect(15, 9, 16, 13, (24, 20, 22), 0)
    c.ball(15.5, 6, 3, 5, (222, 126, 40), 0.35); c.ball(15.5, 7, 1.4, 2.8, (252, 206, 100), 0.2); c.put(15, 3, (222, 126, 40))
@sp('static_pearl', wear=0, grade=0)
def _(c, k, r):
    for pts in (((16, 7), (11, 5), (14, 3), (9, 1)), ((24, 11), (28, 9), (26, 6), (30, 4)), ((25, 22), (29, 24), (26, 27), (30, 29)), ((7, 22), (3, 24), (6, 27), (2, 29)), ((8, 10), (4, 9), (6, 6), (2, 4))):
        for i in range(len(pts) - 1): c.line(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], (150, 232, 250))
    c.ball(16, 17, 10, 10, (190, 214, 222), 0.55); c.ball(18, 19, 5, 5, (150, 190, 214), 0.3); c.disc(12, 12, 2, (246, 252, 255)); c.put(21, 21, (170, 240, 255))
@sp('spindle', wear=0, grade=0)
def _(c, k, r):
    d = (0.707, -0.707); n = (0.707, 0.707); A = (3, 28.5); L = 35
    up, dn = [], []
    for i in range(0, 21):
        t = i / 20; w = 6.2 * math.sin(math.pi * t) ** 0.8
        cx, cy = A[0] + d[0] * L * t, A[1] + d[1] * L * t
        up.append((cx + n[0] * w, cy + n[1] * w)); dn.append((cx - n[0] * w, cy - n[1] * w))
    c.poly(up + dn[::-1], (56, 64, 76), 0.12)
    for t in [0.14 + 0.058 * j for j in range(13)]:
        w = 6.2 * math.sin(math.pi * t) ** 0.8; cx, cy = A[0] + d[0] * L * t, A[1] + d[1] * L * t
        c.line(cx + n[0] * w * 0.9, cy + n[1] * w * 0.9, cx - n[0] * w * 0.9, cy - n[1] * w * 0.9, (112, 214, 232) if int(t * 100) % 2 else (70, 130, 150))
    c.put(28, 3, (240, 252, 255)); c.put(27, 3, (160, 236, 250)); c.put(3, 29, (160, 236, 250))
@sp('rust_shell', wear=0, grade=0)
def _(c, k, r):
    c.ball(15, 16, 12.5, 12, (136, 76, 44), 0.55)
    for (cx, cy, rr) in ((15, 16, 9.5), (16, 16, 6.8), (16.5, 16, 4.4)): c.ring(cx, cy, rr, rr - 1.1, (96, 52, 30))
    c.ring(14, 15, 11, 10.3, (172, 100, 56)); c.disc(16.5, 16, 1.6, (176, 210, 80))
    c.poly([(22, 22), (28, 20), (29, 27), (22, 28)], (96, 56, 34), 0.15); c.ball(26, 24, 2.6, 3, (30, 22, 18), 0.2)
    for (x, y) in ((8, 22), (11, 25), (20, 25), (10, 9), (18, 6)): c.disc(x, y, 1.5, (122, 164, 54))
    c.put(9, 8, (206, 238, 130))
@sp('blood_coral', wear=0, grade=0)
def _(c, k, r):
    def branch(x, y, a, L, rr, d):
        x2 = x + math.sin(a) * L; y2 = y - math.cos(a) * L
        c.tube([(x + (x2 - x) * i / 6, y + (y2 - y) * i / 6) for i in range(7)], rr, max(0.8, rr * 0.7), (126, 42, 40))
        if d > 0:
            branch(x2, y2, a - 0.7, L * 0.78, rr * 0.7, d - 1); branch(x2, y2, a + 0.65, L * 0.78, rr * 0.7, d - 1)
            if d > 1: branch(x2, y2, a + 0.05, L * 0.7, rr * 0.7, d - 1)
        else: c.put(x2, y2, (150, 214, 70)); c.put(x2 - 1, y2 - 1, (210, 244, 130))
    c.ball(16, 29, 8, 2.5, (96, 52, 40), 0.3); branch(16, 29, 0.05, 7.5, 2.6, 3)
@sp('iron_moss', wear=0, grade=0)
def _(c, k, r):
    c.poly([(3, 19), (10, 16), (22, 16), (29, 20), (27, 28), (6, 28)], (92, 82, 78), 0.2)
    c.line(6, 24, 14, 22, (122, 72, 46)); c.line(18, 25, 25, 23, (122, 72, 46)); c.line(8, 27, 25, 27, (62, 56, 54))
    for i in range(18): c.ball(r.uniform(5, 27), r.uniform(9, 20), r.uniform(2.2, 3.6), r.uniform(2, 3.2), (r.randint(64, 96), r.randint(94, 124), r.randint(40, 62)), 0.55)
    for i in range(6): c.put(r.uniform(6, 26), r.uniform(8, 18), (150, 196, 64))
@sp('glass_eye', wear=0, grade=0)
def _(c, k, r):
    c.ball(16, 16, 12.5, 12.5, (108, 72, 140), 0.55); c.disc(16, 17, 6.6, (170, 130, 206)); c.ring(16, 17, 6.6, 5.4, (90, 56, 120)); c.disc(16, 17, 3.2, (22, 14, 34)); c.rect(16, 14, 16, 20, (8, 4, 14), 0)
    c.disc(10, 9, 2.4, (214, 196, 238)); c.put(10, 9, (255, 255, 255)); c.put(21, 22, (176, 140, 214))
@sp('mothers_tear', wear=0, grade=0)
def _(c, k, r):
    c.poly([(16, 1), (7.5, 17), (24.5, 17)], (134, 90, 176), 0.15); c.ball(16, 21, 10, 9.5, (128, 84, 170), 0.55); c.ball(18, 23, 5, 5, (176, 134, 214), 0.3)
    c.line(16, 4, 16, 14, (176, 140, 210)); c.line(11, 20, 14, 26, (96, 58, 132)); c.disc(11, 17, 1.8, (232, 214, 246)); c.put(10, 16, (255, 255, 255))
    c.line(26, 6, 26, 10, (226, 206, 244)); c.line(24, 8, 28, 8, (226, 206, 244))


# ======================================================================= driver
WEAR = {'weapon': 0.7, 'melee': 0.6, 'attach': 0.6, 'ammo': 0.35, 'suit': 0.5, 'helmet': 0.5, 'detector': 0.4, 'trophy': 0.0, 'artifact': 0.0, 'med': 0.1, 'food': 0.08}

def paint(zid, cat):
    c = Cv(); rnd = random.Random(zlib.crc32(zid.encode()))
    f, kw = PAINT[zid]; f(c, zid, rnd)
    kw = dict(kw); kw.setdefault('wear', WEAR.get(cat, 0.3))
    if cat == 'artifact': kw.setdefault('grade', 0); kw.setdefault('noise', 0.03)
    return finish(c, rnd, **kw)

def jdump(path, obj, **kw):
    with open(path, 'w') as f: json.dump(obj, f, separators=(',', ':'), ensure_ascii=False, **kw)

def register(ids):
    tp = os.path.join(DATA, 'tiles.json'); t = json.load(open(tp)); ch = False
    for i in ids:
        n = 'zone_' + i; path = 'textures/item/%s.png' % n
        if n in t['names']:
            idx = t['names'][n]
        else:
            idx = t['count']; t['names'][n] = idx; t['files'].append(path); t['res'].append(32); t['count'] += 1; ch = True
        if t['files'][idx] != path or t['res'][idx] != 32: t['files'][idx] = path; t['res'][idx] = 32; ch = True
    if ch: jdump(tp, t)
    sp_ = os.path.join(DATA, 'itemshapes.json'); s = json.load(open(sp_)); have = set(s['itemTiles']); add = ['zone_' + i for i in ids if 'zone_' + i not in have]
    if add: s['itemTiles'] += add; jdump(sp_, s)
    return ch or bool(add), len(add)

def sheet(ids, sprites, path, cols=14, S=3):
    try: from PIL import Image, ImageDraw
    except ImportError:
        from zonepix import write_png
        rows = (len(ids) + cols - 1) // cols; W = cols * 34 * S; H = rows * 34 * S; px = [(70, 68, 64)] * (W * H)
        for n, s in enumerate(sprites):
            ox, oy = (n % cols) * 34 * S + S, (n // cols) * 34 * S + S
            for y in range(32 * S):
                for x in range(32 * S):
                    p = s[(y // S) * 32 + x // S]
                    if p: px[(oy + y) * W + ox + x] = p
        write_png(path, W, H, px); return
    cw, ch_ = 32 * S + 6, 32 * S + 16; rows = (len(ids) + cols - 1) // cols
    im = Image.new('RGB', (cols * cw, rows * ch_), (70, 68, 64)); d = ImageDraw.Draw(im)
    for n, (i, s) in enumerate(zip(ids, sprites)):
        ox, oy = (n % cols) * cw + 3, (n // cols) * ch_ + 2
        for y in range(32):
            for x in range(32):
                p = s[y * 32 + x]
                if p: d.rectangle([ox + x * S, oy + y * S, ox + x * S + S - 1, oy + y * S + S - 1], fill=p)
        d.text((ox, oy + 32 * S + 1), i[:16], fill=(210, 206, 190))
    im.save(path)

def main():
    z = json.load(open(os.path.join(DATA, 'zone_items.json'))); ids = list(z)
    miss = [i for i in ids if i not in PAINT]
    if miss: print('no painter for:', ', '.join(miss), file=sys.stderr)
    ids = [i for i in ids if i in PAINT]
    out = os.path.join(DATA, 'textures', 'item'); os.makedirs(out, exist_ok=True)
    sprites = []
    for i in ids:
        cv = paint(i, z[i]['cat']); sprites.append(cv.p)
        from zonepix import write_png; write_png(os.path.join(out, 'zone_%s.png' % i), 32, 32, cv.p)
    print('painted %d sprites -> data/textures/item/zone_*.png' % len(ids))
    if '--no-register' not in sys.argv:
        ch, n = register(ids); print('registered: %d new names%s' % (n, '' if ch else ' (already up to date)'))
    if '--sheet' in sys.argv:
        sheet(ids, sprites, sys.argv[sys.argv.index('--sheet') + 1]); print('contact sheet written')

if __name__ == '__main__': main()
