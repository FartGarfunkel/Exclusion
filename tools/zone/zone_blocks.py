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
    # a dead stalker's pack (sim/zone_jobs.cpp drops one where he fell): invisible, drawn by render/pile_layer.cpp from the block
    # entity's `stack` like a placed backpack, opened with the loot screen (its `items`)
    dict(key='zone_body_pack', name="Stalker's pack", render=0, solid=False, opaque=False, hardness=.3, sound='wool', tex='zone_asphalt', drop=None,
         placedBackpack=True, sink=True, container=27),
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
    plant('zone_dandelion_clocks', 'Dandelion Clocks', 'zone_dandelion_clocks'),
    plant('zone_meadow_flowers', 'Meadow Flowers', 'zone_meadow_flowers'),
    plant('zone_dead_weeds', 'Dead Weeds', 'zone_dead_weeds_lower', texUpper='zone_dead_weeds_upper', double=True),
    plant('zone_rebar', 'Rebar', 'zone_rebar', 'metal'),
    dict(plant('zone_barbed_wire', 'Barbed Wire', 'zone_barbed_wire', 'metal'), replaceable=False, hardness=1.5, needsSupport=False),
    # props (data/shapes.json models)
    model('zone_debris', 'Debris', 'zone_debris', 'zone_debris', solid=False, sound='gravel', hard=.2, facing=True),
    model('zone_barrel', 'Rusty Barrel', 'zone_barrel', 'zone_barrel_side'),
    model('zone_barrel_blue', 'Blue Barrel', 'zone_barrel_blue', 'zone_barrel_blue'),
    # a wooden box (STALKER): no lid to open, smash it (melee, a bullet) for what's inside, if anything (sim/zone_crates.cpp)
    model('zone_crate', 'Wooden Crate', 'zone_crate', 'zone_crate', sound='wood', hard=.5, zoneBreak=True),
    model('zone_tyres', 'Tyres', 'zone_tyres', 'zone_tyre', sound='wool', hard=.8),
    model('zone_sandbags', 'Sandbags', 'zone_sandbags', 'zone_sandbag', sound='sand', hard=1.5, facing=True),
    model('zone_barrier', 'Concrete Barrier', 'zone_barrier', 'zone_barrier', sound='stone', hard=4, facing=True),
    model('zone_pipe', 'Pipe', 'zone_pipe', 'zone_pipe', facing=True),
    model('zone_heat_pipe', 'Heating Main', 'zone_heat_pipe', 'zone_heat_pipe', facing=True),
    model('zone_heat_pipe_support', 'Heating Main', 'zone_heat_pipe_support', 'zone_heat_pipe', facing=True),
    model('zone_radiator', 'Radiator', 'zone_radiator', 'zone_radiator', facing=True),
    model('zone_bed_frame', 'Bed Frame', 'zone_bed_frame', 'zone_bed_metal', facing=True, bed=True),   # two blocks long: meta bit 4 = the head half (shape zone_bed_frame_head)
    model('zone_pole', 'Power Pole', 'zone_pole', 'zone_pole_wood', sound='wood', hard=2),
    model('zone_pole_top', 'Power Pole', 'zone_pole_top', 'zone_pole_wood', sound='wood', hard=2, facing=True),
    model('zone_lamp_post', 'Lamp Post', 'zone_lamp_post', 'zone_metal_post', hard=3),
    model('zone_lamp_head', 'Street Lamp', 'zone_lamp_head', 'zone_metal_post', hard=3, facing=True),
    model('zone_fallen_trunk', 'Fallen Trunk', 'zone_fallen_trunk', 'zone_dead_bark', sound='wood', hard=1.6, facing=True),
    model('zone_sign_rad', 'Radiation Sign', 'zone_sign_rad', 'zone_sign_rad', facing=True),
    # loot furniture (gen/zone_loot.cpp places them; Use opens the loot screen, ui/zone_inventory.cpp ZoneLoot). Two-tall ones are
    # a base (the container) and a `furnTop` half above it that hands Use on to the base (sim/controller.cpp)
    *[model(k, n, k, tex, sound=snd, hard=2, facing=True, container=c, openSound=o, **kw) for (k, n, tex, snd, c, o, kw) in (
        ('zone_wardrobe', 'Wardrobe', 'zone_veneer', 'wood', 18, 'zone_open_wood', {'tall': True}),
        ('zone_fridge', 'Fridge', 'zone_enamel', 'metal', 12, 'zone_open_fridge', {'tall': True}),
        ('zone_locker', 'Metal Locker', 'zone_locker_side', 'metal', 12, 'zone_open_metal', {'tall': True}),
        ('zone_desk', 'Office Desk', 'zone_veneer', 'wood', 9, 'zone_open_drawer', {}),
        ('zone_cupboard', 'Kitchen Cupboard', 'zone_veneer', 'wood', 9, 'zone_open_wood', {}),
        ('zone_bedside', 'Bedside Cabinet', 'zone_veneer', 'wood', 6, 'zone_open_drawer', {}),
        ('zone_filing', 'Filing Cabinet', 'zone_locker_side', 'metal', 9, 'zone_open_drawer', {}),
        ('zone_toolbox', 'Toolbox', 'zone_toolbox', 'metal', 6, 'zone_open_metal', {}),
        ('zone_weapon_case', 'Weapon Case', 'zone_case_green', 'wood', 9, 'zone_open_case', {}),
        ('zone_ammo_box', 'Ammo Box', 'zone_case_green', 'wood', 6, 'zone_open_case', {}),
        ('zone_first_aid', 'First-aid Box', 'zone_enamel', 'metal', 6, 'zone_open_metal', {}))],
    *[model(k + '_top', n, k + '_top', tex, sound=snd, hard=2, facing=True, furnTop=True) for (k, n, tex, snd) in (
        ('zone_wardrobe', 'Wardrobe', 'zone_veneer', 'wood'), ('zone_fridge', 'Fridge', 'zone_enamel', 'metal'), ('zone_locker', 'Metal Locker', 'zone_locker_side', 'metal'))],
    # alpha.9: lights. Lit where a base has power (its generator), dead (_off) or stuttering (_flicker: no light of its own, a
    # flickering point light from render/env_layers.cpp) in the abandoned places; gen/zone_world_blocks.cpp picks them for the
    # user's torches, glowstone and lamps. Wall/ceiling ones face like the loot furniture (meta 0: the mount at +z)
    *[model(k + sfx, n + nm, k + ('_off' if sfx == '_off' else ''), 'zone_metal_post', solid=False, hard=1, facing=k != 'zone_lamp_hanging',
            light=(0 if sfx else lv), replaceable=False, **({'flicker': True} if sfx == '_flicker' else {}))
      for (k, n, lv, fl) in (('zone_lamp_cage', 'Caged Lamp', 14, True), ('zone_lamp_hanging', 'Hanging Bulb', 14, True), ('zone_lamp_tube', 'Fluorescent Tube', 15, True),
                            ('zone_lamp_bunker', 'Bulkhead Lamp', 14, False), ('zone_floodlight', 'Floodlight', 15, False))
      for (sfx, nm) in (('', ''), ('_off', ' (dead)'), ('_flicker', ' (flickering)')) if fl or sfx != '_flicker'],
    cube('zone_lamp_panel', 'Ceiling Light', 'zone_lamp_panel', 'glass', 1.0, None, light=15),
    cube('zone_lamp_panel_off', 'Ceiling Light (dead)', 'zone_lamp_panel_off', 'glass', 1.0, None),
    cube('zone_lamp_panel_flicker', 'Ceiling Light (flickering)', 'zone_lamp_panel', 'glass', 1.0, None, flicker=True),
    model('zone_fire_barrel', 'Fire Barrel', 'zone_fire_barrel', 'zone_barrel_side', light=14, emit='campfire'),   # smoke and flames like a camp fire
    # alpha.9: fences that join up (panes): the village's planks, sheet metal, the Soviet concrete panels (2 tall where the user had a wall)
    *[dict(key=k, name=n, render=SHAPE, shapeKind=PANE, solid=True, opaque=False, hardness=h, tool=tl, sound=snd, tex=tx, drop=None)
      for (k, n, tx, snd, h, tl) in (('zone_fence_planks', 'Plank Fence', 'zone_fence_planks', 'wood', 1.5, 'axe'),
                                     ('zone_fence_planks_broken', 'Broken Plank Fence', 'zone_fence_planks_broken', 'wood', 1.2, 'axe'),
                                     ('zone_fence_corrugated', 'Sheet Metal Fence', 'zone_fence_corrugated', 'metal', 2.5, 'pickaxe'),
                                     ('zone_fence_concrete', 'Concrete Fence', 'zone_fence_panel', 'stone', 4, 'pickaxe'))],
    cube('zone_junk', 'Scrap Heap', 'zone_junk', 'gravel', 1.0, 'shovel'),
]

