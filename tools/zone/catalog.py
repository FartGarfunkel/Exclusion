#!/usr/bin/env python3
# tools/zone/catalog.py — Exclusion's item catalogue, the one place every Zone item is defined.
# Writes data/zone_items.json (what sim/zone_*.cpp reads: weight, price, weapon/armour/med/artifact numbers) and
# merges the plain item entries (name, stack, icon, armour slot) into data/items.json.
# Run: python3 tools/zone/catalog.py            (from the repo root)
# Weapon numbers follow Battle of Spades' game/weapons.js (damage per bullet out of 100 HP, rounds/min, magazine);
# sim/zone_guns.cpp scales damage to the engine's health and adds spread/recoil from the gun class.
import json, os, collections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
Z = collections.OrderedDict()

def add(id, name, cat, w, price, stack=1, **kw):
    e = {'name': name, 'cat': cat, 'w': w, 'price': price, 'stack': stack}
    e.update(kw); Z[id] = e

# ---------------- guns ----------------
# fits: which attachments each slot takes. Families keep it readable.
AK_OPT = ['kobra', 'pso1']; NATO_OPT = ['kobra', 'holo', 'acog']; ANY_OPT = AK_OPT + ['holo', 'acog']
PISTOL_UNDER = ['laser', 'light']; NATO_UNDER = ['vgrip', 'laser', 'light', 'm203']; AK_UNDER = ['gp25']
def gun(id, name, cls, cal, rpm, dmg, mag, w, price, fits=None, **kw):
    add(id, name, 'weapon', w, price, gun={'cls': cls, 'cal': cal, 'rpm': rpm, 'dmg': dmg, 'mag': mag, 'fits': fits or {}, **kw})

