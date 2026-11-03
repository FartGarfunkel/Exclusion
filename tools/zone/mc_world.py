#!/usr/bin/env python3
"""Export the user's own Minecraft 1.11 Zone world (TheZone) as the Zone's layout: one tile per 512x512 MC region.
usage: mc_world.py <TheZone.zip or world dir> [out_dir=data/zone_world] [rx,rz ...]
World x/z are kept as they are (1 block = 1 m); y is MC y (sea ~62). Bounds (the hand-built area) are in world.json.
Tile t.<rx>.<rz>.bin = zlib of:
  'ZWT2', u16 npal, npal x (u8 len, name 'mc_name:data')   ('campfire:0' = netherrack that had fire on it; 'air:0' = carved air)
  ground u8[512*512]  MC y of the ground top (0 = no data); building floors on the ground count as ground
  water  u8[512*512]  MC y of the water surface (0 = dry)
  surf   u16[512*512] palette index of the ground's top block
  cover  u8[512*512]  0 none, 1 grass, 2 fern, 3 flower, 4 tall plant, 5 dead bush, 6 crop, 7 lily, 8 reeds, 9 snow
  u32 ntrees, ntrees x (u16 x, u16 z, u8 trunk, u8 kind, u8 leaves) kind: 0 oak 1 spruce 2 birch 3 jungle 4 acacia 5 dark oak
  per column (index z*512+x): varint n, n x (u8 y0, u8 len, u16 pal) runs of built blocks above the ground
  per column again: varint n, n x (u8 y0, u8 len, u16 pal) the underground rooms below the ground top (cellars, bunkers,
    tunnels, stairwells): their air ('air:0') and the man-made blocks round it (see underground()). ZWT1 tiles lack this part.
Locations come from the teleport room's signs (their command blocks hold the anchors), see LOCATIONS."""
import sys, os, io, re, json, zlib, struct, zipfile
import numpy as np
from multiprocessing import Pool
from scipy import ndimage
sys.path.insert(0, os.path.dirname(__file__))
from mc_nbt import region_chunks
from mc_sites import NAMES, GROUND, SOIL, TREE, PLANT, MACHINE

BOUNDS = (-6416, -4480, 6015, 4607)   # x0, z0, x1, z1 (inclusive) of the hand-built area
LOCATIONS = {  # sign name -> anchor (x, y, z), from the spawn teleport room
  'Clear Sky camp (Great Swamp)': (-5583, 67, 4417), 'Cordon': (-4118, 78, 3486), 'Garbage': (-4176, 75, 2224),
  'Agroprom': (-4896, 74, 2390), 'Dark Valley': (-3236, 76, 2438), '100 Rads Bar': (-4379, 76, 1277), 'Yantar': (-5200, 69, 1705),
  'Military Warehouses': (-3960, 75, 490), 'Dead City': (-5532, 67, 772), 'Limansk': (-5533, 71, -396), 'Duga-3': (-1504, 68, 3428),
  'Zaton': (-3426, 93, -3999), 'Yanov Station': (-4249, 72, -3083), 'Pripyat North': (-3040, 65, -3252), 'Pripyat South': (-2860, 65, -1614),
  'Pripyat Center': (-2862, 65, -2312), 'Pripyat East': (-2382, 65, -2344), 'Pripyat West': (-3418, 65, -2296),
  'Chernobyl Nuclear Power Plant': (-622, 71, 912)}
LUT = lambda s: np.isin(np.arange(256), list(s))
G_, S_, T_, P_, M_ = LUT(GROUND), LUT(SOIL), LUT(TREE), LUT(PLANT), LUT(MACHINE)
W_ = LUT({8, 9}); LOG_ = LUT({17, 162}); LEAF_ = LUT({18, 161})
CAMPFIRE = 253; NAMES[CAMPFIRE] = 'campfire'   # (a free 1.11 id) netherrack with fire on it: the user's camp fires
# what you walk through underground: air, torches, ladders, doors, signs, carpets, webs, rails, redstone, levers, fire...
OPEN_ = LUT({0, 6, 27, 28, 30, 31, 32, 37, 38, 39, 40, 50, 51, 55, 59, 63, 64, 65, 66, 68, 69, 70, 71, 72, 75, 76, 77, 78, 83, 90, 93, 94, 96,
             104, 105, 106, 115, 131, 132, 140, 141, 142, 143, 144, 147, 148, 149, 150, 157, 167, 171, 175, 176, 177, 193, 194, 195, 196, 197, 207})
