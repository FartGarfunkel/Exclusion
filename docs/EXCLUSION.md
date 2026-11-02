# Exclusion — design notes (alpha.1)

## Controls
| Action | Keyboard / mouse | Controller |
|---|---|---|
| Fire | Left click | RT |
| Aim down sights | Right click (hold) | LT |
| Reload / clear a jam | R | X |
| Underbarrel launcher | V or middle click | RS click |
| Head torch | L | D-pad up |
| Quick bandage / medkit | H | D-pad down |
| PDA (map, task, news) | Tab or M | Back |
| Weapon bench | U | from the inventory tabs |
| Inventory | E | Y |
| Weapons / quick slots | 1-9, wheel | LB / RB, d-pad left/right |
| Sprint / crouch / jump | Ctrl / Shift / Space | LS click / B / A |
Use (right click / LT) on a trader trades; on an iron hatch climbs into or out of your bunker.

## Systems (where to tune)
- Weight: base 50 kg + suit + artifacts; slower past 80%, no sprint past 100%, crawling past 125% (`sim/zone_core.cpp`).
- Belt: the bag's bottom-left slots (27..31), as many as the suit's belt count; artifacts elsewhere irradiate you harder.
- Needs: food lasts ~55 min, water ~38 min, sleep ~2.5 h of play; at zero they hurt. Healing is slow and only when fed and watered.
- Radiation: dose 0..100 from the ground's hot spots, the regions and artifacts; above 25 it drains health.
- Emissions: every 45 min of world time (`EM_CYCLE`), 90 s warning, 70 s wave; safe under any solid roof or in the bunker.
- A-Life: `ONLINE_R` 150 m, offline step 2 s, faction targets `WANT[]` (`sim/zone_alife.cpp`).
- Items: edit `tools/zone/catalog.py` and run it; sprites `tools/zone/paint_items.py`.

## The story (original)
You wake in Moroz's cellar in Old Hamlet with a PDA holding one task: FIND THE FERRYMAN, the only stalker said to
have come back from the plant. The trail: the army hangar at the Perimeter checkpoint → Grach at the Scrapyard →
the Furnace bar in Molot Works → Thorn's PDA in the Institute sewers → the scientists at Lake Kalina (a psy helmet)
→ the Ferryman's cache in the Red Forest mine → silence the Duga Array (the psy wall comes down for everyone) →
Yanov Station → Lamplighter on the Volga barge → the Choir's square in Pripyat → the light in the sarcophagus,
which grants a wish. Four endings; only one asks the right question.

## Next
Lead-lined artifact containers, toolkits and upgrade trees at technicians, faction reputation and quests, mutant
specials (the shroud's cloak, the crawler's leap, psy attacks), proper gun sounds, ADS hand poses, more labs
underground, the Ferryman himself.
