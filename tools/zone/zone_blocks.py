#!/usr/bin/env python3
# tools/zone/zone_blocks.py — the Zone kit: Exclusion's own blocks (worn asphalt, panel walls, rusty props, thin
# trunks, open crowns, power lines...) written into the game data. Idempotent: run it again after editing.
#   * their tiles (painted by paint_textures.py into data/textures/block/zone_*.png) go into data/tiles.json
#   * their models (boxes in 1/16-block pixels, see world/shapes.h) go into data/shapes.json
#   * the blocks themselves into data/blocks.json (an existing key keeps its id; faces resolved here)
#   python3 tools/zone/paint_textures.py --kit && python3 tools/zone/zone_blocks.py
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, '..', '..', 'data'))
sys.path.insert(0, HERE)
from paint_textures import ZTILES

CUBE, CUTOUT, CROSS, MODEL, SHAPE = 1, 2, 3, 6, 7
PANE, CARPET, BRANCH, CABLE = 6, 10, 37, 38   # ShapeKind (core/registry.h)

def cube(key, name, tex, sound='stone', hard=1.5, tool='pickaxe', **kw):
    return dict(key=key, name=name, render=CUBE, solid=True, opaque=True, hardness=hard, tool=tool, sound=sound, tex=tex, drop=None, **kw)
def model(key, name, shape, tex, solid=True, sound='metal', hard=1.0, facing=False, **kw):
    d = dict(key=key, name=name, render=MODEL, solid=solid, opaque=False, hardness=hard, tool='pickaxe', sound=sound, tex=tex, shape=shape, drop=None, **kw)
    if facing: d['facingModel'] = True
    if not solid: d['replaceable'] = True
    return d
def plant(key, name, tex, sound='grass', **kw):
    return dict(key=key, name=name, render=CROSS, solid=False, opaque=False, replaceable=True, hardness=0, sound=sound, tex=tex, drop=None, needsSupport=True, **kw)

