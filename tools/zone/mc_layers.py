#!/usr/bin/env python3
"""Derive the Zone's coarse layout from the user's world tiles (data/zone_world/t.*.bin, see mc_world.py):
  coarse.bin  zlib of u8 layers [height, water, wdist, forest, open, built, road] at CELL m over the bounds
  coarse.json the grid, the roads as polylines (skeleton of the paved / gravel surfaces), the building sites (footprints of
              the built-up columns: box, floor y, height, class) and the chest spots
Read by gen/zone_world.cpp (layers for the PDA map, the planner, A-Life, the roadside dressing). The terrain fold here
(natural stacks on the ground become ground) must match foldColumn() in gen/zone_world.cpp.
usage: mc_layers.py [dir=data/zone_world]"""
import sys, os, json, zlib, struct
import numpy as np
from multiprocessing import Pool
from scipy import ndimage as ndi
from skimage.morphology import skeletonize

CELL = 8          # coarse cell (m)
FINE = 2          # roads and sites are worked out at 2 m
D = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'zone_world')
META = json.load(open(os.path.join(D, 'world.json')))
X0, Z0, X1, Z1 = META['bounds']
W8, H8 = (X1 - X0 + CELL) // CELL, (Z1 - Z0 + CELL) // CELL
W2, H2 = (X1 - X0 + FINE) // FINE, (Z1 - Z0 + FINE) // FINE

NATURAL = {'stone', 'grass', 'dirt', 'sand', 'gravel', 'clay', 'sandstone', 'red_sandstone', 'coal_ore', 'iron_ore', 'gold_ore',
           'redstone_ore', 'lit_redstone_ore', 'lapis_ore', 'diamond_ore', 'emerald_ore', 'quartz_ore', 'monster_egg', 'bedrock',
           'snow', 'ice', 'packed_ice', 'mycelium', 'farmland', 'grass_path', 'hardened_clay'}
ROADRUN = {'soul_sand', 'cobblestone', 'mossy_cobblestone', 'stone_slab', 'double_stone_slab', 'stonebrick', 'gravel', 'stone', 'concrete'}
HARD = {'soul_sand', 'stone_slab', 'double_stone_slab', 'stonebrick', 'stone', 'concrete'}
CHEST = {'chest', 'trapped_chest'}

def parse(path):
    b = zlib.decompress(open(path, 'rb').read()); assert b[:4] == b'ZWT1'
    n = struct.unpack_from('<H', b, 4)[0]; p = 6; names = []
    for _ in range(n): l = b[p]; names.append(b[p + 1:p + 1 + l].decode()); p += 1 + l
    N = 512 * 512
    g = np.frombuffer(b, np.uint8, N, p).astype(np.int32); p += N
    w = np.frombuffer(b, np.uint8, N, p).astype(np.int32); p += N
    s = np.frombuffer(b, '<u2', N, p).astype(np.int32); p += 2 * N
    c = np.frombuffer(b, np.uint8, N, p); p += N
    nt = struct.unpack_from('<I', b, p)[0]; p += 4
    tr = np.frombuffer(b, np.dtype([('x', '<u2'), ('z', '<u2'), ('t', 'u1'), ('k', 'u1'), ('l', 'u1')]), nt, p); p += 7 * nt
    # the runs: per column a varint count then count x (u8 y0, u8 len, u16 pal)
    cols, starts, cnts = [], [], []
    mv = memoryview(b)
    for i in range(N):
        v = mv[p]
        if v == 0: p += 1; continue
        k = v & 127; sh = 7; p += 1
        while v & 128: v = mv[p]; p += 1; k |= (v & 127) << sh; sh += 7
        cols.append(i); starts.append(p); cnts.append(k); p += 4 * k
    if cols:
        cols = np.array(cols, np.int64); starts = np.array(starts, np.int64); cnts = np.array(cnts, np.int64)
        rc = np.repeat(cols, cnts)
        off = np.repeat(starts, cnts) + 4 * (np.arange(cnts.sum()) - np.repeat(np.cumsum(cnts) - cnts, cnts))
        raw = np.frombuffer(b, np.uint8)
        ry = raw[off].astype(np.int32); rl = raw[off + 1].astype(np.int32); rp = raw[off + 2].astype(np.int32) | (raw[off + 3].astype(np.int32) << 8)
    else:
        rc = np.zeros(0, np.int64); ry = rl = rp = np.zeros(0, np.int32)
    return names, g, w, s, c, tr, rc, ry, rl, rp