gun('pm', 'Makarov PM', 'pistol', '9x18', 300, 18, 8, 0.73, 300, {'muzzle': ['supp_pistol']})
gun('fort12', 'Fort-12', 'pistol', '9x18', 330, 19, 12, 0.8, 650, {'muzzle': ['supp_pistol'], 'under': PISTOL_UNDER})
gun('mp443', 'MP-443 Grach', 'pistol', '9x19', 400, 20, 18, 0.95, 1400, {'muzzle': ['supp_pistol'], 'under': PISTOL_UNDER})
gun('g17', 'G17', 'pistol', '9x19', 450, 20, 17, 0.7, 1600, {'muzzle': ['supp_pistol'], 'under': PISTOL_UNDER, 'optic': ['kobra']})
gun('m1911', 'M1911', 'pistol', '45acp', 350, 30, 8, 1.1, 1800, {'muzzle': ['supp_pistol'], 'under': PISTOL_UNDER})
gun('toz34', 'TOZ-34 Double Barrel', 'shotgun', '12g', 300, 15, 2, 3.2, 900, {}, pellets=9, reload=2.2, slowReload=False, auto=False)
gun('sawnoff', 'Sawn-off Shotgun', 'shotgun', '12g', 400, 13, 2, 2.1, 600, {}, pellets=9, reload=2.0, slowReload=False, auto=False, spreadMul=1.6)
gun('m870', '870 Pump', 'shotgun', '12g', 70, 17, 6, 3.6, 2200, {'under': ['light']})
gun('spas12', 'SPAS-12', 'shotgun', '12g', 80, 17, 8, 4.4, 3600, {'under': ['light']})
gun('saiga', 'Saiga-12K', 'shotgun', '12g', 300, 12, 7, 3.6, 5200, {'optic': ['kobra', 'holo'], 'mag': ['drum12']}, slowReload=False, reload=2.6)
gun('pp19', 'PP-19 Bizon', 'smg', '9x18', 700, 18, 64, 2.7, 2400, {'optic': ['kobra'], 'muzzle': ['supp_pistol']}, reload=2.8)
gun('mp5', 'MP5', 'smg', '9x19', 800, 21, 30, 2.5, 3000, {'optic': ['kobra', 'holo'], 'muzzle': ['supp_pistol'], 'under': PISTOL_UNDER, 'mag': ['mag_ext']})
gun('ump45', 'UMP-45', 'smg', '45acp', 600, 27, 25, 2.3, 3200, {'optic': ['kobra', 'holo'], 'muzzle': ['supp_pistol'], 'under': ['vgrip', 'laser', 'light']})
gun('aks74u', 'AKS-74U', 'carbine', '545', 650, 25, 30, 2.7, 3500, {'optic': AK_OPT, 'muzzle': ['supp_545'], 'mag': ['mag_ext']})
gun('ak74m', 'AK-74M', 'ar', '545', 650, 25, 30, 3.4, 6000, {'optic': AK_OPT + ['holo'], 'muzzle': ['supp_545', 'brake'], 'under': AK_UNDER, 'mag': ['mag_ext']})
gun('akm', 'AKM', 'ar', '762x39', 600, 30, 30, 3.6, 5000, {'optic': AK_OPT, 'muzzle': ['supp_762', 'brake'], 'under': AK_UNDER, 'mag': ['mag_ext']}, kick=1.15, spreadMul=1.15)
gun('an94', 'AN-94 Abakan', 'ar', '545', 600, 26, 30, 3.85, 9500, {'optic': AK_OPT + ['holo'], 'muzzle': ['supp_545'], 'under': AK_UNDER, 'mag': ['mag_ext']}, kick=0.75)
gun('groza', 'OTs-14 Groza', 'ar', '9x39', 700, 32, 20, 3.0, 11000, {'optic': AK_OPT, 'muzzle': ['supp_939'], 'under': AK_UNDER}, model='bullpup')
gun('asval', 'AS Val', 'ar', '9x39', 900, 26, 20, 2.5, 10000, {'optic': AK_OPT}, suppressedBuiltIn=True)
gun('vss', 'VSS Vintorez', 'dmr', '9x39', 400, 40, 10, 2.6, 12000, {'optic': AK_OPT}, suppressedBuiltIn=True, defaultOptic='pso1', auto=True)
gun('m416', 'M416', 'ar', '556', 750, 25, 30, 3.5, 9000, {'optic': NATO_OPT, 'muzzle': ['supp_556', 'brake'], 'under': NATO_UNDER, 'mag': ['mag_ext']}, kick=0.9)
gun('g36', 'G36C', 'carbine', '556', 750, 25, 30, 3.3, 8500, {'optic': NATO_OPT, 'muzzle': ['supp_556'], 'under': NATO_UNDER, 'mag': ['mag_ext']})
gun('l85a2', 'L85A2', 'ar', '556', 650, 25, 30, 3.8, 8000, {'optic': NATO_OPT, 'muzzle': ['supp_556'], 'under': NATO_UNDER}, model='bullpup', defaultOptic='acog', kick=0.85)
gun('scarl', 'SCAR-L', 'ar', '556', 625, 25, 30, 3.3, 11000, {'optic': NATO_OPT, 'muzzle': ['supp_556', 'brake'], 'under': NATO_UNDER, 'mag': ['mag_ext']}, kick=0.85)
gun('sks', 'SKS', 'dmr', '762x39', 270, 43, 10, 3.9, 4500, {'optic': ['pso1']}, kick=0.85)
gun('svd', 'SVD', 'dmr', '762x54', 200, 55, 10, 4.3, 14000, {'optic': ['pso1', 'scope8'], 'muzzle': ['supp_762']}, defaultOptic='pso1')
gun('mk14', 'Mk 14 EBR', 'dmr', '762x51', 240, 46, 20, 5.1, 18000, {'optic': NATO_OPT + ['scope8'], 'muzzle': ['supp_762'], 'under': ['vgrip', 'bipod'], 'mag': ['mag_ext']}, defaultOptic='acog')
gun('sv98', 'SV-98', 'sniper', '762x54', 50, 85, 10, 5.8, 16000, {'optic': ['pso1', 'scope8'], 'muzzle': ['supp_762']}, defaultOptic='scope8')
gun('m82', 'M82 .50', 'sniper', '50bmg', 66, 120, 5, 13.0, 40000, {'optic': ['scope8']}, defaultOptic='scope8', pierce=3, speedMul=0.92)
gun('rpk', 'RPK-74', 'lmg', '545', 600, 25, 45, 5.0, 9000, {'optic': AK_OPT, 'mag': ['mag_ext']}, reload=3.2)
gun('pkm', 'PKM', 'lmg', '762x54', 650, 34, 100, 7.5, 22000, {'optic': ['pso1']})
gun('rpg7', 'RPG-7', 'launcher', 'pg7', 30, 200, 1, 6.3, 25000, {'optic': ['pso1']}, reload=3.0, auto=False)
add('knife', 'Combat Knife', 'melee', 0.4, 400, melee={'dmg': 30, 'delay': 0.45}, desc='Also the only way to cut parts from a mutant.')

