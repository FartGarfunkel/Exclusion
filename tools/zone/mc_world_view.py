#!/usr/bin/env python3
"""Draw data/zone_world tiles (mc_world.py) top-down. usage: mc_world_view.py <dir> <out.png> [step] [rx,rz ...] (all tiles if none)"""
import sys, os, json, zlib, struct, numpy as np
from PIL import Image
def load(path, runs=True):
    b = zlib.decompress(open(path, 'rb').read()); assert b[:4] == b'ZWT1'
    n = struct.unpack_from('<H', b, 4)[0]; p = 6; names = []
    for _ in range(n): l = b[p]; names.append(b[p + 1:p + 1 + l].decode()); p += 1 + l
    N = 512 * 512
    g = np.frombuffer(b, np.uint8, N, p).reshape(512, 512); p += N
    w = np.frombuffer(b, np.uint8, N, p).reshape(512, 512); p += N
    s = np.frombuffer(b, '<u2', N, p).reshape(512, 512); p += 2 * N
    c = np.frombuffer(b, np.uint8, N, p).reshape(512, 512); p += N
    nt = struct.unpack_from('<I', b, p)[0]; p += 4
    trees = np.frombuffer(b, np.dtype([('x', '<u2'), ('z', '<u2'), ('t', 'u1'), ('k', 'u1'), ('l', 'u1')]), nt, p); p += 7 * nt
    top = np.full((512, 512), -1, np.int32); topy = np.zeros((512, 512), np.int32)
    if runs:
        for i in range(N):
            k = 0; sh = 0
            while True:
                v = b[p]; p += 1; k |= (v & 127) << sh; sh += 7
                if not v & 128: break
            for _ in range(k):
                y, ln, pal = struct.unpack_from('<BBH', b, p); p += 4; top[i >> 9, i & 511] = pal; topy[i >> 9, i & 511] = y + ln - 1
    return names, g, w, s, c, trees, top, topy
def colour(name):
    n = name.split(':')[0]
    for k, v in (('glass', (150, 190, 210)), ('wool', (200, 200, 200)), ('carpet', (200, 200, 200)), ('quartz', (230, 228, 222)), ('hardened_clay', (170, 120, 95)),
                 ('plank', (150, 115, 75)), ('wooden', (150, 115, 75)), ('fence', (150, 115, 75)), ('iron', (190, 190, 195)), ('nether', (90, 50, 50))):
        if k in n: return v
    if 'brick' in n and 'stone' not in n: return (160, 80, 60)
    return (135, 135, 135)
SURF = {'sand': (205, 195, 150), 'gravel': (130, 125, 120), 'stone': (120, 120, 120), 'cobblestone': (110, 110, 110), 'dirt': (125, 95, 65),
        'grass_path': (150, 130, 80), 'clay': (150, 155, 165), 'stone_slab': (125, 125, 125), 'double_stone_slab': (125, 125, 125)}
def draw(path, st=1, runs=True):
    names, g, w, s, c, trees, top, topy = load(path, runs)
    gf = g.astype(np.float32); sh = np.clip(1 + (-np.gradient(gf, axis=1) - np.gradient(gf, axis=0)) * 0.15, 0.5, 1.5)
    img = np.zeros((512, 512, 3), np.float32); img[:] = (95, 130, 60); img[c == 3] = (150, 150, 80)
    for i, nm in enumerate(names):
        k = nm.split(':')[0]
        if k in SURF: img[s == i] = SURF[k]
    img *= sh[..., None]; img[w > 0] = (50, 70, 160); img[g == 0] = 0
    for t in trees: tz, tx = int(t['z']), int(t['x']); img[max(tz - 1, 0):tz + 2, max(tx - 1, 0):tx + 2] = (40, 80, 35)
    if runs:
        cols = np.array([colour(n) for n in names] + [(0, 0, 0)], np.float32); bt = top >= 0
        img[bt] = cols[top[bt]] * np.clip(0.7 + (topy[bt] - g[bt]) / 60.0, 0.7, 1.3)[:, None]
    return np.clip(img[::st, ::st], 0, 255).astype(np.uint8)
if __name__ == '__main__':
    d, out = sys.argv[1], sys.argv[2]; st = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    if len(sys.argv) > 4: tl = [tuple(map(int, a.split(','))) for a in sys.argv[4:]]
    else: tl = json.load(open(os.path.join(d, 'world.json')))['tiles']
    xs = [t[0] for t in tl]; zs = [t[1] for t in tl]; T = 512 // st
    im = np.zeros(((max(zs) - min(zs) + 1) * T, (max(xs) - min(xs) + 1) * T, 3), np.uint8)
    for rx, rz in tl:
        f = os.path.join(d, 't.%d.%d.bin' % (rx, rz))
        if os.path.exists(f): im[(rz - min(zs)) * T:(rz - min(zs) + 1) * T, (rx - min(xs)) * T:(rx - min(xs) + 1) * T] = draw(f, st, st <= 2)
    Image.fromarray(im).save(out); print(im.shape)