BLOCKS = [
    # ground, walls, floors
    cube('zone_asphalt', 'Worn Asphalt', 'zone_asphalt'),
    cube('zone_asphalt_cracked', 'Cracked Asphalt', 'zone_asphalt_cracked'),
    cube('zone_paving', 'Broken Paving', 'zone_paving'),
    cube('zone_panel', 'Concrete Panel', 'zone_panel', hard=2.5),
    cube('zone_panel_tiles', 'Tiled Panel', 'zone_panel_tiles', hard=2.5),
    cube('zone_plaster', 'Peeling Plaster', 'zone_plaster', hard=2),
    cube('zone_paint_green', 'Green Painted Wall', 'zone_paint_green', hard=2),
    cube('zone_whitewash', 'Whitewashed Wall', 'zone_whitewash', hard=2),
    cube('zone_brick_ruined', 'Crumbling Brick', 'zone_brick_ruined', hard=2),
    cube('zone_tiles_dirty', 'Dirty Tiles', 'zone_tiles_dirty', hard=2),
    cube('zone_concrete_mossy', 'Mossy Concrete', 'zone_concrete_mossy', hard=2.5),
    cube('zone_fence_panel', 'Concrete Fence Panel', 'zone_fence_panel', hard=3),
    cube('zone_floor_lino', 'Linoleum Floor', {'top': 'zone_linoleum', 'bottom': 'zone_ceiling', 'side': 'zone_panel'}, hard=2.5),
    cube('zone_floor_tiles', 'Tiled Floor', {'top': 'zone_tiles_dirty', 'bottom': 'zone_ceiling', 'side': 'zone_panel'}, hard=2.5),
    cube('zone_planks_rotten', 'Rotten Planks', 'zone_planks_rotten', 'wood', 1.2, 'axe'),
    cube('zone_corrugated_green', 'Green Corrugated Sheet', 'zone_corrugated_green', 'metal', 2),
    cube('zone_roof_rust', 'Rusty Roofing', 'zone_roof_rust', 'metal', 2),
    cube('zone_steel_green', 'Painted Steel', 'zone_steel_green', 'metal', 4),
    cube('zone_leaf_litter', 'Leaf Litter', 'zone_leaf_litter', 'grass', .6, 'shovel'),
    # panes, carpet, power lines
    dict(key='zone_chainlink', name='Chain-link Fence', render=SHAPE, shapeKind=PANE, solid=True, opaque=False, hardness=3, tool='pickaxe', sound='metal', tex='zone_chainlink', drop=None),
    dict(key='zone_chainlink_post', name='Chain-link Fence Post', render=SHAPE, shapeKind=PANE, solid=True, opaque=False, hardness=3, tool='pickaxe', sound='metal', tex='zone_chainlink_post', drop=None),
    dict(key='zone_window_broken', name='Broken Window', render=SHAPE, shapeKind=PANE, solid=True, opaque=False, hardness=.3, sound='glass', tex='zone_window_broken', drop=None),
    dict(key='zone_moss_carpet', name='Moss', render=SHAPE, shapeKind=CARPET, solid=False, opaque=False, replaceable=True, hardness=.1, sound='grass', tex='zone_moss', drop=None),
    dict(key='zone_cable_x', name='Power Line', render=SHAPE, shapeKind=CABLE, solid=False, opaque=False, replaceable=True, hardness=.5, sound='metal', tex='zone_cable', drop=None, cable='x'),
    dict(key='zone_cable_z', name='Power Line', render=SHAPE, shapeKind=CABLE, solid=False, opaque=False, replaceable=True, hardness=.5, sound='metal', tex='zone_cable', drop=None, cable='z'),
    # trees: slim trunks (the branch shape: they reach into the limbs and leaves beside them), open crowns
    *[dict(key=k, name=n, render=SHAPE, shapeKind=BRANCH, branch=True, isLog=True, solid=True, opaque=False, hardness=1.6, tool='axe', sound='log', tex=t, drop=None, fuel=1)
      for k, n, t in (('zone_birch_trunk', 'Birch Trunk', 'zone_birch_bark'), ('zone_pine_trunk', 'Pine Trunk', 'zone_pine_bark'),
                      ('zone_dead_trunk', 'Dead Trunk', 'zone_dead_bark'), ('zone_burnt_trunk', 'Burnt Trunk', 'zone_burnt_bark'))],
    *[dict(key=k, name=n, render=CUTOUT, solid=False, opaque=False, isLeaves=True, hardness=.2, lightAtt=1, sound='leaves', tex=k, drop=None)
      for k, n in (('zone_birch_leaves', 'Birch Leaves'), ('zone_pine_needles', 'Pine Needles'), ('zone_red_needles', 'Dead Needles'), ('zone_dead_twigs', 'Bare Twigs'))],
    # undergrowth and wire
    plant('zone_tall_dry_grass', 'Tall Dry Grass', 'zone_tall_dry_grass'),
    plant('zone_shrub', 'Shrub', 'zone_shrub'),
    plant('zone_reeds', 'Reeds', 'zone_reeds_lower', texUpper='zone_reeds_upper', double=True),
    plant('zone_rebar', 'Rebar', 'zone_rebar', 'metal'),
    dict(plant('zone_barbed_wire', 'Barbed Wire', 'zone_barbed_wire', 'metal'), replaceable=False, hardness=1.5, needsSupport=False),
    # props (data/shapes.json models)
    model('zone_debris', 'Debris', 'zone_debris', 'zone_debris', solid=False, sound='gravel', hard=.2, facing=True),
    model('zone_barrel', 'Rusty Barrel', 'zone_barrel', 'zone_barrel_side'),
    model('zone_barrel_blue', 'Blue Barrel', 'zone_barrel_blue', 'zone_barrel_blue'),
    model('zone_crate', 'Wooden Crate', 'zone_crate', 'zone_crate', sound='wood', hard=1.5),
    model('zone_tyres', 'Tyres', 'zone_tyres', 'zone_tyre', sound='wool', hard=.8),
    model('zone_sandbags', 'Sandbags', 'zone_sandbags', 'zone_sandbag', sound='sand', hard=1.5, facing=True),
    model('zone_barrier', 'Concrete Barrier', 'zone_barrier', 'zone_barrier', sound='stone', hard=4, facing=True),
    model('zone_pipe', 'Pipe', 'zone_pipe', 'zone_pipe', facing=True),
    model('zone_heat_pipe', 'Heating Main', 'zone_heat_pipe', 'zone_heat_pipe', facing=True),
    model('zone_heat_pipe_support', 'Heating Main', 'zone_heat_pipe_support', 'zone_heat_pipe', facing=True),
    model('zone_radiator', 'Radiator', 'zone_radiator', 'zone_radiator', facing=True),
    model('zone_bed_frame', 'Bed Frame', 'zone_bed_frame', 'zone_bed_metal', facing=True),
    model('zone_pole', 'Power Pole', 'zone_pole', 'zone_pole_wood', sound='wood', hard=2),
    model('zone_pole_top', 'Power Pole', 'zone_pole_top', 'zone_pole_wood', sound='wood', hard=2, facing=True),
    model('zone_lamp_post', 'Lamp Post', 'zone_lamp_post', 'zone_metal_post', hard=3),
    model('zone_lamp_head', 'Street Lamp', 'zone_lamp_head', 'zone_metal_post', hard=3, facing=True),
    model('zone_fallen_trunk', 'Fallen Trunk', 'zone_fallen_trunk', 'zone_dead_bark', sound='wood', hard=1.6, facing=True),
    model('zone_sign_rad', 'Radiation Sign', 'zone_sign_rad', 'zone_sign_rad', facing=True),
]