def tile(args):
    rx, rz = args
    f = os.path.join(D, 't.%d.%d.bin' % (rx, rz))
    if not os.path.exists(f): return None
    names, g, w, s, c, tr, rc, ry, rl, rp = parse(f)
    key = [n.split(':')[0] for n in names]
    natL = np.array([k in NATURAL for k in key] + [False]); roadL = np.array([k in ROADRUN for k in key] + [False])
    hardL = np.array([(k in HARD) or n in ('stone:5', 'stone:6') for k, n in zip(key, names)] + [False])
    chestL = np.array([k in CHEST for k in key] + [False])
    N = 512 * 512
    gy = g.copy(); surf = s.copy()
    tall = np.zeros(N, bool); flatroad = np.zeros(N, bool); top = gy.copy()
    if len(rc):
        # per column: the topmost run (runs come bottom-up), its span, how much of it is natural
        last = np.r_[rc[1:] != rc[:-1], True]; first = np.r_[True, rc[1:] != rc[:-1]]
        ucol = rc[last]; topRun = np.nonzero(last)[0]; firstRun = np.nonzero(first)[0]
        cnt = topRun - firstRun + 1
        rtop = ry + rl
        ctop = rtop[topRun]; cfirst = ry[firstRun]
        tot = np.add.reduceat(rl, firstRun); nat = np.add.reduceat(rl * natL[rp], firstRun)
        g0 = gy[ucol]
        contig = (cfirst == g0 + 1) & (tot == ctop - cfirst)
        fold = natL[rp[topRun]] & ((contig & (ctop - g0 <= 8)) | ((nat * 3 >= ctop - g0) & (tot * 4 >= (ctop - g0) * 3)))
        gy[ucol[fold]] = ctop[fold] - 1; surf[ucol[fold]] = rp[topRun][fold]
        keep = ~fold
        fr = keep & (cnt == 1) & (cfirst == g0 + 1) & (rl[firstRun] == 1) & roadL[rp[firstRun]]
        flatroad[ucol[fr]] = True
        tl = keep & ~fr
        tall[ucol[tl]] = True; top[ucol[tl]] = ctop[tl]
        fc = np.zeros(N, bool); fc[ucol[fold]] = True
        ci = np.nonzero(chestL[rp] & ~fc[rc])[0]
        chests = [(int(rc[j] % 512), int(ry[j]), int(rc[j] // 512)) for j in ci]
        # the flat road blocks count as the surface
        hardflat = np.zeros(N, bool); hardflat[ucol[fr]] = hardL[rp[firstRun][fr]]
    else:
        chests = []; hardflat = np.zeros(N, bool)
    gy = gy.reshape(512, 512); wa = (w > 0).reshape(512, 512); valid = (g > 0).reshape(512, 512)
    surfk = np.array([key[i] if i < len(key) else '' for i in range(len(key))] + [''])
    sk = surfk[np.minimum(surf, len(key))].reshape(512, 512)
    hard = (np.isin(sk, ['stone', 'cobblestone', 'stonebrick']) | hardflat.reshape(512, 512)) & ~wa
    grav = ((sk == 'gravel') | (flatroad.reshape(512, 512) & ~hardflat.reshape(512, 512))) & ~wa
    tall = tall.reshape(512, 512); top = top.reshape(512, 512)
    trees = np.zeros((512, 512), np.int32)
    if len(tr): np.add.at(trees, (tr['z'].astype(int), tr['x'].astype(int)), 1)
    def pool(a, k, fn=np.sum): return fn(fn(a.reshape(512 // k, k, 512 // k, k), axis=3), axis=1)
    k8 = CELL
    out = dict(rx=rx, rz=rz,
               hsum=pool(np.where(valid, gy + 2, 0).astype(np.int64), k8), hn=pool(valid.astype(np.int64), k8),
               water=pool(wa.astype(np.int64), k8), trees=pool(trees.astype(np.int64), k8), built=pool(tall.astype(np.int64), k8),
               road=pool((hard | grav).astype(np.int64), k8), dirt=pool(np.isin(sk, ['dirt', 'sand']).astype(np.int64) & valid, k8),
               tall2=pool(tall.astype(np.int64), FINE) >= 1, hard2=pool(hard.astype(np.int64), FINE) >= 2, grav2=pool(grav.astype(np.int64), FINE) >= 2,
               top2=pool(np.where(tall, top, 0), FINE, np.max).astype(np.uint8),
               g2=pool(np.where(valid, gy, 255), FINE, np.min).astype(np.uint8), chests=chests)
    return out

def trace(sk):
    """skeleton pixels -> polylines (lists of (row, col)), split at ends and junctions"""
    ys, xs = np.nonzero(sk)
    S = set(zip(ys.tolist(), xs.tolist()))
    NB = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
    nbs = lambda p: [(p[0] + a, p[1] + b) for a, b in NB if (p[0] + a, p[1] + b) in S]
    deg = {p: len(nbs(p)) for p in S}
    nodes = [p for p in S if deg[p] != 2]
    used = set(); lines = []
    def walk(a, b):
        line = [a, b]; used.add((a, b)); used.add((b, a))
        prev, cur = a, b
        while deg[cur] == 2:
            nx = [q for q in nbs(cur) if q != prev and (cur, q) not in used]
            if not nx: break
            q = nx[0]; used.add((cur, q)); used.add((q, cur)); line.append(q); prev, cur = cur, q
        return line
    for n in nodes:
        for q in nbs(n):
            if (n, q) not in used: lines.append(walk(n, q))
    for p in S:   # loops with no node on them
        if deg[p] == 2 and not any((p, q) in used for q in nbs(p)):
            q = nbs(p)[0]; lines.append(walk(p, q))
    return lines

def rdp(pts, eps):
    if len(pts) < 3: return pts
    a, b = np.array(pts[0], float), np.array(pts[-1], float); d = b - a; L = np.hypot(*d)
    P = np.array(pts, float)
    Q = P - a; dist = np.abs(d[0] * Q[:, 1] - d[1] * Q[:, 0]) / L if L > 0 else np.hypot(Q[:, 0], Q[:, 1])
    i = int(np.argmax(dist))
    if dist[i] > eps and 0 < i < len(pts) - 1: return rdp(pts[:i + 1], eps)[:-1] + rdp(pts[i:], eps)
    return [pts[0], pts[-1]]

def main():
    jobs = [tuple(t) for t in META['tiles']]
    hsum = np.zeros((H8, W8)); hn = np.zeros((H8, W8)); water = np.zeros((H8, W8)); trees = np.zeros((H8, W8)); built = np.zeros((H8, W8))
    road = np.zeros((H8, W8)); dirt = np.zeros((H8, W8))
    tall2 = np.zeros((H2, W2), bool); hard2 = np.zeros((H2, W2), bool); grav2 = np.zeros((H2, W2), bool)
    top2 = np.zeros((H2, W2), np.uint8); g2 = np.full((H2, W2), 255, np.uint8); chests = []
    with Pool(2) as pool:
        for k, r in enumerate(pool.imap_unordered(tile, jobs, chunksize=4)):
            if r is None: continue
            tx, tz = r['rx'] * 512 - X0, r['rz'] * 512 - Z0   # tile corner in bound-relative metres (may be < 0)
            def paste(dst, src, cell):
                n = 512 // cell; i0, j0 = tz // cell, tx // cell
                a0, b0 = max(0, -i0), max(0, -j0); a1, b1 = min(n, dst.shape[0] - i0), min(n, dst.shape[1] - j0)
                if a1 > a0 and b1 > b0: dst[i0 + a0:i0 + a1, j0 + b0:j0 + b1] = src[a0:a1, b0:b1] if dst.dtype == bool or dst.dtype == np.uint8 else dst[i0 + a0:i0 + a1, j0 + b0:j0 + b1] + src[a0:a1, b0:b1]
            for nm, dst in (('hsum', hsum), ('hn', hn), ('water', water), ('trees', trees), ('built', built), ('road', road), ('dirt', dirt)): paste(dst, r[nm], CELL)
            for nm, dst in (('tall2', tall2), ('hard2', hard2), ('grav2', grav2), ('top2', top2), ('g2', g2)): paste(dst, r[nm], FINE)
            for (x, y, z) in r['chests']: chests.append([r['rx'] * 512 + x, y + 2, r['rz'] * 512 + z])
            if k % 50 == 0: print('%d/%d tiles' % (k + 1, len(jobs)), flush=True)
    A = CELL * CELL
    height = np.where(hn > 0, hsum / np.maximum(hn, 1), 66)
    # fill the cells without data from their neighbours (smoothly out to the edge)
    if (hn == 0).any():
        idx = ndi.distance_transform_edt(hn == 0, return_distances=False, return_indices=True)
        height = height[tuple(idx)]
    wf = water / A
    wd = ndi.distance_transform_edt(wf < 0.5) * CELL
    fo = np.clip(ndi.uniform_filter(trees, 3) / 1.6, 0, 1)
    bu = np.clip(ndi.uniform_filter(built / A, 3) * 2.5, 0, 1)
    ro = np.clip(road / A * 1.5, 0, 1)
    op = np.clip((1 - fo) * (1 - bu) * (1 - wf) * (0.6 + 0.4 * np.clip(dirt / A * 2, 0, 1)), 0, 1)
    layers = [height, wf * 255, np.clip(wd / 510 * 255, 0, 255), fo * 255, op * 255, bu * 255, ro * 255]
    blob = np.stack([np.clip(np.round(l), 0, 255).astype(np.uint8) for l in layers])
    open(os.path.join(D, 'coarse.bin'), 'wb').write(zlib.compress(blob.tobytes(), 9))
    # ---- the roads: the paved and gravel surfaces' skeleton, yards and squares left out
    rm = ndi.binary_closing(hard2 | grav2, iterations=1)
    lab, n = ndi.label(rm); sz = ndi.sum(rm, lab, np.arange(1, n + 1))
    rm &= np.isin(lab, np.nonzero(sz >= 40)[0] + 1)
    dt = ndi.distance_transform_edt(rm)
    sk = skeletonize(rm) & (dt <= 5)
    lines = trace(sk)
    roads = []
    hardf = ndi.uniform_filter(hard2.astype(float), 5)
    for ln in lines:
        if len(ln) < 20: continue   # < 40 m
        pts = rdp(ln, 2.0)
        ii = np.array(ln); wd_ = float(np.mean(dt[ii[:, 0], ii[:, 1]])) * 2 * FINE
        hf = float(np.mean(hardf[ii[:, 0], ii[:, 1]]))
        if hf <= 0.45 and len(ln) < 45: continue   # stray gravel patches
        roads.append({'kind': 1 if hf > 0.45 else 2, 'width': round(max(3.0, min(12.0, wd_)), 1),
                      'pts': [[X0 + c * FINE + 1, Z0 + r * FINE + 1] for r, c in pts]})
    # ---- the building sites: the built-up columns' footprints
    bm = ndi.binary_closing(tall2, iterations=1)
    lab, n = ndi.label(bm, structure=np.ones((3, 3)))
    sites = []
    objs = ndi.find_objects(lab)
    for i, sl in enumerate(objs):
        if sl is None: continue
        m = lab[sl] == i + 1; cnt = int(m.sum())
        if cnt < 4: continue
        gy = g2[sl][m & tall2[sl]]; tp = top2[sl][m & tall2[sl]]
        if len(gy) == 0: continue
        gy = gy[gy < 255]
        if len(gy) == 0: continue
        floor = int(np.median(gy)); h = int(tp.max()) - floor - 1
        w_, d_ = (sl[1].stop - sl[1].start) * FINE, (sl[0].stop - sl[0].start) * FINE
        area = cnt * FINE * FINE; fill = cnt / ((sl[0].stop - sl[0].start) * (sl[1].stop - sl[1].start))
        if h < 2 or area < 16: continue
        if (fill < 0.12 and area > 400) or (fill < 0.3 and w_ * d_ > 20000): cls = 'fence'
        elif area >= 1500 and h < 30: cls = 'factory'
        elif h >= 14 and area >= 150: cls = 'panel'
        elif area < 120 and h < 9: cls = 'hut'
        elif area < 400 and h < 14: cls = 'house'
        elif h < 14: cls = 'hangar'
        else: cls = 'office'
        sites.append({'x': X0 + sl[1].start * FINE, 'z': Z0 + sl[0].start * FINE, 'w': w_, 'd': d_, 'y': floor + 3, 'h': h,
                      'n': area, 'cls': cls})
    meta = {'cell': CELL, 'x0': X0, 'z0': Z0, 'w': int(W8), 'h': int(H8),
            'layers': ['height', 'water', 'wdist', 'forest', 'open', 'built', 'road'], 'roads': roads, 'sites': sites, 'chests': chests}
    json.dump(meta, open(os.path.join(D, 'coarse.json'), 'w'), separators=(',', ':'))
    print('coarse %dx%d, %d roads (%.0f km), %d sites, %d chests' % (W8, H8, len(roads),
          sum(np.hypot(*np.diff(np.array(r['pts']), axis=0).T).sum() for r in roads) / 1000, len(sites), len(chests)))

if __name__ == '__main__':
    main()
