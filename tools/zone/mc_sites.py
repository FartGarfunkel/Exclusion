#!/usr/bin/env python3
"""Export the user's own Minecraft 1.11 Zone world (TheZone) into per-location site templates for the Zone generator.
usage: mc_sites.py <TheZone.zip or world dir> [out_dir=data/zone_sites]
Each site: <id>.json (window, palette of MC block names, trees, info) + <id>.bin (zlib):
  ground u8[w*h]  (MC y of the ground top; natural ground only, building floors count as ground)
  water  u8[w*h]  (MC y of the water surface, 0 = dry)
  surf   u8[w*h]  (palette index of the ground's top block)
  cover  u8[w*h]  (0 none, 1 grass, 2 fern, 3 flower, 4 tall plant, 5 dead bush, 6 crop, 7 lily, 8 reeds, 9 snow)
  then per column (row-major): varint n, n x (u8 y0, u8 len, u16 pal) runs of built blocks above ground (MC y)
The teleport room's signs name every location; their command blocks give the anchor points used below."""
import sys, os, re, io, json, zlib, struct, gzip, zipfile, collections
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from mc_nbt import region_chunks

# location -> (our region id, MC anchor x, z, window half-sizes west, north, east, south)
SITES = {
  'marsh':      ('Clear Sky camp (Great Swamp)', -5583, 4417, 550, 550, 550, 450),
  'perimeter':  ('Cordon',                       -4118, 3486, 500, 450, 500, 500),
  'scrapyard':  ('Garbage',                      -4176, 2224, 420, 420, 420, 420),
  'institute':  ('Agroprom',                     -4896, 2390, 420, 450, 380, 450),
  'hollow':     ('Dark Valley',                  -3236, 2438, 450, 450, 450, 450),
  'molot':      ('100 Rads Bar (Rostok)',        -4379, 1277, 380, 380, 380, 380),
  'lake':       ('Yantar',                       -5200, 1705, 400, 400, 400, 400),
  'depot':      ('Military Warehouses',          -3960,  490, 450, 450, 450, 450),
  'sulfur':     ('Dead City',                    -5532,  772, 420, 380, 420, 380),
  'zarechny':   ('Limansk',                      -5533, -396, 450, 500, 450, 450),
  'array':      ('Duga-3',                       -1504, 3428, 500, 500, 500, 500),
  'zaton':      ('Zaton',                        -3426, -3999, 600, 500, 600, 400),
  'jupiter':    ('Yanov Station',                -4249, -3083, 450, 450, 450, 450),
  'pripyat':    ('Pripyat',                      -2862, -2312, 850, 1250, 850, 1050),
  'npp':        ('Chernobyl NPP',                 -622,   912, 900, 750, 900, 800),
}
NAMES = {}  # id -> name (1.11 numeric ids)
for i, n in enumerate("""air stone grass dirt cobblestone planks sapling bedrock flowing_water water flowing_lava lava sand gravel gold_ore iron_ore coal_ore log leaves sponge glass lapis_ore lapis_block dispenser sandstone noteblock bed golden_rail detector_rail sticky_piston web tallgrass deadbush piston piston_head wool piston_extension yellow_flower red_flower brown_mushroom red_mushroom gold_block iron_block double_stone_slab stone_slab brick_block tnt bookshelf mossy_cobblestone obsidian torch fire mob_spawner oak_stairs chest redstone_wire diamond_ore diamond_block crafting_table wheat farmland furnace lit_furnace standing_sign wooden_door ladder rail stone_stairs wall_sign lever stone_pressure_plate iron_door wooden_pressure_plate redstone_ore lit_redstone_ore unlit_redstone_torch redstone_torch stone_button snow_layer ice snow cactus clay reeds jukebox fence pumpkin netherrack soul_sand glowstone portal lit_pumpkin cake unpowered_repeater powered_repeater stained_glass trapdoor monster_egg stonebrick brown_mushroom_block red_mushroom_block iron_bars glass_pane melon_block pumpkin_stem melon_stem vine fence_gate brick_stairs stone_brick_stairs mycelium waterlily nether_brick nether_brick_fence nether_brick_stairs nether_wart enchanting_table brewing_stand cauldron end_portal end_portal_frame end_stone dragon_egg redstone_lamp lit_redstone_lamp double_wooden_slab wooden_slab cocoa sandstone_stairs emerald_ore ender_chest tripwire_hook tripwire emerald_block spruce_stairs birch_stairs jungle_stairs command_block beacon cobblestone_wall flower_pot carrots potatoes wooden_button skull anvil trapped_chest light_weighted_pressure_plate heavy_weighted_pressure_plate unpowered_comparator powered_comparator daylight_detector redstone_block quartz_ore hopper quartz_block quartz_stairs activator_rail dropper stained_hardened_clay stained_glass_pane leaves2 log2 acacia_stairs dark_oak_stairs slime barrier iron_trapdoor prismarine sea_lantern hay_block carpet hardened_clay coal_block packed_ice double_plant standing_banner wall_banner daylight_detector_inverted red_sandstone red_sandstone_stairs double_stone_slab2 stone_slab2 spruce_fence_gate birch_fence_gate jungle_fence_gate dark_oak_fence_gate acacia_fence_gate spruce_fence birch_fence jungle_fence dark_oak_fence acacia_fence spruce_door birch_door jungle_door acacia_door dark_oak_door end_rod chorus_plant chorus_flower purpur_block purpur_pillar purpur_stairs purpur_double_slab purpur_slab end_bricks beetroots grass_path end_gateway repeating_command_block chain_command_block frosted_ice magma nether_wart_block red_nether_brick bone_block structure_void observer""".split()):
    NAMES[i] = n