# ---------------- ammunition ----------------
def ammo(id, name, cal, w, price, stack, dmg=1.0, pen=1.0, wear=1.0, **kw):
    add(id, name, 'ammo', w, price, stack, ammo={'cal': cal, 'dmg': dmg, 'pen': pen, 'wear': wear, **kw})
ammo('ammo_9x18', '9x18 mm', '9x18', 0.008, 4, 100)
ammo('ammo_9x18_pbp', '9x18 mm AP', '9x18', 0.009, 9, 100, dmg=0.9, pen=1.6)
ammo('ammo_9x19', '9x19 mm', '9x19', 0.012, 6, 100)
ammo('ammo_45acp', '.45 ACP', '45acp', 0.015, 8, 100)
ammo('ammo_545', '5.45x39 mm', '545', 0.011, 8, 120)
ammo('ammo_545_ap', '5.45x39 mm AP', '545', 0.012, 16, 120, dmg=0.9, pen=1.6, wear=1.1)
ammo('ammo_556', '5.56x45 mm', '556', 0.012, 10, 120)
ammo('ammo_556_ap', '5.56x45 mm AP', '556', 0.013, 20, 120, dmg=0.9, pen=1.6, wear=1.1)
ammo('ammo_762x39', '7.62x39 mm', '762x39', 0.016, 9, 90)
ammo('ammo_762x39_ap', '7.62x39 mm AP', '762x39', 0.017, 18, 90, dmg=0.9, pen=1.6)
ammo('ammo_762x51', '7.62x51 mm', '762x51', 0.024, 18, 60)
ammo('ammo_762x54', '7.62x54R mm', '762x54', 0.022, 16, 60)
ammo('ammo_762x54_ap', '7.62x54R mm AP', '762x54', 0.023, 32, 60, dmg=0.9, pen=1.7)
ammo('ammo_9x39', '9x39 mm SP-5', '9x39', 0.023, 18, 60)
ammo('ammo_9x39_ap', '9x39 mm SP-6 AP', '9x39', 0.024, 30, 60, dmg=0.9, pen=1.7)
ammo('ammo_12g_buck', '12 gauge buckshot', '12g', 0.04, 12, 40)
ammo('ammo_12g_slug', '12 gauge slug', '12g', 0.04, 16, 40, pellets=1, dmg=4.2, spread=0.18, pen=1.4)
ammo('ammo_12g_dart', '12 gauge flechette', '12g', 0.04, 20, 40, dmg=0.85, spread=0.8, pen=1.5)
ammo('ammo_50bmg', '.50 BMG', '50bmg', 0.11, 80, 30, pen=2.5)
ammo('ammo_pg7', 'PG-7 rocket', 'pg7', 2.2, 2500, 4, blast=7)
ammo('vog25', 'VOG-25 grenade', 'vog', 0.25, 400, 10, blast=4)
ammo('m203_he', 'M433 40 mm grenade', 'm203', 0.23, 450, 10, blast=4)