COVER = np.zeros((256, 16), np.uint8)
COVER[31, :] = 1; COVER[31, 2] = 2; COVER[32, :] = 5; COVER[37, :] = 3; COVER[38, :] = 3; COVER[175, :] = 4
for i in (59, 141, 142, 207): COVER[i, :] = 6
COVER[111, :] = 7; COVER[83, :] = 8; COVER[78, :] = 9
ZIP = None

def varint(n):
    out = bytearray()
    while True:
        b = n & 127; n >>= 7; out.append(b | (128 if n else 0))
        if not n: return bytes(out)

def chunk_arrays(c):
    blk = np.zeros((256, 16, 16), np.uint8); dt = np.zeros((256, 16, 16), np.uint8)
    for s in c['Level']['Sections']:
        y = s['Y']; b = np.frombuffer(s['Blocks'], np.uint8).reshape(16, 16, 16)
        d = np.frombuffer(s['Data'], np.uint8); dd = np.empty(4096, np.uint8); dd[0::2] = d & 15; dd[1::2] = d >> 4
        blk[y*16:y*16+16] = b; dt[y*16:y*16+16] = dd.reshape(16, 16, 16)
    return blk, dt

def top_true(m):  # highest index along axis 0 where m, -1 if none
    any_ = m.any(0); r = m.shape[0] - 1 - np.argmax(m[::-1], axis=0); r[~any_] = -1; return r

def underground(B, D, ground, P):
    """The rooms below the ground top (the exporter's ground is the top of the earth, so a cellar, a bunker, a tunnel under
    an embankment or the foot of a stairwell would come out as solid earth): every pocket of walkable space below it
    (6-connected, across the region) whose walls, floor and ceiling are at least 30% the user's own blocks is a room; its
    air goes out as 'air:0' runs (torches, ladders, doors, signs kept as they are) and so do the man-made blocks touching it.
    Natural caves (walls of stone and earth) stay solid."""
    N = B.shape[1]; out = [b'\x00'] * (N * N)
    top = int(ground.max()) + 1
    if top < 5: return out
    B = B[:top]; D = D[:top]
    Yi = np.arange(top)[:, None, None]
    op = OPEN_[B]
    und = op & (Yi < ground[None].astype(np.int32)) & (Yi >= 3)
    if not und.any(): return out
    lab, n = ndimage.label(und)
    if not n: return out
    mans = ~op & ~G_[B] & ~T_[B] & ~P_[B] & ~M_[B]   # the user's own solid blocks (the camp fires among them)
    nat = ~op & G_[B]
    man = np.zeros(n + 1, np.int64); natc = np.zeros(n + 1, np.int64)
    def nb(a, ax, k):   # a shifted so out[i] = a[i + k] along axis ax (False past the edge)
        r = np.zeros_like(a); src = [slice(None)] * 3; dst = [slice(None)] * 3
        if k > 0: src[ax] = slice(k, None); dst[ax] = slice(None, -k)
        else: src[ax] = slice(None, k); dst[ax] = slice(-k, None)
        r[tuple(dst)] = a[tuple(src)]; return r
    for ax in range(3):
        for k in (1, -1):
            m1 = und & nb(mans, ax, k); man += np.bincount(lab[m1], minlength=n + 1)
            m2 = und & nb(nat, ax, k); natc += np.bincount(lab[m2], minlength=n + 1)
    size = np.bincount(lab.ravel(), minlength=n + 1)
    keep = (man >= 8) & (man * 10 >= (man + natc) * 3) & (size <= 400000); keep[0] = False
    if not keep.any(): return out
    room = keep[lab]
    adj = np.zeros_like(room)
    for ax in range(3):
        for k in (1, -1): adj |= nb(room, ax, k)
    exp = room | (adj & mans & (Yi < ground[None].astype(np.int32)))
    AIR = P(0, 0)
    cols = np.nonzero(exp.any(0))
    for z, x in zip(*cols):
        ys = np.nonzero(exp[:, z, x])[0]
        b = B[ys, z, x]; d = D[ys, z, x]
        drop = (b == 0) | M_[b] | P_[b] | (b == 51)
        key = np.where(drop, -1, b.astype(np.int32) * 16 + d)
        brk = np.nonzero((np.diff(ys) != 1) | (np.diff(key) != 0))[0]
        starts = np.concatenate([[0], brk + 1]); ends = np.concatenate([brk + 1, [len(ys)]])
        o = []
        for s0, e0 in zip(starts, ends):
            y0 = int(ys[s0]); ln = int(e0 - s0); p = AIR if key[s0] < 0 else P(b[s0], d[s0])
            o.append(struct.pack('<BBH', y0, ln, p))
        out[z * N + x] = varint(len(o)) + b''.join(o)
    return out

