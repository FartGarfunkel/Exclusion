#!/usr/bin/env python3
# tools/zone/trace_map.py — derives the Zone's own low-res layout layers from a reference satellite picture of the
# Exclusion Zone (north up, ~7 m per pixel; the picture itself is never committed: only what this writes is).
#   python3 tools/zone/trace_map.py <satellite image> [--debug out.png]
# Writes data/zone_map/:
#   layers.bin  zlib of u8 [layer][h][w], one cell = CELL px (CELL*M metres): water (fraction), wdist (metres to
#               water / 2), forest (density), open (fields, bare ground), built (concrete, roofs, yards)
#   map.json    sizes + world origin, the traced roads / rail (world polylines) and the sites (clusters of the
#               picture's yellow building marks: centre, radius, the marks themselves)
# World: x = px*M - W*M/2, z = py*M - H*M/2 (south = +z); gen/zone_layers.cpp reads it.
import json, os, sys, zlib, warnings
warnings.filterwarnings('ignore')
import numpy as np
from PIL import Image
from scipy import ndimage as ndi
from scipy.spatial import cKDTree
from skimage.morphology import white_tophat, disk, skeletonize, remove_small_objects, binary_opening, binary_closing

M = 7.0        # metres per picture pixel
CELL = 4       # picture pixels per layer cell
OUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'data', 'zone_map'))

def rdp(pts, eps):
    if len(pts) < 3: return pts
    a, b = np.array(pts[0], float), np.array(pts[-1], float); ab = b - a; L = np.hypot(*ab) + 1e-9
    d = [abs(ab[0] * (p[1] - a[1]) - ab[1] * (p[0] - a[0])) / L for p in pts[1:-1]]
    i = int(np.argmax(d)) + 1
    if d[i - 1] > eps: return rdp(pts[:i + 1], eps)[:-1] + rdp(pts[i:], eps)
    return [pts[0], pts[-1]]

def trace_chains(sk):
    # pixel chains between junctions / ends of a skeleton
    H, W = sk.shape
    nb = ndi.convolve(sk.astype(np.uint8), np.ones((3, 3), np.uint8), mode='constant') - sk
    node = sk & (nb != 2)
    seen = np.zeros_like(sk)
    offs = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
    chains = []
    def walk(y, x, py, px):
        ch = [(y, x)]
        while True:
            seen[y, x] = True
            nxt = None
            for dy, dx in offs:
                ny, nx = y + dy, x + dx
                if 0 <= ny < H and 0 <= nx < W and sk[ny, nx] and (ny, nx) != (py, px) and (not seen[ny, nx] or node[ny, nx]):
                    if (ny, nx) in ch[-3:]: continue
                    nxt = (ny, nx); break
            if nxt is None: return ch
            py, px, y, x = y, x, nxt[0], nxt[1]
            ch.append((y, x))
            if node[y, x]: return ch
    for y, x in zip(*np.nonzero(node)):
        for dy, dx in offs:
            ny, nx = y + dy, x + dx
            if 0 <= ny < H and 0 <= nx < W and sk[ny, nx] and not seen[ny, nx] and not node[ny, nx]:
                seen[y, x] = True
                chains.append([(y, x)] + walk(ny, nx, y, x))
    return chains

def join_chains(chains, gap):
    # join chains whose ends meet (within `gap` px) heading the same way: the skeleton breaks at every junction
    chains = [list(c) for c in chains if len(c) >= 2]
    def end(c, last):
        p = np.array(c[-1] if last else c[0], float); q = np.array(c[max(0, len(c) - 7)] if last else c[min(6, len(c) - 1)], float)
        d = p - q; return p, d / (np.hypot(*d) + 1e-9)
    alive = [True] * len(chains)
    for _ in range(6):   # a few passes; each joins every end to its best partner at most once
        idx = [(i, e) for i in range(len(chains)) if alive[i] for e in (0, 1)]
        pts = np.array([end(chains[i], e)[0] for i, e in idx])
        if len(pts) < 2: break
        pairs = sorted(cKDTree(pts).query_pairs(gap), key=lambda ab: np.hypot(*(pts[ab[0]] - pts[ab[1]])))
        used = set(); joined = 0
        for a, b in pairs:
            (i, ei), (j, ej) = idx[a], idx[b]
            if i == j or i in used or j in used or not (alive[i] and alive[j]): continue
            (pa, da), (pb, db) = end(chains[i], ei), end(chains[j], ej)
            if np.dot(da, -db) < 0.6: continue
            A = chains[i] if ei == 1 else chains[i][::-1]; Bc = chains[j] if ej == 0 else chains[j][::-1]
            chains[i] = A + Bc; alive[j] = False; used.update((i, j)); joined += 1
        if not joined: break
    return [c for c, al in zip(chains, alive) if al]