# ---------------- attachments (Battle of Spades' mods) ----------------
def att(id, name, slot, w, price, mods, **kw):
    add(id, name, 'attach', w, price, att={'slot': slot, 'mods': mods, **kw})
att('kobra', 'Kobra red dot', 'optic', 0.25, 2200, {'adsZoom': 1.35}, reticle='dot')
att('holo', 'Holographic sight', 'optic', 0.32, 3500, {'adsZoom': 1.3}, reticle='holo')
att('pso1', 'PSO-1 scope (4x)', 'optic', 0.6, 4500, {'adsZoom': 2.6, 'adsSpreadMul': 0.78}, reticle='scope', scope=True)
att('acog', 'ACOG (4x)', 'optic', 0.45, 5500, {'adsZoom': 2.4, 'adsSpreadMul': 0.8}, reticle='acog', scope=True)
att('scope8', 'Rifle scope (8x)', 'optic', 0.8, 9000, {'adsZoom': 5.0, 'adsSpreadMul': 0.6}, reticle='scope', scope=True)
att('supp_pistol', '9 mm suppressor', 'muzzle', 0.2, 1500, {'rangeMul': 0.7, 'minDmgMul': 0.85, 'loud': 0.35}, hidden=True)
att('supp_545', '5.45 suppressor', 'muzzle', 0.45, 3000, {'rangeMul': 0.7, 'minDmgMul': 0.85, 'loud': 0.35}, hidden=True)
att('supp_556', '5.56 suppressor', 'muzzle', 0.45, 3200, {'rangeMul': 0.7, 'minDmgMul': 0.85, 'loud': 0.35}, hidden=True)
att('supp_762', '7.62 suppressor', 'muzzle', 0.55, 3600, {'rangeMul': 0.72, 'minDmgMul': 0.85, 'loud': 0.35}, hidden=True)
att('supp_939', '9x39 suppressor', 'muzzle', 0.4, 3000, {'rangeMul': 0.8, 'loud': 0.3}, hidden=True)
att('brake', 'Muzzle brake', 'muzzle', 0.15, 1200, {'recoilV': 0.7, 'loud': 1.2})
att('vgrip', 'Foregrip', 'under', 0.15, 900, {'recoilH': 0.6})
att('laser', 'Laser sight', 'under', 0.1, 1400, {'hipSpreadMul': 0.7}, laser=True)
att('light', 'Tactical light', 'under', 0.12, 700, {'hipSpreadMul': 0.9}, light=True)
att('bipod', 'Bipod', 'under', 0.4, 1000, {}, bipod=True)
att('gp25', 'GP-25 grenade launcher', 'under', 1.5, 6000, {}, launcher='vog')
att('m203', 'M203 grenade launcher', 'under', 1.4, 6500, {}, launcher='m203')
att('mag_ext', 'Extended magazine', 'mag', 0.3, 1800, {'clipMul': 1.5, 'reloadMul': 1.15})
att('drum12', '20-round drum', 'mag', 1.2, 3500, {'clipMul': 2.85, 'reloadMul': 1.3})

# ---------------- outfits and helmets ----------------
# resist: share of that harm taken away. belt: artifact slots. carry: extra kg.
def suit(id, name, w, price, bullet, burn, shock, chem, rad, psy, belt, carry=0, **kw):
    add(id, name, 'suit', w, price, suit={'bullet': bullet, 'burn': burn, 'shock': shock, 'chem': chem, 'rad': rad, 'psy': psy, 'belt': belt, 'carry': carry, **kw})
