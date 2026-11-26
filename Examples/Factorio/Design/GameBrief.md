# Rust & Steel - free exploration and factory building

Selected art direction: Rust & Steel. Updated 13 November 2026 to replace the contract prototype with the user's requested open factory game.

Windows desktop, 2D grid construction with a free camera. No forced goals, deadlines, score, winning or losing. Gather finite iron, copper and stone deposits, craft parts and buildings, place a factory and expand production.

New factory: empty inventory, normal material costs and timed queued crafting. Creative: exactly the same inventory and recipes, with instant, free crafting for every item. Building placement consumes the crafted item in both modes. The player can inspect and manually fill/empty machines.

Content:
- ItemDefinition: iron ore, stone, copper ore, iron plate, copper plate, gear, and six building items.
- RecipeDefinition: result/count, time and ingredient list. Raw-resource recipes are creative-only; the ordinary mode gathers deposits. Plate recipes bootstrap personal crafting.
- MachineDefinition: drill, conveyor, furnace, assembler, storage chest, inserter.
- FactoryRules: catalogs, inventory slot limit, mining time, map bounds and colors.
- ResourceDeposit: finite quantity per authored cell.

Belts visually connect at exact tile boundaries; straight segments, bends and side merges update from neighbours. Single-item backpressure prevents loss. Inserters pick up behind and deliver ahead, including belt-to-machine and machine-to-chest transfers. Held cargo survives blocked or missing destinations. Mixed chests select a compatible ingredient.

Exploration is a finite 97 x 73 cell frontier with 88 authored resource cells. Walking characters, infinite terrain, research, combat, power, fuel, fluids, splitters, underground belts and two-lane belts are outside this version.

All initial terrain, resource cells, UI and managers are scene-authored. New machines and cargo visuals instantiate prefabs. build_all.py and presentation.py own scenes, prefabs and structural/UI artwork. Original ImageGen textures are used unmodified. Existing Data Assets are migrated once from credits/contracts; subsequent regeneration preserves tuning.

Tests cover gathering -> timed crafting -> placement -> machine production -> belt/inserter transport -> storage, plus creative mode, capacity/backpressure, saves, malformed snapshots, real input, UI blocking and pause. Human play feel is not established by automated tests.
