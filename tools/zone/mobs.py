#!/usr/bin/env python3
# tools/zone/mobs.py — Exclusion's Zone creatures: merges the z_* entries into data/mobs.json (replaces any earlier z_* entry,
# touches nothing else; same compact formatting as the file). Run from the repo root: python3 tools/zone/mobs.py
# What the engine does with them with NO C++ (sim/creature.cpp parseType + the hostile branch of Creature::tick):
#   * hostile + no "ai"      -> sees you within 24 blocks (40 alerted), paths to you at "speed", bites for "damage" every 1.1 s
#   * "family"               -> hurt one by the player alerts every same-family creature within 16 blocks (pack behaviour)
#   * "ai":"predator" + "stalker" + "angersWhenHurt" (z_shroud) -> creeps up at night while you look away (archetypeAI), fights once provoked
#   * "tint" [r,g,b,a]       -> colour wash over the model; "variantDefs" pick a stock coat/model per spawn, "held" shows an item in the hand
#   * "skin"                 -> a 64x64 player skin (data/textures/entity/zone/*.png) drawn as a voxel character (creature_layer drawSkinMob)
# What they do NOT get without C++ (see the report): natural spawning, ranged gunfire ("ranged"/"held" are data only), the leap/charge.
import json, os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SK = 'textures/entity/zone/z_npc_%s.png'
Z = {}

def mob(key, **kw): Z[key] = kw
def vd(model, w, label=None, **kw):
    d = {'model': model, 'w': w}
    if label: d['label'] = label
    d.update(kw); return d

# ---------------------------------------------------------------- mutants (all hostile; hp/damage on the wolf 8/2, ghoul 20/3, werewolf 40/6 scale)
mob('z_scab_dog', model='wolf_husky', label='a scab dog', hp=7, w=0.3, h=0.85, speed=4.2, run=6.4, xp=4, damage=2, hostile=True, pack=True,
    family='zone_dog', sound='wolf', tint=[0.5, 0.46, 0.38, 0.3],
    drops=[['mutant_meat', 0, 1, 0.5], ['dog_tail', 1, 1, 0.4]],
    variantDefs=[vd('wolf_husky', 3), vd('wolf_black', 1.5, 'a black scab dog'), vd('wolf_chestnut', 1, 'a mangy scab dog')])
mob('z_lurcher', model='wolf_leader', label='a lurcher', hp=18, w=0.36, h=1.0, speed=4.4, run=6.6, xp=14, damage=4, hostile=True, pack=True, knockResist=0.2,
    family='zone_dog', voice='werewolf', tint=[0.5, 0.4, 0.62, 0.3],
    drops=[['mutant_meat', 1, 2, 0.7], ['lurcher_tail', 1, 1, 0.5]])
mob('z_hogspawn', model='boar_scarred', label='a hogspawn', hp=45, w=0.5, h=1.0, speed=1.5, xp=14, damage=5, hostile=True, knockResist=0.5,
    family='zone_hog', voice='boar', tint=[0.74, 0.36, 0.36, 0.38],
    drops=[['mutant_meat', 1, 3, 0.9], ['hog_eye', 1, 1, 0.45]],
    variantDefs=[vd('boar_scarred', 2), vd('boar', 1, 'a bloated hogspawn')])
mob('z_tusker', model='boar_tusker', label='a tusker', hp=22, w=0.46, h=1.05, speed=3.6, run=6.5, xp=8, damage=4, hostile=True, rams=True, knockResist=0.25,
    family='zone_hog', voice='boar', tint=[0.38, 0.3, 0.24, 0.25],
    drops=[['mutant_meat', 1, 2, 0.8], ['boar_hoof', 1, 2, 0.5]])
mob('z_rad_rat', model='mouse', label='a rad rat', hp=3, w=0.15, h=0.22, speed=3.0, run=5.0, xp=1, damage=1, hostile=True, pack=True,
    family='zone_rat', sound='mouse', tint=[0.45, 0.62, 0.28, 0.32],
    drops=[['mutant_meat', 0, 1, 0.25]],
    variantDefs=[vd('mouse', 2), vd('mouse_white', 1, 'a pale rad rat')])