def main():
    src = sys.argv[1]; dbg = sys.argv[sys.argv.index('--debug') + 1] if '--debug' in sys.argv else None
    im = np.asarray(Image.open(src).convert('RGB')).astype(np.float32)
    H, W = im.shape[:2]; R, G, B = im[..., 0], im[..., 1], im[..., 2]
    lum = 0.299 * R + 0.587 * G + 0.114 * B
    def world(px, py): return (round(px * M - W * M / 2, 1), round(py * M - H * M / 2, 1))

    # ---- the yellow building marks: find them, cluster them, paint them out
    yel = (R > 160) & (G > 130) & (B < 110) & (R - B > 80)
    lab, n = ndi.label(binary_closing(yel, disk(1)))
    cent = np.array(ndi.center_of_mass(yel, lab, range(1, n + 1))) if n else np.zeros((0, 2))
    sizes = ndi.sum(yel, lab, range(1, n + 1)) if n else []
    dots = np.array([c for c, s in zip(cent, sizes) if 3 <= s <= 200])           # (y, x)
    med = ndi.median_filter(lum, 9)
    lum = np.where(ndi.binary_dilation(yel, iterations=2), med, lum)
    parent = list(range(len(dots)))
    def find(i):
        while parent[i] != i: parent[i] = parent[parent[i]]; i = parent[i]
        return i
    for i, j in cKDTree(dots).query_pairs(45): parent[find(i)] = find(j)
    groups = {}
    for i in range(len(dots)): groups.setdefault(find(i), []).append(i)

    # ---- texture and colour classes
    mean = ndi.uniform_filter(lum, 7); sq = ndi.uniform_filter(lum * lum, 7); std = np.sqrt(np.maximum(sq - mean * mean, 0))
    sat = im.max(-1) - im.min(-1)
    water = (ndi.uniform_filter(lum, 5) < 34) & (ndi.uniform_filter(std, 9) < 7)
    water = remove_small_objects(binary_closing(binary_opening(water, disk(2)), disk(3)), 400)
    built = ((lum > 165) & (sat < 35)) | ((mean > 105) & (std > 24))
    built = remove_small_objects(binary_opening(built, disk(1)), 30) & ~water
    forest = (mean < 72) & (std > 6) & (G >= R - 4) & ~water & ~built
    openg = (mean >= 72) & (std < 20) & ~water & ~built

    # ---- roads and rail: thin bright lines
    th = white_tophat(lum, disk(4))
    rmask = (th > 16) & (lum > 80) & ~water
    rmask = remove_small_objects(binary_closing(rmask, disk(1)), 80)
    sk = skeletonize(rmask)
    sk = remove_small_objects(sk, 60, connectivity=2)
    dist = ndi.distance_transform_edt(rmask)
    roads = []
    for ch in join_chains([c for c in trace_chains(sk) if len(c) >= 12], 14):
        if len(ch) < 45: continue
        ys = np.array([p[0] for p in ch]); xs = np.array([p[1] for p in ch])
        if built[ys, xs].mean() > 0.6 and len(ch) < 120: continue                        # yard clutter
        w = float(np.median(dist[ys, xs])) * 2; br = float(np.median(th[ys, xs]))
        span = np.hypot(xs[-1] - xs[0], ys[-1] - ys[0])
        kind = 1 if len(ch) > 220 else 2   # (rail is not told apart reliably: the game lays its own)
        pts = rdp([(int(x), int(y)) for y, x in ch], 1.6)
        roads.append({'kind': kind, 'width': 7 if kind == 1 else 4, 'pts': [world(x + .5, y + .5) for x, y in pts]})
    roads.sort(key=lambda r: r['kind'])

    # ---- layers at CELL px
    def down(a):
        h, w = H // CELL, W // CELL
        return a[:h * CELL, :w * CELL].reshape(h, CELL, w, CELL).mean(axis=(1, 3))
    wd = ndi.distance_transform_edt(~water) * M
    lay = {
        'water': down(water.astype(np.float32)),
        'wdist': np.clip(down(wd) / 2, 0, 255) / 255,
        'forest': ndi.gaussian_filter(down(forest.astype(np.float32)), 1.2),
        'open': ndi.gaussian_filter(down(openg.astype(np.float32)), 1.0),
        'built': ndi.gaussian_filter(down(built.astype(np.float32)), 0.8),
    }
    names = list(lay)
    raw = np.stack([np.clip(lay[k] * 255 + .5, 0, 255).astype(np.uint8) for k in names])
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, 'layers.bin'), 'wb') as f: f.write(zlib.compress(raw.tobytes(), 9))
    sites = []
    for g in groups.values():
        if len(g) < 4: continue
        pts = dots[g]; cy, cx = pts.mean(0); r = float(np.max(np.hypot(pts[:, 0] - cy, pts[:, 1] - cx)))
        y0, x0 = int(max(0, cy - r)), int(max(0, cx - r)); y1, x1 = int(min(H, cy + r + 1)), int(min(W, cx + r + 1))
        sites.append({'x': world(cx, cy)[0], 'z': world(cx, cy)[1], 'r': round(r * M + 20), 'n': len(g),
                      'built': round(float(built[y0:y1, x0:x1].mean()), 2), 'forest': round(float(forest[y0:y1, x0:x1].mean()), 2),
                      'dots': [world(p[1], p[0]) for p in pts]})
    sites.sort(key=lambda s: s['z'])
    meta = {'cell': CELL * M, 'w': raw.shape[2], 'h': raw.shape[1], 'x0': -W * M / 2, 'z0': -H * M / 2, 'layers': names,
            'roads': roads, 'sites': sites}
    with open(os.path.join(OUT, 'map.json'), 'w') as f: json.dump(meta, f, separators=(',', ':'))
    print('layers %dx%d, %d roads (%d asphalt, %d rail), %d sites from %d marks' % (raw.shape[2], raw.shape[1], len(roads),
          sum(r['kind'] == 1 for r in roads), sum(r['kind'] == 3 for r in roads), len(sites), len(dots)))
    for s in sites: print('  site %6.0f %6.0f r%4d n%3d built %.2f forest %.2f' % (s['x'], s['z'], s['r'], s['n'], s['built'], s['forest']))
    if dbg:   # water blue, forest green, open tan, built grey, roads white/red, rail yellow, marks magenta
        h, w = raw.shape[1], raw.shape[2]; c = np.zeros((h, w, 3), np.float32) + 40
        for k, col in (('open', (150, 130, 90)), ('forest', (40, 110, 40)), ('built', (170, 170, 170)), ('water', (40, 80, 200))):
            a = lay[k][..., None]; c = c * (1 - a) + np.array(col) * a
        img = Image.fromarray(c.astype(np.uint8)).resize((w * 2, h * 2))
        from PIL import ImageDraw
        d = ImageDraw.Draw(img); s = 2 / (CELL * M)
        P = lambda q: ((q[0] + W * M / 2) * s, (q[1] + H * M / 2) * s)
        for r in roads: d.line([P(q) for q in r['pts']], fill=(255, 255, 255) if r['kind'] == 1 else (230, 60, 40) if r['kind'] == 2 else (255, 230, 0), width=1)
        for st in sites: x, y = P((st['x'], st['z'])); rr = st['r'] * s; d.ellipse([x - rr, y - rr, x + rr, y + rr], outline=(255, 0, 255))
        img.save(dbg)

if __name__ == '__main__': main()