def tile(args):
    src, rx, rz = args
    try:
        data = zipfile.ZipFile(src).read('TheZone/region/r.%d.%d.mca' % (rx, rz)) if src.endswith('.zip') else \
            open(os.path.join(src, 'region', 'r.%d.%d.mca' % (rx, rz)), 'rb').read()
    except (KeyError, FileNotFoundError):
        return None
    N = 512; ground = np.zeros((N, N), np.uint8); water = np.zeros((N, N), np.uint8); surf = np.zeros((N, N), np.uint16)
    cover = np.zeros((N, N), np.uint8); runs = [b'\x00'] * (N * N); trees = []
    pal = {}; names = []
    RB = np.zeros((256, N, N), np.uint8); RD = np.zeros((256, N, N), np.uint8)   # the whole region, for the underground pass
    def P(b, d):
        k = (int(b), int(d))
        if k not in pal: pal[k] = len(names); names.append('%s:%d' % (NAMES.get(k[0], 'id%d' % k[0]), k[1]))
        return pal[k]
    x0, z0, x1, z1 = BOUNDS
    Y = np.arange(256)[:, None, None]
    for cx, cz, c in region_chunks(data):
        wx0 = (rx * 32 + cx) * 16; wz0 = (rz * 32 + cz) * 16
        if wx0 + 15 < x0 or wx0 > x1 or wz0 + 15 < z0 or wz0 > z1: continue
        blk, dt = chunk_arrays(c)
        cf = (blk[:-1] == 87) & (blk[1:] == 51); blk[:-1][cf] = CAMPFIRE
        G = G_[blk]; S = S_[blk]; nz = blk != 0
        gtop = top_true(G)                                   # highest natural block
        b = np.argmax(~G[1:], axis=0); b[G[1:].all(0)] = 255  # natural column built up from the bottom
        sh = lambda m, k: np.concatenate([np.zeros((k, 16, 16), bool), m[:-k]], 0)
        surfm = G & sh(G, 1) & sh(G, 2) & (S | sh(S, 1) | sh(S, 2) | sh(S, 3)) & (Y > b[None]) & (Y <= gtop[None])
        soil = top_true(surfm)
        man = nz & ~G & ~T_[blk] & ~P_[blk]
        cm = np.concatenate([np.zeros((1, 16, 16), np.int32), np.cumsum(man, 0, dtype=np.int32)], 0)
        cs = np.concatenate([np.zeros((1, 16, 16), np.int32), np.cumsum(nz, 0, dtype=np.int32)], 0)
        zz, xx = np.indices((16, 16))
        gt = np.maximum(gtop, 0); bb = np.minimum(b, gt)
        nman = cm[gt + 1, zz, xx] - cm[bb + 1, zz, xx]; frac = (cs[gt + 1, zz, xx] - cs[bb + 1, zz, xx]) / np.maximum(gt - bb, 1)
        gy = np.where(soil > b, soil, np.where((b < gt) & ((nman > 0) | (frac < 0.5)), bb, gt))
        gy[gtop < 0] = 0
        isw = W_[blk[gy, zz, xx]]
        wl = np.where(isw, gy, 0)
        notw = ~W_[blk] & (Y <= gy[None]); gw = top_true(notw); gy = np.where(isw, np.maximum(gw, 0), gy)
        lx0 = cx * 16; lz0 = cz * 16
        sl = (slice(lz0, lz0 + 16), slice(lx0, lx0 + 16))
        RB[:, lz0:lz0 + 16, lx0:lx0 + 16] = blk; RD[:, lz0:lz0 + 16, lx0:lx0 + 16] = dt
        inb = ((wx0 + xx) >= x0) & ((wx0 + xx) <= x1) & ((wz0 + zz) >= z0) & ((wz0 + zz) <= z1) & (gtop >= 0)
        ground[sl] = np.where(inb, gy, 0); water[sl] = np.where(inb, wl, 0)
        sb = blk[gy, zz, xx]; sd = np.where(np.isin(sb, (1, 3, 12, 24, 159, 179)), dt[gy, zz, xx], 0)
        a1 = np.minimum(gy + 1, 255); ab = blk[a1, zz, xx]; ad = dt[a1, zz, xx]
        cover[sl] = np.where(inb, COVER[ab, ad & 15], 0)
        built = nz & ~T_[blk] & ~P_[blk] & ~M_[blk] & ~W_[blk] & (Y > gy[None])
        for z in range(16):
            for x in range(16):
                if not inb[z, x]: continue
                surf[lz0 + z, lx0 + x] = P(sb[z, x], sd[z, x])
                if LOG_[ab[z, x]] and not LOG_[sb[z, x]]:
                    y = gy[z, x] + 1; th = 0
                    while y + th < 256 and LOG_[blk[y + th, z, x]]: th += 1
                    lg = int(ab[z, x]); kind = (int(ad[z, x]) & 3) if lg == 17 else 4 + (int(ad[z, x]) & 1)
                    leaves = bool(y + th < 256 and LEAF_[blk[y + th, z, x]]) or bool(LEAF_[blk[min(y + th + 1, 255), z, x]])
                    trees.append((lx0 + x, lz0 + z, min(th, 255), kind, int(leaves)))
                col = built[:, z, x]
                if not col.any(): continue
                ys = np.nonzero(col)[0]; key = blk[ys, z, x].astype(np.int32) * 16 + dt[ys, z, x]
                brk = np.nonzero((np.diff(ys) != 1) | (np.diff(key) != 0))[0]
                starts = np.concatenate([[0], brk + 1]); ends = np.concatenate([brk + 1, [len(ys)]])
                out = []
                for s0, e0 in zip(starts, ends):
                    y0 = int(ys[s0]); ln = int(e0 - s0); p = P(blk[y0, z, x], dt[y0, z, x])
                    while ln > 255: out.append(struct.pack('<BBH', y0, 255, p)); y0 += 255; ln -= 255
                    out.append(struct.pack('<BBH', y0, ln, p))
                runs[(lz0 + z) * N + lx0 + x] = varint(len(out)) + b''.join(out)
    if not ground.any(): return None
    under = underground(RB, RD, ground, P); del RB, RD
    buf = io.BytesIO(); buf.write(b'ZWT2'); buf.write(struct.pack('<H', len(names)))
    for n in names: e = n.encode(); buf.write(struct.pack('<B', len(e))); buf.write(e)
    buf.write(ground.tobytes()); buf.write(water.tobytes()); buf.write(surf.astype('<u2').tobytes()); buf.write(cover.tobytes())
    buf.write(struct.pack('<I', len(trees)))
    for t in trees: buf.write(struct.pack('<HHBBB', *t))
    for r in runs: buf.write(r)
    for r in under: buf.write(r)
    return rx, rz, zlib.compress(buf.getvalue(), 9), len(names), len(trees)