suit('jacket_leather', 'Leather jacket', 2.5, 1500, 0.06, 0.05, 0.05, 0.05, 0.05, 0.0, 1)
suit('suit_bandit', 'Gang overcoat', 3.0, 2500, 0.12, 0.05, 0.05, 0.05, 0.05, 0.0, 1)
suit('suit_drifter', 'Loner suit', 5.0, 8000, 0.2, 0.15, 0.15, 0.15, 0.2, 0.05, 2)
suit('suit_army', 'Army body armour', 8.0, 14000, 0.35, 0.1, 0.1, 0.1, 0.1, 0.0, 1)
suit('suit_merc', 'Contractor suit', 7.0, 16000, 0.3, 0.15, 0.15, 0.15, 0.15, 0.05, 2)
suit('suit_opensky', 'Freedom wind suit', 5.0, 18000, 0.25, 0.2, 0.2, 0.25, 0.3, 0.1, 3)
suit('suit_bastion', 'Duty armour', 10.0, 25000, 0.45, 0.2, 0.2, 0.2, 0.2, 0.05, 2)
suit('suit_choir', 'Monolith battle suit', 9.0, 30000, 0.45, 0.25, 0.25, 0.25, 0.3, 0.6, 3)
suit('suit_hazmat', 'Hazmat research suit', 9.0, 45000, 0.15, 0.45, 0.45, 0.5, 0.6, 0.3, 4)
suit('suit_sealed', 'Sealed anomaly suit', 8.0, 38000, 0.25, 0.4, 0.4, 0.45, 0.55, 0.35, 4)
suit('suit_assault', 'Army assault armour', 12.0, 32000, 0.5, 0.15, 0.15, 0.2, 0.2, 0.05, 2)
suit('suit_exo', 'Exoskeleton', 20.0, 90000, 0.6, 0.3, 0.3, 0.3, 0.3, 0.1, 3, carry=40, noSprint=True)
def helm(id, name, w, price, bullet, chem, rad, psy, **kw):
    add(id, name, 'helmet', w, price, helmet={'bullet': bullet, 'chem': chem, 'rad': rad, 'psy': psy, **kw})
helm('gasmask', 'Gas mask', 0.9, 2000, 0.0, 0.25, 0.15, 0.05, mask=True)
helm('helmet_tactical', 'Tactical helmet', 1.4, 3500, 0.25, 0.0, 0.0, 0.0)
helm('helmet_sphere', 'Sphere helmet', 1.8, 9000, 0.35, 0.1, 0.1, 0.15)
helm('helmet_psy', 'Psy helmet', 1.2, 15000, 0.05, 0.0, 0.0, 0.7)

# ---------------- medicine, food, drink ----------------
def med(id, name, w, price, stack, **kw): add(id, name, 'med', w, price, stack, use=kw)
med('bandage', 'Bandage', 0.05, 100, 10, bleed=-2.0, time=2.0)
med('medkit', 'Medkit', 0.25, 400, 5, heal=40, bleed=-1.0, time=3.0)
med('medkit_army', 'Army medkit', 0.3, 700, 5, heal=60, bleed=-2.5, time=3.0)
med('medkit_sci', 'Science medkit', 0.3, 900, 5, heal=40, rad=-40, time=3.0)
med('antirad', 'Anti-rad', 0.05, 450, 10, rad=-60, time=1.5)
med('psyblock', 'Psy-block', 0.05, 600, 10, psyGuard=120, psy=-30, time=1.5)
med('atlas', 'Atlas pills', 0.05, 900, 10, carryBoost=20, carryTime=300, time=1.5)
med('vodka', 'Kazak vodka', 0.5, 150, 5, rad=-15, drunk=60, water=5, time=2.5)
med('energy', 'Kick energy drink', 0.35, 120, 5, stamina=100, sleep=15, water=15, time=2.0)
def food(id, name, w, price, stack, **kw): add(id, name, 'food', w, price, stack, use=dict(time=3.0, **kw))
food('bread', 'Bread', 0.3, 60, 8, food=25)
food('sausage', 'Sausage', 0.4, 90, 8, food=30)
food('canned_stew', 'Canned stew', 0.45, 140, 8, food=45)
food('canned_fish', 'Canned sprats', 0.3, 110, 8, food=35, water=-5)
food('crackers', 'Army crackers', 0.2, 50, 10, food=20, water=-5)
food('water', 'Bottled water', 0.5, 50, 6, water=40)
food('mutant_meat', 'Raw mutant meat', 0.6, 40, 8, food=15, rad=12, sick=0.4)
food('mutant_meat_cooked', 'Roast mutant meat', 0.5, 120, 8, food=35, rad=3)

