# Rust & Steel - factory freeplay

Open `project.json` in Funke or double-click `StartGame.cmd`. The launcher compiles the local project incrementally before starting the local Release runtime. No cloud service or Node runtime is involved.

Choose **New factory** for resource crafting, or **Creative mode** for the same inventory and recipe interface with no ingredient cost or crafting delay. Both modes start with an empty factory and empty inventory. Creative recipes create items immediately; placing a building still takes that crafted item from inventory. There are no contracts, deadlines, scores, victory screens or mandatory goals.

## Playing

| Input | Action |
| --- | --- |
| WASD / middle drag | Explore with the construction camera |
| Wheel / Home | Zoom / return to the starting area |
| Hold F over a deposit | Gather iron ore, copper ore or stone |
| E / Inventory | Open the inventory and recipe grid |
| Click recipe / Shift-click | Craft one recipe / queue five |
| Click a building stack | Select it for placement |
| 1-6 | Select drill, belt, furnace, assembler, chest, inserter |
| Q | Empty hand |
| Left drag | Place buildings; consecutive belts turn to follow the drag |
| R | Rotate the placement direction, or the hovered building with an empty hand |
| Right drag | Return buildings and their contents to inventory |
| Left-click machine with empty hand | Inspect its stored items |
| Select inventory stack, Insert selected | Insert one item; Shift inserts up to ten |
| Take contents | Return a machine's input, output and storage to inventory |
| F5 / F9 | Save / load |
| Esc / F1 | Close current panel or selection, then pause / show help |

Gather stone to craft a furnace; gather iron and copper for plates and building parts. Personal plate recipes allow bootstrapping without prebuilt machines. Recipes show exact material requirements and time. Ingredients are reserved when queued; cancelling the last job refunds them. A completed craft waits if inventory is full.

The inventory has 40 slots with stacks of up to 100. Chest storage holds 400 items. Buildings, resources and components all use the same inventory. Removal is refused if the building and all its contents would not fit, so nothing is silently discarded.

## Logistics

Drills extract the deposit below them and can output onto a belt. Belts form visible straight routes, corners and side merges. Cargo moves through the entry side, center and output side; a head-on belt rejects reverse flow. A blocked belt retains its item.

Inserters occupy one cell, pick up from the adjacent cell behind them and place into the cell ahead. Use them between belts and furnaces, assemblers or chests. Furnaces smelt iron or copper ore; assemblers make gears from two iron plates. Mixed chests supply compatible items to the receiving machine. An arm keeps held cargo if its destination is removed or blocked.

Example: drill -> belts -> inserter -> furnace -> inserter -> belts -> inserter -> assembler -> inserter -> belt -> inserter -> chest. This is an optional production example, not an objective.

## Saves and scope

Freeplay uses its own `factory-freeplay-v2` slot. Old contract saves remain untouched and are not imported. Saves include mode, stacked inventory, reserved crafting jobs, deposits, machine buffers, held cargo, progress, rotations and camera. Invalid saves leave the current factory intact.

Launcher saves: `UserData/BenScr/Factorio`. Tests use the separate `Tests/UserData` root and `factorio-freeplay-qa` profile.

This is a small factory prototype: six building types, six raw/processed resources and twelve recipes. Exploration currently uses a free camera on a finite 97 x 73 cell map with 88 resource cells. Belts carry one item per cell; dual lanes, splitters and underground belts are not implemented. There is no walking character, power, fuel, fluids, research or combat.

`Tools/build_all.py` owns scenes, prefabs and structural belt/arm art; `Tools/presentation.py` owns shared UI frames, atlas metadata and feedback sounds. Existing Data Assets remain authoritative after the one-time contract migration. Python with Pillow is needed only to regenerate content. See `Tests/Verification.md`.