if __name__ == '__main__':
    src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'zone_world')
    os.makedirs(out, exist_ok=True)
    x0, z0, x1, z1 = BOUNDS
    jobs = [(src, rx, rz) for rz in range(z0 >> 9, (z1 >> 9) + 1) for rx in range(x0 >> 9, (x1 >> 9) + 1)]
    if len(sys.argv) > 3: jobs = [(src, int(a.split(',')[0]), int(a.split(',')[1])) for a in sys.argv[3:]]
    tiles = []; tot = 0
    with Pool(2) as pool:
        for i, r in enumerate(pool.imap_unordered(tile, jobs)):
            if r is None: continue
            rx, rz, blob, npal, ntr = r
            open(os.path.join(out, 't.%d.%d.bin' % (rx, rz)), 'wb').write(blob); tiles.append([rx, rz]); tot += len(blob)
            if i % 25 == 0: print('%d/%d tiles, %.1f MB' % (i + 1, len(jobs), tot / 1e6), flush=True)
    meta = {'source': 'TheZone (the user\'s own Minecraft 1.11 world)', 'tile': 512, 'bounds': list(BOUNDS), 'seaLevel': 62,
            'tiles': sorted(tiles), 'locations': {k: list(v) for k, v in LOCATIONS.items()}}
    if len(sys.argv) <= 3: json.dump(meta, open(os.path.join(out, 'world.json'), 'w'), indent=1)
    print('done: %d tiles, %.1f MB' % (len(tiles), tot / 1e6))