def B(b, side, end=None, **kw):
    d = {'b': b, 'side': side}
    if end: d['end'] = end
    d.update(kw); return d
def drum(tile):
    return [B([3, 0, 4.5, 13, 14, 11.5], tile, 'zone_barrel_top'), B([4.5, 0, 3, 11.5, 14, 13], tile, 'zone_barrel_top')]
def tyre(dx, y0, y1):
    return [B([2 + dx, y0, 3.5, 14 + dx, y1, 12.5], 'zone_tyre', 'zone_tyre_top'), B([3.5 + dx, y0, 2, 12.5 + dx, y1, 14], 'zone_tyre', 'zone_tyre_top')]
HEAT = B([0, 3, 3, 16, 13, 13], 'zone_heat_pipe', 'zone_heat_pipe', x='zone_pipe')
SHAPES = {
    'zone_debris': [B([0, 0, 0, 16, .5, 16], 'zone_debris'), B([2, 0, 3, 6, 2, 6.5], 'zone_paving'), B([9, 0, 9, 12.5, 1.5, 12], 'zone_brick_ruined'),
                    B([11, 0, 2, 13.5, 1, 4], 'zone_panel'), B([4, 0, 11, 6.5, 1.2, 13.5], 'zone_paving'), B([13, 0, 12, 15, .8, 15], 'zone_brick_ruined')],
    'zone_barrel': drum('zone_barrel_side'),
    'zone_barrel_blue': drum('zone_barrel_blue'),
    'zone_crate': [B([1, 0, 1, 15, 13, 15], 'zone_crate', 'zone_crate')],
    'zone_tyres': tyre(0, 0, 4.5) + tyre(.6, 4.5, 9) + tyre(-.3, 9, 13.5),
    'zone_sandbags': [B(b, 'zone_sandbag', 'zone_sandbag') for b in ([0, 0, 1, 8, 5, 15], [8, 0, 1, 16, 5, 15], [0, 5, 1.5, 4, 10, 14.5], [4, 5, 1.5, 12, 10, 14.5],
                                                                     [12, 5, 1.5, 16, 10, 14.5], [0, 10, 2, 8, 15, 14], [8, 10, 2, 16, 15, 14])],
    'zone_barrier': [B(b, 'zone_barrier', 'zone_barrier') for b in ([0, 0, 2.5, 16, 3, 13.5], [0, 3, 4, 16, 10, 12], [0, 10, 5, 16, 13, 11])],
    'zone_pipe': [B([0, 4, 5, 16, 10, 11], 'zone_pipe', 'zone_pipe'), B([0, 3.5, 4.5, 1, 10.5, 11.5], 'zone_metal_post', 'zone_metal_post')],
    'zone_heat_pipe': [HEAT],
    'zone_heat_pipe_support': [HEAT, B([5, 0, 4, 11, 3, 12], 'zone_panel', 'zone_panel')],
    'zone_radiator': [B([1.5 + 2 * i, 1, 12, 3 + 2 * i, 11, 15], 'zone_radiator', 'zone_radiator') for i in range(7)] +
                     [B([2, 0, 13, 3, 1, 14], 'zone_radiator'), B([13, 0, 13, 14, 1, 14], 'zone_radiator')],
    'zone_bed_frame': [B(b, 'zone_metal_post', 'zone_metal_post') for b in ([.5, 0, 1, 1.5, 5, 2], [14.5, 0, 1, 15.5, 5, 2], [.5, 0, 14, 1.5, 5, 15], [14.5, 0, 14, 15.5, 5, 15],
                                                                           [.5, 4, 1, 15.5, 5, 2], [.5, 4, 14, 15.5, 5, 15])] +
                      [B([1, 5, 1.5, 15, 7, 14.5], 'zone_mattress', 'zone_mattress'), B([0, 0, 1, 1, 12, 15], 'zone_bed_metal', tv=-4), B([15, 0, 1, 16, 9, 15], 'zone_bed_metal', tv=-7)],
    'zone_pole': [B([6, 0, 6, 10, 16, 10], 'zone_pole_wood', 'zone_pole_wood')],
    'zone_pole_top': [B([6, 0, 6, 10, 13, 10], 'zone_pole_wood', 'zone_pole_wood'), B([5.5, 13, 5.5, 10.5, 13.6, 10.5], 'zone_metal_post'),
                      B([-6, 10.5, 6.8, 22, 12.5, 9.2], 'zone_pole_wood', 'zone_pole_wood', nocol=True),
                      B([-4.5, 12.5, 7, -2.5, 15.5, 9], 'zone_insulator', nocol=True), B([18.5, 12.5, 7, 20.5, 15.5, 9], 'zone_insulator', nocol=True)],
    'zone_lamp_post': [B([6.5, 0, 6.5, 9.5, 16, 9.5], 'zone_metal_post', 'zone_metal_post')],
    'zone_lamp_head': [B([6.5, 0, 6.5, 9.5, 9, 9.5], 'zone_metal_post', 'zone_metal_post'), B([7, 8, 8, 9, 10, 16], 'zone_metal_post', nocol=True),
                       B([7, 9, 16, 9, 11, 24], 'zone_metal_post', nocol=True), B([7, 10, 24, 9, 11.5, 28], 'zone_metal_post', nocol=True),
                       B([5.5, 8.5, 25, 10.5, 10.5, 33], 'zone_lamp_head', 'zone_lamp_head', nocol=True)],
    'zone_fallen_trunk': [B([0, 0, 4, 16, 8, 12], 'zone_dead_bark', 'zone_dead_bark', x='zone_bark_end', fit=True, grain='x')],
    'zone_sign_rad': [B([7.5, 0, 7.5, 8.5, 9, 8.5], 'zone_metal_post', 'zone_metal_post'), B([2, 7, 7.4, 14, 16, 8.2], 'zone_sign_rad', 'zone_metal_post')],
}