NAMES[255] = 'structure_block'
GROUND = {1, 2, 3, 7, 12, 13, 14, 15, 16, 21, 24, 56, 73, 74, 82, 110, 60, 208, 8, 9, 10, 11, 79, 80, 174, 129, 153}
SOIL = {2, 3, 12, 13, 82, 110, 60, 208}
TREE = {17, 18, 161, 162, 106, 127}
PLANT = {6, 31, 32, 37, 38, 39, 40, 59, 78, 83, 111, 115, 141, 142, 175, 207, 81, 86, 103, 104, 105}
MACHINE = {7, 23, 29, 33, 34, 36, 52, 55, 69, 70, 72, 75, 76, 77, 93, 94, 119, 120, 122, 137, 143, 147, 148, 149, 150, 152, 154,
           157, 158, 166, 176, 177, 178, 209, 210, 211, 217, 218, 255, 27, 28, 66, 90, 51, 132, 131, 151}
COVER = {31: lambda d: 2 if d == 2 else 1, 32: lambda d: 5, 37: lambda d: 3, 38: lambda d: 3, 175: lambda d: 4,
         59: lambda d: 6, 141: lambda d: 6, 142: lambda d: 6, 207: lambda d: 6, 111: lambda d: 7, 83: lambda d: 8, 78: lambda d: 9}

def load_column_block(src, rx, rz, cache={}):
    key = (rx, rz)
    if key not in cache:
        cache.clear()
        name = 'TheZone/region/r.%d.%d.mca' % (rx, rz)
        try:
            data = src.read(name) if isinstance(src, zipfile.ZipFile) else open(os.path.join(src, 'region', 'r.%d.%d.mca' % (rx, rz)), 'rb').read()
        except (KeyError, FileNotFoundError):
            data = None
        cache[key] = {} if data is None else {(cx, cz): c for cx, cz, c in region_chunks(data)}
    return cache[key]

def chunk_arrays(c):
    blk = np.zeros((256, 16, 16), np.uint8); dt = np.zeros((256, 16, 16), np.uint8)
    for s in c['Level']['Sections']:
        y = s['Y']; b = np.frombuffer(s['Blocks'], np.uint8).reshape(16, 16, 16)
        d = np.frombuffer(s['Data'], np.uint8); dd = np.empty(4096, np.uint8); dd[0::2] = d & 15; dd[1::2] = d >> 4
        blk[y*16:y*16+16] = b; dt[y*16:y*16+16] = dd.reshape(16, 16, 16)
    return blk, dt

def varint(n):
    out = bytearray()
    while True:
        b = n & 127; n >>= 7
        out.append(b | (128 if n else 0))
        if not n: return bytes(out)