mob('z_crawler', model='ghoul_soldier', label='a crawler', hp=20, w=0.3, h=1.7, speed=3.4, xp=8, damage=3, hostile=True, climber=True, hopper=True,
    family='zone_crawler', voice='ghoul', tint=[0.4, 0.45, 0.38, 0.3],
    drops=[['crawler_foot', 1, 1, 0.45], ['mutant_meat', 0, 1, 0.3], ['cloth', 0, 1, 0.3], ['gasmask', 0, 1, 0.04]])
mob('z_shroud', model='werewolf_black', label='a shroud', ai='predator', hp=26, w=0.4, h=2.0, speed=2.6, run=5.5, xp=14, damage=5, knockResist=0.2,
    stalker=True, nocturnal=True, angersWhenHurt=True, family='zone_shroud', sound='bigcat', tint=[0.3, 0.26, 0.4, 0.4],
    drops=[['mutant_meat', 1, 2, 0.6], ['shroud_tentacle', 1, 2, 0.5]])
mob('z_husk', model='ghoul_traveller', label='a husk', hp=20, w=0.3, h=1.95, speed=1.6, xp=5, damage=3, hostile=True, undead=True,
    family='ghoul', voice='husk', tint=[0.5, 0.5, 0.42, 0.28],
    drops=[['rubles', 5, 40, 0.6], ['cloth', 0, 2, 0.5], ['ammo_9x18', 0, 6, 0.4], ['bandage', 0, 1, 0.2], ['documents', 0, 1, 0.1], ['mutant_meat', 0, 1, 0.15]])
mob('z_puppeteer', model='enderman', label='a puppeteer', hp=60, w=0.34, h=2.9, speed=2.4, xp=40, damage=5, hostile=True, knockResist=0.3, noDismember=True,
    family='zone_puppeteer', voice='enderman', tint=[0.55, 0.42, 0.68, 0.32],
    drops=[['mutant_meat', 2, 3, 1.0], ['puppeteer_hand', 1, 1, 0.6], ['psyblock', 0, 1, 0.15]])

# ---------------------------------------------------------------- people (skinned voxel characters; "model": "raider" is the biped rig under the skin)
def human(key, who, label, hp, held, drops, hostile=False, speed=2.6, ranged=True, xp=8, w=0.3, h=1.8, voice=None, **kw):
    e = dict(model='raider', skin=SK % who, label=label, hp=hp, w=w, h=h, speed=speed, xp=xp, damage=3, faction='z_' + who,
             voice=voice or ('raider' if hostile else 'villager'), noDismember=True, drops=drops)
    if hostile: e['hostile'] = True
    else: e['npc'] = True
    if ranged: e['ranged'] = True
    if not hostile: e['passiveXp'] = True
    e['variantDefs'] = [vd('raider', wt, lab, held=hd) if hd else vd('raider', wt, lab) for (hd, wt, lab) in held]
    e.update(kw); Z[key] = e

human('z_npc_drifter', 'drifter', 'a drifter', 20, [('pm', 3, None), ('toz34', 1.5, 'a drifter with a double-barrel'), ('sks', 1, 'a drifter with an SKS'), ('mp443', 1, None)],
      [['rubles', 10, 80, 0.8], ['ammo_9x18', 2, 10, 0.6], ['bandage', 0, 2, 0.4], ['canned_stew', 0, 1, 0.3], ['bread', 0, 1, 0.3], ['vodka', 0, 1, 0.2]])
human('z_npc_bandit', 'bandit', 'a bandit', 20, [('pm', 3, None), ('sawnoff', 2, 'a bandit with a sawn-off'), ('aks74u', 1, 'a bandit with a carbine'), ('fort12', 1, None)],
      [['rubles', 20, 150, 0.9], ['ammo_9x18', 4, 14, 0.7], ['ammo_12g_buck', 0, 6, 0.3], ['bandage', 0, 1, 0.3], ['vodka', 0, 1, 0.3], ['grenade_rgd5', 0, 1, 0.1]], hostile=True, speed=2.8, xp=9)
human('z_npc_army', 'army', 'a soldier', 26, [('ak74m', 3, None), ('an94', 0.8, 'an elite soldier'), ('pkm', 0.4, 'a machine-gunner'), ('svd', 0.4, 'a marksman')],
      [['rubles', 30, 120, 0.6], ['ammo_545', 6, 24, 0.8], ['medkit_army', 0, 1, 0.15], ['crackers', 0, 2, 0.4], ['canned_stew', 0, 1, 0.3], ['grenade_f1', 0, 1, 0.1]], xp=11)
