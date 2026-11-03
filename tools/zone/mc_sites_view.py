#!/usr/bin/env python3
"""Draw a site template (data/zone_sites/<id>.json+.bin from mc_sites.py) top-down: ground hill shade, water, trees, built blocks
coloured by their top block. usage: mc_sites_view.py <id> [out.png] [step]"""
import sys, os, json, zlib, struct, numpy as np
from PIL import Image
D = os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'zone_sites')
def load(sid):
    m = json.load(open(os.path.join(D, sid + '.json'))); b = zlib.decompress(open(os.path.join(D, sid + '.bin'), 'rb').read())
    x0, z0, w, h = m['window']; n = w * h
    g = np.frombuffer(b, np.uint8, n, 0).reshape(h, w); wa = np.frombuffer(b, np.uint8, n, n).reshape(h, w)
    su = np.frombuffer(b, np.uint8, n, 2 * n).reshape(h, w); co = np.frombuffer(b, np.uint8, n, 3 * n).reshape(h, w)
    p = 4 * n; top = np.full((h, w), -1, np.int32); topy = np.zeros((h, w), np.int32)
    for i in range(n):
        k = 0; s = 0
        while True:
            c = b[p]; p += 1; k |= (c & 127) << s; s += 7
            if not c & 128: break
        for _ in range(k):
            y, ln, pal = struct.unpack_from('<BBH', b, p); p += 4
            top[i // w, i % w] = pal; topy[i // w, i % w] = y + ln - 1
    return m, g, wa, su, co, top, topy
def colour(name):
    n = name.split(':')[0]
    if 'glass' in n: return (150, 190, 210)
    if 'wool' in n or 'carpet' in n: return (200, 200, 200)
    if 'brick' in n and 'stone' not in n: return (160, 80, 60)
    if 'quartz' in n or 'snow' in n: return (230, 228, 222)
    if 'hardened_clay' in n: return (170, 120, 95)
    if 'plank' in n or 'wooden' in n or 'oak' in n or 'spruce' in n or 'fence' in n: return (150, 115, 75)
    if 'iron' in n or 'anvil' in n: return (190, 190, 195)
    return (135, 135, 135)
if __name__ == '__main__':
    sid = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else sid + '.png'; st = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    m, g, wa, su, co, top, topy = load(sid)
    gf = g.astype(np.float32); sh = np.clip(1 + (-np.gradient(gf, axis=1) - np.gradient(gf, axis=0)) * 0.15, 0.5, 1.5)
    img = np.zeros(g.shape + (3,), np.float32); img[:] = (95, 130, 60)
    img[co == 1] = (105, 145, 65); img[co == 3] = (150, 150, 80); img[co == 5] = (130, 110, 70)
    for i, nm in enumerate(m['palette']):
        if nm.startswith(('sand', 'gravel', 'stone:', 'cobblestone', 'dirt', 'grass_path', 'clay', 'hardened')):
            c = {'sand': (205, 195, 150), 'gravel': (130, 125, 120), 'stone': (120, 120, 120), 'cobblestone': (110, 110, 110),
                 'dirt': (125, 95, 65), 'grass_path': (150, 130, 80), 'clay': (150, 155, 165), 'hardened_clay': (160, 115, 90)}[nm.split(':')[0].replace('stained_', '')] if nm.split(':')[0].replace('stained_', '') in ('sand', 'gravel', 'stone', 'cobblestone', 'dirt', 'grass_path', 'clay', 'hardened_clay') else (120, 120, 120)
            img[(su == i) & (top < 0)] = c
    img *= sh[..., None]
    img[wa > 0] = (50, 70, 160)
    for x, z, th, lg, kind, lv in m['trees']: img[max(z - 1, 0):z + 2, max(x - 1, 0):x + 2] = (40, 80, 35)
    cols = np.array([colour(n) for n in m['palette']] + [(0, 0, 0)], np.float32)
    built = top >= 0; img[built] = cols[top[built]] * np.clip(0.7 + (topy[built] - g[built]) / 60.0, 0.7, 1.3)[:, None]
    Image.fromarray(np.clip(img[::st, ::st], 0, 255).astype(np.uint8)).save(out)
    print(sid, m['name'], 'window', m['window'], 'ground median', m['groundMedian'], 'water', m['waterLevel'])
