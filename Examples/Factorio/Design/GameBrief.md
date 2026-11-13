# Factorio — Rust & Steel

Selected direction: variant 1, Rust & Steel (13 November 2026).

Windows desktop, 2D factory automation with a free construction camera. Build and rotate machines on a square grid. Connect iron drills, furnaces, assemblers and shipping depots using directional belts. Deliver 20 gears within eight minutes; completion and timeout offer a fresh start. After completion, continue in sandbox mode. Buildings refund their full construction cost when removed, so experimentation cannot consume the entire budget permanently.

This is an independent small prototype, not feature parity with Factorio. No combat, electrical networks, fluid simulation, research tree, multiplayer or infinite terrain in this version. The original game's art is not included.

The opening scene authors four machines and iron/copper deposits. The player completes the conveyor connections and can rebuild or expand. Machines send directly into an adjacent compatible machine or belt; inserters are outside this slice. Copper is scenery reserved for expansion; iron is the active production resource.

Content: MachineDefinition (drill, belt, furnace, assembler, depot), ItemDefinition (iron ore, iron plate, gear), FactoryRules (budget, time, objective, grid bounds), ResourceDeposit (authored iron cells). Small C# scripts own simulation, construction, camera, saves, presentation and menu flow separately.

Tools/build_all.py owns initial scenes and prefabs. Data assets are create-if-missing so Inspector changes survive regeneration. All starting objects are scene-authored. Player-built machines and in-transit item visuals instantiate prefabs.

Tests must exercise placement, rotation, deletion/refunds, UI input blocking, real production and delivery, saves, pause, completion, timeout and restart. Logs and in-engine screenshots are evidence; play feel remains a human judgment.