human('z_npc_bastion', 'bastion', 'a Bastion trooper', 28, [('spas12', 2, None), ('saiga', 2, 'a Bastion shotgunner'), ('pkm', 0.5, 'a Bastion gunner')],
      [['rubles', 40, 180, 0.7], ['ammo_12g_buck', 2, 10, 0.7], ['ammo_12g_slug', 0, 4, 0.3], ['medkit', 0, 1, 0.2], ['canned_fish', 0, 2, 0.3]], hostile=True, speed=2.5, xp=13)
human('z_npc_opensky', 'opensky', 'an Open Sky guard', 20, [('g36', 2, None), ('mp5', 1.5, 'an Open Sky guard with an MP5'), ('m416', 1.5, None)],
      [['rubles', 30, 140, 0.7], ['ammo_556', 4, 18, 0.7], ['ammo_9x19', 0, 12, 0.4], ['antirad', 0, 1, 0.2], ['energy', 0, 1, 0.25], ['water', 0, 1, 0.3]], xp=10)
human('z_npc_merc', 'merc', 'a mercenary', 24, [('scarl', 2, None), ('m416', 2, None), ('mk14', 0.6, 'a mercenary marksman'), ('ump45', 1, 'a mercenary with a UMP')],
      [['rubles', 50, 220, 0.8], ['ammo_556', 6, 24, 0.8], ['ammo_45acp', 0, 12, 0.3], ['medkit', 0, 1, 0.25], ['energy', 0, 1, 0.3], ['repair_weapon', 0, 1, 0.06]], hostile=True, speed=2.8, xp=14)
human('z_npc_choir', 'choir', 'a Choir zealot', 24, [('akm', 2, None), ('sks', 1, 'a Choir zealot with an SKS'), ('knife', 1, 'a Choir blade-bearer')],
      [['rubles', 5, 60, 0.5], ['ammo_762x39', 4, 16, 0.6], ['psyblock', 0, 1, 0.2], ['bandage', 0, 2, 0.4], ['bread', 0, 1, 0.3]], xp=11, speed=2.4)
human('z_npc_scientist', 'scientist', 'a scientist', 14, [('pda', 2, None), ('detector_echo', 1, 'a scientist with a detector'), ('detector_veles', 0.3, 'a scientist with a Veles detector')],
      [['rubles', 20, 100, 0.6], ['electronics', 0, 2, 0.4], ['flash_drive', 0, 1, 0.25], ['documents', 0, 1, 0.3], ['antirad', 0, 1, 0.3]], ranged=False, speed=2.2, xp=4)
human('z_npc_trader', 'trader', 'a trader', 20, [('', 1, None)],
      [['rubles', 100, 400, 0.9], ['canned_stew', 0, 2, 0.5], ['vodka', 0, 2, 0.5], ['medkit', 0, 1, 0.3]], ranged=False, speed=2.0, xp=2, w=0.36, h=1.8)

def main():
    p = os.path.join(ROOT, 'data', 'mobs.json'); m = json.load(open(p))
    items = json.load(open(os.path.join(ROOT, 'data', 'items.json'))); models = json.load(open(os.path.join(ROOT, 'data', 'models.json')))
    bad = []
    for k, e in Z.items():
        for d in e.get('drops', []):
            if d[0] not in items: bad.append((k, 'drop', d[0]))
        for v in e.get('variantDefs', []):
            if v['model'] not in models: bad.append((k, 'model', v['model']))
            if v.get('held') and v['held'] not in items: bad.append((k, 'held', v['held']))
        if e['model'] not in models: bad.append((k, 'model', e['model']))
        if 'skin' in e and not os.path.exists(os.path.join(ROOT, 'data', e['skin'])): bad.append((k, 'skin', e['skin']))
    if bad: raise SystemExit('unknown ids: %r' % bad)
    for k in [k for k in m if k.startswith('z_')]: del m[k]
    m.update(Z)
    with open(p, 'w') as f: json.dump(m, f, separators=(',', ':'), ensure_ascii=False)
    print('mobs.json: %d z_ creatures (%d total)' % (len(Z), len(m)))

if __name__ == '__main__': main()
