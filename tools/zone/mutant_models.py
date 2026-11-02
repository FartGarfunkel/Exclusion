#!/usr/bin/env python3
# tools/zone/mutant_models.py — Exclusion's creature bodies: the Zone's mutants (original designs on the S.T.A.L.K.E.R. /
# Misery / STALCRAFT archetypes) as multi-part voxel rigs, merged into data/models.json under z_* keys (replaces earlier
# z_* models, touches nothing else), and their hides painted into data/textures/entity/zone/mut/*.png ("@zone/mut/<name>").
# Run from the repo root after editing:  python3 tools/zone/mutant_models.py  (then tools/zone/mobs.py if mobs changed)
#
# Parts are the engine's rig parts (sim/creature_rig.cpp): "at" = pivot in px (1/16 block) from the parent's pivot,
# "box" = the part's box about its pivot, front is -z. Roles drive the walk: quad (qbody/qhead/qleg/qshin, tail, ear,
# "neck"), biped (body/head/armR/foreR/legR/shinR...). Extra fields the voxeliser reads (render/creature_models.cpp
# shapePart): "shape" ell|cyl|cylx|cylz, "taper" [axis, scale at min, scale at max], "jit" (mottle), "grad" [top, bottom].
# Each model's "post" (z*) adds its own life in creature_rig.cpp (zonePost): tentacles, a charging head, cloaks, the brute's slam.
import json, os, math, random
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEXDIR = os.path.join(ROOT, 'data', 'textures', 'entity', 'zone', 'mut')
N = 32
TEX = {}

def T(name): return '@zone/mut/' + name

# ------------------------------------------------------------------------------------------------ painting
def canvas(rgb): a = np.zeros((N, N, 4), np.float32); a[..., :3] = rgb; a[..., 3] = 255; return a
def rng(seed): return np.random.default_rng(abs(hash(seed)) % (2**32) if isinstance(seed, str) else seed)