def export(src, sid, info, outdir):
    name, ax, az, hw, hn, he, hs = info
    x0, z0, w, h = ax - hw, az - hn, hw + he, hn + hs
    ground = np.zeros((h, w), np.uint8); water = np.zeros((h, w), np.uint8); surf = np.zeros((h, w), np.uint16)
    cover = np.zeros((h, w), np.uint8)
    pal = {}; palnames = []; trees = []
    cols = [[] for _ in range(w * h)]
    def P(b, d):
        k = (int(b), int(d))
        if k not in pal: pal[k] = len(palnames); palnames.append('%s:%d' % (NAMES.get(k[0], 'id%d' % k[0]), k[1]))
        return pal[k]
    for cz in range(z0 >> 4, (z0 + h - 1 >> 4) + 1):
        for cx in range(x0 >> 4, (x0 + w - 1 >> 4) + 1):
            ch = load_column_block(src, cx >> 5, cz >> 5).get((cx & 31, cz & 31))
            if ch is None: continue
            blk, dt = chunk_arrays(ch)
            for lz in range(16):
                wz = cz * 16 + lz - z0
                if not 0 <= wz < h: continue
                for lx in range(16):
                    wx = cx * 16 + lx - x0
                    if not 0 <= wx < w: continue
                    col = blk[:, lz, lx]; dcol = dt[:, lz, lx]
                    nz = np.nonzero(col)[0]
                    if nz.size == 0: continue
                    # ground: the natural column built up from the bottom (b); if the stretch above it up to the highest natural
                    # block (gy) holds no built block it is a cave roof, so the ground is gy; otherwise b is the floor under a building
                    gy = 0
                    for y in nz[::-1]:
                        if col[y] in GROUND: gy = int(y); break
                    b = 0
                    while b < 255 and col[b + 1] in GROUND: b += 1
                    soil = -1
                    for y in range(gy, max(b, 2), -1):   # a real surface: natural blocks under it and soil near the top
                        if col[y] in GROUND and col[y - 1] in GROUND and col[y - 2] in GROUND and \
                           any(col[y - k] in SOIL for k in range(4)): soil = y; break
                    if b < gy and soil > b: gy = soil       # buried bunkers / labs under real ground
                    elif b < gy:
                        seg = col[b + 1:gy + 1]
                        man = [v for v in seg if v and v not in GROUND and v not in TREE and v not in PLANT]
                        if man or (seg != 0).mean() < 0.5: gy = b
                    if col[gy] in (8, 9):
                        water[wz, wx] = gy
                        while gy > 0 and col[gy] in (8, 9): gy -= 1
                    ground[wz, wx] = gy
                    surf[wz, wx] = P(col[gy], dcol[gy] if col[gy] in (1, 3, 12, 24, 159, 179) else 0)
                    ab = col[gy + 1] if gy < 255 else 0
                    if ab in COVER: cover[wz, wx] = COVER[ab](int(dcol[gy + 1]))
                    if ab in (17, 162) and col[gy] not in (17, 162):
                        th = 1
                        while gy + th < 255 and col[gy + th] in (17, 162): th += 1
                        lv = int(col[min(gy + th, 255)]); trees.append([wx, wz, th - 1, int(ab), int(dcol[gy + 1] & 3), lv in (18, 161)])
                    runs = []; y = gy + 1
                    while y < 256:
                        b = col[y]
                        if b == 0 or b in TREE or b in PLANT or b in MACHINE or b in (8, 9):
                            y += 1; continue
                        p = P(b, dcol[y]); y1 = y + 1
                        while y1 < 256 and col[y1] == b and dcol[y1] == dcol[y]: y1 += 1
                        while y1 - y > 255: runs.append((y, 255, p)); y += 255
                        runs.append((y, y1 - y, p)); y = y1
                    cols[wz * w + wx] = runs
    buf = io.BytesIO()
    buf.write(ground.tobytes()); buf.write(water.tobytes())
    if len(palnames) > 255: print('  warn: palette', len(palnames), '> 255, surf clamps')
    buf.write(np.minimum(surf, 255).astype(np.uint8).tobytes()); buf.write(cover.tobytes())
    nvox = 0
    for runs in cols:
        buf.write(varint(len(runs)))
        for y, ln, p in runs: buf.write(struct.pack('<BBH', y, ln, p)); nvox += ln
    blob = zlib.compress(buf.getvalue(), 9)
    open(os.path.join(outdir, sid + '.bin'), 'wb').write(blob)
    gl = ground[ground > 0]
    meta = {'id': sid, 'name': name, 'anchor': [ax, az], 'window': [x0, z0, w, h], 'anchorInWindow': [ax - x0, az - z0],
            'groundMedian': int(np.median(gl)) if gl.size else 63, 'waterLevel': int(np.median(water[water > 0])) if (water > 0).any() else 0,
            'palette': palnames, 'trees': trees, 'voxels': nvox}
    json.dump(meta, open(os.path.join(outdir, sid + '.json'), 'w'), separators=(',', ':'))
    print('%-10s %-30s %4dx%-4d vox %8d pal %3d trees %6d  %5.1f MB' % (sid, name, w, h, nvox, len(palnames), len(trees), len(blob) / 1e6), flush=True)

if __name__ == '__main__':
    src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'zone_sites')
    os.makedirs(out, exist_ok=True)
    src = zipfile.ZipFile(src) if src.endswith('.zip') else src
    only = sys.argv[3].split(',') if len(sys.argv) > 3 else list(SITES)
    for sid in only: export(src, sid, SITES[sid], out)