# ---------------- tools and gear ----------------
add('detector_echo', 'Echo detector', 'detector', 0.3, 1500, detector={'range': 12, 'mode': 'beep', 'tier': 1})
add('detector_bear', 'Bear detector', 'detector', 0.4, 6000, detector={'range': 22, 'mode': 'arrow', 'tier': 2})
add('detector_veles', 'Veles detector', 'detector', 0.5, 20000, detector={'range': 35, 'mode': 'screen', 'tier': 3})
add('bolt', 'Bolt', 'throw', 0.02, 0, 50, desc='Throw it ahead of you: anomalies go off on a bolt as well as on you.')
add('grenade_rgd5', 'RGD-5 grenade', 'grenade', 0.31, 300, 6, grenade={'blast': 3.5, 'fuse': 3.2})
add('grenade_f1', 'F1 grenade', 'grenade', 0.6, 450, 6, grenade={'blast': 4.5, 'fuse': 3.5})
add('toolkit_basic', 'Basic toolkit', 'toolkit', 1.5, 3000, toolkit={'tier': 1, 'cap': 70})
add('toolkit_adv', 'Advanced toolkit', 'toolkit', 2.0, 9000, toolkit={'tier': 2, 'cap': 85})
add('toolkit_expert', 'Expert toolkit', 'toolkit', 3.0, 25000, toolkit={'tier': 3, 'cap': 100})
add('repair_weapon', 'Gun repair kit', 'repair', 0.6, 1600, 5, repair={'kind': 'weapon', 'amount': 25, 'cap': 70})
add('repair_armor', 'Armour repair kit', 'repair', 0.8, 1800, 5, repair={'kind': 'suit', 'amount': 25, 'cap': 70})
add('headlamp', 'Head torch', 'gear', 0.3, 500, desc='Wear it in the LAMP slot, then press the torch key (L / d-pad up) to switch it on or off.')
add('pda', 'PDA', 'gear', 0.3, 800, desc='The map of the Zone, your contacts and the news.')
add('pda_stalker', "Stalker's PDA", 'gear', 0.3, 300, desc='Taken off a dead stalker. Use it: if he kept a stash, it goes on your map.')
add('rubles', 'Rubles', 'money', 0.0, 1, 100000, desc='Money. Goes straight into your wallet.')

# ---------------- salvage, parts, documents ----------------
for id, name, w, price in [('scrap_metal', 'Scrap metal', 0.5, 40), ('electronics', 'Electronics', 0.2, 150), ('cloth', 'Cloth', 0.2, 30),
                           ('springs', 'Springs', 0.1, 60), ('wires', 'Copper wire', 0.2, 50), ('lead_sheet', 'Lead sheet', 1.0, 200),
                           ('flash_drive', 'Flash drive', 0.05, 900), ('documents', 'Old documents', 0.2, 1500), ('gun_oil', 'Gun oil', 0.3, 150)]:
    add(id, name, 'part', w, price, 20)
for id, name, w, price in [('dog_tail', 'Scab dog tail', 0.3, 300), ('boar_hoof', 'Tusker hoof', 0.6, 500), ('hog_eye', 'Hogspawn eye', 0.2, 400),
                           ('lurcher_tail', 'Lurcher tail', 0.4, 900), ('shroud_tentacle', 'Shroud tentacles', 0.5, 2500),
                           ('crawler_foot', 'Crawler foot', 0.4, 1200), ('puppeteer_hand', 'Puppeteer hand', 0.8, 8000),
                           ('stoneback_plate', 'Stoneback plate', 1.6, 4500), ('hurler_gland', 'Hurler gland', 0.4, 6000), ('ridgecat_claw', 'Ridgecat claw', 0.3, 3500)]:
    add(id, name, 'trophy', w, price, 10)

