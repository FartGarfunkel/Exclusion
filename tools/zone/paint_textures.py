#!/usr/bin/env python3
# tools/zone/paint_textures.py — paints the "Exclusion Zone" resource pack (data/resourcepacks/Exclusion Zone/):
# 32x32 block tiles in a muted, worn, overcast Misery / STALCRAFT look, plus drab grass + foliage colour maps.
# Pure Python (zonepix writes the PNGs with zlib), deterministic. The game lays this pack over "DokuQuest Base"
# (assets/packs.cpp), so only the tiles painted here change; file names are the first picture name of the tile in
# data/packmap.json. Tinted tiles (grass top/fringe, tall grass, fern, birch + acacia leaves) are painted in grey
# on purpose: the engine multiplies them by colormap/grass.png and foliage.png, which are painted here too.
#   python3 tools/zone/paint_textures.py            # writes the pack
#   python3 tools/zone/paint_textures.py --list     # tile names only
import math, os, random, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from zonepix import write_png

N = 32
OUT = os.path.normpath(os.path.join(HERE, '..', '..', 'data', 'resourcepacks', 'Exclusion Zone'))
TEX = os.path.join(OUT, 'assets', 'minecraft', 'textures')

# ---------------------------------------------------------------- maths
def lerp(a, b, t): return a + (b - a) * t
def mixc(a, b, t): return (lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t))
def sstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0 + 1e-9))); return t * t * (3 - 2 * t)
def lum(c): return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
def mulc(c, k): return (c[0] * k, c[1] * k, c[2] * k)
def ramp(cols, v):
    v = max(0.0, min(0.9999, v)) * (len(cols) - 1); i = int(v); return mixc(cols[i], cols[i + 1], v - i)

_lat = {}
def _lattice(seed, px, py):
    k = (seed, px, py)
    if k not in _lat:
        r = random.Random(seed * 7919 + px * 131 + py * 17)
        _lat[k] = [[r.random() for _ in range(px)] for _ in range(py)]
    return _lat[k]

def vnoise(seed, px, py, x, y):                       # value noise that tiles every 32 px
    L = _lattice(seed, px, py); fx = x * px / N; fy = y * py / N
    x0 = int(math.floor(fx)); y0 = int(math.floor(fy)); tx = fx - x0; ty = fy - y0
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty)
    x0 %= px; y0 %= py; x1 = (x0 + 1) % px; y1 = (y0 + 1) % py
    a, b, c, d = L[y0][x0], L[y0][x1], L[y1][x0], L[y1][x1]
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty

