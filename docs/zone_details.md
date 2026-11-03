# Zone details: the plan (phase 1 → phase 2 build list)

This document lists the details the Zone still lacks, compared with the S.T.A.L.K.E.R. trilogy (SoC / CS / CoP), Anomaly (An), Misery (Mis),
STALCRAFT (SC) and Stalzone. For every place it gives the set pieces, where each one goes on the user's world, and the kit needed.
Phase 2 builds what is listed here and nothing beyond it.

## 0. Conventions

- **Coordinates** are world x,z (1 block = 1 m). **N = −z**. A facing is the direction the item's FRONT points (a vehicle's nose, a
  wagon's coupler toward the train's head): N, NE, E, … or "E-W" for things that lie along a line.
- **Ownership tags.** **[U]** is on or in the user's own building or geometry. A user building is named by its coarse.json site
  `(x,z wxd hH)`, where x,z is the NW corner. **[B]** is absent from the user's world, so our builder adds it on clear ground.
  **[?]** marks an assignment to a user structure that phase 2 should confirm by looking at it (`/tp`, smoke shot) before dressing it.
- **Snapping (phase 2).** Each item snaps within ±8 m of its anchor. `road` means the traced road's centreline or verge (coarse
  roads). `crest` means the top of an embankment. `floor` means the user's floor run. `ground` means `groundAt`. `water` means
  sitting in the water at a draught of 0.5–1.5 m. If the spot is blocked, the item moves along its line; it is never dropped
  without a log line. Keep 3 m clear of doors, ladders and stairs (`keepPassagesClear` already does this).
- **Lie states** (applied to every vehicle and wagon):
  - `ok`: rusted but upright.
  - `burnt`: black and bare metal, no glass or tyres.
  - `stripped`: no wheels, sitting on its hubs or on blocks.
  - `side-L` / `side-R`: rolled 90°.
  - `roof`: rolled 180°.
  - `ditch`: roll 10–25° with the nose pitched down.
  - `sunk`: buried 0.5–1.2 m in mud, water or sand.
  - `stack`: sitting on top of another wreck.
  - `jack`: a wagon skewed 15–35° off the rails.
- **Data.** Phase 2 writes everything below into `data/zone_details.json` and reads it in a new `src/gen/zone_details.cpp`, hooked
  after the plan. There are three kinds of entry:
  - point: `{"k":kit,"x","z","f":deg,"st":state,"roll","pitch","snap","v":variant}`
  - line: `{"k":"rail"|"pylon_line"|…,"pts":[[x,z]…],"snap"}`
  - fill: `{"k":"rows"|"scatter","box":[x0,z0,x1,z1],"set":[kits],"n"}`

  The existing random `wreck()` calls in zone_plan stay as filler, but they must not land inside the boxes listed here.

## 1. Kit: what has to be made

### 1a. Vehicles and big props (NEW): prop models, not stacks of blocks

Each vehicle is a static prop model built from the zone_model_kit primitives (boxes, cylinders, wedges) at real size. A `zone_prop`
block entity at the anchor makes it persist and stream like `item_pile`. A RenderLayer draws it. Collision comes from a list of
AABBs, plus hidden `zone_prop_solid` cells so walking and bullets work. Optional loot points (glovebox, truck bed, wagon) are
linked hidden containers that open in ZoneLoot. Every vehicle supports every lie state from section 0. Rust tints vary by seed
(green, olive, blue, red, grey).