# ---------------- artifacts ----------------
# effects per second while on the belt; rad is what it gives off (Sv/s scale, negative cleans)
def art(id, name, family, price, rad, tier, **fx):
    add(id, name, 'artifact', 0.5, price, artifact={'family': family, 'rad': rad, 'tier': tier, 'fx': fx})
art('moth_wing', 'Moth Wing', 'gravity', 4000, 0.02, 1, carry=5)
art('gravel_heart', 'Gravel Heart', 'gravity', 9000, 0.04, 2, carry=10)
art('hollow_star', 'Hollow Star', 'gravity', 30000, 0.02, 4, carry=6, regen=0.05)
art('ember_knot', 'Ember Knot', 'thermal', 3500, 0.02, 1, burn=0.25)
art('night_candle', 'Night Candle', 'thermal', 12000, -0.05, 3, burn=-0.1)
art('static_pearl', 'Static Pearl', 'electric', 3500, 0.02, 1, shock=0.25, stamina=0.2)
art('spindle', 'Spindle', 'electric', 6000, 0.02, 2, stamina=0.6)
art('rust_shell', 'Rust Shell', 'chemical', 3500, 0.02, 1, chem=0.25)
art('blood_coral', 'Blood Coral', 'chemical', 8000, 0.04, 2, bleedHeal=0.5)
art('iron_moss', 'Iron Moss', 'chemical', 9000, 0.04, 3, bullet=0.08)
art('glass_eye', 'Glass Eye', 'psy', 15000, 0.0, 3, psy=0.4)
art('mothers_tear', "Mother's Tear", 'psy', 40000, 0.06, 5, regen=0.15)
# (the anomaly overhaul's five: each family's runs, sim/zone_anomaly.cpp artifactFor)
art('kiln_glass', 'Kiln Glass', 'thermal', 7000, 0.03, 2, burn=0.35, stamina=-0.1)
art('storm_eye', 'Storm Eye', 'electric', 26000, 0.04, 4, shock=0.45, stamina=0.4)
art('shard_lens', 'Shard Lens', 'gravity', 15000, 0.03, 3, bullet=0.12, carry=-3)
art('bog_pearl', 'Bog Pearl', 'chemical', 24000, -0.04, 4, chem=0.35, bleedHeal=0.4)
art('sleepers_seed', "Sleeper's Seed", 'psy', 6500, 0.01, 2, psy=0.2, stamina=-0.15)

os.makedirs(os.path.join(ROOT, 'data'), exist_ok=True)
with open(os.path.join(ROOT, 'data', 'zone_items.json'), 'w') as f:
    json.dump(Z, f, indent=1)

# merge into data/items.json: plain entries the registry understands (the Zone fields stay in zone_items.json)
items_path = os.path.join(ROOT, 'data', 'items.json')
items = json.load(open(items_path))
ARMOR_SLOT = {'helmet': 0, 'suit': 1}
for id, e in Z.items():
    it = {'key': id, 'name': e['name'], 'maxStack': e['stack'], 'icon': 'zone_' + id, 'zone': True}
    if e['cat'] in ARMOR_SLOT:
        s = e.get('suit') or e.get('helmet')
        it['armor'] = {'slot': ARMOR_SLOT[e['cat']], 'points': max(1, round(s['bullet'] * 20)), 'durability': 600 if e['cat'] == 'suit' else 300, 'fireResist': 0}
    if e['cat'] in ('weapon', 'melee'):
        it['tool'] = {'type': 'gun' if e['cat'] == 'weapon' else 'sword', 'tier': 0, 'speed': 1, 'damage': 1 if e['cat'] == 'weapon' else 5, 'durability': 1000}
    if 'desc' in e: it['desc'] = e['desc']
    items[id] = it
json.dump(items, open(items_path, 'w'), separators=(',', ':'))
print(len(Z), 'zone items')