def fbm(seed, base=4, octs=3, ax=1.0, ay=1.0):        # 32x32 field 0..1 (ax/ay stretch: ax<1 = streaks along x)
    f = [[0.0] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            v = 0.0; amp = 1.0; tot = 0.0
            for o in range(octs):
                px = max(1, int(round(base * ax * 2 ** o))); py = max(1, int(round(base * ay * 2 ** o)))
                v += amp * vnoise(seed * 31 + o * 17, px, py, x, y); tot += amp; amp *= 0.5
            f[y][x] = v / tot
    lo = min(map(min, f)); hi = max(map(max, f))
    return [[(v - lo) / (hi - lo + 1e-9) for v in r] for r in f]

def voronoi(seed, cells, jit=0.85):                   # tiling cell pattern: ids, distance to nearest + second nearest
    r = random.Random(seed); cs = N / cells
    pts = [[((i + .5 + (r.random() - .5) * jit) * cs, (j + .5 + (r.random() - .5) * jit) * cs) for i in range(cells)] for j in range(cells)]
    cid = [[0] * N for _ in range(N)]; d1 = [[0.0] * N for _ in range(N)]; d2 = [[0.0] * N for _ in range(N)]
    off = [[(0.0, 0.0)] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            cx = int((x + .5) // cs); cy = int((y + .5) // cs); b1 = b2 = 1e9; bi = 0; bo = (0.0, 0.0)
            for dj in (-1, 0, 1):
                for di in (-1, 0, 1):
                    ci = cx + di; cj = cy + dj
                    px, py = pts[cj % cells][ci % cells]; px += (ci // cells) * N; py += (cj // cells) * N
                    d = math.hypot(x + .5 - px, y + .5 - py)
                    if d < b1: b2 = b1; b1 = d; bi = (cj % cells) * cells + ci % cells; bo = (x + .5 - px, y + .5 - py)
                    elif d < b2: b2 = d
            cid[y][x] = bi; d1[y][x] = b1; d2[y][x] = b2; off[y][x] = bo
    return cid, d1, d2, off

# ---------------------------------------------------------------- tile canvas
class T:
    def __init__(s, seed, base=(110, 110, 110)):
        s.seed = seed; s.rng = random.Random(seed * 99991 + 7)
        s.p = [[list(base) for _ in range(N)] for _ in range(N)]
        s.a = [[255] * N for _ in range(N)]
    def nz(s, k, base=4, octs=3, ax=1.0, ay=1.0): return fbm(s.seed * 101 + k, base, octs, ax, ay)
    def fill(s, f, cols):
        for y in range(N):
            for x in range(N): s.p[y][x] = list(ramp(cols, f[y][x]))
    def shade(s, f, lo, hi):
        for y in range(N):
            for x in range(N):
                k = lo + (hi - lo) * f[y][x]; q = s.p[y][x]; q[0] *= k; q[1] *= k; q[2] *= k
    def blend(s, f, t0, t1, col, k=1.0):
        for y in range(N):
            for x in range(N):
                w = sstep(t0, t1, f[y][x]) * k
                if w > 0: s.p[y][x] = list(mixc(s.p[y][x], col, w))
    def grain(s, amt):
        for y in range(N):
            for x in range(N):
                k = 1 + (s.rng.random() - .5) * 2 * amt; q = s.p[y][x]; q[0] *= k; q[1] *= k; q[2] *= k
    def dot(s, x, y, col, k=1.0):
        x = int(round(x)) % N; y = int(round(y)) % N; s.p[y][x] = list(mixc(s.p[y][x], col, k))
    def scatter(s, n, cols, k=1.0, shadow=None, size=1):
        for _ in range(n):
            x = s.rng.randrange(N); y = s.rng.randrange(N); c = s.rng.choice(cols)
            for dy in range(size):
                for dx in range(size): s.dot(x + dx, y + dy, c, k)
            if shadow: s.dot(x + size, y + size, shadow, 0.5)
    def line(s, x0, y0, x1, y1, col, k=1.0):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            f = i / n; s.dot(x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, col, k)
    def walk(s, x, y, ang, n, col, k=1.0, wob=.55, lip=None):
        for _ in range(n):
            s.dot(x, y, col, k)
            if lip: s.dot(x + 1, y, lip, k * .45)
            ang += (s.rng.random() - .5) * 2 * wob; x += math.cos(ang); y += math.sin(ang)
    def streak(s, k, col, strength=.5, thr=.55, base=3):   # vertical water / rust runs
        s.blend(s.nz(k, base, 2, ax=1.3, ay=.22), thr, thr + .25, col, strength)
    def edges(s, dark, light=None):                        # panel seam: darker top + left, lighter next row
        for i in range(N):
            for (x, y) in ((i, 0), (0, i)):
                q = s.p[y][x]; q[0] *= dark; q[1] *= dark; q[2] *= dark
            if light:
                for (x, y) in ((i, 1), (1, i)):
                    q = s.p[y][x]; q[0] *= light; q[1] *= light; q[2] *= light
    def out(s, sat=1.0, gain=1.0):
        px = []
        for y in range(N):
            for x in range(N):
                if s.a[y][x] < 128: px.append(None); continue
                c = s.p[y][x]; l = lum(c)
                px.append(tuple(max(0, min(255, int((l + (v - l) * sat) * gain + .5))) for v in c))
        return px

TILES = {}                                            # file name -> function returning pixels
def tile(name):
    def deco(fn): TILES[name] = fn; return fn
    return deco
def grey(v): return (v, v, v)

# ---------------------------------------------------------------- earth
def dirt_t(seed, lo=(70, 54, 40), hi=(104, 82, 60), pebbles=9, coarse=0.0):
    t = T(seed); t.fill(t.nz(1, 4, 4), [lo, mixc(lo, hi, .55), hi])
    t.blend(t.nz(2, 7, 2), .70, .95, mulc(lo, .72), .7)
    t.blend(t.nz(3, 3, 2), .62, .92, (112, 98, 78), .35)                       # dry dust
    if coarse:
        cid, d1, d2, off = voronoi(seed + 5, 8, .9); rr = random.Random(seed)
        tone = [rr.uniform(.8, 1.2) for _ in range(64)]; n = t.nz(4, 3, 2)
        for y in range(N):
            for x in range(N):
                if n[y][x] > 1 - coarse * 2.4:
                    e = d2[y][x] - d1[y][x]; c = mulc((104, 98, 88), tone[cid[y][x]] * (1.08 - .07 * d1[y][x]))
                    if e < 1.0: c = mixc(c, (60, 50, 40), .8)
                    t.p[y][x] = list(c)
    t.scatter(pebbles, [(118, 108, 92), (96, 90, 80), (132, 124, 108)], 1.0, shadow=(40, 31, 24))
    t.scatter(14, [(46, 36, 28), (58, 44, 30)], .8)
    t.grain(.06); return t

@tile('dirt')
def _dirt(): return dirt_t(11).out(.82)
@tile('coarse_dirt')
def _coarse(): return dirt_t(12, (74, 60, 46), (108, 90, 68), 14, .17).out(.78)

def cap_over(base, cap, seed, depth=5, var=3, shadow=.72, tuft=False):    # cap picture hangs over the top of a side picture
    t = T(seed); t.p = [[list(c) for c in r] for r in base.p]; f = fbm(seed, 6, 2)[0]; rr = random.Random(seed)
    for x in range(N):
        d = depth + int(var * f[x] + .5) + (rr.randint(2, 4) if tuft and rr.random() < .2 else 0)
        for y in range(min(d, N)):
            t.p[y][x] = list(cap.p[y][x])
        if d - 1 < N:
            q = t.p[d - 1][x]; q[0] *= shadow; q[1] *= shadow; q[2] *= shadow
        t.a[0][x] = 255
    return t

def podzol_t(seed, lo, hi, litter=False):
    t = T(seed); t.fill(t.nz(1, 5, 3), [lo, mixc(lo, hi, .5), hi])
    t.blend(t.nz(2, 4, 2), .55, .85, (96, 78, 48), .35)
    for _ in range(95 if litter else 55):                                           # fallen needles
        x = t.rng.randrange(N); y = t.rng.randrange(N); a = t.rng.uniform(-.9, .9) + (math.pi / 2 if t.rng.random() < .35 else 0)
        c = t.rng.choice([(104, 80, 50), (82, 60, 38), (126, 98, 60), (52, 38, 26)] if litter else [(96, 76, 48), (64, 48, 32), (112, 90, 56)])
        for i in range(t.rng.randint(3, 5)): t.dot(x + math.cos(a) * i, y + math.sin(a) * i, c, .85)
    t.grain(.06); return t

@tile('podzol_top')
def _pz_top(): return podzol_t(13, (50, 38, 28), (82, 64, 42)).out(.78)
@tile('podzol_side')
def _pz_side(): return cap_over(dirt_t(11), podzol_t(13, (50, 38, 28), (82, 64, 42)), 14, 5, 3).out(.8)
@tile('coniferous_litter')
def _lit_top(): return podzol_t(15, (46, 34, 24), (74, 54, 34), True).out(.8)
@tile('coniferous_litter_side')
def _lit_side(): return cap_over(dirt_t(11), podzol_t(15, (46, 34, 24), (74, 54, 34), True), 16, 6, 3, tuft=True).out(.8)

@tile('dry_grass_top')
def _dg_top():
    t = T(17); t.fill(t.nz(1, 4, 3), [(104, 92, 62), (130, 116, 80), (150, 134, 94)])
    t.blend(t.nz(2, 3, 2), .6, .9, (88, 80, 62), .6)
    for _ in range(110):                                                           # dead straw blades
        x = t.rng.randrange(N); y = t.rng.randrange(N); a = t.rng.uniform(-1.1, 1.1); c = t.rng.choice([(168,152,108),(86,74,52),(144,126,86),(118,100,66)])
        for i in range(t.rng.randint(2, 5)): t.dot(x + math.sin(a) * i, y - math.cos(a) * i, c, .85)
    t.scatter(6, [(70,58,40)], .8); t.grain(.05); return t.out(.7)
@tile('dry_grass_side')
def _dg_side(): return cap_over(dirt_t(11), _raw(_dg_top), 18, 5, 3, tuft=True).out(.82)

def _raw(fn):                                                                       # painter -> canvas (for compositing)
    px = fn(); t = T(0)
    for i, c in enumerate(px): t.p[i // N][i % N] = list(c) if c else [0, 0, 0]; t.a[i // N][i % N] = 255 if c else 0
    return t

@tile('mud')
def _mud():
    t = T(19); t.fill(t.nz(1, 4, 4), [(40, 33, 28), (56, 46, 38), (74, 62, 50)])
    t.blend(t.nz(2, 5, 2), .72, .9, (30, 25, 22), .8)
    for _ in range(26):                                                            # wet sheen
        x = t.rng.randrange(N); y = t.rng.randrange(N)
        for i in range(t.rng.randint(1, 3)): t.dot(x + i, y, (104, 98, 90), .55)
    t.scatter(10, [(30,24,20)], .9); t.grain(.04); return t.out(.7)
@tile('packed_mud')
def _packed_mud():
    t = dirt_t(20, (84, 68, 52), (118, 98, 76), 8); t.blend(t.nz(5, 3, 2), .5, .8, (140,126,104), .22)
    for k in range(3): t.walk(t.rng.randrange(N), t.rng.randrange(N), t.rng.uniform(0, 6.28), 14, (48, 38, 30), .8)
    return t.out(.72)
@tile('peat')
def _peat():
    t = T(21); t.fill(t.nz(1, 3, 3, ax=2, ay=1), [(26, 21, 17), (42, 33, 25), (62, 48, 34)])
    for _ in range(70):
        x = t.rng.randrange(N); y = t.rng.randrange(N); c = t.rng.choice([(78, 58, 36), (22, 17, 14), (92, 70, 44), (52, 40, 28)])
        for i in range(t.rng.randint(2, 6)): t.dot(x + i, y + (1 if i > 3 else 0), c, .8)
    t.grain(.06); return t.out(.7)
@tile('moss_block')
def _moss():
    t = T(22); t.fill(t.nz(1, 7, 3), [(46, 56, 30), (66, 78, 42), (92, 102, 60)])
    t.blend(t.nz(2, 4, 2), .65, .9, (36, 44, 28), .7)
    t.scatter(60, [(112, 122, 74), (84, 94, 54)], .6); t.scatter(30, [(30, 38, 22)], .7); t.grain(.07); return t.out(.65)

@tile('gravel')
def _gravel():
    t = T(23); cid, d1, d2, off = voronoi(23, 9, .95); rr = random.Random(23)
    tones = [(mulc(rr.choice([(96, 94, 90), (110, 100, 88), (88, 92, 96), (104, 102, 98), (82, 78, 72)]), rr.uniform(.8, 1.15))) for _ in range(81)]
    for y in range(N):
        for x in range(N):
            e = d2[y][x] - d1[y][x]; c = mulc(tones[cid[y][x]], 1.12 - .09 * d1[y][x] - .035 * (off[y][x][0] + off[y][x][1]))
            if e < 1.1: c = mixc(c, (46, 43, 40), .85 * (1 - sstep(.2, 1.1, e)))
            t.p[y][x] = list(c)
    t.grain(.05); return t.out(.7)
@tile('sand')
def _sand():
    t = T(24); t.fill(t.nz(1, 4, 3), [(150, 140, 114), (170, 160, 132), (186, 176, 148)])
    ripple = t.nz(2, 3, 2, ax=1, ay=2.5); t.shade(ripple, .9, 1.06)
    t.scatter(26, [(120, 108, 86), (204, 196, 170), (96, 92, 82)], .7); t.grain(.045); return t.out(.62)
@tile('dirt_path_top')
def _path_top():
    t = dirt_t(25, (92, 78, 56), (126, 110, 82), 12); t.blend(t.nz(6, 3, 2), .55, .85, (146, 134, 108), .3)
    t.walk(2, 8, .1, 30, (66, 54, 40), .4); t.walk(1, 24, -.05, 30, (66, 54, 40), .35)   # ruts
    return t.out(.74)
@tile('dirt_path_side')
def _path_side():
    t = cap_over(dirt_t(11), _raw(_path_top), 26, 5, 2); px = t.out(.8)
    for i in range(2 * N): px[i] = None                                             # the path sits 2 px low
    return px

@tile('grass_block_top')                                                              # TINTED: grey on purpose
def _grass_top():
    t = T(27); t.fill(t.nz(1, 3, 3), [grey(136), grey(164), grey(188)])
    t.blend(t.nz(2, 3, 3), .64, .84, (112, 100, 92), .75)                            # worn, bare patches
    t.blend(t.nz(3, 4, 2), .7, .9, (186, 172, 148), .55)                              # dead straw
    for _ in range(120):
        x = t.rng.randrange(N); y = t.rng.randrange(N); c = grey(t.rng.choice([108, 124, 204, 216]))
        for i in range(t.rng.randint(2, 4)): t.dot(x + (1 if i > 2 else 0), y - i, c, .8)
    t.grain(.05); return t.out(1.0)
@tile('grass_block_side')
def _grass_side(): return dirt_t(11).out(.82)
@tile('grass_block_side_overlay')                                                      # TINTED fringe, cut out
def _grass_over():
    top = T(27); top.fill(top.nz(1, 3, 3), [grey(136), grey(164), grey(188)]); top.grain(.06)
    t = cap_over(T(0), top, 28, 5, 3, shadow=.78, tuft=True)
    f = fbm(28, 6, 2)[0]; rr = random.Random(28)
    for x in range(N):
        d = 5 + int(3 * f[x] + .5) + (rr.randint(2, 4) if rr.random() < .2 else 0)
        for y in range(N): t.a[y][x] = 255 if y < d else 0
    return t.out(1.0)

# ---------------------------------------------------------------- stone, brick, concrete
@tile('stone')
def _stone():
    t = T(31); t.fill(t.nz(1, 3, 4), [(88, 89, 86), (110, 111, 107), (130, 131, 126)])
    t.blend(t.nz(2, 8, 2), .70, .95, (66, 67, 64), .8); t.blend(t.nz(3, 6, 2), .78, .98, (146, 146, 140), .5)
    t.streak(4, (70, 66, 58), .45, .56); t.scatter(14, [(64, 64, 62)], .9)
    t.walk(5, 20, .3, 18, (58, 58, 56), .9, .5, (128, 128, 124)); t.grain(.05); return t.out(.75)
@tile('andesite')
def _andesite():
    t = T(32); t.fill(t.nz(1, 4, 3), [(92, 94, 90), (108, 110, 106), (124, 125, 120)])
    t.scatter(110, [(144, 144, 138), (72, 73, 71), (130, 128, 120)], .85); t.streak(2, (74, 70, 62), .35, .6); t.grain(.05); return t.out(.65)

def cobble_t(seed, moss=False):
    t = T(seed); cid, d1, d2, off = voronoi(seed, 5, .9); rr = random.Random(seed)
    tones = [mulc(rr.choice([(112, 111, 106), (104, 104, 100), (116, 108, 98), (98, 102, 104)]), rr.uniform(.78, 1.12)) for _ in range(25)]
    m = t.nz(4, 3, 3); g = t.nz(5, 7, 2)
    for y in range(N):
        for x in range(N):
            e = d2[y][x] - d1[y][x]; ox, oy = off[y][x]
            c = mulc(tones[cid[y][x]], 1.13 - .085 * d1[y][x] - .035 * (ox + oy) + (g[y][x] - .5) * .18)
            if e < 1.9: c = mixc(c, (44, 43, 40), 1 - sstep(.5, 1.9, e))
            if moss:
                w = sstep(.52, .78, m[y][x] + (.28 if e < 3 else 0)) * .92
                if w > 0: c = mixc(c, mixc((52, 64, 38), (84, 96, 56), g[y][x]), w)
            t.p[y][x] = list(c)
    t.grain(.05); return t
@tile('cobblestone')
def _cobble(): return cobble_t(33).out(.72)
@tile('mossy_cobblestone')
def _mcobble(): return cobble_t(33, True).out(.7)

def brick_courses(t, ph, bw, tones, mortar, fine, mf, bevel=.1, jitter=0):
    rr = t.rng; rows = N // ph
    for row in range(rows):
        off = (row % 2) * (bw // 2)
        for b in range(N // bw):
            tone = rr.choice(tones); k = rr.uniform(.9, 1.1); x0 = b * bw + off
            for yy in range(ph):
                y = row * ph + yy
                for xx in range(bw):
                    x = (x0 + xx) % N
                    if yy == ph - 1 or xx == bw - 1:
                        t.p[y][x] = list(mulc(mortar, .62 + .7 * mf[y][x])); continue
                    k2 = .86 + .28 * fine[y][x]
                    if yy == 0: k2 *= 1 + bevel
                    elif yy == ph - 2: k2 *= 1 - bevel
                    if xx == 0: k2 *= 1 + bevel * .6
                    t.p[y][x] = list(mulc(tone, k * k2))

@tile('bricks')                                                                          # Soviet red-brown brick, dirty mortar
def _bricks():
    t = T(41); fine = t.nz(1, 9, 2); mf = t.nz(2, 6, 3)
    brick_courses(t, 4, 16, [(122, 70, 52), (112, 64, 48), (134, 80, 58), (98, 58, 46), (118, 78, 60), (86, 52, 42), (126, 74, 54)], (126, 120, 106), fine, mf, .09)
    t.streak(3, (36, 32, 30), .55, .52); t.blend(t.nz(4, 3, 2), .76, .96, (176, 168, 148), .22)   # soot, salts
    for _ in range(9):                                                                    # spalled corners
        x = t.rng.randrange(N); y = t.rng.randrange(N)
        for dx, dy in ((0, 0), (1, 0), (0, 1)): t.dot(x + dx, y + dy, (92, 86, 76), .9)
    t.grain(.05); return t.out(.8)

def stonebrick_t(seed, cracked=False):
    t = T(seed); fine = t.nz(1, 8, 3); mf = t.nz(2, 5, 3)
    brick_courses(t, 8, 16, [(120, 121, 117), (110, 111, 108), (128, 128, 123), (104, 106, 104), (116, 114, 106)], (62, 63, 60), fine, mf, .09)
    t.streak(3, (60, 58, 50), .5, .55); t.blend(t.nz(4, 4, 2), .72, .92, (80, 86, 62), .35)
    if cracked:
        for _ in range(4): t.walk(t.rng.randrange(N), t.rng.randrange(N), t.rng.uniform(0, 6.28), t.rng.randint(12, 22), (40, 40, 38), 1.0, .5, (130, 130, 126))
        for _ in range(5):
            x = t.rng.randrange(N); y = t.rng.randrange(N)
            for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1), (2, 1)): t.dot(x + dx, y + dy, (54, 54, 51), .9)
    t.grain(.05); return t
@tile('stone_bricks')
def _sb(): return stonebrick_t(42).out(.72)
@tile('cracked_stone_bricks')
def _csb(): return stonebrick_t(43, True).out(.72)

def concrete_t(seed, lo, hi, stain, rust=0.0, pits=22, seam=.86, crack=False):
    t = T(seed); t.fill(t.nz(1, 3, 4), [lo, mixc(lo, hi, .5), hi]); t.shade(t.nz(2, 10, 2), .93, 1.07)
    for _ in range(pits):
        x = t.rng.randrange(N); y = t.rng.randrange(N); t.dot(x, y, mulc(lo, .55), .9); t.dot(x + 1, y + 1, mulc(hi, 1.1), .5)
    t.streak(3, stain, .6, .5)
    if rust: t.streak(4, (112, 70, 46), rust, .6)
    t.blend(t.nz(5, 3, 3), .72, .95, mulc(lo, .8), .55)
    if crack: t.walk(t.rng.randrange(N), 0, 1.4, 34, mulc(lo, .45), 1.0, .5, mulc(hi, 1.05))
    t.grain(.03); t.edges(seam, 1.035); return t
@tile('light_gray_concrete')
def _lgc(): return concrete_t(51, (122, 122, 118), (148, 148, 142), (84, 80, 72), .35).out(.8)
@tile('gray_concrete')
def _gc(): return concrete_t(52, (82, 83, 82), (106, 106, 104), (56, 54, 50), .25, 26, crack=True).out(.8)
@tile('white_concrete')
def _wc(): return concrete_t(53, (150, 148, 138), (182, 180, 170), (116, 106, 86), .22, 14, .88).out(.72)

@tile('black_concrete')                                                                   # the Zone's asphalt
def _asphalt():
    t = T(54); t.fill(t.nz(1, 4, 4), [(32, 33, 34), (42, 43, 44), (54, 55, 56)])
    t.blend(t.nz(2, 3, 3), .7, .92, (60, 61, 62), .5); t.blend(t.nz(3, 4, 2), .76, .95, (22, 22, 24), .8)   # sun-bleached, oil
    t.scatter(95, [(70, 70, 72), (88, 88, 88), (104, 102, 98), (54, 54, 56)], .85); t.scatter(14, [(112, 110, 104)], .8, size=2)
    for _ in range(3): t.walk(t.rng.randrange(N), t.rng.randrange(N), t.rng.uniform(0, 6.28), t.rng.randint(26, 40), (12, 12, 13), 1.0, .42, (74, 74, 74))
    t.grain(.04); return t.out(.55)
@tile('gray_concrete_powder')                                                             # broken tarmac / cracks
def _rubble():
    t = T(55); t.fill(t.nz(1, 5, 4), [(40, 41, 42), (60, 61, 62), (84, 85, 85)])
    t.scatter(80, [(110, 108, 104), (28, 28, 30), (92, 92, 92)], .9); t.scatter(10, [(120, 116, 108)], .9, size=2)
    for _ in range(5): t.walk(t.rng.randrange(N), t.rng.randrange(N), t.rng.uniform(0, 6.28), t.rng.randint(14, 30), (12, 12, 13), 1.0, .6, (92, 92, 92))
    t.grain(.06); return t.out(.5)
@tile('smooth_stone')
def _smooth():
    t = T(56); t.fill(t.nz(1, 3, 3), [(110, 110, 106), (126, 126, 121), (142, 141, 134)]); t.shade(t.nz(2, 9, 2), .94, 1.05)
    t.streak(3, (70, 64, 54), .35, .55); t.blend(t.nz(4, 3, 3), .74, .95, (84, 82, 74), .4)
    for _ in range(7): t.line(t.rng.randrange(N), t.rng.randrange(N), t.rng.randrange(N), t.rng.randrange(N), (160, 158, 150), .22)
    for i in range(N):                                                                      # slab bevel
        for (x, y, k) in ((i, 0, .72), (0, i, .78), (i, N - 1, .9), (N - 1, i, .92), (i, 1, 1.07), (1, i, 1.05)):
            q = t.p[y][x]; q[0] *= k; q[1] *= k; q[2] *= k
    t.grain(.03); return t.out(.7)

# ---------------------------------------------------------------- wood
def planks_t(seed, lo, hi, silver, gap=.5):
    t = T(seed); gf = t.nz(1, 4, 3, ax=.35, ay=2.0); rr = t.rng; sv = t.nz(2, 3, 3); dmp = t.nz(3, 4, 2)
    for b in range(4):
        tone = rr.uniform(.85, 1.12); sh = rr.randrange(N); jx = rr.randrange(N)
        for yy in range(8):
            y = b * 8 + yy
            for x in range(N):
                g = gf[y][(x + sh) % N]; c = mulc(ramp([lo, mixc(lo, hi, .5), hi], g), tone)
                w = sstep(.5, .8, sv[y][x]) * .78; c = mixc(c, mulc(silver, .6 + .5 * g), w)
                if dmp[y][x] > .78: c = mulc(c, .8)
                if yy == 0: c = mulc(c, gap)
                elif yy == 1: c = mulc(c, 1.07)
                elif yy == 7: c = mulc(c, .86)
                if x == jx and 0 < yy < 7: c = mulc(c, .55)
                t.p[y][x] = list(c)
        for nx in (3, 28):
            t.dot(nx, b * 8 + 3, (34, 30, 26), .9); t.dot(nx + 1, b * 8 + 3, mulc(silver, 1.1), .4)
    kx = rr.randrange(4, 26); ky = rr.randrange(2, 5) * 8 + 3
    for dx in range(-3, 4):
        for dy in range(-2, 3):
            r = math.hypot(dx / 3, dy / 2)
            if r < 1: t.dot(kx + dx, ky + dy, mulc(lo, .55 + .35 * r), .85 * (1 - r * .3))
    t.streak(4, (40, 36, 32), .4, .58); t.grain(.045); return t
@tile('spruce_planks')
def _spl(): return planks_t(61, (70, 60, 44), (110, 96, 70), (128, 122, 110)).out(.72)
@tile('dark_oak_planks')
def _dpl(): return planks_t(62, (42, 32, 25), (72, 56, 42), (88, 82, 74), .45).out(.7)

def bark_t(seed, cols, fiss, lichen=0.0, ax=2.4, ay=.28, plated=False):
    t = T(seed); t.fill(t.nz(1, 4, 3, ax=ax, ay=ay), cols)
    if plated: t.shade(t.nz(6, 4, 2, ax=1, ay=2.2), .8, 1.1)
    t.blend(t.nz(2, 6, 2, ax=2, ay=.4), .66, .86, fiss, .9)
    if lichen: t.blend(t.nz(3, 3, 3), .66, .84, (92, 98, 80), lichen)
    t.streak(4, mulc(cols[0], .7), .35, .6); t.grain(.06); return t
@tile('spruce_log')
def _spl_side(): return bark_t(71, [(42, 34, 28), (62, 50, 40), (86, 72, 58)], (22, 17, 14), .5).out(.72)
@tile('pine_log_side')
def _pn_side(): return bark_t(72, [(70, 48, 34), (96, 66, 46), (122, 88, 62)], (30, 20, 15), 0, 2.0, .4, True).out(.72)
@tile('birch_log')
def _bl_side():
    t = T(73); t.fill(t.nz(1, 4, 3, ax=1.2, ay=.5), [(150, 146, 136), (176, 172, 162), (200, 196, 186)])
    t.blend(t.nz(2, 3, 3), .66, .86, (112, 106, 92), .6); t.streak(3, (84, 80, 70), .5, .55)
    for _ in range(15):
        x = t.rng.randrange(N); y = t.rng.randrange(N); ln = t.rng.randint(3, 8)
        for i in range(ln): t.dot(x + i, y, (36, 34, 32), .9); t.dot(x + i, y + 1, (96, 92, 84), .4 if i % 3 else .0)
    t.grain(.05); return t.out(.55)
@tile('charred_log_side')
def _ch_side():
    t = bark_t(74, [(20, 17, 15), (34, 29, 26), (52, 44, 38)], (8, 6, 6), 0, 2.2, .4, True)
    t.blend(t.nz(7, 5, 2), .78, .93, (84, 46, 28), .45); t.blend(t.nz(8, 3, 2), .72, .9, (88, 84, 78), .22); return t.out(.65)

def logtop_t(seed, barkc, lo, hi, charred=False):
    t = T(seed); w = fbm(seed, 4, 2); bf = fbm(seed + 1, 8, 2); rr = random.Random(seed)
    for y in range(N):
        for x in range(N):
            dx = x + .5 - 16; dy = y + .5 - 16; ch = max(abs(dx), abs(dy)); r = math.hypot(dx, dy)
            if ch >= 14:
                c = mulc(ramp(barkc, bf[y][x]), 1 - .1 * (ch - 14));
            else:
                v = .5 + .5 * math.sin(r * 1.55 + w[y][x] * 3.2); c = ramp([lo, hi], v * .8 + w[y][x] * .25)
                if r < 1.8: c = mulc(c, .65)
            t.p[y][x] = list(c)
    for _ in range(3):
        a = rr.uniform(0, 6.28); t.walk(16 + math.cos(a) * 2, 16 + math.sin(a) * 2, a, 11, mulc(lo, .42), 1.0, .22)
    if charred: t.blend(w, .74, .92, (84, 50, 32), .4)
    t.grain(.05); return t
@tile('spruce_log_top')
def _spl_top(): return logtop_t(75, [(40, 33, 27), (60, 50, 40)], (112, 96, 72), (146, 128, 98)).out(.7)
@tile('pine_log_top')
def _pn_top(): return logtop_t(76, [(66, 46, 34), (92, 64, 46)], (122, 92, 64), (158, 124, 88)).out(.7)
@tile('birch_log_top')
def _bl_top(): return logtop_t(77, [(150, 146, 136), (190, 186, 176)], (128, 112, 86), (164, 148, 118)).out(.62)
@tile('charred_log_top')
def _ch_top(): return logtop_t(78, [(18, 15, 13), (36, 31, 27)], (30, 26, 23), (58, 50, 44), True).out(.6)

# ---------------------------------------------------------------- leaves (cut-outs)
def leaves_t(seed, cols, cover, base, nstroke, slen, dead=None, thick=1):
    t = T(seed); m = t.nz(1, base, 2); flat = sorted(v for r in m for v in r); thr = flat[int(N * N * (1 - cover))]
    t.a = [[255 if m[y][x] >= thr else 0 for x in range(N)] for y in range(N)]
    t.fill(t.nz(2, base + 3, 2), cols); rr = t.rng
    for _ in range(nstroke):                                                          # needle / leaf strokes on top
        x = rr.randrange(N); y = rr.randrange(N); a = rr.uniform(0, 6.28); c = rr.choice([cols[0], cols[-1], cols[-2], cols[1]])
        if dead and rr.random() < .12: c = dead
        for i in range(rr.randint(*slen)):
            for th in range(thick):
                px = int(round(x + math.cos(a) * i - math.sin(a) * th)) % N; py = int(round(y + math.sin(a) * i + math.cos(a) * th)) % N
                if t.a[py][px]: t.p[py][px] = list(mixc(t.p[py][px], c, .85))
    edge = [[any(t.a[(y + dy) % N][(x + dx) % N] == 0 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))) for x in range(N)] for y in range(N)]
    for y in range(N):
        for x in range(N):
            if edge[y][x]: q = t.p[y][x]; q[0] *= .8; q[1] *= .8; q[2] *= .8
    t.grain(.05); return t
@tile('pine_needles')
def _needles(): return leaves_t(81, [(22, 32, 26), (32, 46, 34), (44, 60, 42), (56, 72, 48), (70, 86, 58)], .84, 6, 150, (3, 6), (84, 68, 44)).out(.8)
@tile('birch_leaves')                                                                       # TINTED: grey on purpose
def _birch_l(): return leaves_t(82, [grey(96), grey(122), grey(146), grey(170), grey(192)], .86, 7, 70, (2, 4), None, 2).out(1.0)
@tile('acacia_leaves')                                                                      # TINTED: the Red Forest's dead needles
def _acacia_l(): return leaves_t(83, [grey(104), grey(128), grey(152), grey(176), grey(198)], .72, 5, 110, (5, 9)).out(1.0)

# ---------------------------------------------------------------- metal
def corrugated(seed, paint, rustc, rusty, bolts=True):
    t = T(seed); rf = t.nz(1, 3, 4); fine = t.nz(2, 8, 2); sf = t.nz(3, 6, 2, ax=1.0, ay=.2); rr = t.rng
    for y in range(N):
        for x in range(N):
            rib = .5 + .5 * math.cos(2 * math.pi * (x + .5) / 8); shade = .78 + .34 * rib
            v = rf[y][x] * .55 + fine[y][x] * .15 + sf[y][x] * .38 + (1 - rib) * .1
            amt = sstep(1 - rusty - .1, 1 - rusty + .12, v); rc = ramp(rustc, fine[y][x] * .7 + rf[y][x] * .5)
            t.p[y][x] = list(mulc(mixc(paint, rc, amt), shade * (.94 + .12 * fine[y][x])))
    for sy in (0, 16):
        for x in range(N):
            q = t.p[sy][x]; q[0] *= .62; q[1] *= .62; q[2] *= .62
            q = t.p[(sy + 1) % N][x]; q[0] *= 1.12; q[1] *= 1.12; q[2] *= 1.12
    if bolts:
        for sy in (3, 19):
            for bx in (7, 15, 23, 31):
                t.dot(bx, sy, (170, 160, 146), .85); t.dot(bx + 1, sy + 1, (30, 26, 22), .6)
                for i in range(1, rr.randint(5, 11)): t.dot(bx, sy + i, rustc[1], .42 * (1 - i / 12))
    for _ in range(4): t.line(rr.randrange(N), rr.randrange(N), rr.randrange(N), rr.randrange(N), (166, 154, 138), .2)
    t.grain(.04); return t
@tile('brown_terracotta')
def _rust1(): return corrugated(91, (74, 70, 62), [(70, 38, 24), (98, 54, 32), (122, 70, 40)], .78).out(.8)
@tile('orange_terracotta')
def _rust2(): return corrugated(92, (86, 80, 68), [(110, 58, 28), (146, 82, 40), (170, 102, 52)], .64).out(.8)
@tile('light_gray_terracotta')
def _rust3(): return corrugated(93, (132, 130, 122), [(96, 54, 34), (126, 76, 46), (146, 94, 58)], .3).out(.78)

def plate_t(seed, lo, hi, rust):
    t = T(seed); br = t.nz(1, 4, 3, ax=.35, ay=3.5); t.fill(br, [lo, mixc(lo, hi, .5), hi])
    t.shade(t.nz(2, 3, 3), .88, 1.08)
    for y in range(N):
        for x in range(N):
            e = min(x, y, N - 1 - x, N - 1 - y); q = t.p[y][x]
            k = .52 if e == 0 else (1.16 if (e == 1 and (x < 8 or y < 8) and x <= y + 1) else (.8 if e == 1 else 1.0))
            if e == 1 and x > N - 3 or e == 1 and y > N - 3: k = .78
            q[0] *= k; q[1] *= k; q[2] *= k
    for (rx, ry) in ((5, 5), (26, 5), (5, 26), (26, 26)):
        for i in range(1, 11): t.dot(rx, ry + i, rust, .5 * (1 - i / 11) + .05)
        t.dot(rx, ry, mulc(hi, 1.25), 1); t.dot(rx - 1, ry, hi, .8); t.dot(rx, ry - 1, hi, .8); t.dot(rx + 1, ry + 1, mulc(lo, .4), .9); t.dot(rx, ry + 1, mulc(lo, .5), .8)
    t.streak(3, rust, .62, .52); t.blend(t.nz(4, 3, 3), .72, .92, (30, 30, 30), .55)
    for _ in range(5): t.line(t.rng.randrange(N), t.rng.randrange(N), t.rng.randrange(N), t.rng.randrange(N), mulc(hi, 1.2), .2)
    t.grain(.04); return t
@tile('iron_block')
def _iron(): return plate_t(95, (62, 63, 62), (96, 97, 95), (110, 66, 40)).out(.7)
@tile('steel_block')
def _steel(): return plate_t(96, (88, 92, 96), (128, 132, 136), (96, 70, 50)).out(.6)

@tile('iron_bars')
def _bars():
    t = T(97); t.fill(t.nz(1, 4, 3), [(40, 39, 37), (62, 60, 57), (84, 80, 74)]); rf = t.nz(2, 5, 2)
    for y in range(N):
        for x in range(N):
            v = (x % 32) in (3, 4, 5, 6, 14, 15, 16, 17, 25, 26, 27, 28); h = y % 24 in (6, 7, 8) or y in (24, 25, 26) or y in (0, 1)
            hy = y in (6, 7, 8, 24, 25, 26)
            if not (v or hy): t.a[y][x] = 0; continue
            kk = [1.22, 1.0, .85, .66][(x - 3) % 11 % 4] if v and not hy else [1.2, 1.0, .78][(y - 6) % 18 % 3]
            c = mulc(t.p[y][x], kk)
            if rf[y][x] > .62: c = mixc(c, (104, 62, 38), sstep(.62, .85, rf[y][x]) * .85)
            t.p[y][x] = list(c)
    t.grain(.05); return t.out(.7)
@tile('glass')
def _glass():
    t = T(98, (0, 0, 0)); t.a = [[0] * N for _ in range(N)]; rr = t.rng; df = fbm(98, 4, 3)
    def solid(x, y, c, k=1.0): t.a[y % N][x % N] = 255; t.p[y % N][x % N] = list(c)
    for y in range(N):
        for x in range(N):
            if min(x, y, N - 1 - x, N - 1 - y) < 2: solid(x, y, mulc((84, 90, 84), .8 + .4 * df[y][x]))
    for (x0, y0, d) in ((3, 3, 1), (28, 28, -1)):                                          # glints
        for i in range(7):
            for w in range(2 if i < 4 else 1): solid(x0 + d * (i + w), y0 + d * (6 - i), (132, 142, 138))
    for _ in range(3):                                                                       # cracks
        x = rr.choice([2, 29, rr.randrange(6, 26)]); y = rr.choice([2, 29, rr.randrange(6, 26)]); a = rr.uniform(0, 6.28)
        for _i in range(rr.randint(8, 18)):
            solid(int(x), int(y), (150, 156, 150)); a += rr.uniform(-.6, .6); x += math.cos(a); y += math.sin(a)
            if not (1 < x < 30 and 1 < y < 30): break
    for y in range(N):                                                                       # grime at the bottom + corners
        for x in range(N):
            if df[y][x] > .62 + (1 - y / N) * .28 and rr.random() < .8: solid(x, y, mulc((74, 70, 54), .8 + .5 * df[y][x]))
    return t.out(.8)

@tile('rail')
def _rail():
    t = T(99); wf = t.nz(1, 4, 3, ax=2, ay=.5); rf = t.nz(2, 5, 2); t.a = [[0] * N for _ in range(N)]
    tie = lambda y: 1 <= y <= 8 or 12 <= y <= 19 or 23 <= y <= 30
    for y in range(N):
        for x in range(N):
            if 1 <= x <= 30 and tie(y):
                c = mulc(ramp([(40, 32, 26), (62, 50, 38), (84, 70, 54)], wf[y][x]), 1.1 if y in (1, 12, 23) else .85 if y in (8, 19, 30) else 1)
                t.p[y][x] = list(c); t.a[y][x] = 255
            if 4 <= x <= 8 or 23 <= x <= 27:
                k = [.7, 1.12, 1.3, 1.0, .62][(x - 4) % 19 % 5]
                c = mulc(mixc((76, 66, 58), (116, 70, 44), sstep(.45, .8, rf[y][x])), k)
                t.p[y][x] = list(c); t.a[y][x] = 255
    t.scatter(12, [(150, 140, 126)], .35); t.grain(.05); return t.out(.7)

# ---------------------------------------------------------------- plants (sprites)
def sprite(seed): t = T(seed, (0, 0, 0)); t.a = [[0] * N for _ in range(N)]; return t
def put(t, x, y, c):
    if 0 <= x < N and 0 <= y < N: t.a[y][x] = 255; t.p[y][x] = list(c)

def blade(t, x0, h, lean, cfn, wide=2):
    for i in range(h):
        f = i / max(1, h - 1); x = x0 + lean * f * f * h * .35; y = N - 1 - i
        w = wide if f < .55 else 1
        for k in range(w): put(t, int(round(x)) + k, y, cfn(f))

@tile('short_grass')                                                                       # TINTED: grey on purpose
def _tall():
    t = sprite(101); rr = t.rng
    for _ in range(17):
        x = rr.randint(2, 28); h = rr.randint(10, 27); lean = rr.uniform(-1.6, 1.6) * (1 if h > 16 else .6)
        pale = rr.random() < .14; b = rr.uniform(.85, 1.15)
        blade(t, x, h, lean, lambda f: (lambda v: (v, v, v))(int(min(235, (92 + 100 * f) * b + rr.randint(-8, 8)))) if not pale else (int(196 * b), int(184 * b), int(160 * b)))
    return t.out(1.0)
@tile('dry_grass')
def _dry():
    t = sprite(102); rr = t.rng
    for _ in range(13):
        x = rr.randint(2, 28); h = rr.randint(8, 24); lean = rr.uniform(-2.2, 2.2); cols = rr.choice([((96, 84, 58), (160, 144, 100)), ((78, 66, 46), (136, 120, 82)), ((110, 98, 66), (176, 160, 116))])
        blade(t, x, h, lean, lambda f, c=cols: mixc(c[0], c[1], f), 1 if rr.random() < .6 else 2)
    return t.out(.7)
@tile('fern')                                                                              # TINTED: grey on purpose
def _fern():
    t = sprite(103); rr = t.rng
    for k in range(9):
        a = -1.3 + k * .325 + rr.uniform(-.08, .08); ln = rr.randint(19, 29)
        x, y = 16.0, 31.0; ang = a
        for i in range(ln):
            ang += -a * .018; x += math.sin(ang) * .95; y -= math.cos(ang) * .95
            f = i / ln; v = int(92 + 70 * f); put(t, int(round(x)), int(round(y)), (v - 20, v - 20, v - 20))
            if i > 3 and i % 2 == 0:
                tip = max(1, int(6 * (1 - f * .75)) + 1)
                for side in (-1, 1):
                    for j in range(1, tip + 1):
                        lx = x + math.cos(ang) * side * j * .9 + math.sin(ang) * j * .45; ly = y + math.sin(ang) * side * j * .9 - math.cos(ang) * j * .45
                        vv = int(v + 24 + rr.randint(-14, 14)); put(t, int(round(lx)), int(round(ly)), (vv, vv, vv))
    return t.out(1.0)
@tile('dead_bush')
def _deadbush():
    t = sprite(104); rr = t.rng
    def branch(x, y, ang, ln, depth):
        for i in range(ln):
            x += math.sin(ang); y -= math.cos(ang); ang += rr.uniform(-.14, .14)
            c = mixc((60, 49, 38), (112, 96, 74), rr.random() * .6 + (.2 if depth == 0 else .0)); put(t, int(round(x)), int(round(y)), c)
            if depth < 2 and i > 3 and rr.random() < .2: branch(x, y, ang + rr.choice([-1, 1]) * rr.uniform(.4, .9), ln - i - rr.randint(0, 3), depth + 1)
    for a in (-.7, -.3, .05, .35, .7): branch(16 + a * 3, 31, a, rr.randint(18, 28), 0)
    return t.out(.6)

# ================================================================ the Zone kit: NEW tiles (not in any pack)
# These are the game's own pictures for the Zone's own blocks (tools/zone/zone_blocks.py registers them in
# data/tiles.json + blocks.json + shapes.json); written to data/textures/block/<name>.png, never into the pack.
ZTILES = {}
def ztile(name):
    def deco(fn): ZTILES[name] = fn; return fn
    return deco

def cracks(t, n, col, lip=None, ln=(10, 26), wob=.55):
    for _ in range(n): t.walk(t.rng.randrange(N), t.rng.randrange(N), t.rng.uniform(0, 6.28), t.rng.randint(*ln), col, 1.0, wob, lip)

def asphalt_t(seed, worn=0.0):
    t = T(seed); t.fill(t.nz(1, 5, 4), [(36, 37, 38), (46, 47, 48), (58, 58, 58)])
    t.blend(t.nz(2, 3, 3), .62, .9, (72, 72, 70), .55 + worn * .3)                       # sun-bleached, dusty
    t.blend(t.nz(3, 4, 2), .78, .95, (26, 26, 27), .7)                                    # old oil
    t.scatter(140, [(74, 74, 74), (90, 89, 86), (104, 101, 96), (52, 52, 53), (120, 116, 108)], .85)
    t.scatter(10, [(124, 120, 112)], .8, shadow=(20, 20, 20), size=2)
    return t
@ztile('zone_asphalt')
def _z_asph():
    t = asphalt_t(301); cracks(t, 2, (16, 16, 17), (70, 70, 70), (14, 30), .45); t.grain(.05); return t.out(.5)
@ztile('zone_asphalt_cracked')
def _z_asph_cr():
    t = asphalt_t(302, 1.0); cid, d1, d2, off = voronoi(302, 4, .9); m = t.nz(6, 3, 2)
    for y in range(N):
        for x in range(N):
            e = d2[y][x] - d1[y][x]
            if e < 1.1:                                                                   # alligator cracks, weeds in them
                t.p[y][x] = list((54, 62, 34) if m[y][x] > .62 and e < .8 else (14, 14, 15))
            elif e < 1.9: t.p[y][x] = list(mulc(t.p[y][x], 1.12))                         # lifted plate edges
    t.scatter(18, [(84, 96, 48), (64, 76, 38)], .8); t.grain(.05); return t.out(.55)

@ztile('zone_paving')                                                                    # 50 cm concrete slabs, grass in the joints
def _z_paving():
    t = T(303); fine = t.nz(1, 6, 3); st = t.nz(2, 3, 2); rr = t.rng
    tones = [rr.uniform(.86, 1.1) for _ in range(4)]
    for y in range(N):
        for x in range(N):
            sx, sy = x // 16, y // 16; lx, ly = x % 16, y % 16
            c = mulc(ramp([(104, 102, 96), (124, 122, 114), (140, 138, 128)], fine[y][x]), tones[sy * 2 + sx])
            if lx == 15 or ly == 15: c = (60, 70, 40) if st[y][x] > .55 else (54, 52, 48)
            elif lx == 0 or ly == 0: c = mulc(c, 1.1)
            t.p[y][x] = list(c)
    t.streak(3, (76, 72, 62), .4, .55); t.blend(t.nz(4, 3, 2), .72, .92, (70, 66, 56), .5)
    cracks(t, 3, (44, 42, 40), (150, 148, 140), (6, 14))
    for _ in range(3):                                                                   # broken corners
        x = rr.choice([0, 15, 16, 31]); y = rr.choice([0, 15, 16, 31])
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                if abs(dx) + abs(dy) < 3: t.dot(x + dx, y + dy, (66, 60, 50), .85)
    t.grain(.04); return t.out(.65)

@ztile('zone_panel')                                                                     # Soviet panel block: pebbled concrete, caulked seams
def _z_panel():
    t = concrete_t(304, (112, 110, 102), (138, 136, 126), (78, 74, 66), .45, 10, 1.0)
    t.scatter(170, [(150, 146, 134), (96, 92, 86), (126, 118, 104), (84, 82, 78)], .55)    # exposed aggregate
    for i in range(N):
        for (x, y) in ((i, 0), (0, i)): t.p[y][x] = list((50, 48, 44))                     # tar caulking
        for (x, y) in ((i, 1), (1, i)): q = t.p[y][x]; q[0] *= .82; q[1] *= .82; q[2] *= .82
    t.blend(t.nz(7, 4, 2, ax=1.4, ay=.3), .7, .95, (96, 100, 88), .3)                      # damp at the joints
    t.grain(.04); return t.out(.7)
@ztile('zone_panel_tiles')                                                               # the faded mosaic band of the panel blocks
def _z_ptiles():
    t = T(305); rr = t.rng; g = t.nz(1, 5, 2)
    pal = [(84, 116, 128), (96, 128, 136), (70, 100, 116), (150, 146, 128), (110, 134, 138)]
    for by in range(8):
        for bx in range(8):
            c = rr.choice(pal); k = rr.uniform(.85, 1.1); gone = rr.random() < .14
            for yy in range(4):
                for xx in range(4):
                    x = bx * 4 + xx; y = by * 4 + yy
                    if gone: v = (96, 94, 88) if (xx + yy) % 3 else (80, 78, 72)
                    elif xx == 3 or yy == 3: v = (118, 116, 106)
                    else: v = mulc(c, k * (1.08 if xx == 0 or yy == 0 else 1) * (.9 + .2 * g[y][x]))
                    t.p[y][x] = list(v)
    t.streak(3, (70, 62, 52), .55, .52); t.blend(t.nz(4, 3, 2), .74, .95, (60, 58, 54), .45)
    t.grain(.04); return t.out(.75)

def peel(t, f, thr, under, k=1.0, lip=.75):                                             # flakes of paint gone, a dark lip round each
    for y in range(N):
        for x in range(N):
            v = f[y][x]
            if v > thr: t.p[y][x] = list(under[y][x] if isinstance(under, list) else under)
            elif v > thr - .035: q = t.p[y][x]; q[0] *= lip; q[1] *= lip; q[2] *= lip
@ztile('zone_plaster')                                                                   # cream plaster falling off red brick
def _z_plaster():
    br = T(306); brick_courses(br, 4, 16, [(118, 68, 50), (106, 62, 46), (128, 76, 56)], (120, 114, 100), br.nz(1, 9, 2), br.nz(2, 6, 3), .08)
    t = T(307); t.fill(t.nz(1, 3, 4), [(150, 140, 112), (170, 160, 130), (184, 176, 146)]); t.shade(t.nz(2, 9, 2), .94, 1.05)
    f = t.nz(3, 3, 3); peel(t, f, .66, [[tuple(c) for c in r] for r in br.p])
    t.streak(4, (104, 96, 76), .55, .5); t.blend(t.nz(5, 3, 2, ax=2, ay=.5), .7, .9, (110, 112, 90), .35)
    cracks(t, 2, (96, 88, 70), None, (8, 16)); t.grain(.04); return t.out(.72)
@ztile('zone_paint_green')                                                               # the institutional green wall, a dark band at its top
def _z_pgreen():
    t = T(308); t.fill(t.nz(1, 3, 3), [(64, 88, 70), (76, 102, 80), (88, 114, 90)]); t.shade(t.nz(2, 8, 2), .92, 1.06)
    peel(t, t.nz(3, 4, 3), .7, (150, 146, 132)); t.streak(4, (52, 60, 48), .45, .55)
    for x in range(N):
        for y in (0, 1): t.p[y][x] = list(mulc((86, 52, 40), .9 + .2 * t.nz(4, 6, 2)[y][x]))
    t.grain(.04); return t.out(.8)
@ztile('zone_whitewash')
def _z_white():
    t = T(309); t.fill(t.nz(1, 3, 3), [(150, 148, 138), (168, 166, 156), (182, 180, 170)])
    peel(t, t.nz(2, 4, 3), .72, (120, 116, 106)); t.blend(t.nz(3, 3, 2), .68, .9, (110, 108, 90), .45)   # damp, mould
    t.scatter(26, [(70, 74, 58), (90, 92, 74)], .5); t.streak(4, (120, 112, 92), .45, .55)
    cracks(t, 2, (100, 98, 90), None, (8, 18)); t.grain(.035); return t.out(.65)
@ztile('zone_ceiling')
def _z_ceiling():
    t = T(310); t.fill(t.nz(1, 3, 3), [(146, 144, 136), (162, 160, 150), (174, 172, 162)])
    t.blend(t.nz(2, 2, 2), .6, .85, (128, 112, 82), .55); t.blend(t.nz(3, 3, 2), .72, .9, (108, 94, 70), .45)   # water rings
    peel(t, t.nz(4, 5, 3), .78, (112, 110, 104)); cracks(t, 3, (96, 94, 88), None, (10, 22)); t.grain(.03); return t.out(.6)

@ztile('zone_brick_ruined')
def _z_brick():
    t = T(311); fine = t.nz(1, 9, 2); mf = t.nz(2, 6, 3)
    brick_courses(t, 4, 16, [(116, 64, 48), (104, 58, 44), (126, 74, 54), (92, 54, 42), (110, 70, 52)], (116, 110, 96), fine, mf, .1)
    rr = t.rng
    for _ in range(4):                                                                   # whole bricks gone: dark holes
        row = rr.randrange(8); b0 = rr.randrange(2) * 16 + (row % 2) * 8
        for yy in range(3):
            for xx in range(15): t.dot(b0 + xx, row * 4 + yy, (34, 28, 26) if yy else (52, 40, 34), 1.0)
    t.streak(3, (34, 30, 28), .6, .5); t.blend(t.nz(4, 3, 2), .74, .95, (170, 164, 146), .25)
    t.blend(t.nz(5, 4, 2), .78, .95, (70, 82, 46), .55); t.grain(.05); return t.out(.8)
@ztile('zone_tiles_dirty')                                                               # lab / hospital tiles
def _z_tiles():
    t = T(312); g = t.nz(1, 5, 2); rr = t.rng
    for by in range(4):
        for bx in range(4):
            k = rr.uniform(.92, 1.05); gone = rr.random() < .08
            for yy in range(8):
                for xx in range(8):
                    x = bx * 8 + xx; y = by * 8 + yy
                    if gone: c = (100, 98, 92) if (x * 7 + y * 3) % 5 else (82, 80, 76)
                    elif xx == 7 or yy == 7: c = (108, 104, 92)
                    else: c = mulc((176, 178, 170), k * (1.06 if xx == 0 or yy == 0 else .97 if xx == 6 or yy == 6 else 1) * (.94 + .1 * g[y][x]))
                    t.p[y][x] = list(c)
    t.blend(t.nz(2, 3, 2), .66, .9, (128, 118, 90), .5); t.streak(4, (110, 98, 74), .45, .55)
    cracks(t, 2, (70, 68, 62), None, (6, 14)); t.grain(.03); return t.out(.6)
@ztile('zone_concrete_mossy')
def _z_cmoss():
    t = concrete_t(313, (96, 96, 90), (124, 122, 114), (70, 68, 60), .2, 18, .9)
    m = t.nz(6, 3, 3); g = t.nz(7, 8, 2)
    for y in range(N):
        for x in range(N):
            w = sstep(.55, .75, m[y][x] + (y / N) * .25)
            if w > 0: t.p[y][x] = list(mixc(t.p[y][x], mixc((48, 60, 32), (86, 98, 52), g[y][x]), w * .9))
    t.scatter(30, [(150, 150, 120), (120, 126, 96)], .6); t.grain(.04); return t.out(.75)
@ztile('zone_fence_panel')                                                               # the Soviet concrete fence: rows of raised diamonds
def _z_fence():
    t = concrete_t(314, (106, 106, 100), (130, 128, 120), (72, 70, 64), .3, 12, 1.0)
    for y in range(N):
        for x in range(N):
            cx = (x % 8) - 3.5; cy = (y % 8) - 3.5; d = abs(cx) + abs(cy)
            q = t.p[y][x]
            k = 1.14 if d < 2.6 and cx + cy < 0 else 0.86 if d < 2.6 else (0.78 if d < 3.6 and cx + cy > 0 else 1.0)
            q[0] *= k; q[1] *= k; q[2] *= k
    t.streak(5, (64, 60, 52), .5, .52); t.grain(.035); return t.out(.7)

@ztile('zone_linoleum')                                                                  # brown parquet-print lino, torn
def _z_lino():
    t = T(315); g = t.nz(1, 6, 2)
    for y in range(N):
        for x in range(N):
            blk = ((x // 8) + (y // 8)) % 2; stripe = (y if blk else x) % 4 == 0
            c = mulc((108, 70, 52) if blk else (96, 62, 46), (.9 + .2 * g[y][x]) * (.86 if stripe else 1))
            t.p[y][x] = list(c)
    t.blend(t.nz(2, 3, 2), .6, .85, (128, 110, 88), .45)                                  # worn pale paths
    peel(t, t.nz(3, 3, 3), .8, (88, 86, 80), lip=1.25)                                    # torn: grey screed under
    t.scatter(20, [(50, 40, 32)], .6); t.grain(.04); return t.out(.75)
@ztile('zone_planks_rotten')
def _z_rotten():
    t = planks_t(316, (60, 54, 44), (96, 88, 72), (120, 118, 108), .35)
    for _ in range(3):                                                                   # holes through
        x = t.rng.randrange(N); y = t.rng.randrange(4) * 8 + 2
        for dx in range(t.rng.randint(3, 7)):
            for dy in range(4): t.dot(x + dx, y + dy, (16, 14, 12), .95)
    t.blend(t.nz(8, 4, 2), .72, .9, (64, 76, 42), .6); t.grain(.05); return t.out(.72)

@ztile('zone_corrugated_green')
def _z_corr_g(): return corrugated(317, (72, 92, 70), [(86, 50, 30), (118, 68, 38), (140, 88, 50)], .5).out(.82)
@ztile('zone_roof_rust')
def _z_roof():
    t = corrugated(318, (88, 80, 70), [(62, 34, 22), (92, 50, 30), (118, 66, 38)], .86, False)
    t.blend(t.nz(9, 3, 2), .75, .95, (48, 56, 34), .5); return t.out(.8)
@ztile('zone_steel_green')
def _z_steel_g():
    t = plate_t(319, (64, 82, 66), (88, 108, 88), (116, 70, 42)); t.blend(t.nz(9, 4, 3), .68, .9, (110, 66, 40), .7); return t.out(.8)

@ztile('zone_leaf_litter')                                                               # fallen birch leaves over dark soil
def _z_litter():
    t = dirt_t(320, (48, 38, 28), (72, 58, 42), 4); rr = t.rng
    for _ in range(85):
        x = rr.randrange(N); y = rr.randrange(N); c = rr.choice([(150, 112, 50), (128, 86, 42), (112, 96, 52), (96, 70, 40), (160, 132, 64), (84, 88, 46)])
        a = rr.uniform(0, 6.28); ca, sa = math.cos(a), math.sin(a)
        for i in range(-2, 3):
            for j in (-1, 0, 1):
                if abs(j) + abs(i) * .5 < 1.6: t.dot(x + ca * i - sa * j * .7, y + sa * i + ca * j * .7, mulc(c, 1 - .08 * abs(i)), .9)
    for _ in range(8): t.walk(rr.randrange(N), rr.randrange(N), rr.uniform(0, 6.28), rr.randint(4, 9), (52, 40, 30), .9, .3)
    t.grain(.06); return t.out(.75)

# ---- panes, overlays, wire (cut-outs)
@ztile('zone_chainlink')
def _z_chain():
    t = sprite(321); rf = t.nz(1, 4, 2)
    for y in range(N):
        for x in range(N):
            on = (x + y) % 6 == 0 or (x - y) % 6 == 0
            if on: put(t, x, y, mixc((104, 104, 98), (112, 66, 40), sstep(.45, .8, rf[y][x])))
    for x in range(N):                                                                   # top rail + bottom tension wire
        for y in (0, 1): put(t, x, y, mixc((90, 90, 86) if y else (124, 122, 116), (110, 64, 38), sstep(.5, .8, rf[y][x])))
        put(t, x, 30, (70, 66, 60))
    return t.out(.85)
@ztile('zone_chainlink_post')
def _z_chain_post():
    px = _z_chain(); t = _raw(lambda: px)
    for y in range(N):
        for x in range(14, 18): put(t, x, y, mulc(mixc((96, 96, 92), (108, 64, 40), .4 + .3 * math.sin(y * .7)), [.7, 1.15, 1.0, .75][x - 14]))
    return t.out(1.0)
@ztile('zone_window_broken')                                                             # peeling frame, glass mostly gone
def _z_window():
    t = sprite(322); rr = t.rng; wf = t.nz(1, 4, 3, ax=.4, ay=2)
    for y in range(N):
        for x in range(N):
            e = min(x, y, N - 1 - x, N - 1 - y); mull = 15 <= x <= 16 or 13 <= y <= 14
            if e < 3 or mull:
                c = mixc((150, 146, 132), (96, 74, 52), sstep(.45, .7, wf[y][x]))            # white paint gone to bare wood
                put(t, x, y, mulc(c, .7 if e == 2 or (mull and (x == 16 or y == 14)) else 1))
    for (cx, cy) in ((3, 3), (28, 3), (3, 28), (28, 28), (17, 15)):                       # shards left in the corners
        n = rr.randint(3, 7)
        for k in range(n):
            for j in range(n - k):
                xx = cx + (k if cx < 16 else -k); yy = cy + (j if cy < 16 else -j)
                if t.a[yy % N][xx % N] == 0 and rr.random() < .9: put(t, xx, yy, mixc((92, 104, 100), (150, 160, 150), rr.random() * .5))
    return t.out(.9)
@ztile('zone_moss')
def _z_moss():
    t = sprite(323); m = t.nz(1, 4, 3); g = t.nz(2, 9, 2)
    for y in range(N):
        for x in range(N):
            if m[y][x] > .5: put(t, x, y, mixc((46, 58, 30), (92, 104, 56), g[y][x]) if m[y][x] > .56 else (40, 48, 28))
    return t.out(.8)
@ztile('zone_cable')
def _z_cable():
    t = T(324); t.fill(t.nz(1, 6, 2), [(24, 24, 26), (36, 36, 38), (50, 50, 52)]); return t.out(.5)

# ---- bark, wood ends, leaves
@ztile('zone_birch_bark')
def _z_birch():
    t = T(325); t.fill(t.nz(1, 3, 3, ax=1.5, ay=.4), [(156, 154, 144), (184, 182, 172), (204, 202, 194)])
    t.blend(t.nz(2, 3, 3), .62, .86, (126, 120, 104), .5); rr = t.rng
    for _ in range(26):                                                                   # lenticels
        x = rr.randrange(N); y = rr.randrange(N); ln = rr.randint(3, 9)
        for i in range(ln): t.dot(x + i, y, (40, 38, 36), .9)
    for _ in range(3):                                                                    # the black diamond scars
        x = rr.randrange(N); y = rr.randrange(N); w = rr.randint(3, 6)
        for dy in range(-w, w + 1):
            for dx in range(-(w - abs(dy)) * 2, (w - abs(dy)) * 2 + 1): t.dot(x + dx, y + dy, (30, 28, 26), .92)
    t.streak(3, (96, 92, 82), .45, .58); t.grain(.05); return t.out(.5)
@ztile('zone_pine_bark')                                                                  # Scots pine: grey plates, orange under
def _z_pine():
    t = bark_t(326, [(70, 56, 44), (96, 76, 58), (120, 96, 72)], (36, 26, 20), .2, 1.6, .5, True)
    t.blend(t.nz(7, 3, 3), .6, .82, (150, 92, 56), .6); return t.out(.78)
@ztile('zone_dead_bark')                                                                  # weathered grey, bark half gone
def _z_dead():
    t = T(327); t.fill(t.nz(1, 4, 3, ax=.3, ay=3.0), [(84, 80, 74), (112, 108, 100), (138, 134, 124)])
    t.blend(t.nz(2, 4, 2, ax=.3, ay=2.5), .68, .86, (52, 48, 44), .9)                     # grain cracks along the trunk
    peel(t, t.nz(3, 3, 3), .62, (58, 48, 38), lip=.7)                                     # patches of dark bark left
    t.blend(t.nz(4, 3, 3), .72, .9, (92, 100, 76), .4); t.grain(.05); return t.out(.6)
@ztile('zone_burnt_bark')
def _z_burnt():
    t = bark_t(328, [(18, 16, 15), (30, 27, 25), (46, 40, 36)], (6, 5, 5), 0, 2.0, .45, True)
    t.blend(t.nz(7, 6, 2), .8, .94, (96, 52, 30), .5); t.blend(t.nz(8, 3, 2), .7, .9, (92, 88, 82), .3); return t.out(.6)
@ztile('zone_bark_end')
def _z_end(): return logtop_t(329, [(76, 72, 66), (104, 100, 92)], (110, 100, 84), (146, 136, 116)).out(.55)

def zleaves(seed, cols, cover, base, nstroke, slen, twig, thick=1, dead=None):
    t = leaves_t(seed, cols, cover, base, nstroke, slen, dead, thick); rr = random.Random(seed + 9)
    for _ in range(3):                                                                    # the twigs the leaves hang on
        x, y, a = rr.randrange(N), rr.randrange(N), rr.uniform(0, 6.28)
        for i in range(rr.randint(10, 20)):
            put(t, int(x) % N, int(y) % N, twig); x += math.cos(a); y += math.sin(a); a += rr.uniform(-.3, .3)
    return t
@ztile('zone_birch_leaves')
def _z_bl(): return zleaves(330, [(62, 80, 38), (78, 98, 46), (96, 112, 52), (116, 122, 56), (138, 130, 60)], .56, 8, 120, (1, 3), (70, 64, 54), 2, (170, 126, 48)).out(.85)
@ztile('zone_pine_needles')
def _z_pn(): return zleaves(331, [(26, 38, 28), (36, 52, 36), (48, 64, 42), (60, 76, 48), (74, 88, 56)], .62, 6, 170, (3, 6), (62, 44, 32), 1, (96, 76, 44)).out(.82)
@ztile('zone_red_needles')                                                                 # the Red Forest's dead needles
def _z_rn(): return zleaves(332, [(84, 40, 22), (112, 56, 28), (136, 72, 34), (156, 92, 44), (170, 112, 56)], .5, 6, 150, (3, 6), (46, 30, 22), 1, (70, 50, 36)).out(.85)
@ztile('zone_dead_twigs')
def _z_twigs():
    t = sprite(333); rr = t.rng
    def br(x, y, a, ln, d):
        for i in range(ln):
            x += math.cos(a); y += math.sin(a); a += rr.uniform(-.25, .25); put(t, int(round(x)) % N, int(round(y)) % N, mixc((70, 66, 60), (118, 112, 102), rr.random()))
            if d < 3 and rr.random() < .16: br(x, y, a + rr.choice([-1, 1]) * rr.uniform(.4, 1.0), max(3, ln - i - 2), d + 1)
    for _ in range(4): br(rr.randrange(N), rr.randrange(N), rr.uniform(0, 6.28), rr.randint(12, 22), 0)
    return t.out(.7)

# ---- plants (cross sprites)
@ztile('zone_tall_dry_grass')
def _z_tdg():
    t = sprite(334); rr = t.rng
    for _ in range(22):
        x = rr.randint(1, 30); h = rr.randint(14, 31); lean = rr.uniform(-1.8, 1.8)
        cols = rr.choice([((92, 88, 52), (176, 158, 104)), ((74, 80, 44), (150, 136, 88)), ((102, 92, 60), (190, 172, 120)), ((70, 76, 46), (124, 128, 76))])
        blade(t, x, h, lean, lambda f, c=cols: mixc(c[0], c[1], f), 1 if rr.random() < .7 else 2)
        if h > 24 and rr.random() < .5:                                                    # seed heads
            tx = int(round(x + lean * h * .35));
            for k in range(4): put(t, tx + (k % 2), N - h + k, (150, 130, 92))
    return t.out(.75)
@ztile('zone_shrub')
def _z_shrub():
    t = sprite(335); rr = t.rng
    def br(x, y, a, ln, d):
        for i in range(ln):
            x += math.sin(a); y -= math.cos(a); a += rr.uniform(-.15, .15); put(t, int(round(x)), int(round(y)), (58, 46, 36))
            if d < 2 and i > 2 and rr.random() < .25: br(x, y, a + rr.choice([-1, 1]) * rr.uniform(.4, .9), ln - i, d + 1)
    for a in (-.8, -.4, 0, .4, .8): br(16, 31, a, rr.randint(14, 24), 0)
    m = fbm(335, 5, 2)
    for _ in range(260):                                                                  # leaf clumps round the upper twigs
        x = rr.randint(2, 29); y = rr.randint(3, 26)
        if m[y][x] > .42 and (x - 16) ** 2 / 196 + (y - 15) ** 2 / 144 < 1:
            put(t, x, y, rr.choice([(52, 66, 34), (66, 82, 40), (84, 96, 48), (100, 104, 52), (120, 110, 56)]))
    return t.out(.8)
@ztile('zone_dandelion_clocks')
def _z_dandelion():                                                                        # STALCRAFT's white meadows: seed clocks on bent stalks
    t = sprite(337); rr = t.rng
    for _ in range(9):
        x = rr.randint(2, 29); blade(t, x, rr.randint(5, 10), rr.uniform(-1.4, 1.4), lambda f: mixc((52, 70, 34), (86, 104, 52), f), 1)
    for _ in range(4):
        x = rr.randint(5, 26); h = rr.randint(12, 26); lean = rr.uniform(-.5, .5)
        blade(t, x, h, lean, lambda f: mixc((70, 84, 46), (104, 112, 70), f), 1)
        cx = int(round(x + lean * h * .35)); cy = N - h
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                e = dx * dx + dy * dy
                if e <= 9 and rr.random() < (.95 if e <= 4 else .55):
                    put(t, cx + dx, cy + dy, mixc((214, 214, 204), (246, 246, 238), rr.random()) if e > 1 else (196, 190, 160))
    return t.out(.9)
@ztile('zone_meadow_flowers')
def _z_flowers():                                                                          # yarrow, chamomile, a few yellow heads in the grass
    t = sprite(338); rr = t.rng
    for _ in range(12):
        x = rr.randint(1, 30); blade(t, x, rr.randint(6, 16), rr.uniform(-1.6, 1.6), lambda f: mixc((56, 70, 36), (100, 112, 60), f), 1)
    for _ in range(7):
        x = rr.randint(3, 28); h = rr.randint(10, 22); lean = rr.uniform(-.6, .6)
        blade(t, x, h, lean, lambda f: mixc((64, 78, 42), (96, 104, 62), f), 1)
        cx = int(round(x + lean * h * .35)); cy = N - h; yel = rr.random() < .3
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, -1), (1, -1), (-1, -1), (0, 1)):
            put(t, cx + dx, cy + dy, (206, 176, 64) if yel else mixc((218, 214, 196), (240, 238, 226), rr.random()))
        put(t, cx, cy, (190, 160, 70) if not yel else (150, 110, 40))
    return t.out(.9)
@ztile('zone_dead_weeds_lower')
def _z_weeds_lo():                                                                         # last year's tansy, dock and hogweed: brown stalks
    t = sprite(339); rr = t.rng
    for _ in range(10):
        x = rr.randint(2, 29); blade(t, x, 32, rr.uniform(-.25, .25), lambda f: mixc((70, 56, 38), (118, 96, 64), f), 1)
    for _ in range(8):
        x = rr.randint(1, 30); blade(t, x, rr.randint(6, 14), rr.uniform(-1.5, 1.5), lambda f: mixc((84, 76, 46), (136, 120, 78), f), 1)
    return t.out(.8)
@ztile('zone_dead_weeds_upper')
def _z_weeds_hi():
    t = sprite(340); rr = t.rng
    for _ in range(9):
        x = rr.randint(3, 28); h = rr.randint(10, 28); lean = rr.uniform(-.5, .5)
        blade(t, x, h, lean, lambda f: mixc((78, 62, 42), (124, 102, 70), f), 1)
        cx = int(round(x + lean * h * .35)); cy = N - h
        for _ in range(9):                                                                  # dry seed umbels
            put(t, cx + rr.randint(-3, 3), cy + rr.randint(-1, 2), rr.choice([(92, 66, 40), (120, 90, 54), (146, 116, 72), (70, 50, 32)]))
    return t.out(.8)
@ztile('zone_reeds_lower')
def _z_reeds_lo():
    t = sprite(336); rr = t.rng
    for _ in range(14):
        x = rr.randint(2, 29); lean = rr.uniform(-.6, .6); cols = rr.choice([((70, 78, 44), (110, 116, 64)), ((86, 84, 50), (128, 122, 76))])
        blade(t, x, 32, lean * .3, lambda f, c=cols: mixc(c[0], c[1], f), 1)
    return t.out(.8)
@ztile('zone_reeds_upper')
def _z_reeds_hi():
    t = sprite(336); rr = t.rng
    for _ in range(14):
        x = rr.randint(2, 29); h = rr.randint(10, 28); lean = rr.uniform(-.6, .6) * 2; cols = rr.choice([((110, 116, 64), (150, 146, 96)), ((128, 122, 76), (170, 158, 110))])
        blade(t, x, h, lean, lambda f, c=cols: mixc(c[0], c[1], f), 1)
        if rr.random() < .45:                                                               # cattail heads
            tx = int(round(x + lean * h * .35));
            for k in range(5): put(t, tx, N - h + k, (78, 52, 34)); put(t, tx + 1, N - h + k, (92, 62, 40))
    return t.out(.8)
@ztile('zone_barbed_wire')                                                                 # a coil, loops overlapping
def _z_barbed():
    t = sprite(337); rr = t.rng
    for k in range(7):
        cx = 3 + k * 4.4; r = 11 + rr.uniform(-1, 1)
        for i in range(90):
            a = i / 90 * 6.283; x = cx + math.cos(a) * 4.2; y = 18 + math.sin(a) * r * .9
            c = mixc((76, 74, 70), (112, 66, 40), rr.random() * .6); put(t, int(round(x)), int(round(y)), c)
            if i % 9 == 0: put(t, int(round(x)) + 1, int(round(y)) - 1, (120, 116, 108)); put(t, int(round(x)) - 1, int(round(y)) + 1, (60, 58, 54))
    return t.out(.8)
@ztile('zone_rebar')
def _z_rebar():
    t = sprite(338); rr = t.rng
    for _ in range(5):
        x = rr.randint(6, 25); h = rr.randint(14, 30); bend = rr.uniform(-3, 3)
        for i in range(h):
            f = i / h; xx = x + bend * max(0, f - .5) * 4 * f; c = mixc((104, 60, 36), (70, 44, 30), (i % 3) / 3)
            put(t, int(round(xx)), N - 1 - i, c); put(t, int(round(xx)) + 1, N - 1 - i, mulc(c, .7))
    for y in range(26, 32):                                                                 # the lump of concrete they stick out of
        for x in range(4 + (31 - y) // 2, 28 - (31 - y) // 2): put(t, x, y, mulc((116, 114, 106), .8 + .3 * ((x * 7 + y * 3) % 5) / 5))
    return t.out(.8)

# ---- props (model boxes take the part of the picture where they sit in the block)
@ztile('zone_debris')                                                                      # what lies on every floor: grit, paper, glass, chips
def _z_debris():
    t = sprite(339); rr = t.rng
    for _ in range(70): put(t, rr.randrange(N), rr.randrange(N), rr.choice([(96, 94, 88), (120, 116, 108), (78, 74, 68), (110, 70, 52)]))
    for _ in range(5):                                                                      # paper scraps
        x = rr.randrange(N); y = rr.randrange(N); w = rr.randint(2, 4); h = rr.randint(2, 3); c = rr.choice([(170, 164, 146), (150, 144, 124), (180, 176, 160)])
        for dx in range(w):
            for dy in range(h): put(t, (x + dx) % N, (y + dy) % N, mulc(c, .9 + .1 * ((dx + dy) % 2)))
    for _ in range(6):                                                                      # glass
        x = rr.randrange(N); y = rr.randrange(N)
        for k in range(rr.randint(1, 3)): put(t, (x + k) % N, (y + (k % 2)) % N, (132, 150, 146))
    return t.out(.85)
def drum(seed, paint, rustk, band=None):
    t = T(seed); f = t.nz(1, 4, 3); fine = t.nz(2, 8, 2)
    for y in range(N):
        for x in range(N):
            c = mixc(paint, ramp([(70, 38, 24), (104, 58, 32), (128, 78, 44)], fine[y][x]), sstep(1 - rustk, 1.1 - rustk, f[y][x] * .7 + fine[y][x] * .3))
            rib = y in (7, 8, 18, 19) ; c = mulc(c, 1.15 if y in (7, 18) else .7 if y in (8, 19) else 1)
            if band and 22 <= y <= 25 and not rib: c = mixc(c, band, .7 * (1 - sstep(.6, .8, f[y][x])))
            t.p[y][x] = list(c)
    t.streak(3, (84, 46, 28), .6, .55); t.grain(.05); return t
@ztile('zone_barrel_side')
def _z_drum(): return drum(340, (70, 66, 56), .55).out(.8)
@ztile('zone_barrel_blue')
def _z_drum_b(): return drum(341, (54, 74, 96), .4, (150, 140, 60)).out(.85)
@ztile('zone_barrel_top')
def _z_drum_top():
    t = T(342); t.fill(t.nz(1, 4, 3), [(70, 62, 52), (88, 76, 62), (104, 86, 66)])
    for y in range(N):
        for x in range(N):
            r = math.hypot(x - 15.5, y - 15.5)
            if 11.5 < r < 13: t.p[y][x] = list(mulc(t.p[y][x], 1.25 if r < 12.2 else .6))
    for (bx, by, rr_) in ((9, 10, 2), (21, 20, 1.4)):
        for y in range(N):
            for x in range(N):
                if math.hypot(x - bx, y - by) < rr_: t.p[y][x] = list((36, 30, 26))
    t.blend(t.nz(3, 4, 2), .6, .85, (112, 62, 34), .7); t.grain(.05); return t.out(.8)
@ztile('zone_crate')
def _z_crate():
    t = planks_t(343, (82, 70, 52), (118, 102, 76), (130, 124, 110), .55)
    for i in range(N):                                                                      # the frame
        for (x, y) in ((i, 0), (i, 1), (i, 2), (0, i), (1, i), (2, i), (i, 29), (i, 30), (i, 31), (29, i), (30, i), (31, i)):
            t.p[y][x] = list(mulc((96, 82, 60), .85 + .25 * ((x + y) % 7) / 7))
    for i in range(3, 29): t.dot(i, i, (74, 62, 46), 1.0); t.dot(i + 1, i, (74, 62, 46), 1.0)   # brace
    for (x0, y0) in ((9, 12), (13, 12), (17, 12)):                                           # a stencil number
        for k in range(5): t.dot(x0, y0 + k, (40, 40, 34), .7); t.dot(x0 + 2, y0 + k, (40, 40, 34), .7 if k % 2 == 0 else 0)
    t.grain(.04); return t.out(.75)
@ztile('zone_tyre')
def _z_tyre():
    t = T(344); t.fill(t.nz(1, 6, 2), [(22, 22, 22), (30, 30, 30), (40, 40, 40)])
    for y in range(N):
        for x in range(N):
            if (x + (y // 3) * 2) % 6 < 2: q = t.p[y][x]; q[0] *= .6; q[1] *= .6; q[2] *= .6
    t.blend(t.nz(2, 3, 2), .7, .9, (70, 64, 56), .5); t.grain(.05); return t.out(.5)
@ztile('zone_tyre_top')
def _z_tyre_top():
    t = T(345); t.fill(t.nz(1, 6, 2), [(24, 24, 24), (34, 34, 34), (44, 44, 44)])
    for y in range(N):
        for x in range(N):
            r = math.hypot(x - 15.5, y - 15.5)
            if r < 7: t.p[y][x] = list((12, 11, 10)); t.a[y][x] = 255
            elif r < 8.5: q = t.p[y][x]; q[0] *= 1.4; q[1] *= 1.4; q[2] *= 1.4
    t.blend(t.nz(2, 3, 2), .7, .9, (70, 64, 56), .4); return t.out(.5)
@ztile('zone_sandbag')                                                                       # three courses of bags (5 px each)
def _z_sandbag():
    t = T(346); t.fill(t.nz(1, 5, 3), [(96, 88, 62), (116, 106, 76), (132, 122, 88)]); wv = t.nz(2, 16, 1)
    for y in range(N):
        for x in range(N):
            row = (N - 1 - y) * 16 // N // 5; ly = ((N - 1 - y) * 16 / N) % 5; off = 8 if row % 2 else 0; lx = ((x * 16 / N) + off) % 8
            k = .72 + .28 * math.sin(math.pi * min(1, ly / 5)) ** .6 * math.sin(math.pi * lx / 8) ** .35
            q = t.p[y][x]; v = k * (.94 + .12 * wv[y][x] if (x + y) % 2 else 1)
            q[0] *= v; q[1] *= v; q[2] *= v
    t.blend(t.nz(3, 3, 2), .7, .9, (70, 62, 46), .5); t.grain(.05); return t.out(.7)
@ztile('zone_barrier')                                                                       # concrete road block, faded stripes
def _z_barrier():
    t = concrete_t(347, (112, 110, 102), (136, 134, 124), (76, 72, 64), .4, 14, 1.0)
    f = t.nz(6, 4, 3)
    for y in range(N):
        for x in range(N):
            if ((x + y) // 6) % 2 == 0 and 4 < y < 22 and f[y][x] < .62:
                t.p[y][x] = list(mixc(t.p[y][x], (150, 64, 50), .55))
    cracks(t, 2, (60, 58, 54), None, (6, 12)); t.grain(.04); return t.out(.8)
@ztile('zone_pipe')
def _z_pipe():
    t = T(348); f = t.nz(1, 4, 3, ax=1, ay=.4); t.fill(t.nz(2, 6, 2), [(60, 72, 68), (74, 86, 80), (88, 98, 90)])
    t.blend(f, .5, .75, (104, 60, 34), .85); t.streak(3, (80, 44, 26), .6, .5)
    for y in range(N):
        q = t.p[y][0]; q[0] *= .6; q[1] *= .6; q[2] *= .6
    t.grain(.05); return t.out(.8)
@ztile('zone_heat_pipe')                                                                     # foil-wrapped heating main, torn to the wool
def _z_heat():
    t = T(349); t.fill(t.nz(1, 6, 3), [(120, 120, 114), (146, 146, 140), (168, 168, 160)])
    for y in range(N):
        for x in range(N):
            if x % 8 == 0: q = t.p[y][x]; q[0] *= .7; q[1] *= .7; q[2] *= .7                 # wrap bands
            elif x % 8 == 1: q = t.p[y][x]; q[0] *= 1.1; q[1] *= 1.1; q[2] *= 1.1
    peel(t, t.nz(2, 3, 3), .7, (150, 132, 86)); t.blend(t.nz(3, 3, 2), .7, .9, (70, 66, 56), .5); t.grain(.04); return t.out(.6)
@ztile('zone_radiator')
def _z_rad():
    t = T(350); t.fill(t.nz(1, 4, 3), [(132, 128, 112), (150, 146, 128), (164, 160, 142)])
    t.blend(t.nz(2, 4, 3), .6, .82, (112, 66, 40), .7); t.streak(3, (96, 56, 34), .55, .55); t.grain(.04); return t.out(.75)
@ztile('zone_bed_metal')                                                                     # a rusty bed end: a tube frame and bars (cut out)
def _z_bed():
    t = sprite(351); rf = t.nz(1, 4, 2)
    for y in range(N):
        for x in range(N):
            if x < 3 or x > 28 or y < 3 or y > 27 or (x % 6 == 3 and y > 6) or 6 <= y <= 8:
                put(t, x, y, mixc((92, 98, 96), (112, 64, 38), sstep(.35, .7, rf[y][x])))
    return t.out(.8)
@ztile('zone_mattress')
def _z_matt():
    t = T(352); t.fill(t.nz(1, 4, 3), [(120, 112, 96), (140, 132, 114), (154, 146, 126)])
    for y in range(N):
        for x in range(N):
            if x % 6 < 2: q = t.p[y][x]; q[0] *= .82; q[1] *= .86; q[2] *= .98
    t.blend(t.nz(2, 2, 2), .6, .85, (110, 90, 60), .6); t.blend(t.nz(3, 3, 2), .72, .92, (70, 62, 48), .6)   # stains
    t.grain(.05); return t.out(.7)
@ztile('zone_pole_wood')                                                                      # creosoted power pole
def _z_pole():
    t = T(353); t.fill(t.nz(1, 4, 3, ax=.25, ay=3.0), [(40, 32, 26), (56, 46, 36), (72, 60, 46)])
    t.blend(t.nz(2, 4, 2, ax=.25, ay=3.0), .7, .88, (26, 20, 16), .9); t.blend(t.nz(3, 3, 3), .7, .9, (96, 92, 82), .3); t.grain(.05); return t.out(.65)
@ztile('zone_insulator')
def _z_ins():
    t = T(354); t.fill(t.nz(1, 6, 2), [(58, 88, 74), (80, 116, 98), (110, 146, 126)])
    for y in range(N):
        if y % 6 < 2:
            for x in range(N): q = t.p[y][x]; q[0] *= .65; q[1] *= .65; q[2] *= .65
    return t.out(.8)
@ztile('zone_metal_post')
def _z_mpost():
    t = T(355); t.fill(t.nz(1, 4, 3, ax=.4, ay=2.5), [(84, 86, 84), (104, 106, 102), (124, 124, 118)])
    t.blend(t.nz(2, 4, 3), .66, .86, (110, 66, 40), .65); t.streak(3, (90, 52, 32), .5, .55); t.grain(.05); return t.out(.65)
@ztile('zone_lamp_head')
def _z_lamp():
    t = T(356); t.fill(t.nz(1, 4, 3), [(76, 78, 76), (94, 96, 92), (110, 110, 104)])
    t.blend(t.nz(2, 3, 3), .66, .86, (104, 62, 38), .6)
    for y in range(N):
        for x in range(N):
            if 4 <= x <= 27 and 4 <= y <= 27 and (x + y) % 5 and t.nz(3, 5, 2)[y][x] > .5: t.p[y][x] = list((140, 150, 140))   # what's left of the glass
    t.grain(.04); return t.out(.7)
@ztile('zone_sign_rad')                                                                       # the plate: rows 0..18, columns 4..28
def _z_sign():
    t = T(357); t.fill(t.nz(1, 4, 3), [(80, 82, 80), (100, 100, 96), (116, 116, 110)])
    f = t.nz(2, 4, 3)
    for y in range(N):
        for x in range(N):
            if 4 <= x <= 27 and y <= 17:
                c = (190, 160, 52) if f[y][x] < .72 else (124, 74, 42)
                if x in (4, 27) or y in (0, 17): c = mulc(c, .6)
                dx = x - 15.5; dy = y - 8.5; r = math.hypot(dx, dy); a = math.atan2(dy, dx)
                if r < 1.6: c = (24, 22, 20)
                elif 2.6 < r < 7 and math.cos(3 * (a + math.pi / 2)) > .5: c = (24, 22, 20)   # trefoil
                t.p[y][x] = list(c)
    for (hx, hy) in ((8, 4), (22, 12), (19, 3)): t.dot(hx, hy, (20, 18, 16)); t.dot(hx + 1, hy + 1, (150, 120, 60), .6)   # bullet holes
    t.streak(3, (110, 64, 36), .55, .55); t.grain(.04); return t.out(.85)

# ---------------------------------------------------------------- loot furniture (gen/zone_loot.cpp): worn Soviet veneer, enamel, painted steel
def _fill_rect(t, x0, y0, x1, y1, col, k=1.0):
    for y in range(max(0, y0), min(N - 1, y1) + 1):
        for x in range(max(0, x0), min(N - 1, x1) + 1): t.p[y][x] = list(mixc(t.p[y][x], col, k))
def _bevel(t, x0, y0, x1, y1, lo=.62, hi=1.18):                                           # a raised panel's edge: lit top-left, dark bottom-right
    for x in range(x0, x1 + 1):
        for (y, k) in ((y0, hi), (y1, lo)):
            q = t.p[y][x]; q[0] *= k; q[1] *= k; q[2] *= k
    for y in range(y0, y1 + 1):
        for (x, k) in ((x0, hi), (x1, lo)):
            q = t.p[y][x]; q[0] *= k; q[1] *= k; q[2] *= k
def _gap(t, x0, y0, x1, y1, col=(28, 22, 18)): _fill_rect(t, x0, y0, x1, y1, col, .9)
def _chips(t, k, thr, col):                                                               # paint knocked off the edges and corners
    f = t.nz(k, 6, 2)
    for y in range(N):
        for x in range(N):
            e = min(x, y, N - 1 - x, N - 1 - y)
            if f[y][x] > thr + e * .035: t.p[y][x] = list(mixc(t.p[y][x], col, .85))
def veneer_t(seed):
    t = T(seed); t.fill(t.nz(1, 4, 3, ax=.18, ay=1.6), [(64, 42, 28), (86, 58, 38), (104, 72, 48), (118, 84, 56)])
    t.blend(t.nz(2, 3, 2), .66, .9, (128, 112, 84), .45)                                     # sun-bleached, the lacquer gone
    for _ in range(5): t.walk(t.rng.randrange(N), t.rng.randrange(N), t.rng.random() * .4 - .2, t.rng.randrange(5, 14), (150, 128, 96), .5, .1)   # scratches
    _chips(t, 3, .86, (172, 150, 112)); t.grain(.04); return t
def enamel_t(seed):
    t = T(seed); t.fill(t.nz(1, 4, 3), [(170, 164, 140), (188, 184, 164), (202, 198, 182)])
    t.streak(2, (150, 132, 96), .35, .6)                                                     # yellowed runs
    f = t.nz(3, 6, 2)
    for y in range(N):
        for x in range(N):
            if f[y][x] > .8: t.p[y][x] = list((46, 40, 36)) if f[y][x] > .86 else list(mixc(t.p[y][x], (120, 70, 40), .8))   # chips with rust rings
    t.grain(.03); return t
def locker_t(seed, lo=(70, 86, 74), hi=(96, 112, 98)):
    t = T(seed); t.fill(t.nz(1, 4, 3), [lo, mixc(lo, hi, .5), hi])
    t.blend(t.nz(2, 4, 3), .7, .88, (112, 66, 40), .7); t.streak(3, (96, 56, 34), .5, .58)
    _chips(t, 4, .88, (128, 126, 120)); t.grain(.04); return t
def _handle_v(t, x, y0, y1):
    for y in range(y0, y1 + 1): t.dot(x, y, (176, 176, 168)); t.dot(x + 1, y, (96, 96, 92))
def _handle_h(t, x0, x1, y):
    for x in range(x0, x1 + 1): t.dot(x, y, (172, 170, 160)); t.dot(x, y + 1, (70, 66, 60))
def _vents(t, x0, x1, y0, rows):
    for i in range(rows):
        for x in range(x0, x1 + 1): t.dot(x, y0 + i * 2, (26, 28, 26)); t.dot(x, y0 + i * 2 + 1, (130, 140, 128), .5)
@ztile('zone_veneer')
def _z_veneer(): return veneer_t(360).out(.8)
@ztile('zone_wardrobe_lo')                                                                    # the doors' lower half: handles, keyhole, plinth
def _z_ward_lo():
    t = veneer_t(361); _gap(t, 15, 0, 16, 27); _bevel(t, 1, 0, 14, 27); _bevel(t, 17, 0, 30, 27)
    _handle_v(t, 12, 1, 6); _handle_v(t, 18, 1, 6); t.dot(13, 9, (20, 16, 12)); t.dot(13, 10, (20, 16, 12))
    _fill_rect(t, 0, 28, 31, 31, (34, 26, 20), .85); return t.out(.8)
@ztile('zone_wardrobe_hi')                                                                    # the doors' upper half: a tarnished mirror on the left one
def _z_ward_hi():
    t = veneer_t(362); _fill_rect(t, 0, 0, 31, 2, (52, 36, 24), .7); _gap(t, 15, 3, 16, 31); _bevel(t, 1, 3, 14, 31); _bevel(t, 17, 3, 30, 31)
    f = t.nz(5, 5, 3)
    for y in range(6, 30):
        for x in range(3, 13):
            c = mixc((132, 142, 146), (164, 172, 170), f[y][x])
            if f[y][x] > .72: c = mixc(c, (60, 56, 48), .7)                                    # the silvering gone
            t.p[y][x] = list(c)
    t.line(4, 9, 11, 22, (44, 46, 46), .8); _bevel(t, 2, 5, 13, 30, .55, 1.1); return t.out(.8)
@ztile('zone_enamel')
def _z_enamel(): return enamel_t(363).out(.75)
@ztile('zone_fridge_lo')                                                                      # the door: its edge, the handle, the vent grille at the foot
def _z_fridge_lo():
    t = enamel_t(364); _gap(t, 1, 0, 1, 24, (60, 56, 50)); _gap(t, 30, 0, 30, 24, (60, 56, 50)); _gap(t, 1, 24, 30, 24, (60, 56, 50))
    _handle_v(t, 25, 0, 9); _fill_rect(t, 3, 26, 28, 31, (40, 38, 34), .9); _vents(t, 4, 27, 27, 2); return t.out(.75)
@ztile('zone_fridge_hi')                                                                      # the top of the door: a chrome badge (rows are squeezed 2:1 on the model)
def _z_fridge_hi():
    t = enamel_t(365)
    for y in range(0, 6):
        for x in range(N): q = t.p[y][x]; k = .7 + .05 * y; q[0] *= k; q[1] *= k; q[2] *= k   # the rounded top
    _gap(t, 1, 6, 1, 31, (60, 56, 50)); _gap(t, 30, 6, 30, 31, (60, 56, 50)); _gap(t, 1, 6, 30, 6, (60, 56, 50))
    _fill_rect(t, 8, 12, 23, 16, (176, 178, 172)); _bevel(t, 8, 12, 23, 16, .6, 1.15)
    for x in (10, 11, 13, 15, 16, 18, 20, 21): t.dot(x, 14, (60, 30, 28))                 # the maker's name, red on chrome
    _handle_v(t, 25, 22, 31); return t.out(.75)
@ztile('zone_locker_side')
def _z_locker_side(): return locker_t(366).out(.75)
@ztile('zone_locker_lo')                                                                      # a locker door's lower half: the lever handle, vents
def _z_locker_lo():
    t = locker_t(367); _bevel(t, 1, 0, 30, 31, .55, 1.15); _handle_v(t, 24, 1, 7); t.dot(24, 8, (30, 30, 28)); _vents(t, 9, 22, 21, 4); return t.out(.75)
@ztile('zone_locker_hi')                                                                      # the upper half: vents and a number plate
def _z_locker_hi():
    t = locker_t(368); _bevel(t, 1, 0, 30, 31, .55, 1.15); _vents(t, 9, 22, 4, 4)
    _fill_rect(t, 12, 17, 19, 21, (196, 190, 170)); _bevel(t, 12, 17, 19, 21, .7, 1.05)
    for (x, y) in ((14, 18), (14, 19), (14, 20), (16, 18), (17, 18), (17, 19), (16, 20), (17, 20)): t.dot(x, y, (34, 32, 30))
    return t.out(.75)
@ztile('zone_drawers')                                                                        # three drawers in veneer (desk pedestal, bedside table)
def _z_drawers():
    t = veneer_t(369)
    for (y0, y1) in ((1, 9), (11, 20), (22, 30)): _bevel(t, 1, y0, 30, y1); _handle_h(t, 12, 19, (y0 + y1) // 2 - 1)
    _gap(t, 0, 10, 31, 10); _gap(t, 0, 21, 31, 21); return t.out(.8)
@ztile('zone_cupboard_front')                                                                 # a kitchen cupboard: two doors, little knobs
def _z_cupboard():
    t = veneer_t(370); _bevel(t, 1, 1, 14, 30); _bevel(t, 17, 1, 30, 30); _gap(t, 15, 0, 16, 31)
    for x in (12, 19): t.dot(x, 6, (176, 170, 150)); t.dot(x, 7, (60, 50, 40))
    return t.out(.8)
@ztile('zone_filing_front')                                                                   # a steel filing cabinet: four drawers with label holders
def _z_filing():
    t = locker_t(371, (88, 96, 86), (112, 120, 108))
    for i in range(4):
        y0 = i * 8 + 1; _bevel(t, 1, y0, 30, y0 + 6, .55, 1.15); _fill_rect(t, 12, y0 + 1, 19, y0 + 3, (190, 184, 160)); _handle_h(t, 13, 18, y0 + 4)
    return t.out(.75)
@ztile('zone_toolbox')                                                                        # red painted steel, rusty, the lid line and its latch
def _z_toolbox():
    t = locker_t(372, (104, 30, 24), (140, 48, 36)); _gap(t, 0, 9, 31, 9, (40, 20, 16)); _fill_rect(t, 14, 7, 17, 12, (150, 148, 140)); return t.out(.8)
@ztile('zone_case_green')                                                                     # army green boards, a stencil, steel corners
def _z_case():
    t = planks_t(373, (52, 62, 38), (78, 90, 56), (96, 104, 84), .5)
    f = t.nz(9, 5, 2)
    for y in range(11, 20):
        for x in range(6, 26):
            if (x % 4 != 1) and (y in (11, 12, 15, 18, 19) or x % 4 == 0) and f[y][x] < .7: t.p[y][x] = list(mixc(t.p[y][x], (190, 178, 120), .7))   # the stencil
    for (x0, y0) in ((0, 0), (27, 0), (0, 27), (27, 27)): _fill_rect(t, x0, y0, x0 + 4, y0 + 4, (92, 92, 88)); _bevel(t, x0, y0, x0 + 4, y0 + 4)
    t.grain(.04); return t.out(.75)
@ztile('zone_first_aid')                                                                      # a wall first-aid box: white enamel, a red cross
def _z_aid():
    t = enamel_t(374); _bevel(t, 1, 1, 30, 30, .55, 1.1)
    _fill_rect(t, 13, 7, 18, 24, (164, 34, 30)); _fill_rect(t, 7, 13, 24, 18, (164, 34, 30))
    t.blend(t.nz(6, 4, 2), .7, .9, (120, 92, 70), .4); return t.out(.8)

# ---------------------------------------------------------------- colour maps (the engine multiplies tinted tiles by these)
def colormap(anchors, name):
    px = []
    for py in range(256):
        for pxx in range(256):
            tt = 1 - pxx / 255.0; hp = 1 - py / 255.0; hum = min(1.0, hp / tt) if tt > .001 else 0.0
            wsum = 0.0; acc = [0.0, 0.0, 0.0]
            for (at, ah, col) in anchors:
                d = math.hypot((tt - at) * 1.0, (hum - ah) * .8) + .02; w = 1 / d ** 3; wsum += w
                for i in range(3): acc[i] += col[i] * w
            px.append(tuple(int(acc[i] / wsum + .5) for i in range(3)))
    return px
GRASS = [(.66, .27, (150, 150, 92)), (.59, .59, (112, 128, 76)), (.745, .157, (160, 150, 100)), (.98, .03, (164, 98, 62)), (.92, .10, (156, 108, 68)),
         (1.0, .0, (166, 94, 58)), (.1, .5, (120, 130, 110)), (.1, .0, (130, 130, 114)), (.8, .9, (104, 122, 64)), (.4, .3, (134, 140, 98)), (.5, 1.0, (96, 116, 70))]
FOLIAGE = [(.66, .27, (168, 152, 78)), (.59, .59, (116, 130, 72)), (.745, .157, (156, 142, 84)), (.98, .03, (178, 98, 62)), (.92, .10, (166, 112, 66)),
           (1.0, .0, (180, 92, 56)), (.1, .5, (114, 126, 104)), (.1, .0, (124, 126, 108)), (.8, .9, (100, 120, 60)), (.4, .3, (136, 138, 90)), (.5, 1.0, (92, 112, 66))]

# ---------------------------------------------------------------- main
def main():
    if '--list' in sys.argv: print('\n'.join(sorted(TILES)) + '\ncolormap/grass\ncolormap/foliage\n' + '\n'.join(sorted(ZTILES))); return
    zdir = os.path.normpath(os.path.join(HERE, '..', '..', 'data', 'textures', 'block'))
    for name, fn in sorted(ZTILES.items()):                                         # the Zone kit: built-in tiles
        if '--only' in sys.argv and name not in sys.argv: continue
        write_png(os.path.join(zdir, name + '.png'), N, N, fn())
    print('wrote %d Zone kit tiles to %s' % (len(ZTILES), zdir))
    if '--kit' in sys.argv or '--only' in sys.argv: return
    os.makedirs(os.path.join(TEX, 'block'), exist_ok=True); os.makedirs(os.path.join(TEX, 'colormap'), exist_ok=True)
    for name, fn in sorted(TILES.items()):
        write_png(os.path.join(TEX, 'block', name + '.png'), N, N, fn())
    for name, anchors in (('grass', GRASS), ('foliage', FOLIAGE)):
        write_png(os.path.join(TEX, 'colormap', name + '.png'), 256, 256, colormap(anchors, name))
    with open(os.path.join(OUT, 'pack.mcmeta'), 'w') as f:
        f.write('{"pack":{"pack_format":12,"description":"Exclusion Zone: muted, worn, overcast tiles over DokuQuest Base (tools/zone/paint_textures.py)"}}\n')
    print('wrote %d tiles + 2 colour maps to %s' % (len(TILES), OUT))

if __name__ == '__main__': main()