def load(name): return json.load(open(os.path.join(DATA, name), encoding='utf-8'))
def save(name, j):
    with open(os.path.join(DATA, name), 'w', encoding='utf-8') as f: f.write(json.dumps(j, separators=(',', ':'), ensure_ascii=False))

def main():
    tiles = load('tiles.json'); names = tiles['names']
    missing = [n for n in ZTILES if not os.path.exists(os.path.join(DATA, 'textures', 'block', n + '.png'))]
    if missing: sys.exit('paint the kit first (paint_textures.py --kit): missing ' + ', '.join(missing))
    added = 0
    for n in sorted(ZTILES):
        if n in names: continue
        names[n] = tiles['count']; tiles['files'].append('textures/block/%s.png' % n); tiles['res'].append(32); tiles['count'] += 1; added += 1
    save('tiles.json', tiles)

    sh = load('shapes.json')
    for k, v in SHAPES.items(): sh['shapes'][k] = v
    for v in SHAPES.values():
        for b in v:
            for f in ('side', 'end', 'x', 'z'):
                if f in b and b[f] not in names: sys.exit('shape tile %s unknown' % b[f])
    save('shapes.json', sh)

    bj = load('blocks.json'); bl = bj['blocks']; faces = bj['faces']
    ids = {b['key']: i for i, b in enumerate(bl) if b}
    def T(n):
        if n not in names: sys.exit('tile %s unknown' % n)
        return names[n]
    for d in BLOCKS:
        d = dict(d); d.setdefault('replaceable', False); d.setdefault('tier', 0); d.setdefault('light', 0); d.setdefault('lightAtt', 0); d.setdefault('tool', None)
        tex = d['tex']
        f6 = [T(tex)] * 6 if isinstance(tex, str) else [T(tex['side']), T(tex['side']), T(tex['top']), T(tex['bottom']), T(tex['side']), T(tex['side'])]
        if 'texUpper' in d: d['upperTile'] = T(d['texUpper'])
        i = ids.get(d['key'], len(bl))
        d['id'] = i
        if i == len(bl): bl.append(d); faces.append(f6); ids[d['key']] = i
        else: bl[i] = d; faces[i] = f6
    save('blocks.json', bj)
    print('zone kit: %d tiles (%d new), %d models, %d blocks' % (len(ZTILES), added, len(SHAPES), len(BLOCKS)))

if __name__ == '__main__': main()
