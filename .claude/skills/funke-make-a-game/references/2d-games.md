# 2D game recipes

Engine-specific recipes for the common 2D genres. The house rules in `SKILL.md` still apply: levels authored or generated as JSON, stats in Data Assets, built-ins first. Component keys are in `scene-json.md`, and art and pixel density in `funke-game-art`.

## Units, cameras, sorting

- **One world unit = one tile.** Pick the tile size in art pixels once (16 or 32, from `Design/DesignLanguage.md`) and give every `SpriteRenderer` `"pixelsPerUnit": <tile px × upscale>` (art saved 4× → 64 for 16 px tiles) with `Transform2D` scale 1. Each sprite then keeps its native size and pivot, so the pixel density stays the same everywhere by construction.
- **`Camera2D.orthoSize`** is half the visible height in world units. For pixel art at k screen pixels per art pixel: `orthoSize = screenHeight / (2 · tilePx · k)`. There is no built-in pixel-perfect camera: a small `PixelCamera` script sets `Camera2D.Main.OrthographicSize` from `Window.Height` (k = the largest whole number that still shows the rows you need) and rounds the followed position to `1 / (tilePx · k)` units, so sprites don't shimmer while scrolling.
- **Camera follow** is not built in. Write a `CameraFollow` script: ease toward the target with `1 − exp(−k · dt)` (frame-rate independent), a dead zone, and a look-ahead in the movement direction, clamped to level bounds that a `LevelBounds` entity in the scene provides. Shake comes from `Funke.Tween` (`Camera2D.Shake(duration, strength)`).
- **Sorting:** `sortLayer` (background 0, world 1, foreground 2, …) then `sortOrder`. Top-down games sort by height: a moving sprite sets `SpriteRenderer.SortingOrder = (int)(-y * 100)` each frame, and static props get the same value from the generator. World `TextRenderer`s draw after all sprites.
- **Parallax:** background layers are children of a `Parallax` entity, and one script moves each layer by `camera.x · factor` (0 = far, 1 = with the world). Tile them horizontally with a texture whose `.meta` uses `wrapU: Repeat`.

## Platformer

- Player: `Rigidbody2D {bodyType 2, freezeRotation true, gravityScale ~3}` + `BoxCollider2D {friction 0}` (friction 0 stops the player sticking to walls) + `registerContacts: true` if scripts need collisions.
- **Horizontal:** target velocity `input.X · MaxSpeed`, approached with separate acceleration on ground and in the air; write `LinearVelocity` and keep its `Y`.
- **Jump feel**, which makes or breaks the genre (all values in a `PlayerSettings` Data Asset):
  - ground check: `Physics2D.OverlapBox(feet, new Vector2(width * 0.9f, 0.1f), 0, groundMask)`, or a short `Raycast` down from each foot corner;
  - coyote time ~0.1 s after leaving a ledge, jump buffer ~0.1 s before landing;
  - variable height: when the button is released while rising, multiply `vy` by ~0.5;
  - faster fall (`gravityScale` up while falling), clamp the fall speed;
  - squash/stretch on jump and land (`Funke.Tween` `PunchScale`), dust particles on landing.
- **Tiles + colliders:** don't give every tile its own collider. A ball or player catches on the seams between neighbouring boxes. The level generator merges each horizontal run of solid tiles into one `BoxCollider2D` (or merges rectangles greedily). The visuals can stay one sprite per tile, or use `Funke.Tilemap2D`, which has no collision of its own.
- Hazards and pickups: a sensor collider (`"sensor": true`) + `OnTriggerEnter2D`. Identify the player through its singleton (`other.Other == PlayerMovement.Instance?.Entity`), never by name.
- One-off moving platforms: `bodyType 1` (kinematic), moved in `OnFixedUpdate` along waypoints given as entity references.

## Top-down (action, twin-stick, RPG)

- `Rigidbody2D {bodyType 2, gravityScale 0, freezeRotation true}` and velocity = `Input.GetAxis()` (a `Vector2`, already smoothed; `GetAxisRaw()` for snappy movement) × speed. Normalise diagonals.
- Aim at the mouse: `Camera2D.Main.ScreenToWorld(Input.MousePosition)` (or `CameraUtility.GetMousePosition2D()`), and rotate with `TransformUtility.LookAt2D`.
- Projectiles: a prefab with `Rigidbody2D` + sensor collider, a `Projectile` script holding a `WeaponData` reference (damage, speed, lifetime) and `Entity.Destroy(lifetime)`.
- Enemies: `EnemyData` per type, small behaviour scripts (`ChaseTarget`, `KeepDistance`, `ShootAtTarget`) combined per prefab instead of one enemy class with a type switch.
- Pathfinding isn't built in. For grid levels, write one `GridPathfinder` (A* over the generator's walkability grid, exported as a Data Asset or computed from the level) and use it from a `SceneSystem`.

## Grid and puzzle games (match-3, Tetris-likes, Sokoban, tower defense paths)

- The grid **model** lives in plain C# (arrays) inside one `Board` script. Entities only show it. Tile visuals are prefab instances that the board moves with `Funke.Tween` (`MoveTo` + `SetEase`).
- Levels (layouts, goals, move limits) are `LevelData` Data Assets, with layouts as a `List<string>` of rows (`"..#..", "..o.."`) that the board parses. The level select reads a `LevelCatalog`.
- Tower defense paths: a `Path` entity with waypoint children placed in the scene. Enemies follow the waypoint list, so a path is editable in the Editor and is not hidden in code.

## Juice that 2D games need

- **Hit-stop:** set `Time.TimeScale = 0` for 0.04–0.08 s. `WaitForSeconds` uses scaled time and would never finish, so restore it with `Tween.Delay(t, cb, unscaledTime: true)` or by counting `Time.UnscaledDeltaTime`.
- **Hit flash:** a white `color` on the `SpriteRenderer` for one or two frames, or a flash `.anim` clip on an `Animator`.
- Screen shake on big hits, particles on every impact and pickup, a coin counter that counts up, enemies that squash when they die.

## Checks

- Screenshot every level at its start and at one or two spots reached with `player.tp`. Check tile seams, sorting (feet in front of or behind props), and the HUD over the busiest area.
- A `level.load N` dev command plus one input script per level to prove it is finishable, or an offline simulation for physics-heavy levels (`testing.md`).