def smooth_noise(r, scale=4, oct=3):
    out = np.zeros((N, N), np.float32); amp = 1.0; tot = 0
    for o in range(oct):
        s = max(2, N // (scale * 2 ** o) + 1)
        g = r.random((s + 1, s + 1)).astype(np.float32)
        ys = np.linspace(0, s - 1e-3, N); xs = np.linspace(0, s - 1e-3, N)
        y0 = ys.astype(int); x0 = xs.astype(int); fy = ys - y0; fx = xs - x0
        fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
        a = g[y0][:, x0]; b = g[y0][:, x0 + 1]; c = g[y0 + 1][:, x0]; d = g[y0 + 1][:, x0 + 1]
        v = (a * (1 - fx) + b * fx) * (1 - fy)[:, None] + (c * (1 - fx) + d * fx) * fy[:, None]
        out += v * amp; tot += amp; amp *= 0.5
    return out / tot

def hide(name, base, var=0.18, seed=None, fur=0.0, patches=None, patch_amt=0.0, scabs=0, spots=None, spot_amt=0.0,
         stripes=None, stripe_n=0, ribs=None, veins=None, vein_n=0, dark_bottom=0.0, scars=0):
    r = rng(seed or name)
    a = canvas(base)
    n = smooth_noise(r, 3, 3)
    a[..., :3] *= (1 + (n[..., None] - 0.5) * 2 * var)
    if fur:   # short dark strokes running down
        for _ in range(int(140 * fur)):
            x, y = r.integers(0, N), r.integers(0, N - 3); L = r.integers(2, 5); k = 0.72 + r.random() * 0.15
            for i in range(L):
                if y + i < N: a[y + i, x, :3] *= k
    if patches is not None and patch_amt > 0:   # bald / bare skin blotches
        m = smooth_noise(r, 4, 2)
        t = np.quantile(m, 1 - patch_amt)
        mask = m > t
        pn = smooth_noise(r, 6, 2)
        a[mask, :3] = np.array(patches, np.float32) * (0.9 + 0.2 * pn[mask, None])
        edge = (m > t - 0.03) & ~mask
        a[edge, :3] *= 0.8
    if spots is not None and spot_amt > 0:
        m = smooth_noise(r, 6, 2); t = np.quantile(m, 1 - spot_amt); mask = m > t
        a[mask, :3] = a[mask, :3] * 0.4 + np.array(spots, np.float32) * 0.6
    for _ in range(scabs):   # crusted sores
        cx, cy = r.integers(2, N - 2), r.integers(2, N - 2); rad = 1 + r.random() * 1.6
        for y in range(N):
            for x in range(N):
                d = math.hypot(x - cx, y - cy)
                if d < rad: a[y, x, :3] = np.array([92, 40, 34]) * (0.8 + 0.3 * r.random())
                elif d < rad + 1: a[y, x, :3] = a[y, x, :3] * 0.6 + np.array([140, 90, 80]) * 0.4
    if stripes is not None:
        for i in range(stripe_n):
            x0 = r.integers(0, N); w = 1 + r.integers(0, 2)
            for y in range(N):
                xx = int(x0 + math.sin(y * 0.3 + i) * 2)
                for k in range(w):
                    if 0 <= xx + k < N: a[y, xx + k, :3] = a[y, xx + k, :3] * 0.5 + np.array(stripes) * 0.5
    if ribs is not None:   # ribs showing through the hide (sides)
        for i in range(6):
            y = 5 + i * 4
            for x in range(4, N - 4):
                yy = y + int(2 * math.sin(x / N * math.pi))
                if 0 <= yy < N: a[yy, x, :3] = a[yy, x, :3] * 0.65 + np.array(ribs) * 0.35
                if 0 <= yy + 1 < N: a[yy + 1, x, :3] *= 0.78
    if veins is not None:
        for _ in range(vein_n):
            x, y = float(r.integers(0, N)), float(r.integers(0, N)); ang = r.random() * 6.28
            for s in range(14):
                ang += (r.random() - 0.5) * 0.9; x += math.cos(ang); y += math.sin(ang)
                xi, yi = int(x), int(y)
                if 0 <= xi < N and 0 <= yi < N: a[yi, xi, :3] = a[yi, xi, :3] * 0.4 + np.array(veins) * 0.6
    for _ in range(scars):
        x, y = float(r.integers(3, N - 3)), float(r.integers(3, N - 3)); ang = r.random() * 6.28
        for s in range(7 + r.integers(0, 6)):
            x += math.cos(ang); y += math.sin(ang)
            xi, yi = int(x), int(y)
            if 0 <= xi < N and 0 <= yi < N: a[yi, xi, :3] = np.array([150, 110, 100]) * (0.9 + 0.2 * r.random())
    if dark_bottom:
        for y in range(N): a[y, :, :3] *= 1 - dark_bottom * max(0, (y - N * 0.55) / (N * 0.45))
    TEX[name] = a
    return T(name)

def flat(name, rgb, var=0.08, seed=None):
    return hide(name, rgb, var, seed)

def derive(name, src, fn):
    a = TEX[src].copy(); fn(a); TEX[name] = a; return T(name)

def disc(a, cx, cy, rad, rgb, alpha=None):
    for y in range(N):
        for x in range(N):
            if math.hypot(x + 0.5 - cx, y + 0.5 - cy) < rad:
                if rgb is None: a[y, x, 3] = 0
                else: a[y, x, :3] = rgb
def rect(a, x0, y0, x1, y1, rgb):
    a[max(0, y0):min(N, y1), max(0, x0):min(N, x1), :3] = rgb
def clear(a, x0, y0, x1, y1): a[max(0, y0):min(N, y1), max(0, x0):min(N, x1), 3] = 0

def jagged_top(a, seed, lo=0.25, hi=0.75, period=3):
    """a bristle / rag edge: the top of the picture cut into points (transparent between)"""
    r = rng(seed)
    for x in range(N):
        h = lo + (hi - lo) * (0.5 + 0.5 * math.sin(x * 2 * math.pi / period)) * (0.6 + 0.4 * r.random())
        clear(a, x, 0, x + 1, int(h * N))
def jagged_bottom(a, seed, lo=0.15, hi=0.5, period=4):
    r = rng(seed)
    for x in range(N):
        h = lo + (hi - lo) * (0.5 + 0.5 * math.sin(x * 2 * math.pi / period + r.random())) * (0.5 + 0.5 * r.random())
        clear(a, x, N - int(h * N), x + 1, N)

def save_textures():
    os.makedirs(TEXDIR, exist_ok=True)
    from struct import pack
    import zlib
    for name, a in TEX.items():
        px = np.clip(a + 0.5, 0, 255).astype(np.uint8)
        raw = b''.join(b'\0' + px[y].tobytes() for y in range(N))
        def ch(t, d): return pack('>I', len(d)) + t + d + pack('>I', zlib.crc32(t + d) & 0xffffffff)
        png = b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', pack('>IIBBBBB', N, N, 8, 6, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b'')
        open(os.path.join(TEXDIR, name + '.png'), 'wb').write(png)

# ------------------------------------------------------------------------------------------------ rigs
MODELS = {}
class M:
    def __init__(s, key, rig, **kw): s.key, s.rig, s.kw, s.parts = key, rig, kw, []
    def p(s, name, parent, at, box, tex, **kw):
        d = {'name': name, 'parent': parent, 'at': list(at), 'box': list(box), 'tex': tex}
        d.update(kw); s.parts.append(d); return d
    def pair(s, name, parent, at, box, tex, mirror_box=True, **kw):
        """name with R (+x) and L (-x) — at.x and the box mirrored for L"""
        r = dict(kw); l = dict(kw)
        if 'side' not in kw: r['side'] = 1; l['side'] = -1
        if 'rest' in kw: l['rest'] = [kw['rest'][0], -kw['rest'][1], -kw['rest'][2]]
        bl = [-box[3], box[1], box[2], -box[0], box[4], box[5]] if mirror_box else list(box)
        def par(side):
            if parent and parent.endswith('*'): return parent[:-1] + side
            return parent
        s.p(name.replace('*', 'R') if '*' in name else name + 'R', par('R'), at, box, tex, **r)
        s.p(name.replace('*', 'L') if '*' in name else name + 'L', par('L'), [-at[0], at[1], at[2]], bl, tex, **l)
    def done(s):
        out = {'rig': s.rig}; out.update(s.kw); parts = []
        for d in s.parts:
            q = dict(d)
            q['pivot'] = [v / 16 for v in d['at']]
            q['min'] = [v / 16 for v in d['box'][:3]]
            q['max'] = [v / 16 for v in d['box'][3:]]
            parts.append(q)
        out['parts'] = parts
        MODELS[s.key] = out

def quad_legs(m, fx, bx, fz, bz, y, leg, shin, tex, front_k=1.0, shin_tex=None, shape='cyl', taper=0.8):
    """four legs (legFR..BL + shinFR..BL) at hip height y; leg/shin = (radius, length)"""
    for (nm, x, z, front, side) in [('FR', fx, fz, True, 1), ('FL', -fx, fz, True, -1), ('BR', bx, bz, False, 1), ('BL', -bx, bz, False, -1)]:
        k = front_k if front else 1.0
        lr, ll = leg[0] * k, leg[1]; sr, sl = shin[0] * k, shin[1]
        m.p('leg' + nm, None, [x, y, z], [-lr, -ll, -lr, lr, 0, lr], tex, role='qleg', front=front, side=side, limb='leg' + nm, shape=shape, taper=[1, taper, 1.0], jit=0.08)
        m.p('shin' + nm, 'leg' + nm, [0, -ll, 0], [-sr, -sl, -sr, sr, 0, sr], shin_tex or tex, role='qshin', front=front, side=side, limb='leg' + nm, shape=shape, taper=[1, 0.75, 1.0], jit=0.08, grad=[1.0, 0.55])

# ================================================================================================ the hides
EYE_RED = None
def paint_all():
    global EYE_RED
    # eyes
    a = canvas((150, 20, 14)); disc(a, 16, 16, 9, (230, 60, 30)); disc(a, 16, 16, 4, (255, 170, 80)); TEX['eye_red'] = a
    a = canvas((150, 110, 20)); disc(a, 16, 16, 9, (230, 190, 60)); disc(a, 16, 16, 4, (40, 20, 10)); TEX['eye_yellow'] = a
    a = canvas((200, 120, 30)); disc(a, 16, 16, 9, (255, 190, 70)); disc(a, 16, 16, 4, (255, 240, 180)); TEX['eye_ember'] = a
    a = canvas((205, 200, 170)); disc(a, 16, 16, 7, (150, 120, 40)); disc(a, 16, 16, 3.5, (20, 14, 10)); disc(a, 13, 13, 1.6, (240, 240, 230)); TEX['eye_wet'] = a
    a = canvas((188, 176, 140)); TEX['bone'] = a; hide('bone', (196, 184, 146), 0.12, dark_bottom=0.3)
    hide('tusk', (210, 196, 150), 0.14)
    TEX['tusk'][:6, :, :3] *= 0.75   # the root end darker
    hide('claw', (58, 52, 46), 0.15)
    hide('hoof', (44, 38, 34), 0.15)
    hide('teeth', (214, 204, 170), 0.1)
    TEX['teeth'][::4, :, :3] *= 0.6

    # ---- tusker (mutant boar): dark bristly brown-grey, bald scarred patches
    hide('boar_hide', (76, 62, 50), 0.22, fur=1.2, patches=(124, 96, 86), patch_amt=0.12, scars=3, scabs=1)
    hide('boar_belly', (92, 78, 66), 0.2, fur=0.6, patches=(130, 100, 92), patch_amt=0.25)
    hide('boar_bristle', (40, 32, 26), 0.25, fur=2.0)
    derive('boar_bristle_side', 'boar_bristle', lambda a: jagged_top(a, 'bb', 0.0, 0.55, 2.5))
    hide('boar_snout', (112, 88, 80), 0.15, patches=(90, 64, 60), patch_amt=0.2)
    a = TEX['boar_snout'].copy(); disc(a, 16, 16, 13, (128, 98, 90)); disc(a, 10.5, 17, 3.2, (24, 14, 12)); disc(a, 21.5, 17, 3.2, (24, 14, 12)); TEX['boar_nose'] = a
    hide('boar_face', (70, 58, 48), 0.2, fur=1.0, scars=2)

    # ---- hogspawn (mutant pig): bloated, diseased pink-grey, purple blotches, sores
    hide('hog_skin', (150, 116, 104), 0.2, patches=(112, 70, 84), patch_amt=0.18, scabs=4, veins=(120, 60, 80), vein_n=6, scars=1)
    hide('hog_belly', (164, 128, 112), 0.18, veins=(130, 70, 86), vein_n=8, scabs=2)
    hide('hog_lump', (172, 112, 104), 0.25, veins=(110, 50, 70), vein_n=10, scabs=3)
    a = TEX['hog_skin'].copy(); disc(a, 16, 15, 14, (176, 128, 118)); disc(a, 10, 15, 3.6, (40, 18, 22)); disc(a, 22, 16, 4.2, (40, 18, 22)); TEX['hog_nose'] = a
    a = TEX['hog_skin'].copy(); rect(a, 0, 0, 32, 16, (60, 26, 30));
    for x in range(2, 30, 4): rect(a, x, 0, x + 2, 9 + (x * 7) % 5, (210, 196, 160))
    TEX['hog_jaw'] = a

    # ---- scab dog (blind dog): mangy grey-brown fur, raw bald skin, scabs, ribs, no eyes
    hide('dog_hide', (92, 82, 68), 0.22, fur=1.0, patches=(146, 112, 102), patch_amt=0.3, scabs=3, ribs=(150, 120, 110))
    hide('dog_back', (74, 66, 54), 0.22, fur=1.4, patches=(140, 106, 98), patch_amt=0.22, scabs=2)
    hide('dog_skin', (146, 114, 104), 0.18, veins=(120, 70, 74), vein_n=3, scabs=1)
    hide('dog_leg', (84, 74, 62), 0.2, fur=1.0, patches=(140, 108, 98), patch_amt=0.3, dark_bottom=0.5)
    a = TEX['dog_skin'].copy()   # the blind face: skin grown over the eye sockets, scar lines where eyes were
    for cx in (8, 24):
        disc(a, cx, 12, 4.5, (120, 88, 82)); rect(a, cx - 4, 12, cx + 4, 13, (80, 46, 44))
    rect(a, 4, 22, 28, 24, (110, 80, 76)); TEX['dog_face'] = a
    a = TEX['dog_skin'].copy(); disc(a, 16, 13, 8, (30, 22, 22)); disc(a, 13, 14, 1.8, (8, 6, 6)); disc(a, 19, 14, 1.8, (8, 6, 6))
    rect(a, 0, 24, 32, 32, (60, 26, 28))
    for x in range(1, 31, 3): rect(a, x, 24, x + 2, 29, (214, 204, 170))
    TEX['dog_nose'] = a
    a = TEX['dog_skin'].copy(); rect(a, 0, 18, 32, 32, (60, 26, 28))
    for x in range(0, 32, 3): rect(a, x, 13 + (x % 2), x + 2, 22, (220, 210, 178))
    TEX['dog_muzzle'] = a
    derive('dog_spine', 'dog_skin', lambda a: jagged_top(a, 'ds', 0.1, 0.7, 4))
    hide('dog_black', (46, 42, 38), 0.22, fur=1.2, patches=(130, 98, 92), patch_amt=0.28, scabs=3, ribs=(110, 90, 84))
    hide('dog_ginger', (120, 86, 56), 0.22, fur=1.0, patches=(150, 112, 100), patch_amt=0.3, scabs=3, ribs=(160, 120, 100))

    # ---- lurcher (bigger hound): heavy dark grey-brown shag, scarred, burning yellow eyes
    hide('lurch_hide', (70, 64, 56), 0.24, fur=1.6, patches=(122, 96, 88), patch_amt=0.12, scars=3)
    hide('lurch_mane', (52, 46, 40), 0.26, fur=2.2)
    derive('lurch_mane_side', 'lurch_mane', lambda a: jagged_top(a, 'lm', 0.0, 0.4, 3))
    hide('lurch_leg', (64, 58, 50), 0.22, fur=1.4, dark_bottom=0.5)
    a = TEX['lurch_hide'].copy(); rect(a, 2, 9, 30, 12, (40, 34, 30)); TEX['lurch_face'] = a   # the heavy brow

    # ---- rad rat
    hide('rat_fur', (88, 80, 66), 0.2, fur=1.6, patches=(150, 116, 106), patch_amt=0.18, scabs=2)
    hide('rat_skin', (168, 128, 120), 0.14)
    a = TEX['rat_skin'].copy(); disc(a, 16, 18, 5, (60, 30, 32)); TEX['rat_nose'] = a

    # ---- shroud (camp stalker): gaunt grey-brown hide, purple veins, ragged loincloth; dark-red feeding tentacles
    hide('shroud_skin', (128, 112, 98), 0.18, veins=(96, 70, 92), vein_n=8, scars=2, patches=(110, 92, 84), patch_amt=0.15)
    hide('shroud_torso', (124, 108, 94), 0.16, veins=(96, 70, 92), vein_n=6, ribs=(150, 132, 116))
    hide('shroud_tent', (110, 56, 54), 0.2, veins=(70, 30, 36), vein_n=4)
    a = TEX['shroud_skin'].copy()   # the face: a heavy brow over two small red eyes, the cheeks gone to folds
    rect(a, 3, 8, 29, 11, (84, 70, 62)); disc(a, 10, 13, 2.5, (28, 14, 14)); disc(a, 22, 13, 2.5, (28, 14, 14))
    rect(a, 6, 20, 26, 32, (92, 52, 52)); TEX['shroud_face'] = a
    hide('shroud_rag', (64, 58, 46), 0.2, fur=0.5)
    derive('shroud_rag_cut', 'shroud_rag', lambda a: jagged_bottom(a, 'sr', 0.1, 0.55, 5))
    hide('shroud_claw', (44, 38, 34), 0.12)

    # ---- crawler (gas-masked leaper): torn olive fatigues, rubber mask, dull lenses
    hide('crawl_cloth', (74, 78, 54), 0.2, fur=0.4, patches=(52, 48, 40), patch_amt=0.12, scars=0)
    hide('crawl_pants', (66, 68, 50), 0.2, patches=(48, 44, 38), patch_amt=0.15, dark_bottom=0.3)
    hide('crawl_skin', (120, 112, 100), 0.18, veins=(90, 70, 80), vein_n=4, scabs=2)
    hide('crawl_boot', (40, 34, 28), 0.15)
    hide('mask_rubber', (50, 52, 48), 0.12)
    a = TEX['mask_rubber'].copy()
    for cx in (9, 23):
        disc(a, cx, 12, 6.2, (30, 30, 28)); disc(a, cx, 12, 4.8, (40, 62, 54)); disc(a, cx - 1.4, 10.6, 1.3, (130, 160, 150))
    TEX['mask_face'] = a
    hide('mask_filter', (96, 98, 88), 0.15)
    a = TEX['mask_filter'].copy(); disc(a, 16, 16, 9, (60, 62, 56)); disc(a, 16, 16, 5, (30, 30, 28)); TEX['mask_filter_f'] = a
    hide('hose', (34, 34, 32), 0.1)
    for y in range(0, N, 3): TEX['hose'][y, :, :3] *= 0.6

    # ---- husk (zombified stalker): a stalker's suit gone to rags, grey dead skin
    hide('husk_jacket', (82, 86, 66), 0.2, patches=(58, 54, 44), patch_amt=0.14, scabs=0, scars=0, fur=0.3)
    a = TEX['husk_jacket']; rect(a, 4, 6, 12, 12, (62, 64, 50)); rect(a, 20, 6, 28, 12, (62, 64, 50)); rect(a, 15, 0, 17, 32, (54, 56, 44))   # chest pockets, zip
    for (x, y) in ((6, 20), (22, 24), (10, 27)): disc(a, x, y, 2.2, (60, 30, 28))   # bullet holes, dried blood
    hide('husk_jacket2', (92, 84, 62), 0.2, patches=(60, 54, 42), patch_amt=0.16, fur=0.3)
    a = TEX['husk_jacket2']; rect(a, 0, 14, 32, 17, (50, 44, 34)); disc(a, 24, 8, 2.5, (64, 28, 26))
    hide('husk_army', (70, 76, 58), 0.24, spots=(52, 58, 40), spot_amt=0.3, patches=(56, 50, 40), patch_amt=0.1)
    hide('husk_pants', (60, 62, 52), 0.18, patches=(46, 44, 38), patch_amt=0.12, dark_bottom=0.35)
    hide('husk_skin', (118, 122, 104), 0.2, veins=(84, 88, 76), vein_n=5, scabs=2)
    a = TEX['husk_skin'].copy(); disc(a, 10, 14, 3, (34, 30, 26)); disc(a, 22, 14, 3, (34, 30, 26)); disc(a, 10, 14, 1, (150, 150, 120)); disc(a, 22, 14, 1, (150, 150, 120))
    rect(a, 9, 22, 23, 25, (44, 30, 28)); rect(a, 11, 22, 21, 23, (170, 160, 130)); TEX['husk_face'] = a
    hide('husk_hood', (70, 74, 58), 0.2, patches=(52, 50, 42), patch_amt=0.12)
    a = TEX['husk_hood'].copy(); rect(a, 5, 6, 27, 30, (20, 20, 18)); TEX['husk_hood_f'] = a
    hide('husk_helmet', (66, 72, 56), 0.16, scars=2)
    hide('husk_pack', (76, 70, 52), 0.2, patches=(56, 50, 40), patch_amt=0.2)
    a = TEX['husk_pack']; rect(a, 0, 10, 32, 13, (48, 42, 32)); rect(a, 6, 16, 26, 26, (68, 62, 46))
    hide('boot', (38, 32, 28), 0.15)

    # ---- puppeteer (psy master): stunted, a swollen veined cranium, a small crumpled face, rags
    hide('pup_skin', (156, 144, 122), 0.16, veins=(132, 86, 98), vein_n=6, patches=(140, 116, 106), patch_amt=0.12)
    hide('pup_skull', (166, 148, 128), 0.2, veins=(126, 74, 92), vein_n=16, patches=(150, 116, 112), patch_amt=0.14, scabs=2)
    a = TEX['pup_skin'].copy()   # the face: small sunk eyes, a slack mouth
    disc(a, 10, 12, 3, (40, 26, 26)); disc(a, 22, 12, 3, (40, 26, 26)); disc(a, 10, 12, 1.2, (200, 180, 120)); disc(a, 22, 12, 1.2, (200, 180, 120))
    rect(a, 14, 15, 18, 20, (130, 112, 96)); rect(a, 9, 24, 23, 27, (56, 30, 32)); TEX['pup_face'] = a
    hide('pup_rag', (92, 84, 70), 0.24, patches=(66, 58, 48), patch_amt=0.2, fur=0.4)
    derive('pup_rag_cut', 'pup_rag', lambda a: jagged_bottom(a, 'pr', 0.05, 0.4, 4))

    # ---- stoneback (giant brute): leathery grey-brown, callused stone plates on its back
    hide('brute_skin', (112, 100, 88), 0.2, veins=(90, 70, 70), vein_n=5, scars=4, patches=(130, 110, 100), patch_amt=0.12)
    hide('brute_plate', (96, 94, 88), 0.3, spots=(70, 68, 64), spot_amt=0.35, scars=2)
    hide('brute_belly', (138, 120, 104), 0.18, veins=(110, 76, 80), vein_n=8)
    a = TEX['brute_skin'].copy(); rect(a, 2, 6, 30, 10, (70, 60, 54)); disc(a, 10, 12, 2, (200, 120, 30)); disc(a, 22, 12, 2, (200, 120, 30))
    rect(a, 4, 20, 28, 30, (50, 24, 24))
    for x in range(5, 28, 3): rect(a, x, 20, x + 2, 24, (210, 196, 160))
    TEX['brute_face'] = a

    # ---- hurler (dwarfish telekinetic): a heavy hooded cloak of sacking, big bare hands, eyes burning in the hood
    hide('hurl_cloak', (74, 62, 48), 0.22, fur=0.8, patches=(56, 46, 36), patch_amt=0.18)
    derive('hurl_cloak_cut', 'hurl_cloak', lambda a: jagged_bottom(a, 'hc', 0.04, 0.3, 5))
    a = TEX['hurl_cloak'].copy(); rect(a, 4, 6, 28, 32, (16, 12, 10)); TEX['hurl_hood_f'] = a
    hide('hurl_skin', (140, 122, 100), 0.18, veins=(110, 80, 70), vein_n=4, scabs=1)
    hide('hurl_belt', (46, 36, 28), 0.12)
    hide('shield', (150, 190, 220), 0.25)

    # ---- ridgecat (two-headed cat of the north): tawny-grey hide in dark stripes, bald scarred shoulders
    hide('cat_hide', (118, 102, 84), 0.2, fur=1.0, stripes=(52, 44, 38), stripe_n=7, patches=(138, 110, 100), patch_amt=0.1, scars=2)
    hide('cat_back', (96, 82, 66), 0.2, fur=1.2, stripes=(46, 38, 32), stripe_n=8)
    hide('cat_belly', (150, 138, 116), 0.16, fur=0.6)
    a = TEX['cat_hide'].copy(); rect(a, 0, 20, 32, 32, (50, 28, 28))
    for x in range(1, 31, 4): rect(a, x, 20, x + 2, 27, (218, 206, 172))
    disc(a, 16, 16, 3, (40, 26, 24)); TEX['cat_face'] = a
    hide('cat_leg', (108, 94, 78), 0.2, fur=1.0, dark_bottom=0.55)

    # ---- crow (fauna)
    hide('crow', (30, 30, 34), 0.25, fur=1.2)
    TEX['crow'][..., 2] *= 1.15
    hide('crow_beak', (40, 38, 36), 0.1)

# ================================================================================================ the bodies
def build():
    # ---------------------------------------------------------------- TUSKER — the mutant boar: a hunched slab of
    # shoulders, a bristled ridge, a long scarred snout and yellow tusks, small hind quarters, red pig eyes
    m = M('z_boar', 'quad', scale=1.15, gallop=True, bodyY=12, bodyLen=17, legH=8, height=24, hipY=12, post='zboar')
    m.p('body', None, [0, 12, 0], [-5, -4.5, -8, 5, 4.5, 8], T('boar_hide'), role='qbody', shape='ell', jit=0.1, grad=[1.0, 0.8])
    m.p('hump', 'body', [0, 2.5, -4], [-6, -5, -5.5, 6, 4.5, 5.5], {'all': T('boar_hide'), 'bottom': T('boar_belly')}, shape='ell', jit=0.1)
    m.p('haunch', 'body', [0, 0, 5], [-4.5, -4, -4, 4.5, 3.5, 4], T('boar_hide'), shape='ell', jit=0.1)
    m.p('belly', 'body', [0, -3.5, -1], [-4, -2, -6, 4, 2, 6], T('boar_belly'), shape='ell', jit=0.08)
    m.p('mane', 'body', [0, 5.5, -2], [-1.6, -1.5, -9, 1.6, 4, 9], {'side': T('boar_bristle_side'), 'top': T('boar_bristle'), 'front': T('boar_bristle'), 'back': T('boar_bristle')}, jit=0.12)
    m.p('head', 'body', [0, 0.5, -9.5], [-3.6, -3.6, -6, 3.6, 3.4, 0], {'all': T('boar_face'), 'top': T('boar_hide')}, role='qhead', limb='head', rest=[0.25, 0, 0], shape='ell', jit=0.1)
    m.p('snout', 'head', [0, -1.3, -5], [-2.4, -2.1, -6, 2.4, 1.9, 0], {'all': T('boar_snout'), 'front': T('boar_nose')}, shape='cylz', taper=[2, 0.9, 1.15], jit=0.08)
    m.p('jaw', 'head', [0, -3.2, -3.5], [-2.2, -1, -6.5, 2.2, 0.6, 0], T('boar_belly'), shape='cylz', taper=[2, 0.8, 1.0])
    m.pair('tusk', 'snout', [2.3, -1.2, -3.5], [-0.65, -0.4, -0.65, 0.65, 4.2, 0.65], T('tusk'), shape='cyl', taper=[1, 1.1, 0.35], rest=[-0.55, 0, -0.3])
    m.pair('eye', 'head', [2.9, 1.0, -4.6], [-0.5, -0.5, -0.5, 0.5, 0.5, 0.5], T('eye_red'), glow=True)
    m.pair('ear', 'head', [2.8, 2.6, -1.5], [-0.5, 0, -0.6, 1.8, 2.6, 0.6], T('boar_hide'), role='ear', rest=[0.3, 0, -0.5], taper=[1, 1.0, 0.4])
    quad_legs(m, 3.4, 3.0, -5.5, 5.5, 8, (1.7, 4), (1.35, 4), T('boar_hide'), front_k=1.15, shin_tex=T('boar_hide'))
    m.p('tail', 'body', [0, 2.5, 8.5], [-0.45, -5, -0.45, 0.45, 0, 0.45], T('boar_bristle'), role='tail', rest=[0.35, 0, 0], shape='cyl', taper=[1, 1.8, 0.8])
    m.done()

    # ---------------------------------------------------------------- HOGSPAWN — the mutant pig: a bloated, lopsided sack of
    # diseased flesh on stubby legs, a tumour on its flank, four mismatched wet eyes, an underbite of crooked teeth
    m = M('z_hog', 'quad', scale=1.2, gallop=False, bodyY=11, bodyLen=18, legH=5, height=20, hipY=10, post='zhog')
    m.p('body', None, [0, 10.5, 0], [-7, -6.5, -8.5, 7.8, 6.5, 8.5], {'all': T('hog_skin'), 'bottom': T('hog_belly')}, role='qbody', shape='ell', jit=0.12, grad=[1.05, 0.8])
    m.p('lump', 'body', [5, 3.5, -1], [-3.5, -3.5, -3.8, 3.8, 3.8, 3.8], T('hog_lump'), shape='ell', jit=0.15)
    m.p('lump2', 'body', [-3.5, 5, 4], [-2.5, -2, -2.5, 2.5, 2.5, 2.5], T('hog_lump'), shape='ell', jit=0.15)
    m.p('belly', 'body', [0.5, -5, 1], [-5.5, -2.5, -6.5, 5.5, 2.5, 6.5], T('hog_belly'), shape='ell', jit=0.1)
    m.p('head', 'body', [0, -0.5, -8], [-4.2, -4, -5, 4.2, 3.6, 0], {'all': T('hog_skin')}, role='qhead', limb='head', shape='ell', jit=0.12)
    m.p('snout', 'head', [0.3, -1.4, -4.3], [-2.7, -2.2, -3, 2.7, 2.2, 0], {'all': T('hog_skin'), 'front': T('hog_nose')}, shape='cylz', jit=0.08)
    m.p('jaw', 'head', [0, -3.6, -3], [-2.6, -1.1, -2.8, 2.6, 0.9, 0], {'all': T('hog_skin'), 'front': T('hog_jaw')}, rest=[0.15, 0, 0])
    for i, (x, y, z, r) in enumerate([(-2.6, 1.4, -4.3, 0.85), (2.3, 2.2, -4.2, 1.05), (0.4, 3.4, -3.4, 0.7), (3.4, -0.2, -3.6, 0.55)]):
        m.p('eye%d' % i, 'head', [x, y, z], [-r, -r, -r, r, r, r], T('eye_wet'), shape='ell')
    m.pair('ear', 'head', [3.2, 2.6, -1], [-0.4, -2.6, -1, 2.4, 0.4, 1], T('hog_skin'), role='ear', rest=[0.2, 0, 0.5])
    quad_legs(m, 4.4, 4.6, -4.5, 5, 5.5, (2.1, 2.7), (1.8, 2.8), T('hog_skin'), shin_tex=T('hog_skin'), taper=0.9)
    m.p('tail', 'body', [0, 2, 8.5], [-0.5, -2.5, -0.5, 0.5, 0, 0.5], T('hog_skin'), role='tail', rest=[-0.6, 0, 0.6], shape='cyl')
    m.done()

    # ---------------------------------------------------------------- SCAB DOG — the blind dog: mangy, half bald, ribs showing,
    # skin grown over where the eyes were, a narrow skull, a mouth full of teeth, a rat's naked tail
    def dog(key, hide_t, back_t, leg_t, scale, eyes=None, mane=None, face=None, post='zdog', brow=False):
        m = M(key, 'quad', scale=scale, gallop=True, bodyY=11, bodyLen=13, legH=8, height=19, hipY=11, post=post)
        m.p('body', None, [0, 11, 0], [-2.9, -2.8, -6, 2.9, 3.2, 6.5], {'all': hide_t, 'top': back_t}, role='qbody', shape='ell', jit=0.1)
        m.p('chest', 'body', [0, -0.6, -3.2], [-3.3, -3.6, -3.5, 3.3, 3.4, 3.5], {'all': hide_t, 'top': back_t}, shape='ell', jit=0.1)
        m.p('loin', 'body', [0, 0.6, 3.5], [-2.4, -2.2, -3, 2.4, 2.4, 3.5], {'all': hide_t, 'top': back_t}, shape='ell', jit=0.1)
        if mane:
            m.p('ruff', 'body', [0, 1.5, -3.5], [-3.6, -2.5, -3.5, 3.6, 3.6, 3.5], mane, shape='ell', jit=0.14)
            m.p('crest', 'body', [0, 3.8, -2.5], [-1.4, -0.5, -5, 1.4, 3, 5], {'side': T('lurch_mane_side'), 'top': mane, 'front': mane, 'back': mane})
        else:
            m.p('spine', 'body', [0, 3.0, 0.5], [-0.6, -0.5, -5.5, 0.6, 1.3, 5.5], {'side': T('dog_spine'), 'top': T('dog_skin'), 'front': T('dog_skin'), 'back': T('dog_skin')})
        m.p('neck', 'body', [0, 1.6, -6], [-1.7, -1.7, -3.2, 1.7, 1.7, 0.5], hide_t, rest=[-0.35, 0, 0], shape='cylz', jit=0.1)
        m.p('head', 'neck', [0, 0.4, -2.8], [-2.3, -2.1, -3.4, 2.3, 2.3, 0.2], {'all': hide_t, 'front': face or T('dog_face'), 'top': back_t}, role='qhead', limb='head', rest=[0.3, 0, 0], shape='ell', jit=0.1)
        if brow: m.p('brow', 'head', [0, 1.4, -2.6], [-2.6, -0.6, -1, 2.6, 0.8, 1], back_t, jit=0.1)
        m.p('snout', 'head', [0, -0.6, -3], [-1.35, -1.1, -4.3, 1.35, 1.0, 0], {'all': T('dog_muzzle'), 'front': T('dog_nose'), 'top': hide_t}, shape='cylz', taper=[2, 0.8, 1.0])
        m.p('jaw', 'head', [0, -1.9, -2.6], [-1.15, -0.6, -4.3, 1.15, 0.5, 0], {'all': T('dog_muzzle'), 'bottom': T('dog_skin')}, rest=[0.12, 0, 0], taper=[2, 0.75, 1.0])
        if eyes: m.pair('eye', 'head', [1.45, 0.7, -3.0], [-0.42, -0.36, -0.4, 0.42, 0.36, 0.4], eyes, glow=True)
        m.pair('ear', 'head', [1.6, 1.8, -0.8], [-0.7, 0, -0.25, 0.7, 2.4, 0.25], T('dog_skin'), role='ear', rest=[-0.25, 0, 0.35], taper=[1, 1.0, 0.35])
        quad_legs(m, 1.8, 1.7, -4.2, 4.6, 8, (0.95, 4.2), (0.75, 4.2), leg_t, front_k=1.05, taper=0.75)
        m.p('tail', 'body', [0, 1.8, 6.4], [-0.38, -7, -0.38, 0.38, 0, 0.38], T('dog_skin'), role='tail', rest=[-0.85, 0, 0], shape='cyl', taper=[1, 0.35, 1.0])
        m.done()
    dog('z_dog', T('dog_hide'), T('dog_back'), T('dog_leg'), 1.0)
    dog('z_dog_black', T('dog_black'), T('dog_black'), T('dog_black'), 1.0)
    dog('z_dog_ginger', T('dog_ginger'), T('dog_ginger'), T('dog_ginger'), 0.95)
    # LURCHER — the big hound: heavier, shaggy-maned, a brutal brow, eyes that burn yellow in the dark
    dog('z_lurcher', T('lurch_hide'), T('lurch_mane'), T('lurch_leg'), 1.4, eyes=T('eye_yellow'), mane=T('lurch_mane'), face=T('lurch_face'), brow=True)

    # ---------------------------------------------------------------- RAD RAT — a fat, mangy rat with a naked tail and red eyes
    m = M('z_rat', 'quad', scale=1.0, gallop=False, bodyY=2.6, bodyLen=6, legH=1.2, height=5, hipY=2.6, post='zrat')
    m.p('body', None, [0, 2.6, 0], [-1.7, -1.5, -3.2, 1.7, 1.7, 3.2], T('rat_fur'), role='qbody', shape='ell', jit=0.12)
    m.p('head', 'body', [0, 0.3, -2.8], [-1.2, -1.1, -2.4, 1.2, 1.2, 0.3], T('rat_fur'), role='qhead', limb='head', shape='ell', taper=[2, 0.6, 1.0])
    m.p('nose', 'head', [0, -0.3, -2.3], [-0.45, -0.4, -0.6, 0.45, 0.4, 0], T('rat_nose'), shape='ell')
    m.pair('eye', 'head', [0.75, 0.35, -1.4], [-0.25, -0.25, -0.25, 0.25, 0.25, 0.25], T('eye_red'), glow=True)
    m.pair('ear', 'head', [0.9, 1.0, -0.3], [-0.5, 0, -0.15, 0.5, 1.0, 0.15], T('rat_skin'), role='ear', shape='cylz')
    quad_legs(m, 1.1, 1.1, -2.0, 2.0, 1.4, (0.45, 0.7), (0.4, 0.7), T('rat_skin'))
    m.p('tail', 'body', [0, 0, 3.1], [-0.25, -0.25, 0, 0.25, 0.25, 7], T('rat_skin'), role='tail', rest=[0.25, 0, 0], shape='cylz', taper=[2, 1.0, 0.4])
    m.done()

    # ---------------------------------------------------------------- SHROUD — the camp stalker: a tall, gaunt, hunched hunter
    # with long clawed arms, an egg of a bald skull and a beard of feeding tentacles where the jaw should be; it walks unseen
    m = M('z_shroud', 'biped', scale=1.12, hipY=14, bodyDepth=5, height=34, armsReach=False, heavy=False, post='zshroud')
    m.p('body', None, [0, 14, 0], [-3.8, 0, -2.4, 3.8, 12, 2.4], {'all': T('shroud_torso'), 'back': T('shroud_skin')}, role='body', rest=[-0.34, 0, 0], shape='cyl', taper=[1, 0.72, 1.0], jit=0.1)
    m.p('rags', 'body', [0, 0.5, 0], [-4.0, -4, -2.7, 4.0, 1.5, 2.7], T('shroud_rag_cut'), shape='cyl')
    m.p('head', 'body', [0, 12, -0.6], [-2.7, -0.5, -3.4, 2.7, 6.8, 3.0], {'all': T('shroud_skin'), 'front': T('shroud_face')}, role='head', limb='head', rest=[0.22, 0, 0], shape='ell', taper=[1, 0.85, 1.0], jit=0.08)
    for i, (x, rz) in enumerate([(-1.6, 0.18), (-0.55, 0.05), (0.55, -0.05), (1.6, -0.18)]):
        m.p('tent%d' % i, 'head', [x, 1.4, -2.7], [-0.55, -6.5 + abs(x) * 0.8, -0.55, 0.55, 0, 0.55], T('shroud_tent'), shape='cyl', taper=[1, 0.35, 1.1], rest=[0.25, 0, rz])
    m.pair('eye', 'head', [1.15, 3.7, -2.8], [-0.35, -0.3, -0.3, 0.35, 0.3, 0.3], T('eye_red'), glow=True)
    m.p('armR', 'body', [5, 11, 0], [-1.3, -7.5, -1.3, 1.3, 1, 1.3], T('shroud_skin'), role='armR', limb='armR', shape='cyl', taper=[1, 0.8, 1.0], jit=0.08, rest=[0.3, 0, 0.08])
    m.p('foreR', 'armR', [0, -7.5, 0], [-1.1, -8, -1.1, 1.1, 0, 1.1], T('shroud_skin'), role='foreR', limb='armR', hand=[0, -8, 0], shape='cyl', taper=[1, 0.85, 1.0])
    m.p('clawR', 'foreR', [0, -8, -0.2], [-1.3, -3.6, -1.1, 1.3, 0, 0.7], T('shroud_claw'), shape='cyl', taper=[1, 0.3, 1.0])
    m.p('armL', 'body', [-5, 11, 0], [-1.3, -7.5, -1.3, 1.3, 1, 1.3], T('shroud_skin'), role='armL', limb='armL', shape='cyl', taper=[1, 0.8, 1.0], jit=0.08, rest=[0.3, 0, -0.08])
    m.p('foreL', 'armL', [0, -7.5, 0], [-1.1, -8, -1.1, 1.1, 0, 1.1], T('shroud_skin'), role='foreL', limb='armL', hand=[0, -8, 0], shape='cyl', taper=[1, 0.85, 1.0])
    m.p('clawL', 'foreL', [0, -8, -0.2], [-1.3, -3.6, -1.1, 1.3, 0, 0.7], T('shroud_claw'), shape='cyl', taper=[1, 0.3, 1.0])
    for S, sx in (('R', 1), ('L', -1)):
        m.p('leg' + S, None, [2 * sx, 14, 0], [-1.6, -7, -1.6, 1.6, 0, 1.6], T('shroud_skin'), role='leg' + S, limb='leg' + S, shape='cyl', taper=[1, 0.8, 1.0], rest=[0.25, 0, 0])
        m.p('shin' + S, 'leg' + S, [0, -7, 0], [-1.3, -7, -1.3, 1.3, 0, 1.3], T('shroud_skin'), role='shin' + S, limb='leg' + S, shape='cyl', taper=[1, 0.75, 1.0], rest=[-0.4, 0, 0])
        m.p('foot' + S, 'shin' + S, [0, -6.6, -0.4], [-1.3, -0.5, -3.2, 1.3, 0.7, 1.0], T('shroud_claw'), taper=[2, 0.5, 1.0], rest=[0.15, 0, 0])
    m.done()

    # ---------------------------------------------------------------- CRAWLER — the gas-masked leaper: a man that runs on all
    # fours in what is left of army fatigues, the mask grown into his face, the hose swinging; he springs from ten metres
    m = M('z_crawler', 'biped', scale=1.0, hipY=11, bodyDepth=4, height=24, armsReach=False, heavy=False, post='zcrawler')
    m.p('body', None, [0, 11, 0], [-3.5, 0, -2.1, 3.5, 10.5, 2.1], {'all': T('crawl_cloth')}, role='body', rest=[-1.05, 0, 0], shape='cyl', taper=[1, 0.85, 1.0], jit=0.08)
    m.p('vest', 'body', [0, 5.5, 0], [-3.8, -3, -2.5, 3.8, 3.5, 2.5], T('mask_rubber'), shape='cyl', jit=0.1)
    m.p('head', 'body', [0, 10.5, -0.4], [-2.8, -0.3, -3, 2.8, 5.6, 2.8], {'all': T('mask_rubber'), 'front': T('mask_face')}, role='head', limb='head', rest=[0.95, 0, 0], shape='ell', jit=0.06)
    m.p('filter', 'head', [0, 1.4, -2.8], [-1.25, -1.25, -2.4, 1.25, 1.25, 0], {'all': T('mask_filter'), 'front': T('mask_filter_f')}, shape='cylz', rest=[0.2, 0, 0])
    m.p('hose', 'filter', [0.4, -0.9, -1.0], [-0.45, -6, -0.45, 0.45, 0, 0.45], T('hose'), shape='cyl', rest=[0.4, 0, 0.3])
    m.p('strap', 'head', [0, 3.0, 0], [-3.0, -0.6, -2.0, 3.0, 0.6, 3.0], T('mask_rubber'), shape='cyl')
    for S, sx in (('R', 1), ('L', -1)):
        m.p('arm' + S, 'body', [4.3 * sx, 9.5, 0], [-1.3, -6, -1.3, 1.3, 1, 1.3], T('crawl_cloth'), role='arm' + S, limb='arm' + S, shape='cyl', rest=[1.0, 0, 0])
        m.p('fore' + S, 'arm' + S, [0, -6, 0], [-1.1, -6.5, -1.1, 1.1, 0, 1.1], T('crawl_skin'), role='fore' + S, limb='arm' + S, hand=[0, -6.5, 0], shape='cyl', taper=[1, 0.8, 1.0])
        m.p('hand' + S, 'fore' + S, [0, -6.5, 0], [-1.3, -2.4, -1.5, 1.3, 0, 0.9], T('crawl_skin'), taper=[1, 0.5, 1.0])
        m.p('leg' + S, None, [1.9 * sx, 11, 0], [-1.5, -5.5, -1.5, 1.5, 0, 1.5], T('crawl_pants'), role='leg' + S, limb='leg' + S, shape='cyl', rest=[0.9, 0, 0])
        m.p('shin' + S, 'leg' + S, [0, -5.5, 0], [-1.3, -5.5, -1.3, 1.3, 0, 1.3], T('crawl_pants'), role='shin' + S, limb='leg' + S, shape='cyl', rest=[-1.5, 0, 0])
        m.p('boot' + S, 'shin' + S, [0, -5.0, -0.4], [-1.4, -1, -2.6, 1.4, 1.2, 1.2], T('crawl_boot'), rest=[0.6, 0, 0])
    m.done()

    # ---------------------------------------------------------------- HUSK — the zombified stalker: what the Zone left of a man,
    # still in his suit (jacket, hood or helmet, a pack), grey dead skin, slack mouth, a rifle he still points if he has one
    def husk(key, jacket, head_tex, hat):
        m = M(key, 'biped', scale=1.0, hipY=12, bodyDepth=4.5, height=31, armsReach=False, heavy=False, post='zhusk')
        m.p('body', None, [0, 12, 0], [-4, 0, -2.2, 4, 12, 2.2], jacket, role='body', rest=[-0.12, 0, 0.04], jit=0.08)
        m.p('pack', 'body', [0, 6.5, 2.2], [-3.2, -4.5, 0, 3.2, 4.5, 3], T('husk_pack'), jit=0.1)
        m.p('belt', 'body', [0, 1.0, 0], [-4.3, -1, -2.5, 4.3, 1, 2.5], T('hurl_belt'))
        m.p('head', 'body', [0, 12, 0], [-3.6, 0, -3.6, 3.6, 7.6, 3.6], {'all': T('husk_skin'), 'front': T('husk_face')}, role='head', limb='head', rest=[-0.3, 0.15, 0.22], jit=0.08)
        if hat == 'hood':
            m.p('hood', 'head', [0, 3.8, 0.3], [-4.2, -4.0, -4.0, 4.2, 4.5, 4.2], {'all': T('husk_hood'), 'front': False}, jit=0.08)
        elif hat == 'helmet':
            m.p('helmet', 'head', [0, 6.2, 0], [-4.1, -1.5, -4.1, 4.1, 2.6, 4.1], T('husk_helmet'), shape='cyl', jit=0.08)
            m.p('brim', 'helmet', [0, -1.2, 0], [-4.5, -0.5, -4.5, 4.5, 0.4, 4.5], T('husk_helmet'), shape='cyl')
        elif hat == 'mask':
            m.p('mask', 'head', [0, 2.6, -3.6], [-2.6, -2.2, -1.4, 2.6, 2.4, 0.4], {'all': T('mask_rubber'), 'front': T('mask_face')}, shape='cylz')
            m.p('filter', 'mask', [0, -1.2, -1.2], [-1.0, -1.0, -1.8, 1.0, 1.0, 0], {'all': T('mask_filter'), 'front': T('mask_filter_f')}, shape='cylz')
        for S, sx in (('R', 1), ('L', -1)):
            m.p('arm' + S, 'body', [6 * sx, 10, 0], [-2, -4, -2, 2, 2, 2], jacket, role='arm' + S, limb='arm' + S, jit=0.08)
            m.p('fore' + S, 'arm' + S, [0, -4, 0], [-1.8, -6, -1.8, 1.8, 0, 1.8], {'all': jacket, 'bottom': T('husk_skin')}, role='fore' + S, limb='arm' + S, hand=[0, -5, 0], jit=0.08)
            m.p('hand' + S, 'fore' + S, [0, -6, 0], [-1.4, -2, -1.4, 1.4, 0, 1.4], T('husk_skin'))
            m.p('leg' + S, None, [2 * sx, 12, 0], [-2, -6, -2, 2, 0, 2], T('husk_pants'), role='leg' + S, limb='leg' + S, jit=0.08)
            m.p('shin' + S, 'leg' + S, [0, -6, 0], [-2, -6, -2, 2, 0, 2], {'all': T('husk_pants'), 'bottom': T('boot')}, role='shin' + S, limb='leg' + S, jit=0.08, grad=[1.0, 0.6])
        m.done()
    husk('z_husk', T('husk_jacket'), None, 'hood')
    husk('z_husk_army', T('husk_army'), None, 'helmet')
    husk('z_husk_mask', T('husk_jacket2'), None, 'mask')

    # ---------------------------------------------------------------- PUPPETEER — the psy master: a stunted man in rags under a
    # monstrous swollen cranium, lumpy and veined, the face a small crumpled thing low on the front of it
    m = M('z_puppeteer', 'biped', scale=1.0, hipY=9, bodyDepth=4, height=30, armsReach=False, heavy=False, post='zpuppet')
    m.p('body', None, [0, 9, 0], [-3.6, 0, -2.2, 3.6, 9, 2.2], T('pup_rag'), role='body', rest=[-0.18, 0, 0], shape='cyl', taper=[1, 1.0, 0.85], jit=0.1)
    m.p('rags', 'body', [0, 0.4, 0], [-3.9, -3.5, -2.5, 3.9, 1, 2.5], T('pup_rag_cut'), shape='cyl')
    m.p('neck', 'body', [0, 9, 0], [-1.4, -0.5, -1.4, 1.4, 1.5, 1.4], T('pup_skin'), shape='cyl')
    m.p('head', 'body', [0, 10, 0.4], [-5.6, 0, -5.4, 5.6, 11.5, 5.8], {'all': T('pup_skull')}, role='head', limb='head', rest=[0.12, 0, 0], shape='ell', jit=0.12)
    m.p('face', 'head', [0, 2.6, -4.3], [-2.6, -2.4, -1.4, 2.6, 2.6, 0.6], {'all': T('pup_skin'), 'front': T('pup_face')}, shape='ell')
    m.p('lumpA', 'head', [3.2, 8.2, -1.5], [-2.2, -2, -2.2, 2.2, 2.2, 2.2], T('pup_skull'), shape='ell', jit=0.14)
    m.p('lumpB', 'head', [-2.4, 9.4, 2.2], [-2.6, -2, -2.4, 2.6, 2.4, 2.4], T('pup_skull'), shape='ell', jit=0.14)
    m.p('lumpC', 'head', [-4.6, 5.0, -1], [-1.5, -1.8, -1.8, 1.5, 1.8, 1.8], T('pup_skull'), shape='ell', jit=0.14)
    for S, sx in (('R', 1), ('L', -1)):
        m.p('arm' + S, 'body', [4.7 * sx, 8, 0], [-1.1, -6, -1.1, 1.1, 1, 1.1], T('pup_rag'), role='arm' + S, limb='arm' + S, shape='cyl')
        m.p('fore' + S, 'arm' + S, [0, -6, 0], [-0.95, -6.5, -0.95, 0.95, 0, 0.95], T('pup_skin'), role='fore' + S, limb='arm' + S, hand=[0, -6.5, 0], shape='cyl', taper=[1, 0.8, 1.0])
        m.p('hand' + S, 'fore' + S, [0, -6.5, 0], [-1.3, -2.6, -1.0, 1.3, 0, 1.0], T('pup_skin'), taper=[1, 0.4, 1.0])
        m.p('leg' + S, None, [1.8 * sx, 9, 0], [-1.4, -4.5, -1.4, 1.4, 0, 1.4], T('pup_rag'), role='leg' + S, limb='leg' + S, shape='cyl')
        m.p('shin' + S, 'leg' + S, [0, -4.5, 0], [-1.2, -4.5, -1.2, 1.2, 0, 1.2], T('pup_skin'), role='shin' + S, limb='leg' + S, shape='cyl', grad=[1.0, 0.7])
    m.done()

    # ---------------------------------------------------------------- STONEBACK — the giant brute of the labs and the Red Forest:
    # a hunched hill of muscle under stone-hard callus plates, a small head sunk between its shoulders, huge knuckle arms
    m = M('z_stoneback', 'biped', scale=1.55, hipY=10, bodyDepth=9, height=30, armsReach=False, heavy=True, post='zbrute')
    m.p('body', None, [0, 10, 0], [-6.5, -1, -4.5, 6.5, 13, 5], {'all': T('brute_skin'), 'front': T('brute_belly')}, role='body', rest=[-0.45, 0, 0], shape='ell', jit=0.1)
    m.p('gut', 'body', [0, 2.5, -2.5], [-5, -3.5, -3, 5, 3.5, 2.5], T('brute_belly'), shape='ell', jit=0.08)
    for i, (y, z, w, h) in enumerate([(11.5, 2.5, 6.5, 3.2), (8, 4.2, 6.2, 3.2), (4.5, 4.8, 5.5, 3), (1.5, 4.3, 4.5, 2.6)]):
        m.p('plate%d' % i, 'body', [0, y, z], [-w, -h * 0.5, -1.6, w, h * 0.5, 1.8], T('brute_plate'), shape='cylx', jit=0.18, rest=[-0.25, 0, 0])
    m.p('head', 'body', [0, 11.5, -4.0], [-2.8, -1.5, -3.6, 2.8, 3.8, 1.5], {'all': T('brute_skin'), 'front': T('brute_face')}, role='head', limb='head', rest=[0.5, 0, 0], shape='ell', jit=0.08)
    m.p('jaw', 'head', [0, -1.2, -1.5], [-2.6, -1.4, -2.4, 2.6, 0.6, 1.0], T('brute_skin'), shape='ell')
    for S, sx in (('R', 1), ('L', -1)):
        m.p('arm' + S, 'body', [7.5 * sx, 11, -0.5], [-2.6, -7, -2.6, 2.6, 2, 2.6], T('brute_skin'), role='arm' + S, limb='arm' + S, shape='ell', jit=0.1, rest=[0.55, 0, 0.12 * sx])
        m.p('fore' + S, 'arm' + S, [0, -6.5, 0], [-2.8, -8, -2.8, 2.8, 0, 2.8], T('brute_skin'), role='fore' + S, limb='arm' + S, hand=[0, -8, 0], shape='cyl', taper=[1, 1.25, 0.9], jit=0.1)
        m.p('fist' + S, 'fore' + S, [0, -8, 0], [-3.0, -3.4, -2.8, 3.0, 0.5, 2.6], T('brute_plate'), shape='ell', jit=0.14)
        m.p('leg' + S, None, [3.5 * sx, 10, 0.5], [-2.6, -5, -2.6, 2.6, 0.5, 2.6], T('brute_skin'), role='leg' + S, limb='leg' + S, shape='cyl', jit=0.1)
        m.p('shin' + S, 'leg' + S, [0, -5, 0], [-2.4, -5, -2.4, 2.4, 0, 2.4], T('brute_skin'), role='shin' + S, limb='leg' + S, shape='cyl', taper=[1, 1.15, 1.0], grad=[1.0, 0.65])
        m.p('foot' + S, 'shin' + S, [0, -4.4, -0.8], [-2.8, -0.6, -3.6, 2.8, 1.0, 2.4], T('brute_plate'), shape='ell')
    m.done()

    # ---------------------------------------------------------------- HURLER — the dwarfish telekinetic of tunnels and labs: a fat
    # little man under a hooded cloak of sacking, big bare hands, eyes burning in the hood; a shimmer folds round him at need
    m = M('z_hurler', 'biped', scale=1.0, hipY=7, bodyDepth=6, height=21, armsReach=False, heavy=False, post='zhurler')
    m.p('body', None, [0, 7, 0], [-4.4, 0, -3.2, 4.4, 8.5, 3.2], T('hurl_cloak'), role='body', rest=[-0.1, 0, 0], shape='ell', taper=[1, 1.15, 0.8], jit=0.1)
    m.p('hem', 'body', [0, 1, 0], [-4.8, -8, -3.6, 4.8, 1, 3.6], {'all': T('hurl_cloak_cut'), 'top': T('hurl_cloak'), 'bottom': False}, shape='cyl', taper=[1, 1.2, 0.95])
    m.p('belt', 'body', [0, 2.2, 0], [-4.6, -0.6, -3.4, 4.6, 0.6, 3.4], T('hurl_belt'), shape='cyl')
    m.p('head', 'body', [0, 8.2, -0.5], [-3.0, -0.5, -3.2, 3.0, 5.4, 3.0], T('hurl_skin'), role='head', limb='head', rest=[-0.1, 0, 0], shape='ell')
    m.p('hood', 'head', [0, 2.6, 0.4], [-3.8, -3.6, -4.0, 3.8, 3.8, 3.6], {'all': T('hurl_cloak'), 'front': T('hurl_hood_f')}, shape='ell', taper=[2, 1.0, 0.8], jit=0.1)
    m.p('hoodtip', 'hood', [0, 2.6, 2.4], [-1.6, -1.6, 0, 1.6, 1.6, 3.6], T('hurl_cloak'), shape='cylz', taper=[2, 1.0, 0.25], rest=[0.6, 0, 0])
    m.pair('eye', 'head', [1.15, 2.1, -3.0], [-0.42, -0.32, -0.3, 0.42, 0.32, 0.3], T('eye_ember'), glow=True)
    for S, sx in (('R', 1), ('L', -1)):
        m.p('arm' + S, 'body', [5.2 * sx, 7.5, 0], [-1.5, -4.5, -1.5, 1.5, 1, 1.5], T('hurl_cloak'), role='arm' + S, limb='arm' + S, shape='cyl', taper=[1, 1.3, 1.0])
        m.p('fore' + S, 'arm' + S, [0, -4.5, 0], [-1.3, -4.5, -1.3, 1.3, 0, 1.3], {'all': T('hurl_cloak'), 'bottom': T('hurl_skin')}, role='fore' + S, limb='arm' + S, hand=[0, -4.5, 0], shape='cyl', taper=[1, 1.25, 1.0])
        m.p('hand' + S, 'fore' + S, [0, -4.5, 0], [-1.7, -2.8, -1.5, 1.7, 0, 1.5], T('hurl_skin'), shape='ell')
        m.p('leg' + S, None, [1.8 * sx, 7, 0], [-1.4, -3.5, -1.4, 1.4, 0, 1.4], T('hurl_cloak'), role='leg' + S, limb='leg' + S, shape='cyl')
        m.p('shin' + S, 'leg' + S, [0, -3.5, 0], [-1.3, -3.5, -1.3, 1.3, 0, 1.3], {'all': T('hurl_skin'), 'bottom': T('boot')}, role='shin' + S, limb='leg' + S, shape='cyl', grad=[1.0, 0.5])
    m.p('shield', None, [0, 9, 0], [-12, -11, -12, 12, 11, 12], T('shield'), shape='ell', alpha=0.28, glow=True, only='d:shield')
    m.done()

    # ---------------------------------------------------------------- RIDGECAT — the two-headed cat of the far north: a long
    # muscled body in a striped hide, scarred bare shoulders, two heads on two necks, a long tail, claws like hooks
    m = M('z_ridgecat', 'quad', scale=1.4, gallop=True, bodyY=12, bodyLen=20, legH=9, height=22, hipY=12, post='zcat')
    m.p('body', None, [0, 12, 0], [-4, -3.6, -9, 4, 3.8, 9], {'all': T('cat_hide'), 'top': T('cat_back'), 'bottom': T('cat_belly')}, role='qbody', shape='ell', jit=0.1)
    m.p('shoulders', 'body', [0, 0.8, -5], [-4.8, -4.2, -4.5, 4.8, 4.3, 4.5], {'all': T('cat_hide'), 'top': T('cat_back'), 'bottom': T('cat_belly')}, shape='ell', jit=0.1)
    m.p('haunch', 'body', [0, 0.4, 5.5], [-4.4, -3.8, -4, 4.4, 4, 4], {'all': T('cat_hide'), 'top': T('cat_back')}, shape='ell', jit=0.1)
    for nm, x, ry in (('', 2.4, -0.28), ('2', -2.4, 0.28)):
        m.p('neck' + nm, 'shoulders', [x, 2.0, -3.6], [-1.9, -1.9, -4, 1.9, 1.9, 0.5], T('cat_hide'), rest=[-0.45, ry, 0], shape='cylz', jit=0.1)
        m.p('head' + nm, 'neck' + nm, [0, 0.6, -3.6], [-2.6, -2.2, -3.6, 2.6, 2.4, 0.4], {'all': T('cat_hide'), 'front': T('cat_face'), 'top': T('cat_back')}, role='qhead' if not nm else '', limb='head' + nm, rest=[0.35, -ry * 0.6, 0], shape='ell', jit=0.08)
        m.p('muzzle' + nm, 'head' + nm, [0, -1.0, -3.2], [-1.6, -1.2, -2.2, 1.6, 1.2, 0.4], {'all': T('cat_belly'), 'front': T('cat_face')}, shape='ell')
        m.p('jaw' + nm, 'head' + nm, [0, -2.0, -2.4], [-1.4, -0.7, -2.8, 1.4, 0.5, 0], T('cat_face'), rest=[0.35, 0, 0])
        for ex in (1.2, -1.2):
            m.p('eye%s%s' % (nm, 'R' if ex > 0 else 'L'), 'head' + nm, [ex, 0.8, -3.3], [-0.42, -0.3, -0.3, 0.42, 0.3, 0.3], T('eye_yellow'), glow=True)
            m.p('ear%s%s' % (nm, 'R' if ex > 0 else 'L'), 'head' + nm, [ex * 1.4, 2.2, -0.6], [-0.7, 0, -0.3, 0.7, 2.0, 0.3], T('cat_back'), role='ear', taper=[1, 1.0, 0.2])
    quad_legs(m, 3.0, 3.0, -6.0, 6.5, 9, (1.6, 4.5), (1.3, 4.5), T('cat_leg'), front_k=1.1, taper=0.8)
    for nm in ('FR', 'FL', 'BR', 'BL'):
        m.p('paw' + nm, 'shin' + nm, [0, -4.2, -0.5], [-1.6, -0.6, -2.4, 1.6, 0.9, 1.2], T('cat_leg'), shape='ell')
        m.p('claws' + nm, 'paw' + nm, [0, -0.3, -2.2], [-1.3, -0.4, -1.2, 1.3, 0.4, 0], T('claw'))
    m.p('tail', 'body', [0, 2.2, 8.6], [-0.7, -0.7, 0, 0.7, 0.7, 14], T('cat_back'), role='tail', rest=[0.55, 0, 0], shape='cylz', taper=[2, 1.0, 0.55], jit=0.1)
    m.done()

def main():
    paint_all(); build(); save_textures()
    p = os.path.join(ROOT, 'data', 'models.json'); allm = json.load(open(p))
    for k in [k for k in allm if k.startswith('z_')]: del allm[k]
    allm.update(MODELS)
    with open(p, 'w') as f: json.dump(allm, f, separators=(',', ':'), ensure_ascii=False)
    print('models.json: %d z_ models, %d hides' % (len(MODELS), len(TEX)))

if __name__ == '__main__': main()