def B(b, side, end=None, **kw):
    d = {'b': b, 'side': side}
    if end: d['end'] = end
    d.update(kw); return d
def drum(tile):
    return [B([3, 0, 4.5, 13, 14, 11.5], tile, 'zone_barrel_top'), B([4.5, 0, 3, 11.5, 14, 13], tile, 'zone_barrel_top')]
def tyre(dx, y0, y1):
    return [B([2 + dx, y0, 3.5, 14 + dx, y1, 12.5], 'zone_tyre', 'zone_tyre_top'), B([3.5 + dx, y0, 2, 12.5 + dx, y1, 14], 'zone_tyre', 'zone_tyre_top')]
def FR(b, body, front, **kw): return B(b, body, body, z=front, fit=True, **kw)   # a box with its front picture stretched over the z faces
HEAT = B([0, 3, 3, 16, 13, 13], 'zone_heat_pipe', 'zone_heat_pipe', x='zone_pipe')
SHAPES = {
    'zone_debris': [B([0, 0, 0, 16, .5, 16], 'zone_debris'), B([2, 0, 3, 6, 2, 6.5], 'zone_paving'), B([9, 0, 9, 12.5, 1.5, 12], 'zone_brick_ruined'),
                    B([11, 0, 2, 13.5, 1, 4], 'zone_panel'), B([4, 0, 11, 6.5, 1.2, 13.5], 'zone_paving'), B([13, 0, 12, 15, .8, 15], 'zone_brick_ruined')],
    'zone_barrel': drum('zone_barrel_side'),
    'zone_barrel_blue': drum('zone_barrel_blue'),
    'zone_crate': [B([1, 0, 1, 15, 16, 15], 'zone_crate', 'zone_crate', x='zone_crate', z='zone_crate', fit=True)],   # full height: they stack
    'zone_tyres': tyre(0, 0, 4.5) + tyre(.6, 4.5, 9) + tyre(-.3, 9, 13.5),
    'zone_sandbags': [B(b, 'zone_sandbag', 'zone_sandbag') for b in ([0, 0, 1, 8, 5, 15], [8, 0, 1, 16, 5, 15], [0, 5, 1.5, 4, 10, 14.5], [4, 5, 1.5, 12, 10, 14.5],
                                                                     [12, 5, 1.5, 16, 10, 14.5], [0, 10, 2, 8, 15, 14], [8, 10, 2, 16, 15, 14])],
    'zone_barrier': [B(b, 'zone_barrier', 'zone_barrier') for b in ([0, 0, 2.5, 16, 3, 13.5], [0, 3, 4, 16, 10, 12], [0, 10, 5, 16, 13, 11])],
    'zone_pipe': [B([0, 4, 5, 16, 10, 11], 'zone_pipe', 'zone_pipe'), B([0, 3.5, 4.5, 1, 10.5, 11.5], 'zone_metal_post', 'zone_metal_post')],
    'zone_heat_pipe': [HEAT],
    'zone_heat_pipe_support': [HEAT, B([5, 0, 4, 11, 3, 12], 'zone_panel', 'zone_panel')],
    'zone_radiator': [B([1.5 + 2 * i, 1, 12, 3 + 2 * i, 11, 15], 'zone_radiator', 'zone_radiator') for i in range(7)] +
                     [B([2, 0, 13, 3, 1, 14], 'zone_radiator'), B([13, 0, 13, 14, 1, 14], 'zone_radiator')],
    # two blocks long, modelled with the head to the north (-z): the foot half, and the head half (_head)
    'zone_bed_frame': [B(b, 'zone_metal_post', 'zone_metal_post') for b in ([.5, 0, 14, 1.5, 5, 15], [14.5, 0, 14, 15.5, 5, 15], [.5, 4, 0, 1.5, 5, 15], [14.5, 4, 0, 15.5, 5, 15])] +
                      [B([1, 5, 0, 15, 7, 14.5], 'zone_mattress', 'zone_mattress'), B([1, 0, 15, 15, 9, 16], 'zone_bed_metal', tv=-7)],
    'zone_bed_frame_head': [B(b, 'zone_metal_post', 'zone_metal_post') for b in ([.5, 0, 1, 1.5, 5, 2], [14.5, 0, 1, 15.5, 5, 2], [.5, 4, 1, 1.5, 5, 16], [14.5, 4, 1, 15.5, 5, 16])] +
                           [B([1, 5, 1.5, 15, 7, 16], 'zone_mattress', 'zone_mattress'), B([1, 0, 0, 15, 12, 1], 'zone_bed_metal', tv=-4),
                            B([2.5, 7, 2.5, 13.5, 8.5, 6.5], 'zone_mattress', 'zone_mattress', nocol=True)],
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
    # loot furniture, facing 0: back to the wall at +z, the front (the `z` picture, stretched over it) toward -z; 1 px = 1/16 m
    'zone_wardrobe': [FR([0.5, 0, 6, 15.5, 16, 16], 'zone_veneer', 'zone_wardrobe_lo')],
    'zone_wardrobe_top': [FR([0.5, 0, 6, 15.5, 13, 16], 'zone_veneer', 'zone_wardrobe_hi'), B([0, 13, 5.5, 16, 14.5, 16], 'zone_veneer', 'zone_veneer')],
    'zone_fridge': [FR([3, 0, 6, 13, 16, 16], 'zone_enamel', 'zone_fridge_lo'), B([10.4, 11, 5.2, 11.4, 16, 6], 'zone_metal_post', 'zone_metal_post', nocol=True)],
    'zone_fridge_top': [FR([3, 0, 6, 13, 8, 16], 'zone_enamel', 'zone_fridge_hi'), B([3.5, 8, 6.5, 12.5, 8.8, 15.5], 'zone_enamel', 'zone_enamel'),
                        B([10.4, 0, 5.2, 11.4, 3, 6], 'zone_metal_post', 'zone_metal_post', nocol=True)],
    'zone_locker': [FR([3, 0, 7, 13, 16, 16], 'zone_locker_side', 'zone_locker_lo')],
    'zone_locker_top': [FR([3, 0, 7, 13, 13, 16], 'zone_locker_side', 'zone_locker_hi'), B([2.8, 13, 6.8, 13.2, 13.6, 16], 'zone_locker_side', 'zone_locker_side')],
    'zone_desk': [B([0, 11, 4, 16, 12.5, 16], 'zone_veneer', 'zone_veneer'), FR([9, 0, 4.5, 15.5, 11, 15.5], 'zone_veneer', 'zone_drawers'),
                  B([0.5, 0, 4.5, 1.5, 11, 15.5], 'zone_veneer', 'zone_veneer'), B([1.5, 4, 14.5, 9, 11, 15.5], 'zone_veneer', 'zone_veneer')],
    'zone_cupboard': [FR([1, 0, 5, 15, 15, 16], 'zone_veneer', 'zone_cupboard_front'), B([0.5, 15, 4.5, 15.5, 16, 16], 'zone_tiles_dirty', 'zone_tiles_dirty')],
    'zone_bedside': [FR([3, 0, 7, 13, 9, 16], 'zone_veneer', 'zone_drawers'), B([2.5, 9, 6.5, 13.5, 10, 16], 'zone_veneer', 'zone_veneer')],
    'zone_filing': [FR([3, 0, 6, 13, 16, 16], 'zone_locker_side', 'zone_filing_front')],
    'zone_toolbox': [B([4, 0, 5.5, 12, 3.5, 10.5], 'zone_toolbox', 'zone_toolbox'), B([3.8, 3.5, 5.3, 12.2, 4.2, 10.7], 'zone_toolbox', 'zone_toolbox'),
                     B([6, 4.2, 7.6, 6.6, 5.6, 8.4], 'zone_metal_post', nocol=True), B([9.4, 4.2, 7.6, 10, 5.6, 8.4], 'zone_metal_post', nocol=True),
                     B([6, 5.2, 7.6, 10, 5.8, 8.4], 'zone_metal_post', nocol=True)],
    'zone_weapon_case': [B([0, 0, 4, 16, 4.5, 12], 'zone_case_green', 'zone_case_green'), B([1, 0, 3.8, 2, 4.7, 12.2], 'zone_metal_post', 'zone_metal_post'),
                         B([14, 0, 3.8, 15, 4.7, 12.2], 'zone_metal_post', 'zone_metal_post')],
    'zone_ammo_box': [B([4, 0, 5, 12, 4, 11], 'zone_case_green', 'zone_case_green'), B([3.8, 3.4, 4.8, 12.2, 4.4, 11.2], 'zone_metal_post', 'zone_case_green'),
                      B([7, 4.4, 7.5, 9, 5, 8.5], 'zone_metal_post', nocol=True)],
    'zone_first_aid': [FR([4, 3, 13, 12, 11, 16], 'zone_enamel', 'zone_first_aid', nocol=True)],
    'zone_fire_barrel': drum('zone_barrel_side') + [B([3.5, 13, 5, 12.5, 13.7, 11], 'zone_embers', 'zone_embers', nocol=True), B([5, 13, 3.5, 11, 13.7, 12.5], 'zone_embers', 'zone_embers', nocol=True)],
}
def lamps(g, sfx):
    M = 'zone_metal_post'
    return {
        'zone_lamp_cage' + sfx: [B([5.5, 5, 15, 10.5, 11, 16], M, M), B([7.5, 7.5, 12, 8.5, 8.5, 15], M, M), B([6.5, 6, 8, 9.5, 10, 12], g, g),
                                 B([6, 5.5, 7.5, 6.4, 10.5, 12.5], M, M, nocol=True), B([9.6, 5.5, 7.5, 10, 10.5, 12.5], M, M, nocol=True),
                                 B([6, 10.1, 7.5, 10, 10.5, 12.5], M, M, nocol=True), B([6, 5.5, 7.5, 10, 5.9, 12.5], M, M, nocol=True)],
        'zone_lamp_hanging' + sfx: [B([7.8, 9, 7.8, 8.2, 16, 8.2], 'zone_cable', 'zone_cable', nocol=True), B([7, 7.5, 7, 9, 9, 9], M, M),
                                    B([6.8, 4, 6.8, 9.2, 7.5, 9.2], g, g), B([5, 8.5, 5, 11, 9.3, 11], 'zone_enamel', 'zone_enamel', nocol=True)],
        'zone_lamp_tube' + sfx: [B([1, 14.5, 5.5, 15, 16, 10.5], 'zone_enamel', 'zone_enamel'), B([1.5, 13.4, 6.5, 14.5, 14.5, 9.5], g, g)],
        'zone_lamp_bunker' + sfx: [B([4, 4, 14.5, 12, 12, 16], 'zone_steel_green', 'zone_steel_green'), B([5, 5, 11.5, 11, 11, 14.5], g, g),
                                   B([4.5, 7.6, 11, 11.5, 8.4, 15], M, M, nocol=True), B([7.6, 4.5, 11, 8.4, 11.5, 15], M, M, nocol=True)],
        'zone_floodlight' + sfx: [B([7, 0, 9, 9, 3, 11], M, M), B([4, 3, 8, 12, 4, 12], M, M), B([3, 4, 6, 13, 12, 14], 'zone_lamp_head', 'zone_lamp_head'),
                                  B([3.5, 4.5, 5.4, 12.5, 11.5, 6], g, g, nocol=True)],
    }
SHAPES.update(lamps('zone_lamp_glow', '')); SHAPES.update(lamps('zone_lamp_glass_off', '_off'))

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