| id | look (one line) | size m (L×W×H) |
|---|---|---|
| car_zhiguli | VAZ-2101 boxy sedan: flat bonnet, round headlights, chrome bumpers | 4.1×1.6×1.4 |
| car_moskvich | Moskvich-412: sedan with a slight wedge and long boot | 4.2×1.6×1.5 |
| car_zaporozhets | ZAZ-968: tiny 2-door with the engine at the back and side air "ears" | 3.7×1.5×1.4 |
| car_volga | GAZ-24: long sedan with a big chrome grille | 4.7×1.8×1.5 |
| uaz_469 | army jeep: canvas or hard top, spare wheel on the tailgate | 4.0×1.8×2.0 |
| uaz_452 | "bukhanka" loaf-shaped van (army green, or the ambulance variant with a red cross) | 4.4×1.9×2.1 |
| raf_2203 | RAF ambulance minibus: white with a red stripe and roof lamp | 4.9×1.9×2.0 |
| zil_130 | flat-nosed-bonnet lorry with a wooden drop-side bed (variants: box van, cistern, dump) | 6.7×2.5×2.4 |
| zil_131 | 6×6 army lorry with a canvas tilt over the bed (variant: KUNG box body) | 7.0×2.5×2.9 |
| ural_4320 | 6×6 army lorry with a long bonnet and canvas tilt | 7.4×2.5×2.9 |
| kraz_255 | huge 6×6 with a long snout (variants: log carrier, dump) | 8.6×2.8×3.0 |
| gaz_53 | small farm lorry with a wooden bed | 6.4×2.4×2.2 |
| fire_zil | red ZIL-131 fire engine with a ladder on the roof and side lockers | 7.6×2.5×3.0 |
| tanker_zil | ZIL with an oval fuel tank and "ОГНЕОПАСНО" ("flammable") on the side | 7.0×2.5×2.6 |
| bus_laz695 | LAZ-695 city bus: rounded rear, roof air scoop, 2 doors | 9.2×2.5×3.0 |
| bus_paz672 | PAZ-672 small rural bus with a rounded front | 7.2×2.4×2.9 |
| bus_ikarus | Ikarus-280 articulated bus with a bellows joint | 16.5×2.5×3.1 |
| trolleybus | ZiU-9: roof poles hanging on their ropes | 11.8×2.5×3.4 |
| btr_70 | 8-wheel APC with a boat-shaped hull and small turret | 7.5×2.8×2.3 |
| btr_80 | as btr_70 with side doors and a taller turret | 7.6×2.9×2.4 |
| brdm_2 | 4-wheel scout car with a small turret | 5.8×2.4×2.3 |
| bmp_2 | tracked IFV with a low wedge front and a turret with a 30 mm gun | 6.7×3.1×2.5 |
| t_72 | tank: road wheels, side skirts, long gun (variants: turret blown off, `hull`) | 9.5×3.6×2.2 |
| imr_2 | engineering tank: crane arm and dozer blade | 9.6×3.6×3.7 |
| mi_8 | transport helicopter: round nose, 5 drooping rotor blades, tail boom (variants: broken tail, no rotors) | 18×2.5×4.8 |
| mi_24 | gunship "Skat": stepped tandem canopy, stub wings (variants: crash = broken in two, burnt, ground scar) | 17.5×6.5×4 |
| tractor_mtz | Belarus MTZ-80: big rear wheels and a cab (+ `trailer` 2-axle cart) | 3.8×1.9×2.8 |
| tractor_t40 | small tractor without a cab | 3.1×1.6×2.4 |
| combine | Niva combine harvester with the header attached | 7×4×3.8 |
| dozer_t130 | tracked bulldozer with a blade | 5.5×3.2×3.1 |
| excavator | tracked excavator: boom and bucket, rests with the bucket on the ground | 9×3×3.5 (boom 7) |
| crane_truck | KS-3577 truck crane: lattice or telescopic boom, raised or stowed | 10×2.5×3.4 |
| tower_crane | KB-type construction crane: lattice mast, jib, counter-jib, cab, hook (built from blocks: §1b truss) | 30–45 tall |
| gantry_crane | overhead travelling crane: two girders on rails, hoist trolley, hook (indoor and yard) | span 12–30 |
| portal_crane | river port crane: portal legs on rails, rotating cab, long jib (built from blocks) | 25 tall |
| loco_tem2 | diesel shunter: long hood, end cab, red and green livery with yellow stripes | 16.9×3.1×4.6 |
| loco_chme3 | ChME3 shunter: centre cab, sloping hoods | 17.2×3.1×4.6 |
| boxcar | covered goods wagon: brown planks and ribs, sliding door (open or closed) | 14.7×3.2×4.7 |
| gondola | open wagon of high steel sides (empty, scrap, coal or ore) | 13.9×3.2×3.5 |
| tankcar | cistern wagon on a frame with a dome and ladder (black or grey) | 12×3×4.4 |
| hopper | ore wagon with a sloped bottom (CoP's "ore wagons") | 14×3.2×3.9 |
| flatcar | platform wagon (empty, with a crate, or with a military vehicle) | 14.6×3.2×1.5 |
| passcar | passenger carriage: green, windows in a row, end vestibules | 24×3.1×4.2 |
| ship_cargo | river cargo ship (Skadovsk / Shevchenko class): bow, deck, aft superstructure | 80×12×10 |
| dredger | floating dredge: bucket-chain ladder, gantry, cabin | 45×12×18 |
| tug | river tug: high wheelhouse, low stern | 22×6×7 |
| boat_row | wooden or tin rowing boat (upright, upside-down on the bank, or sunk) | 4×1.4×0.6 |
| boat_kazanka | aluminium "Kazanka" motor boat with an outboard motor | 4.6×1.6×0.9 |
| barge | (exists as bBarge; upgrade it to a prop or keep it) | 40×11 |

### 1b. NEW blocks (tools/zone/zone_blocks.py + paint_textures.py --kit)

| id | look |
|---|---|
| **rail and track** | |
| zone_rail | 2 rusty steel rails on rotting wooden sleepers over ballast (a flat-shaped block, 0.25 high); straight N-S / E-W plus a 45° diagonal |
| zone_rail_concrete | the same on grey concrete sleepers |
| zone_ballast | grey crushed stone with brown rust stains (the embankment surface) |
| zone_buffer_stop | the end-of-track stop: steel frame with two round buffers |
| zone_points_lever | a switch lever on a little white post with a weight |
| zone_rail_signal | lamp signal mast: striped post, black head with 2–3 dark or red lenses |
| zone_catenary_mast | concrete pole with a steel cantilever arm; the wires use `zone_cable` sagging or broken |
| zone_platform_edge | concrete platform-edge slab with a white painted line |
| zone_km_post | small white railway kilometre post |
| **scaffold, steel, cranes** | |
| zone_scaffold | tube-and-coupler pipe scaffolding frame (a cutout cube of verticals and ledgers) |
| zone_scaffold_deck | scaffold boards: grey planks on the frame, some missing |
| zone_grating | steel grating catwalk (a walkable slab, see-through cutout) |
| zone_handrail | 1 m steel tube railing (edge-shaped, like a fence) |
| zone_cage_ladder | steel ladder with a safety cage |
| zone_steel_stair | open steel stair (a stair shape with a grating texture) |
| zone_ibeam | rusty I-beam (logs-like axis orientation) |
| zone_truss | lattice truss (cutout) for crane masts and jibs, the Duga, pylons and bridges |
| zone_crane_hook | hook block on a chain |
| zone_chain | hanging chain |
| zone_crane_rail | rail on top of a beam (for gantry cranes) |
| **lights** (each has `_off` and `_flicker`, lit where `zonePowered`) | |
| zone_searchlight | big tripod or pole searchlight that casts a sweeping beam (a light-cone RenderLayer) |
| zone_yard_lamp | sodium lamp on a tall concrete pole with a bent arm (orange light) |
| zone_rail_lamp | railway yard floodlight on a lattice mast |
| zone_beacon_red | red aviation warning light that blinks (chimneys, Duga, masts) |
| zone_string_lights | bulbs on a cable (bars, Skadovsk deck) |
| zone_lantern | hanging kerosene lantern (warm flicker) |
| zone_candle | candle stub on a saucer (stashes, shrines) |
| zone_lab_alarm | red rotating alarm lamp (labs, the CNPP) |
| zone_bar_sign | painted board with a lamp over it ("100 RADS"; text from the block entity) |
| **electricity** | |
| zone_ktp | pole-mounted transformer: a box on two poles with three insulators |
| zone_substation | grey steel kiosk substation with a "Danger" bolt sign |
| zone_el_cabinet | electrical cabinet (doors ajar) |
| zone_genset | diesel generator on skids that chugs (sound) and powers its camp |
| zone_wire_broken | a power wire hanging to the ground that sparks occasionally (shares the electro tell) |
| zone_insulator_tree | switchyard insulator column with disconnectors (CNPP South) |
| zone_pylon_lattice | lattice pylon piece (the S_PYLON builder makes the tall pylons from it) |
| **fences, barriers, checkpoints** | |
| zone_fence_po2 | Soviet concrete fence panel with diamond relief, 2.5 m (variants: leaning, broken with a hole, fallen flat) |
| zone_fence_post_y | concrete post with a Y-shaped top carrying barbed strands (the Zone perimeter) |
| zone_concertina | coil of razor wire (on the ground or on a wall top) |
| zone_picket | wooden village picket (grey, gap-toothed; variants: leaning, fallen) |
| zone_wattle | woven-willow village fence |
| zone_gate_steel | double steel gate leaf with a sheet-metal infill and a red star or bars (open, closed, fallen) |
| zone_boom | striped red-white checkpoint boom with a counterweight (zone_barrier exists; add the raised/lowered model) |
| zone_booth | small checkpoint booth (steel, with windows) |
| zone_fbs | grey FBS foundation block, 2.4×0.6×0.6 (road chicanes) |
| zone_jersey | concrete jersey barrier |
| zone_dragon_tooth | concrete pyramid anti-tank block |
| zone_hedgehog | Czech hedgehog of three welded rails |
| zone_mg_nest | sandbag ring with a mounted machine gun (dressing) |
| **signs** | |
| zone_sign_stop | "STOP! Zone of alienation" board (red and white) |
| zone_sign_road | Soviet road signs on a pole: speed, priority, no entry, height limit (variants: bent, shot full of holes) |
| zone_sign_town | town entry stele (concrete letters: Limansk-13, Pripyat 1970) |
| zone_flag | faction banner on a pole (Duty red/black, Freedom green, Clear Sky blue, Military, Monolith grey; meta = faction) |
| zone_graffiti | wall decal, a cutout overlay (arrows, faction tags, "STALKER", warnings) |
| zone_poster | Soviet poster or board (faded) |
| zone_bus_stop | concrete bus-stop shelter with a mosaic back wall |
| **props** | |
| zone_cable_reel | big wooden cable reel on its edge |
| zone_concrete_pipe | 1.5 m concrete culvert pipe section |
| zone_well_ring | concrete well ring |
| zone_pallet | wooden pallet (flat or stacked) |
| zone_gas_bottle | red gas bottles (propane) |
| zone_engine_block | car engine block (dirty grey) |
| zone_car_door | car door, bonnet or seat lying loose (variants) |
| zone_scrap | heap of rusty sheet, pipes and wheels (a denser zone_junk) |
| zone_well_house | wooden village well with a little roof and winch |
| zone_outhouse | wooden privy with a heart cut-out |
| zone_haystack | rotten grey-brown haystack (or hay bales) |
| zone_firewood | stacked logs under a lean-to |
| zone_boardwalk | swamp duckboards: planks on posts, some missing |
| zone_jetty_pile | wooden piles standing in water |
| zone_net | fishing net hung on a frame |
| zone_grave | grave mound with a wooden cross or an iron fence and star (village cemeteries) |
| zone_tent_army | olive army tent (UST-56) |
| zone_tent_small | stalker's dome tent |
| zone_stove_burzhuika | pot-belly stove with its pipe (camps; cooking station) |
| zone_bench | park bench, or a log seat |
| zone_swing | playground swing or roundabout (Pripyat yards) |
| zone_bumper_car | bumper car (Pripyat amusement park) |
| zone_kiosk | street kiosk ("Soyuzpechat" newsstand) |
| zone_phonebox | phone booth (glass broken) |
| zone_plinth | empty monument plinth (squares) |
| zone_tv / zone_radio_set | TV set and valve radio (bars, flats) |
| zone_ore_cart | mine cart on narrow track (Red Forest mine) |
| zone_headframe | mine headframe piece (truss + wheel) |
| **ground and plants** | |
| zone_dirt_worn | trampled dirt (doorways, camps) |
| zone_ash | campfire ash ring |
| zone_asphalt_crack_weeds | cracked asphalt with weeds in the crack (block plus a plant) |
| zone_cattail | cattail at the water's edge (2 tall) |
| zone_sedge | wet-bank sedge tuft |
| zone_nettle | nettle and burdock clump along walls and fences (dark green, 1–2 tall) |
| zone_grass_short / _mid / _tall (2 tall) | three heights, each with a `_dry` variant (yellow-brown) |
| zone_sapling_birch / _pine | young tree, 1–3 tall (overgrown yards, track sides, through asphalt) |
| zone_willow | swamp willow bush |
| zone_tussock | swamp tussock (a mound of grass) |
| zone_dead_grass | grey-brown dead grass (radiation hot spots, Red Forest) |
| zone_burnt_ground | black soil with ash (anomaly burns, wreck sites) |

### 1c. Lights and electricity: rules

| what | where it is LIT (powered) | where it stands DEAD |
|---|---|---|
| floodlights, searchlights, bulkheads | army posts (Cordon checkpoint and bridge, Agroprom), Duty (Rostok, the Garbage checkpoint), Freedom (Warehouses), Ecologists (Yantar), traders, the Skadovsk, Yanov, the Laundry, Monolith posts at Radar and the CNPP (flicker) | all other camps, ruins and wrecks |
| zone_genset | next to each powered camp that has no grid: Moroz's bunker, Yantar, the Skadovsk, Yanov, the Barrier, the Monolith posts | broken ones at abandoned camps |
| street and yard lamps | Rostok's yard (on) | everywhere else (heads broken, poles bent ~10%) |
| power lines | wooden 0.4 kV poles beside village roads, 6–10 kV concrete poles along main roads (zone_dress does this), 110–330 kV lattice pylons on corridors (Garbage, Meadow, CNPP, Generators). 1 in 12 spans broken (`zone_wire_broken` sparks); 1 pylon in 20 fallen | — |
| red beacons | the Duga's tops, the CNPP stacks, Rostok's mast: they blink even when the rest is dead (the Zone's "still alive" tell) | — |
| emergency light | labs (X-16, X-18, X-10, Agroprom Underground): red `zone_lab_alarm` plus flickering tube lights | — |

### 1d. Fences: rules (which kind goes where)

| fence | where |
|---|---|
| zone_fence_po2 (concrete panels) | factory and institute perimeters: Agroprom, Rostok, the Garbage depot, Dark Valley, the Warehouses, Yantar factory, the CNPP. 1 panel in 10 leaning or broken, 1 in 30 fallen |
| chain-link + concrete posts + concertina top | army sites, the Ecologists, the Truck Cemetery, the CNPP inner zones, substations |
| zone_fence_post_y + 4 barbed strands, doubled 6 m apart | the Zone perimeter (the existing border fence in zone_gen), a "STOP" sign every 60 m, a floodlight pole every 150 m near the checkpoint |
| zone_picket / zone_wattle | village yards (Cordon village, Bloodsucker village, Swamps, Dark Valley farm); 40% of spans missing |
| corrugated sheet | bandit camps, garages, the scrapyard's inner pens |
| FBS blocks, sandbags, hedgehogs, dragon's teeth | checkpoints, the Barrier, Monolith lines |
| no fence | open countryside; never a fence ring around a lone ruin |

### 1e. Natural ground cover: rules (replaces the current per-column random undergrowth in zone_gen)

The goal is to read like the games: grass in tufts and patches, a mix of heights, green and dry, worn bare where people walk.

1. **Two noise masks.** `patch` is value noise at 7–10 m: it forms meadows and gaps. `clump` is Worley or cellular noise at 1.5–2.5 m:
   it forms tufts. A column grows a plant when `hash < density(context) × patch × clumpShape`, where `clumpShape` is 1 at a cell's
   centre and 0 at its edge. Never use one uniform random per column.
2. **Height mix.** In clump centres: `_tall` (2-block) 30% and `_mid` 45%. Toward the clump edge: `_short`. Tall plants also go
   anywhere within 1.5 m of a wall, fence or wreck (the mower never got there).
3. **Dry and green.** A third noise at 12 m chooses dry or green per patch, never per block. The dry share varies by region:

   | region | dry share |
   |---|---|
   | Cordon, Meadow | 35% |
   | Garbage, Wild Territory, Army Warehouses | 50% |
   | Swamps, Yantar edges | 15% |
   | Red Forest | 100% (`zone_dead_grass` + red needles) |
   | Radar, CNPP, Generators | 55% |
   | radiation hot spots (rad > 1) | `zone_dead_grass` |

4. **Density by context.** The first matching row wins.

   | context (computed per column) | density | what grows there |
   |---|---|---|
   | paved road, asphalt, concrete pad, building floor, platform, rail ballast | 0 | nothing (ballast: 3% weeds between sleepers) |
   | cracked asphalt (`zone_asphalt_cracked`) | 0.06 | in the cracks only: weeds and the odd birch sapling |
   | dirt track: the wheel ruts (1 m each) | 0 | worn dirt |
   | dirt track: the centre strip between the ruts | 0.8 | short grass (Soviet dirt tracks always have one) |
   | road verge, 0.5–2 m from the edge | 0.75 | tall dry grass and nettle |
   | doorways (2×3 m in front of a door), under ladders, the circle round a camp fire (r 4 m) | 0 | `zone_dirt_worn`, with `zone_ash` round the fire |
   | desire lines (2 m wide) from camp fire to doors, camp to road, and the trader's door | 0.05 | worn |
   | within 1.5 m of a wall or fence | 0.9 | tall and nettle, `zone_shrub` 1 in 15 |
   | forest edge band (2–10 m outside the canopy) | 0.7 | tall, ferns, shrubs, saplings |
   | under canopy (≤3 m from a trunk) | 0.12 | ferns, leaf litter or pine needles, moss |
   | open meadow | 0.45 | mixed; flowers ≤3% in small patches (`zone_meadow_flowers`, `zone_dandelion_clocks`) |
   | sand | 0.05 | dry tufts and `zone_dead_weeds` only |
   | gravel and scrap yards | 0.15 | weeds, dead weeds |
   | water edge: 0–1.5 m into water ≤1 m deep, plus 1 m of bank | 0.9 | reeds and cattail |
   | wet bank, 1–4 m | 0.7 | sedge, tussock, willow 1 in 25 |
   | still water | — | lily pads 8% in clusters |
   | anomaly ground and burnt ground | 0 | burnt ground |

5. **Overgrowth with age.** Saplings go in yards and on roofs' gutters (1 in 400), birches through the cracks of empty town squares,
   and moss on north faces (wear exists already).
6. **Season.** In winter: dry share +40% and the snow layer suppresses short grass. In autumn: dry share +25%.

### 1f. Railways: the user's lines (lay `zone_rail` on them, with `snap: crest`)

| line | points (x,z) | notes |
|---|---|---|
| R1 Cordon | (−4573,3423) → (−4375,3443) → (−4287,3449) → (−4221,3463) → (−4141,3463) → bridge deck x −4140..−4095 at z≈3466 (the user's quartz deck with iron-bar rails, y≈85) → east, following the user's gravel embankment NE to (−3700,3375) | single track on the embankment crest (y≈84); telegraph poles every 50 m on the north shoulder |
| R2 Garbage depot | (−4520,2222) → (−4461,2227) → (−4363,2227) (the user's track bed on an embankment, y≈82) → two tracks through the depot at z 2222 and z 2232 → out of the east doors to (−4262,2227) | |
| R3 Agroprom line | (−4880,2321) → (−5120,2321) (the user's gravel bed z 2318–2324) → into the institute yard | join R2 to R3 along the slope (−4520,2222) → (−4790,2240) → (−4880,2310) |
| R4 Jupiter | Yanov: tracks at x −4258 and x −4229 along the user's two 8×46 strips (−4252..−4234, −3050..−3004). The Pripyat Underpass cutting is the user's concrete trench (−3864..−3842, −3326..−2930), 8 m deep, white walls; lay the track on its floor | catenary masts every 50 m |
| R5 Wild Territory | along both long sides of the user's 190 m concrete platform (−4030..−3840, 1088..1110): tracks at z 1084 and z 1114 | plus a spur along the L wing x −3836 |

## 2. Places

Priority for phase 2: **P1** covers the user's own examples (Garbage scrapyard and depot, the Garbage bus, the Cordon truck and train,
grass, fences, lights). **P2** is everything else, place by place, in the order below.

### Cordon (perimeter) — SoC / CS / An

Wooded hills; the railway embankment E-W with its road bridge; the Rookie Village SW; the Military at the south and at the bridge.

| # | item (game) | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | Military checkpoint on the south road (SoC/CS) | boom across the road (arm E-W), zone_booth, 2 FBS chicanes at z 3960/3975, btr_70 `ok`, uaz_469, zone_mg_nest, sandbags; 2 floodlights (lit); the 2 S_TOWERs the plan already places | road (−4358,3990); BTR (−4374,3972); UAZ (−4340,3968) | BTR N, UAZ S | [U] the booth (−4350..−4320, 3985..4015) + the plan's S_CHECKPOINT (dress it, don't duplicate) |
| 2 | Zone perimeter south | double fence_post_y line, STOP signs, floodlights near the checkpoint | z≈4590, x −4700..−3600 | E-W | border fence (zone_gen) |
| 3 | Rookie Village | picket and wattle fences (broken); zone_well_house (−4400,3824); outhouses behind each house; a lean-to with a firewood stack; washing line; dog kennel; gardens gone to weed and nettle | (−4440..−4310, 3780..3850) | — | [U] (−4426,3790 70x30), (−4436,3820 72x28) |
| 4 | village car wrecks | car_zaporozhets `stripped` in a yard; car_moskvich `burnt` at the village entrance | (−4392,3838); (−4330,3832) | W; NE | [U] village |
| 5 | Moroz's bunker door (Sidorovich-style) | concrete portal in the slope, steel door, zone_lamp_cage above (lit), zone_genset at the door (chugging), exhaust pipe and antenna mast on the mound | (−4440,3802) | door faces E | [U] bunker (−4436,62,3808) |
| 6 | Wolf's lookout | sandbag ring on the north hillock, 2 log seats | (−4385,3772) | N toward the road | [U] |
| 7 | bridge post (SoC: the Military at the bridge) | sandbag walls at both deck ends; uaz_469 under the bridge on the west verge; FBS blocks narrowing the road to one lane; boom at (−4106,3482); searchlight on the deck rail (lit) | deck (−4140,3466) & (−4095,3466); UAZ (−4115,3452) | searchlight S | [U] the user's bridge |
| 8 | derailed train west of the bridge (SoC/CS) | loco_tem2 `ditch` (front off the rails, nose down the N slope, roll 20°); boxcar on the rails; boxcar `jack` 30°; tankcar `side-R` down the S slope; gondola on the rails (scrap); flatcar with a crate | loco (−4188,3457); then westward (−4210,3463), (−4232,3461), (−4256,3476), (−4280,3451), (−4302,3447) | train heads E | [U] R1 embankment (crest y≈84) |
| 9 | the tunnel under the embankment (SoC: electro tunnel) | [B] concrete box culvert 3×3 m through the embankment N-S, a puddle floor, 2–3 Electro anomalies inside, a body pack at the N mouth | (−4322, 3428..3462) | N-S | beside [U] railway huts (−4282,3464 20x14), (−4274,3420 18x16) |
| 10 | railway huts | signal hut: zone_stove_burzhuika, a bench, a rail lamp (dead) | as above | — | [U] |
| 11 | road anomalies N of the bridge (SoC) | keep the 3 HAMLET fields; add a body and a broken uaz_469 `burnt` on the verge | (−4098,3405) | N | road |
| 12 | ATP / motor depot (SoC: Bandits hold a Loner there) [?] | zil_130 box van `stripped`; bus_laz695 `stripped`; gaz_53 on blocks; car_volga `roof`; tyre stacks, drums, an inspection pit; corrugated gates; fire barrel (Bandits) | (−4196,3326), (−4212,3317), (−4190,3338), (−4172,3331) | W, E, N, — | [U] the compound (−4204,3306 38x40) + its spur |
| 13 | Fox's farm on the hill NE (SoC) [?] | collapsed shed roof, tractor_t40 `ok`, rotten haystacks, picket fence; dog lair; a Vortex field on the hilltop (SoC "anomaly hill") | (−4040,3285); field (−4045,3255) | S | [U] sheds (−4024,3274), (−4026,3296) |
| 14 | Southern farm (CS) [?] | cowshed dressing; gaz_53 grain lorry; tractor_mtz + trailer; a steel water tower (bWaterTower) if none | (−4075,3705); (−4020,3712); tower (−4000,3638) | S; E | [U] (−4062,3646 50x56), (−4034,3642) |
| 15 | Elevator (CS) | [B] grain elevator: 4 concrete silos (Ø6, 24 m) + a head house (30 m) + conveyor bridge; a Military post on top in CS (searchlight, dead) | (−3960,3300) on open ground | head house W | [B] |
| 16 | **truck at the crossing to Garbage** (user's example) | zil_131 army lorry `ditch` (nose in the W ditch, roll 8°), canvas torn; an old dropped boom and 2 FBS blocks (abandoned post) 20 m S; body pack by the wheel | truck (−4150,2895); post (−4150,2875) | N | road 555 |
| 17 | wrecks on Cordon roads | car_zhiguli `burnt` (−4145,3150); uaz_452 `ditch` (−4250,3700) | — | along road | road |
| 18 | power and telegraph | wooden 3-wire poles along R1's N shoulder (every 50 m, 2 spans down); 6 kV concrete poles along road 555 (zone_dress) | R1 | — | — |

Vegetation: mixed pine and birch woods; meadows of mixed green and yellow grass; wild apple trees in the village gardens; bushes along the embankment toe.
Anomalies: electro tunnel (#9), road fields (#11), Vortex hill (#13), one Burner in the ATP yard.

### Great Swamps (marsh) — CS / An

Reed beds, backwaters, peat islands joined by duckboards; collapsed pylons in the water; Clear Sky's base in the old fishing village.

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | Clear Sky base | blue flags; wooden watchtower; jetty with zone_jetty_pile + 3 boat_row (1 `sunk`) + boat_kazanka; nets on frames; genset (lit bulkheads, string lights); sandbags at the gate | (−5606..−5514, 4386..4446) | jetty faces W to the water | [U] (−5606,4386 92x60) |
| 2 | duckboard paths | zone_boardwalk lines from the base N to the Church and E to the Machine yard (over water and mud only) | (−5560,4380) → (−5450,3800); (−5520,4380) → (−5070,4250) | — | [B] along the user's dry islands |
| 3 | Church (CS, Renegades) [?] | broken belfry, fallen cross, iron-fenced graves; Renegade camp: corrugated walls, fire barrel | (−5442,3756) | — | [U] (−5442,3756 28x10 h19) + (−5466,3780 34x34) |
| 4 | Machine-Tractor Station (CS "Machine yard") [?] | combine `stripped`, 2 tractor_mtz (1 `side-L`), gaz_53, rusted harrows, a fuel tank on legs | (−5060..−5000, 4225..4290) | random | [U] (−5070,4218 76x78) + tower (−5052,4242 h26) |
| 5 | Pump station / Fish farm (CS) [?] | concrete fish ponds with sluice gates, pump house pipes into the water, a rusted pipe on trestles | (−5388..−5316, 4026..4116) | — | [U] (−5388,4026 72x90 h22) |
| 6 | Tower (CS lookout) [?] | the user's tall west structures: dress as a fire lookout or water tower with a ladder and a lookout cabin | (−6204,4184) | — | [U] (−6204,4184 16x16 h67) |
| 7 | fallen 110 kV line across the marsh | 6 pylon_lattice, 3 fallen into the water, wires in the reeds | (−5800,4000) → (−5100,4300) | NE | [B] |
| 8 | sunken things | barge `sunk` (bBarge); 2 boat_row `sunk`; uaz_469 `sunk` to its windows on a flooded track | (−5700,4200); (−5450,4150); (−5300,4300) | random | water |

Vegetation: reed and cattail at every water edge, tussocks, willow bushes, dead birches standing in water, lily pads; green dominant (dry 15%).
Lights: only the base. Anomalies: Burners in the reeds, the acid "Fruit punch" in the backwaters, an Electro at the fallen pylon.

### Darkscape (gorge) — An / Lost Alpha

A long, abandoned asphalt road through dark, deep pine woods; fallen trees across it; a broken bridge.

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | the asphalt road's wrecks | 6 cars (`burnt`, `ditch`); bus_paz672 `ditch`; zil_130 jack-knifed across the road (cut through with a gap); fallen trunks over the road every ~150 m | along the user's winding road (−3650,3100) → (−3200,3200) → (−2800,3400) | along the road | [U] road |
| 2 | broken road bridge | the middle span fallen into the river; uaz_469 `roof` on the riverbed; plank crossing beside it | (−2800,3405) | N-S | [U] the user's bridge (white, −2810..−2790) [?] |
| 3 | Bandit roadblock | zil_131 across the road, tyres, fire barrel, sandbags, a corrugated shack | (−3500,3120) | E | [B] |
| 4 | forester's hut / quarry camp | hut, firewood, a stalker camp fire | (−3700,2950) | — | [B] (plan's gorge builder) |

Vegetation: densest pine forest, dark understory (ferns, moss), little grass under canopy, tall grass on verges.

### Meadow (lesnoe) — An

A small factory sector in grass.

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | HV gantries / substation [?] | the user's two 36 m towers: dress as switchyard gantries (truss, insulators, broken wires); zone_substation kiosks; chain-link + concertina | (−2768..−2690, 3414..3424) | E-W | [U] (−2768,3414), (−2728,3414) |
| 2 | 330 kV line along the straight corridor z≈3405 | pylon_lattice every 120 m, 1 fallen | (−3400,3405) → (−2500,3405) | E-W | [U] corridor |
| 3 | factory yard (plan's farmstead builder) | gaz_53, tractor_mtz `stripped`, cable reels, concrete pipes, a crane_truck with its boom raised | near (−2950,3560) | — | [B] |
| 4 | the loop track | rally-like dirt loops: 2 cars `burnt`, a motorcycle with sidecar (small prop; optional) | (−2850..−2700, 3600..3750) | — | [U] |

Vegetation: wide meadows of tall grass (dense, 60% tall), flowers ≤5%, birch copses.

### Garbage (scrapyard) — SoC / CS / An — P1

Three huge junk heaps hot with radiation; the vehicle scrapyard; the train depot (the CS Bandit base); the Duty checkpoint north.

| # | item (game) | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | **vehicle scrapyard** (SoC: Loners vs Bandits) | `rows` fill. Row A z 2472: 8 trucks (zil_130/131, kraz_255, ural) every 22 m from x −4410, mixed `stripped`/`ok`, one `side-L`. Row B z 2492: 6 armour (btr_70, btr_80, brdm_2, bmp_2) `stripped`. Lane z 2505–2515 left clear. Row C z 2528: 4 bus_laz695 + 1 bus_paz672 + fire_zil + tanker_zil. Row D z 2552: dozer_t130, excavator (bucket down), crane_truck (boom up at (−4250,2470)), 2 tractor_mtz, mi_8 hull without rotors (−4405,2552). Stacks: 3 cars `stack` at (−4228,2476). N-S lane at x −4320 left clear | box (−4430,2460,−4210,2570) | rows A/C S, B/D N; mi_8 E | [U] the fenced gravel yard |
| 2 | Loners' camp in the yard (SoC) | camp fire (−4320,2510), lean-to between two buses, 2 sleeping bags, crates; the "seats" | (−4320,2510) | — | [U] |
| 3 | scrapyard gate | zone_gate_steel (one leaf fallen), dead floodlight mast | (−4212,2541) | faces E | [U] fence + spur 368 |
| 4 | **bus at the south entrance** (user's example; CS Bandits' robbery spot) | bus_laz695 `ditch` on the E verge, windows gone; car_moskvich `burnt` beside it; Bandit post: tyres + fire barrel | bus (−4133,2601); car (−4122,2615) | NW (toward Garbage) | road 555 |
| 5 | **train depot** (CS Bandit base; user's example) | outside catwalk (zone_grating + handrail) along the N and S walls at eave height (roof line −2) for the full length; zone_steel_stair up the SW corner (−4339,2246); zone_cage_ladder at the NE corner (−4270,2204); tube scaffold 3 levels on the E gable (−4271, 2209..2241); roof lookout at the W end with sandbags + searchlight (lit) | hall (−4337..−4272, 2207..2243), floor y 72, roof y 89 | — | [U] (−4338,2208 72x36 h17) |
| 6 | inside the depot | 2 tracks (z 2222, 2232) with inspection pits; gantry_crane on rails along both long walls at y+12, parked near the W end, hook down; inner gallery (grating) at y+6 along the N wall; boxcar ×2 on the N track; loco_chme3 on the S track, hood panels off (`stripped`); workbench, lockers, tyres | inside | wagons E | [U] |
| 7 | **train cars** outside, W (user's example) | gondola (scrap), tankcar, boxcar (door open), flatcar with a crate, coupled on R2; one boxcar `side-L` off the embankment into the N ditch | x −4440..−4370 on z 2227; fallen (−4482,2210) | train heads E | [U] track bed (−4461..−4363) |
| 8 | south siding | 2 flatcars + zone_buffer_stop | (−4325..−4290, 2266) | E | [U] concrete pad (−4327..−4287, 2260..2272) |
| 9 | depot yard | fence → zone_fence_po2 with 2 gaps; brick hut = Bandit guard room (fire barrel, lit bulkhead); yard lamps (lit, Bandits) | fence (−4378..−4242, 2193..2276); hut (−4265,2210) | — | [U] (−4378,2192 136x84) |
| 10 | the junk heaps | each heap: 6–10 crushed car bodies (`roof`, `side`, `sunk` 1 m into the slope), zone_scrap, pipes, tyres, concrete slabs, rebar; a dozer_t130 abandoned on top of H2 (−4060,2200); a bus `sunk` on the SE flank of H3 | H1 (−4370..−4180, 2040..2160); H2 (−4150..−3960, 2135..2280); H3 (−4350..−4150, 2325..2430) | random | [U] the 3 grey heaps |
| 11 | north yard with 2 tanks (CS "Flea market" style) [?] | 2 round tanks rusted, with a spiral steel stair; uaz_452 parked; stalls (tables under tarps), fire barrel, radio | tanks (−4097,2083), (−4069,2083); van (−4118,2010) | van E | [U] (−4108,2072), (−4080,2072), (−4066,1982) |
| 12 | Duty checkpoint to Rostok (SoC/CS) | FBS chicane, sandbags, zone_booth, boom, Duty flag, fire barrel, searchlight (lit) | (−4196,1845) | N-S | road 360 |
| 13 | 110 kV line | the user's thin line at z 1955 (−4420..−4180 then the diagonal to (−4140,1910)) [?]: 5 pylons, 1 fallen across it at (−4300,1955) | — | E-W | [U] [?] |
| 14 | road NE to Dark Valley | btr_70 `burnt` (−3870,2003); car_zhiguli `ditch` (−3790,2150) | — | E; S | road |

Vegetation: open, patchy, 50% dry; nothing on the heaps but dead weeds; birch scrub along the fences.
Anomalies: central fields on H2 (Springboards, a Vortex), Burnt Fuzz on the trees by the depot, Electro along R2. Radiation is strong on the heaps.

### Agroprom (institute) — SoC / CS / An

The research institute behind its concrete fence, held by the Military; the railway; the underground through a manhole.

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | institute perimeter | zone_fence_po2 ring with an E gate; watchtower at the SE corner; floodlights (lit, army) | ring (−5180..−5010, 2295..2410); gate (−5020,2340) | gate E | [U] institute (−5165..−5030, 2310..2395) |
| 2 | Military gate post | btr_80 `ok`, uaz_469, boom, sandbags, zone_mg_nest | (−5000,2343) | E | road 867 |
| 3 | army camp in the yard (CS) | 2 zone_tent_army, ammo crates, a radio table, a genset | (−5100,2390) | — | [U] yard |
| 4 | the 4 round tanks | rust, a ladder each, a pipe bridge between them | (−5160..−5135, 2315..2340) | — | [U] |
| 5 | railway into the yard | R3; 3 boxcars on the rails inside the gate | (−5100..−5060, 2321) | E | [U] gravel bed |
| 6 | manhole to Agroprom Underground | concrete well ring + hatch (the plan's S_BUNKER); a caged lamp | in the yard (plan's position) | — | plan |
| 7 | water tower (SoC sniper perch) | [B] bWaterTower (Rozhnovsky) beside the institute | (−5190,2405) | — | [B] |
| 8 | Mole's camp at the vent-shaft exit (SoC/CS) | concrete vent head with a grille; Loner camp fire, a tent | (−4950,2205) | — | [B] |
| 9 | old factory SW (CS Mercs / An) [?] | tower_crane rusted beside it; scaffolding on one wall | crane (−5310,2640) | jib NE | [U] brick buildings (−5360..−5300, 2650..2700) |
| 10 | swamp corner (SoC bloodsuckers) | reeds, dead trees, a uaz_469 `sunk` | west, low ground near (−5300,2450) | — | [U] low ground |

Agroprom Underground (S_BUNKER interior dressing): sewer collectors with pipes along the walls (zone_pipe, valves), cable trays, caged and
tube lamps (flicker), puddles, rat nests, iron ladders up the shafts, a flooded stretch, the "controller room" with a desk and lockers.

### Dark Valley (hollow) — SoC / CS / An (+ X-18)

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | Bandit HQ factory | zone_fence_po2 perimeter; boom gate + wooden watchtower at the S gate; kamaz-like zil_131 and a uaz_469 in the yard; fire barrels; bulkheads (lit, Bandit garrison) | gate (−3300,2268) | gate S | [U] (−3370,2106 138x238 h31) |
| 2 | the 3 tanks | rust, ladders, pipe rack to the main hall | (−3180,2205..2230) | — | [U] |
| 3 | X-18 building (SoC) [?] | heavy steel door, red lab alarm (flicker), the hatch (plan's S_BUNKER at (−3076,2558) → move it inside or beside this building) | (−3230..−3112, 2412..2486) | — | [U] (−3230,2412 118x74 h31) |
| 4 | Pig farm (CS: Freedom outpost) | long cowsheds with roofs partly collapsed; feed silo; slurry pond; tractor_mtz + trailer; gaz_53; Freedom flag (CS) | sheds (−3390,2626), (−3364,2626); silo (−3340,2600) | E-W | [U] (−3390,2626 14x36), (−3364,2626 14x36), (−3404,2610 66x68) |
| 5 | the bridge over the lake | zil_130 `side-R` in the water by the bridge; a car `burnt` on the deck | (−3285,2520); (−3300,2508) | — | [U] bridge (−3300,2510) |
| 6 | road wrecks | btr_70 `burnt` at the N road bend; bus_paz672 `stripped` at the S entry | (−3330,2330); (−3330,2700) | — | road |

Vegetation: damp valley with heavy grass, willows by the lake, reeds. Anomalies: Springboards on the bend, a Burner by the bridge, an Electro at the X-18 door.

### Rostok / 100 Rads bar (molot) — SoC / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | Duty checkpoint at the S gate | FBS chicane, sandbags, zone_booth, boom, Duty flags on both posts, searchlight (lit), PA horn on a pole, "no weapons drawn" sign | (−4265,1425) | N-S | [U] gate yard (−4288,1382 88x102) + road 360 |
| 2 | 100 Rads bar entrance | zone_bar_sign "100 RADS" + red lamp over the door; 2 guards behind a sandbag chicane; inside: counter, stools, zone_tv, zone_radio_set, string lights, beer crates | (−4379,1277) | door as built | [U] the bar building |
| 3 | Arena (SoC) | fighting pit fenced with chain-link inside the hall; a grating viewing gallery on two sides; floodlights | (−4498..−4388, 1324..1372) | — | [U] (−4498,1324 110x48) [?] |
| 4 | Duty HQ | lattice radio mast 35 m on the roof with zone_beacon_red; btr_80 `ok` (Duty) parked; 2 uaz_469 | mast (−4402,1118); BTR (−4520,1200) | BTR S | [U] (−4402,1108 38x20) + 4 tanks (−4430..−4390, 1118..1128) [?] |
| 5 | heat pipelines | 2 zone_heat_pipe on trestles along the N wall (z≈1095), with U expansion loops; an arch over the yard road at x≈−4440 | (−4800,1095) → (−4250,1095) | E-W | [U] |
| 6 | yard siding + wagons | 5 wagons (2 boxcar, gondola, 2 tankcar) on a siding, with a gantry_crane spanning it | (−4700..−4600, 1135); crane (−4640, 1120..1150) | E | [U] yard [?] |
| 7 | tower crane | rusted, jib swung over the halls | (−4560,1210) | jib E | [B] in the yard |
| 8 | perimeter | zone_fence_po2 + concertina; sandbag nests on 3 rooftops; yard lamps ON, the only lit streets in the Zone | wall of (−4842,1076 624x366) | — | [U] |
| 9 | E gate to Wild Territory | steel gate ajar, boxcar shoved across half of it | (−4218,1250) | — | [U] [?] |

Vegetation: little; weeds through cracked concrete; birches along the walls.

### Wild Territory (wild) — SoC / An

A railway yard and factory; Mercenary snipers; the rail viaduct; a toxic sludge basin (the user's sunken soul-sand basin).

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | rail yard on the platform | R5 tracks. S track: a stalled freight train (loco_tem2 + 6 wagons, the one the Ecologist hides in), x −4020..−3880. N track: 3 tankcar, one `jack` onto the platform. Buffer stops at the E ends. Platform lamps dead | platform (−4030..−3840, 1088..1110) + L wing | train E | [U] the cobblestone platform 190 m |
| 2 | footbridge over the tracks | steel truss with zone_steel_stair on both sides | (−3950, 1078..1120) | N-S | [B] |
| 3 | signal box | 2-storey brick box with big windows, a lever frame inside | (−3825,1100) | — | [B] |
| 4 | rail viaduct over the basin's N arm (SoC: the bridge with the snipers) | concrete piers + rusted truss span (3 spans), Merc sniper nest (sandbags) mid-span | (−3830,1100) → (−3640,1100) | E-W | [B] over [U] basin |
| 5 | sludge basin | 2 gondola `sunk`, a zil_130 `sunk`, pipe racks on trestles across it, dead reeds at the edges | (−3760..−3330, 1010..1590) | — | [U] soul-sand basin |
| 6 | Merc camp | fire barrel, tents, crates on the concrete square | (−3570,1325) | — | [U] (−3602,1270 108x70) |
| 7 | workshop and tank | tower_crane; stack of concrete pipe sections; cable reels | crane (−3520,1130); pipes (−3570,1150) | jib S | [U] (−3550..−3480, 1080..1105) + tank (−3600,1112) |
| 8 | tunnel to Yantar | concrete culvert with a Burner inside | west edge (−4040,1250) [?] | E-W | [B] |
| 9 | village SW | houses as found; picket fences; 2 cars `burnt` | (−4010..−3700, 1410..1620) | — | [U] |

Anomalies: Burners in the tunnel and at the platform, Electro along the rails, Burnt Fuzz on the yard trees.

### Yantar (lake) — SoC / An (+ X-16)

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | Ecologists' bunker | chain-link + concertina ring; 2 lab containers (white, steel); antenna mast with dishes; searchlight mast; genset; weather station; raf_2203 parked; lit | (−5240..−5190, 1660..1720) | — | [U] (−5236,1692 28x26) [?] |
| 2 | helipad | mi_8 `ok` (Ecologists' supply, parked, rotors drooping) | (−5240,1640) | E | [U] the "H" pad |
| 3 | the factory (X-16) | big pipes on trestles from the factory down to the lake; conveyor gallery between two halls; tall chimney with a beacon (dead); red lab alarm at the X-16 door | (−5360..−5150, 1370..1650) | — | [U] (−5360,1370 212x384 h36) |
| 4 | dry lake bed | excavator `stripped` (bucket in the mud), zil_130 dump `sunk`, a pontoon `sunk`; dead trees; acid pools (anomaly ground) | (−5340..−5250, 1650..1730) | random | [U] gravel basin |
| 5 | ring road | 2 btr_70 `burnt` on the road (army attempt), body packs | ring road (−5380..−5140) | along the road | [U] |

Vegetation: dead swamp (rusty water, dead birches), reeds, dry 20%. Anomalies: Electro near the factory, acid pools in the bed, a psy rise near X-16.

### Army Warehouses (depot) — SoC / CS / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | Freedom base | green flags; graffiti; gate with sandbags and a boom; 2 watchtowers; floodlights (lit); radio mast 30 m + red beacon | gate (−3945,630) | gate E | [U] compound (−4090..−3940, 575..700), (−4128,580), (−4146,580) |
| 2 | vehicle shed | 4 zil_131 in a row under an open shed + 2 btr_70 (Freedom-painted, `ok`) | (−4060..−3990, 655..690) | N | [U] yard |
| 3 | helipad | mi_8 `stripped` hulk | (−4000,560) | NE | plan's S_HELIPAD (dress) |
| 4 | the Barrier (SoC/CS: the line facing north) | FBS wall + sandbags across the road, bus_laz695 `burnt` as the gate, hedgehogs, concertina, a pillbox, 2 searchlights (lit, genset), Duty/Freedom flags | (−3925,300) | faces N | [B] on the N road |
| 5 | Bloodsucker village (SoC) | ruined houses, overgrown yards, fallen picket; a well; car_volga `stripped`; a village cemetery with crosses | (−4320..−4150, 330..460) | — | [U] the scattered houses |
| 6 | Mercs' farm (SoC) [?] | long cowshed, haystacks, tractor_mtz, fire barrel | (−4344..−4246, 626..656) | — | [U] (−4344,626 98x30) |
| 7 | the long wall E | dress as the old depot wall: PO-2 texture, a gate in it, a guard booth | (−3766..−3614, 594..604) | E-W | [U] (−3766,594 152x10 h14) |
| 8 | radioactive hills | dead grass rings, warning signs, two uaz_469 `burnt` | (−3820,680) (the user's grey boulders) | — | [U] |

Vegetation: rolling meadows, tall grass, birch groves, pines on the ridges.

### Dead City (sulfur) — CS / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | street barricades (CS) | a bus or lorry across each town entry with FBS blocks and sandbags | W (−5715,768); E (−5345,768); N (−5680,605) | across the street | road |
| 2 | Mercs' HQ on the square | lit (genset), sandbags in the windows, searchlight on the roof | the largest building facing the square | — | [U] [?] |
| 3 | the square | zone_plinth (empty), dead lamps, kiosk, phonebox, btr_70 `burnt` | (−5565,708) | — | [U] square (−5600..−5530, 690..725) |
| 4 | bus station | 2 bus_laz695 + trolleybus `roof`; zone_bus_stop | a free yard by the E-W road (−5420,780) [?] | E | [U] |
| 5 | fire station | fire_zil `stripped` half out of the garage | (−5690,880) [?] | S | [U] (−5690,912 14x14) [?] |
| 6 | north bridge | steel truss road bridge with a uaz_469 `burnt` on the deck | (−5680, 470..570) | N-S | [U] bridge |
| 7 | water tower | bWaterTower, rusted, with a sniper nest | (−5450,650) | — | [B] |

Vegetation: overgrown town: birches through asphalt, saplings in yards, tall grass on lawns.

### Limansk (zarechny) — CS / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | construction site (CS) | tube scaffolding all round the unfinished block (6 levels); yellow tower_crane 40 m beside it; concrete panels stacked; a cement mixer | block (−5482..−5398, −726..−698); crane (−5440,−690) | jib W | [U] (−5482,−726 84x28 h27) |
| 2 | river bridge (CS: the Monolith MG post) | zone_mg_nest + sandbags at the town end; btr_80 `burnt` on the deck | (−5470,−790) | E-W | [U] bridge [?] |
| 3 | street fighting | 2 btr_80 `burnt`, an ural_4320 barricade, FBS lines, burning barrels; trolleybus `side-L` | along the central N-S street (−5560, −900..−450) | — | [U] streets |
| 4 | anomalous street (CS: the Electro crossing) | a field of Electros across a street between 2 blocks; scorch marks | (−5600,−600) [?] | — | [U] |
| 5 | town sign | zone_sign_town "LIMANSK-13" | S entry by the roundabout (−5560,−250) | faces S | [U] |
| 6 | roundabout | plinth, dead lamps | (−5580,−270) | — | [U] |

Lights: Monolith posts flicker; Clear Sky camp at the S entry (lit). Vegetation: overgrown, lots of birches.

### Truck Cemetery (graveyard) — An / Rassokha — built by us

| # | item | kit / state | x,z | facing |
|---|---|---|---|---|
| 1 | fenced field | chain-link on concrete posts, 260×160 m, gate on the road side, guard booth + dead floodlight | box (−1180,−850,−920,−690); gate (−920,−770) | gate E |
| 2 | helicopter row | 6 mi_8 nose to tail (rotors drooping or removed), 2 mi_24 | z −830 | E |
| 3 | buses | 5 bus_laz695, 3 bus_paz672, 1 bus_ikarus | z −800 | E |
| 4 | fire engines and specials | 4 fire_zil, 2 tanker_zil, 2 uaz_452 ambulance | z −785 | W |
| 5 | lorries | 20: zil_131, kraz_255, ural_4320, in 2 rows | z −765, −745 | E / W |
| 6 | armour | 6 btr_70/80, 2 brdm_2, 2 bmp_2 | z −725 | E |
| 7 | engineering | 2 imr_2, 3 dozer_t130, 2 crane_truck (booms up), 1 excavator | z −705 | W |
| 8 | burial trench | a long mound with a buried vehicle roof poking out, radiation signs | (−900, −850..−690) | N-S |

All `ok` or `stripped` (looted parts), 10% `burnt`, 5% `stack`; strong radiation glow (the existing hot spots). This replaces the plan's
row loop (zone_plan line ~504) with this data.

### Red Forest (redforest) — CS / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | Forester's lookout tower (CS) | 25 m steel fire tower with a cabin; hut below; a lantern (lit) | (−1950,−1250) | — | [B] |
| 2 | burnt tanks (CS) | 3 t_72 (1 turret blown off and lying beside it), 2 bmp_2, all `burnt`, half-sunk in red needles | (−1800,−1150), (−1760,−1180), (−1830,−1100), (−1700,−1210), (−1880,−1190) | various | [B] |
| 3 | the old mine | zone_headframe 20 m, mine portal in a hillside, narrow track + 4 zone_ore_cart, conveyor gallery | (−2150,−1050) | portal E | [B] (plan's mine) |
| 4 | the user's slab rows [?] | 11 parallel rows of stone-brick slabs 5 m high: read as the radioactive burial trenches; dress with rusted vehicle roofs, wheels and a bus end sticking out; rad signs; dead grass | (−2166..−1974, −1686..−1450) | N-S | [U] ask the user what these rows are |
| 5 | bridge toward Limansk (CS lift bridge) | [B] steel lift bridge on the route to the W (by the pair's track) | where track redforest→pripyat crosses water [?] | — | [B] |

Vegetation: rust-red dead pines (red needles), no green grass (dead grass 100%), burnt ground patches. Anomalies: dense, Burners and Electro.

### Radar / Brain Scorcher (array) — SoC / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | the Duga arrays | catwalks (grating + handrail) every 20 m up the front face, cage ladders every 60 m, guy-wire anchors, zone_beacon_red on top every 40 m (blinking, powered), floodlights at the base (lit) | (−1790..−1520, 3400..3422) h103; (−1726..−966, 3404..3428) h150 | faces S | [U] p@(−1788,3400), p@(−1472,3404) |
| 2 | approach checkpoint (old army KPP) | gate, booths, FBS, 2 btr_80 `burnt`, Monolith MG nests | (−1500,3150) | faces S | road N-S x≈−1500 |
| 3 | the dead | 20 body packs scattered in the fields south of the array (scorched stalkers) | box (−1800,3450,−1100,3800) | — | — |
| 4 | X-10 entrance building | satellite dishes on the roof, a heavy door, red alarm (flicker) | (−1398..−1338, 3946..4006) | — | [U] (−1398,3946 60x60 h28) |
| 5 | Monolith barricades | concertina + sandbags at the array foot | along z 3440 | E-W | [B] |

Vegetation: dense pine forest; grass along the array service road. Anomalies: Electro at the masts' feet, psy field (exists).

### Jupiter / Yanov Station (jupiter) — CoP / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | Yanov station | R4 tracks. W track: 6 tankcar. E track: loco_chme3 + 5 boxcar. Passcar on a third track if it fits. Catenary masts every 50 m (wires sag, 1 down). Platform lamps dead. The station is lit (genset). Duty and Freedom flags inside | (−4258 / −4229, −3060..−2990) | trains S | [U] station (−4246,−3106 20x44 h12) + strips (−4252..−4234, −3050..−3004) [?] |
| 2 | footbridge over the tracks (CoP) | steel truss + stairs | (−4243,−3058) | E-W | [B] |
| 3 | Pripyat Underpass cutting (CoP) | track on the floor; 4 hopper (the CoP "ore wagons") at the N end; a tunnel portal (concrete arch, steel doors ajar) at the N end; caged lamps (dead) | cutting (−3864..−3842, −3326..−2930); hoppers (−3853, −3290..−3230); portal (−3853,−3330) | portal N | [U] concrete trench |
| 4 | Jupiter plant | tall chimney with beacon (dead); conveyor and pipe bridges between halls; 3 zil_131 `stripped` and a crane_truck in the yard; gantry_crane in the main hall | (−4116..−3800, −2898..−2634) | — | [U] (−4116,−2898 318x264) |
| 5 | cement factory (CoP) [?] | silo battery, conveyor gallery up to the top, tanker_zil cement truck | (−4342..−4282, −3226..−3188) | — | [U] (−4342,−3226 60x38 h51) |
| 6 | Iron Forest (CoP) [?] | the user's ringed dome: twisted rebar and steel "trees" (truss and rebar), Electros | (−4025,−3040) | — | [U] (−4038,−3066 24x54 h36) |
| 7 | Skat crash sites (CoP: 2 here) | mi_24 `burnt`, broken in two, with a ground scar and debris trail | (−4500,−3300); (−3700,−2700) | NE; W | [B] |
| 8 | Kopachy-style burnt village | burnt houses (plan's huts), a well, uaz_452 `burnt` | (−4520,−2700) [?] | — | [U] (−4536,−2686 60x30) |

### Zaton (zaton) — CoP / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | Skadovsk | the user's ship: list it 5° (block shift is optional; skip if too big a change); gangway ramp from the bank; string lights (lit) and a genset on deck; antenna mast; Loner flag; deck crates | (−3610..−3530, −4110..−4095) | bow E | [U] the ship |
| 2 | Shevchenko (second beached ship) | ship_cargo `sunk` 1.5 m, listing, on a sandbar in the NW backwater | (−3800,−4300) | NE | [B] (or upgrade one of the plan's 3 barges) |
| 3 | Dredge station | dredger anchored in the river | (−3250,−3800) | E | [B] water |
| 4 | Riverport (CoP) [?] | 2 portal_crane on quay rails; boxcars on the quay track; warehouses | crane (−3560,−3965); quay (−3600..−3500, −3960) | jibs N | [U] (−3550,−3996 22x70 h37) |
| 5 | Waste processing station [?] | round settling tanks (clarifiers) with bridges, pump houses, sludge pits | (−4020..−3918, −3740..−3668) | — | [U] (−4020,−3740 102x72 h31) |
| 6 | Sawmill | log piles, saw shed, kraz_255 log truck, conveyor | (−3300,−4200) | — | [B] |
| 7 | Skat crash sites (CoP: 3 here) | mi_24 `burnt` + debris trail | (−3480,−3880); (−3820,−4150) on sand; (−3150,−4200) on the plateau | various | [B] |
| 8 | shores | boat_row and boat_kazanka (upright, upside-down, `sunk`), jetty piles, nets | backwaters | — | water edges |
| 9 | power line | wooden poles on the user's diagonal line | (−3600,−4060) → (−3480,−3880) | — | [U] |

Vegetation: sandbars with sparse dry tufts, reeds and willows, pine on the high ground.

### Pripyat Outskirts (outskirts) — CoP / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | city entry stele "PRIPYAT 1970" | zone_sign_town | where the road from Jupiter enters (snap road) ≈(−3400,−3000) [?] | faces W | road |
| 2 | road of the evacuation | 8 cars (`burnt`/`stripped`), 2 bus_laz695, an army ural `ditch`, all along the road into the city | the main road (snap) | along the road | road |
| 3 | Monolith lookouts | sandbags + searchlight (flicker) on 4 high-rise roofs | the tallest roofs: (−3018,−3230 h55), (−3082,−3228 h55), (−3338,−2940 h43), (−3152,−2902 h43) | — | [U] |
| 4 | school | yard with a gym frame, swings, a fallen fence | the user's school [?] | — | [U] |

### Pripyat (pripyat) — SoC / CoP / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | amusement park | yellow Ferris wheel (bFerris, if not there), 6 zone_bumper_car under the user's round roof, a swing carousel | wheel (−2770,−2580); bumper cars (−2800,−2555) | wheel faces S | [U] round roof (−2810..−2790, −2565..−2545) + [B] wheel |
| 2 | central square | dead lamps, kiosks, benches, birches through the paving, 2 btr_70 `burnt`, an army ural, a mi_8 `burnt` crash in the square (SoC battle) | square (−2990..−2780, −2470..−2380) | — | [U] |
| 3 | Palace of Culture / hotel roofs | Monolith sniper nests, searchlights (flicker), rooftop letters (slogan frames, empty) | (−2900..−2840, −2510..−2460) | — | [U] |
| 4 | stadium Avanhard (SoC) | stands, trees growing on the pitch, floodlight masts (dead) | oval (−2840..−2730, −2830..−2770) | — | [U] |
| 5 | Laundry base (CoP) [?] | army flags, sandbags, genset, lit | a 2-storey building near the S edge [?] | — | [U] |
| 6 | streets | rows of parked cars (`stripped`), zone_bus_stop, phoneboxes, kiosks, playgrounds (swings), shot-up road signs | city-wide scatter (no boxes) | — | — |

Vegetation: an overgrown city: birches in every yard and on roofs (saplings), tall grass on lawns, ivy-like moss.

### Deserted Hospital (sanatorium) — CS / An — built by us

| # | item | kit / state | x,z | facing |
|---|---|---|---|---|
| 1 | courtyard | 3 raf_2203 (`stripped`, 1 `side-L`), a uaz_452 ambulance, hospital beds outside, an Electro field | (−1650,−300) | various |
| 2 | boiler house | brick chimney 30 m + beacon (dead) | (−1560,−380) | — |
| 3 | morgue annex | steel door, lab lamps (flicker) | (−1720,−240) | — |
| 4 | fence | PO-2 with gaps; Monolith sandbags in the gaps | ring r 140 | — |

### CNPP North (npp) — SoC / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | vent stacks | red-white bands, lattice bracing, zone_beacon_red (blinking) | (−874,798), (−830,798) | — | [U] (−874,798 h136), (−830,798 h136) [?] |
| 2 | sarcophagus | buttress "cascade" wall, debris at the foot, red alarm lamps (flicker) | (−720..−610, 870..1050) | — | [U] the orange block |
| 3 | turbine hall rail entry | track into the hall + 2 flatcars + boxcar | W end of the white hall (−500,1000) [?] | E | [U] |
| 4 | units 5/6 construction | 2 tower_crane over an unfinished block, scaffolding | (−200,1100) [?] | jibs N | [B]/[U] (−222,1160), (−156,1114) [?] |
| 5 | Monolith barricades | FBS, sandbags, burnt btr_80 ×2 at the approach, concertina | approach roads | — | [B] |
| 6 | open switchyard | insulator trees, fallen wires | (−640..−570, 715..790) | — | [U] |
| 7 | oil tank farm | 6 rusty tanks with stairs | (−185..−115, 665..720) | — | [U] |

### CNPP South (npp_south) — SoC / An

| # | item | kit / state | x,z | facing | belongs to |
|---|---|---|---|---|---|
| 1 | switchyards | the user's many gantries: insulator trees, broken wires (sparks), PO-2 + chain-link | (−930..120, 1450..1850) | — | [U] |
| 2 | approach road | army column destroyed: 3 btr_80, 2 ural, a t_72, all `burnt`, in a line along the road | snap to the main S road | N | road |
| 3 | cooling channel | concrete-lined channel, a tug `sunk`, rusted pumps | [U] water | — | [U] |

### Generators (turbines) — An — built by us

| # | item | kit / state | x,z | facing |
|---|---|---|---|---|
| 1 | power corridors | 330 kV pylon_lattice along the user's parallel diagonal corridors, every 120 m, 1 in 6 fallen, sparking wires | the corridors around (4600,−3700) (snap: corridor centrelines) | NE |
| 2 | army front line | trench lines with sandbags, dugouts, 3 t_72 `burnt`, 2 bmp_2, a mi_24 `burnt` | ring r 300 round the centre | — |
| 3 | the Generators hall | (plan's spheres) + gantry_crane, cable trays, red alarm | centre | — |

## 3. Phase 2 order of work

1. The prop system (§1a), with these first: bus_laz695, zil_130, zil_131, btr_70, mi_8, boxcar, gondola, tankcar, flatcar, loco_tem2.
2. Blocks for rail, scaffold, catwalk and lights.
3. Garbage #1–#8, then Cordon #8, #16 and #1.
4. Ground cover rules (§1e) in zone_gen undergrowth.
5. Fences (§1d): remap the user's fences by context.
6. Lights (§1c).
7. The other places, in the order of this document.

Smoke-check every place with `--smoke-shot` from the item's x,z. Leave every [?] untouched until it has been looked at.
