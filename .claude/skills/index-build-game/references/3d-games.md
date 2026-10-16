# Building 3D games with Index

Lessons from building a complete 3D game (MarbleSky: menu + level select + four rolling-marble levels with crates, coins, moving platforms, checkpoints, PC and mobile portrait) end to end as an agent. Read this before the first 3D scene; the house rules in `SKILL.md` still apply unchanged.

Component keys: `scene-json.md` → "3D" and "Physics 3D". Engine side of the physics: `Github-Docs/Documentation/Engine-Core/Physics-3D.md`.

## 1. Project setup

- `project.json` → `"modules": ["Index.3D"]` (3D projects open the Editor View in 3D). `index create` makes a 2D project; set the module yourself.
- Scenes need a `Camera3D` (`"background": 0` for the procedural sky) and a `DirectionalLight` with `castShadows: true`. A scene without a light gets a default one, which looks flat.
- Graphics for a good-looking 3D game, in `project.json` → `graphics`: `"shadowQuality": "High"`, `"shadowDistance": 60–80`, `"maxLights": 16`, `"toneMapping": "ACES"`, `"exposure": ~1.05`.

## 2. Build levels with a generator, never by hand

A 3D course has hundreds of entities (road segments, rails, coins, crates, lamps, clouds). Write one Python generator in the project (`Tools/generate_scenes.py`) that writes all scenes, prefabs and materials, and re-run it after every change. Regenerating is cheaper than patching JSON.

- **Write atomically and only on change**, with `json.dumps(..., ensure_ascii=False)` and UTF-8. **Never round-trip a file with umlauts through PowerShell `Get-Content`/`Set-Content`** (Windows PowerShell 5.1 reads UTF-8 without BOM as ANSI): the result is double-encoded mojibake (`Ã¼`) that the game shows verbatim. Edit generator sources with the Edit tool or Python.
- Keep stable GUIDs for materials/prefabs/textures (fixed strings in the generator; only write a `.meta` if none exists).
- Give every entity a deterministic uuid (counter per scene).

### Rotation and direction conventions (verified)

- `Transform3D.rotX/rotY/rotZ` are Euler degrees and compose as **R = Ry · Rx** (pitch first, then yaw). Local −Z is "forward".
- A segment from `a` to `c` (x, z, y-top): `yaw = atan2(-dx, -dz)`, `pitch = atan2(dy, horizontal)`. **`rotX > 0` lifts the −Z (forward) end.**
- Its up-normal is `(sin(yaw)·sin(pitch), cos(pitch), cos(yaw)·sin(pitch))`, its right vector `(cos(yaw), 0, −sin(yaw))`. Box centre = top-centre − normal · thickness/2.
- An upright spinning coin is a cylinder with `rotX: 90` and the spin in `rotY`.

### Road / path building block

Describe a course as paths of points `(x, z, topY)` with a width; generate one box per segment. This produces long, readable courses quickly (straights, slopes, S-bends, kickers). Put bends only where both neighbouring segments are flat.

### No overlapping geometry (house rule 10) — how to get it right

- **Bends:** neighbouring segments overlap on the inside of a bend. Fill the outside with a round pad (cylinder, `MeshCollider3D` convex) placed **~3 cm below** the road top, and give alternating segments a ~1 cm height offset. Coplanar overlaps otherwise z-fight (flicker).
- **Rails/walls at bends must be mitred:** change the length at each end by `offset · tan(turn/2)` (`offset` = signed lateral distance, `turn` = signed yaw change) and shift the centre by half the difference. Without that, rails cross each other and poke into the road.
- **Props sit on a surface:** lamps inside the road edge (not beside it in the air), crates `0.5 + layer` above the top, coins ~0.9 above the road, spaced with a little gap (crate spacing 1.04, not 1.0).
- Take a screenshot of every bend, ledge and prop group (see §7) before calling a level done.

### Clean courses: generate meshes, not boxes

Boxes per road segment overlap at bends (crossing textures, corner pads, rails with gaps) — it reads as "unfinished". Generate the course as meshes instead (MarbleSky `Tools/meshkit.py` + `road_mesh`/`rail_mesh` in `generate_scenes.py`):
- One mitred cross-section per path point (the same frames feed the welded collision strip), extruded into top (road material), chamfer + sides + bottom (darker edge material) and end caps. World-space UVs (1 repeat = 4 m) so planks/tiles run on through bends and slopes; give those materials `tiling 1`.
- Railings: posts every ~2.4 m + round bars whose rings lie in the mitre plane; keep invisible box colliders for the rails. Put them on the deck, inset ~0.2 m from the edge (frames of `width − 2·inset`) — rails placed beside the road float in the air. Give every road and platform rails by default (gaps where paths join), and keep obstacles placed across the road clear of them.
- Terrain levels (forest, meadow): a height field (function of x, z) sampled every ~2 m → grass triangles + the same quads in the welded collision mesh; draw the dirt path as its own ribbon along the path curve ~5 cm above the grass. Colouring grid cells by "is on the path" gives stair-stepped edges.
- Decor (trees, rocks) must test against the course footprint before placing, or a tree ends up inside a platform.
- In-game track editors work with runtime meshes: `Mesh.Create()` + `SetData(positions, indices, normals, uvs)` for the visuals and a `MeshCollider3D` whose `CustomMesh` is a second runtime mesh with the welded road tops (physics rebuilds when the mesh changes). Spawn parts from script-free prefabs and add the scripts with `AddComponent<T>()` right after `Entity.Instantiate`, then set their fields (a script already in the prefab has no instance yet). `Entity.Create` gives a Transform2D in projects with the 2D module: swap it for Transform3D. Scripts that cache references to their parts should do it in `OnStart`, not `OnAwake`.
- Tap/click picking in 3D: `Physics3D.RaycastFromScreen(Input.MousePosition, out hit)` (or `Camera3D.Main.ScreenPointToRay`); `Camera3D.WorldToScreenPoint` for markers over objects. Treat a press that moved less than ~14 px as a tap, the rest as a camera drag, and ignore presses that start on UI (`UIPointer.HoveredEntity != null`).
- Endless tracks: generate chunks with the same layout code as the editor, each starting with a landing + checkpoint and ending in a jump, so chunk seams (separate mesh colliders) are never rolled over; keep 3 ahead, destroy the ones behind, and move the cloud floor and kill height with the marble.
- Ghost runs: record position + scale every 0.05 s of level time and keep the run in `SaveData` (a `float[]`) when it beats the stored one; replay it on a see-through marble without collider.
- Many levels: once a few hand-made levels play well, turn their proven pieces into a cursor-based builder (MarbleSky `Track`: straight, bend, kicker, pad_up, shuttle, planks, beam, island, switch_bridge, slit, heavy, goal — each with the exact gap/jump sizes of the level it came from) and write new courses as short lists of pieces. Ravines in a height field: drop the grid rows inside (edges on grid lines) and add rock walls on the cut edges. Compute the respawn height from the lowest course point, not the start.
- Static platforms/steps: bevelled boxes appended to the same per-material level mesh (one OBJ + one entity per material per level = few draw calls) plus an invisible collider.

### Mechanics that make a marble level fun (all small scripts + static events)

- **Floor switch → channel → SwitchMover:** a trigger plate fires `Activated(channel)`; kinematic gates sink / bridges rise by `Travel` over `Duration`. Put plates where the player must aim or jump (on a step, at the edge, between bumpers).
- **Size portals:** ring trigger sets small/normal/big — `Transform3D.Scale` eased over a few fixed steps (the physics body is rebuilt with velocity kept; lift the marble while growing), mass per size, jump as `VelocityChange` so height doesn't depend on mass. Small fits slits in walls; big (mass ~12) shoves 25 kg blocks and presses heavy plates (`MinMass`). Remember the size at each checkpoint for respawns; pull the camera back for big.
- **Tilting planks** (kinematic, rotZ sine) for balancing; **hurdles** (0.95 m) and **step blocks** for jumping; coins on top as optional rewards.
- **Satisfying feedback:** coin combo climbing a scale (8 pitched clips), coin magnet, landing dust + thud + small camera shake (shake the rotation — position is smoothed), FOV kick on boost, a HUD toast ("Checkpoint!", "Mini-Murmel!", "Zu leicht – werde größer!").
- Verify with an env-var probe: teleport (`tp:x,y,z`), set size, push forward and log position/size/switch events; check the normal marble is blocked where it should be.

### Texture stretching

Primitive UVs are per face and stretch with scale. A 2×1×10 plank stretches its texture 5× along its length. Keep a material's `tiling` matched to typical piece sizes, use separate materials (and `tiling`) for long pieces, or accept a little stretch on wood-like textures where it reads as grain.

## 3. 3D physics recipes (Jolt)

- **Static level geometry:** collider only (`BoxCollider3D: {}`), no `Rigidbody3D`. Moving a static collider's Transform3D teleports it.
- **Player ball:** `Rigidbody3D {type 2, mass 1, linearDrag .25, angularDrag .3, continuous: true}` + `SphereCollider3D {friction .8}`. `interpolate` defaults to `true`; keep it on for anything the camera follows.
- **Pushable props (crates):** a prefab with `Rigidbody3D {type 2, mass .35}` + `BoxCollider3D`, placed as prefab instances with `Overrides: {"Transform3D.posX": …, "Transform3D.rotY": …}`.
- **Pickups / goals / checkpoints:** a collider with `"isTrigger": true`. The script on that entity gets `OnTriggerEnter3D(Collision3D other)`. Identify the player by **singleton** (`other.Other != MarbleMovement.Instance?.Entity`), never by name. Kinematic platforms and crates also enter triggers.
- **Moving platforms:** `Rigidbody3D {type 1}` (kinematic) + a small script that sets `Transform3D.Position` in **`OnFixedUpdate`**. The physics system turns that into a smooth kinematic move that carries the player.
- **Mesh colliders:** `MeshCollider3D {useMeshRenderer: true}`; set `convex: true` for anything dynamic or for bend pads (a concave mesh only collides as static/kinematic).
- **Rolling surfaces = one welded mesh.** A ball hops at every seam between separate box colliders (and between children of one compound), even when the tops are exactly flush. For courses, keep the road boxes visual only and let the generator also write one OBJ of the walkable tops as a shared-vertex triangle strip (mitred at bends) → `MeshCollider3D {useMeshRenderer: false, customMesh: <obj guid>, convex: false}` on a "Course Collision" entity. Rails/walls/props can stay boxes.
- **Grounded check:** use contact normals: in `OnCollisionEnter3D`/`OnCollisionStay3D` set a 2-step counter when `collision.Normal.Y > 0.6`, decrement it in `OnFixedUpdate`. A downward raycast from below the ball misses thin mesh surfaces (it starts beneath them) and a ray from inside hits the ball itself.
- **Grip:** marble `SphereCollider3D {friction 1, bounciness 0}`, road surface friction ~0.9, `gravityScale` ~1.3 so it stays on slopes and doesn't float after crests.
- **Cursor on desktop:** hide it while playing in built desktop games and show it when a pause/result screen is open: a HUD script under `#if INDEX_BUILD && !INDEX_ANDROID && !INDEX_IOS` that sets `Cursor.Visible = panelOpen` and restores `true` in `OnDestroy`. Never in the editor (it would steal the user's cursor).
- **Colliders on child entities** join the nearest `Rigidbody3D` ancestor's body (compound). A body is a trigger only if all its colliders are.
- `OnCollision*3D`/`OnTrigger*3D` go to the scripts of the **body's** entity (the Rigidbody3D entity, or the static collider's own entity).

## 4. Movement feel

- **Steer by velocity, not raw force.** Compute a target horizontal velocity (`input · MaxSpeed`) and apply `AddForce((target − v_horizontal) · Responsiveness, ForceMode3D.Acceleration)`, clamped to a max acceleration. Use a lower gain with no input (gentle braking) and in the air (air control). Raw `AddForce(dir · force)` with an "only push if below max speed" check feels floaty, and slopes make the ball ever faster. Good marble values: MaxSpeed 10, Responsiveness 3.5, MaxAcceleration 22, IdleBraking 0.25, AirControl 0.35, JumpImpulse 6.5.
- **Camera-relative input:** up = away from the camera. With a follow camera that turns with the direction of travel (yaw eased toward `atan2(−vx, −vz)` when horizontal speed > 3), curves stay readable and controls stay intuitive.
- **Follow camera:** position eased with `1 − exp(−k·dt)` (frame-rate independent), using `Time.UnscaledDeltaTime`; set the rotation directly from `(pitch, yaw, 0)`.
- **Portrait screens:** if `Application.AspectRatio < 1`, pull the camera back/up (~1.35×) and widen `Camera3D.FieldOfView` (~78°).

## 5. Smooth motion — measure, don't guess

When the user reports judder or "motion blur", measure with a temporary probe instead of guessing:

1. Gate the probe behind an env var (`MYGAME_PROBE`). It drives constant input and logs one line per frame from the follow camera's `OnUpdate`: `dt`, player position, camera position (format with `CultureInfo.InvariantCulture`).
2. Run with `Run.ps1 … -Seconds 8`, pull the lines from `%LOCALAPPDATA%\Index\Logs\Index-Runtime.log` (filter strictly: other sessions' lines may be interleaved), and compute with a small Python script: dt mean/σ, player speed and its frame-to-frame σ, σ of the per-frame change of (player − camera).
3. Fix, measure again, and delete the probe.

Root causes found this way, both now fixed in the engine:
- Physics runs at a fixed 50 Hz, so without interpolation the body moves in steps → `Rigidbody3D.Interpolate` (default on, runs before scripts so the camera follows the smoothed pose).
- With VSync the measured frame delta jittered ±25 % while the display advances evenly → the engine snaps the delta to the refresh interval.

Targets after the fix: dt σ < 0.3 ms, camera↔player offset change σ < 0.5 mm per frame.

## 6. Looks

- **Materials** (`.material` JSON, PBR): stone/wood with procedural textures (write PNGs with a tiny pure-Python encoder: `zlib` + `struct`), gold `metallic 1, roughness .22`, glassy player `metallic .25, roughness .06` with a little emissive, goal/coins/lamps with `emissiveColor` + `emissiveIntensity` (2–5) so they glow.
- **Lights:** one shadow-casting sun (`rotX ≈ −40`), small warm `PointLight`s (`intensity 6, range 6`) in lanterns, a coloured point light at the goal.
- **Sky:** set `skyTop`/`skyHorizon`/`skyGround` on the `Camera3D`. For "high in the sky" games make `skyGround` blue too, otherwise the lower half reads as brown ground, and put a layer of cloud meshes ~30 m below the course so there is something underneath.
- **Fog:** `Camera3D` `fog: true`, `fogColor` ≈ the horizon color, `fogStart`/`fogEnd` (e.g. 35/110) so the goal is not visible from the start and the course fades into the sky. Sky dome materials get `"fog": false`.
- **Clouds:** clusters of 3–4 flattened spheres (`scale (s, .55s, .8s)`) with a white, slightly emissive material, below and beside the course.

- **Painted skyboxes:** `Camera3D` only has a gradient sky. For sunset/night/storm themes generate an equirectangular PNG (numpy noise sampled on the unit-sphere direction → seamless) and put it on an inward-facing sphere OBJ (the sphere primitive's UV layout: `v=0` at the zenith) with an **unlit** material (`"lit": false`), scale ~450 (inside `far`), `castShadows false`, and a tiny script that keeps it centred on the camera. Paint the sun/moon at `-forward` of the `DirectionalLight` (`(cos p·sin y, −sin p, cos p·cos y)` for rotation `(p, y)`), and match the camera's `skyTop/skyHorizon/skyGround` (they still drive ambient light), sun color/intensity and cloud material to the theme.
- **Animated characters:** a rigged `.glb` (skin + clips) on a `SkinnedMeshRenderer`; one small script calls `Play("Idle"/"Run"/"Jump", 0.15f)` from the movement state every frame (re-playing the running clip is a no-op). Attach held items with `TryGetBone`.
- **Own 3D models:** write OBJ files from a Python generator (`v`/`vt`/`vn`, counter-clockwise front faces, the loader flips `vt.y`): revolve profiles for coins, domes, rings and low-poly rocks/trees, flat normals for a low-poly look. `MeshRenderer {"mesh": 0, "customMesh": "<guid>", "material": …}`.

## 6b. Effects with Particle System 3D

`ParticleSystem3D` (keys in `scene-json.md`) gives fire, smoke, sparks, dust, pickups and explosions without custom code. Put each effect on its **own child entity** with a `Transform3D` (one particle system per entity; it conflicts with `ParticleSystem2D`), so you can offset it and toggle it independently. Verified-looking presets (from a test scene, screenshot-checked):

| Effect | Key values |
|---|---|
| Fire / torch | `shape 2` (cone, opens along local **+Y**, no rotation needed), `radius .25`, `coneAngle 12`, `speed 2.5`, `speedVariation .3`, `lifetime 1.2`, `lifetimeVariation .3`, `size .45`, `emitOverTime 120`, `color` orange, **`blendMode 1`** (additive), scale curve 1 → 0.1, color gradient yellow-white → red, alpha 1 → 0 |
| Smoke | `shape 1` (hemisphere), `speed .8`, `lifetime 3`, `size .6`, `emitOverTime 20`, grey `color` with `a .6`, `blendMode 0`, `randomRotation`, `angularVelocity 30`, scale 0.5 → 2, alpha keys 0 → 1 (at 0.2) → 0 |
| Spark burst / explosion | `shape 0`, `emitFromSurface`, `radius .1`, `speed 5`, `speedVariation .4`, `size .08`, `emitOverTime 0`, `useGravity`, additive, alpha 1 → 0, `bursts [{count 80, interval 1}]` — or `emitOverTime 0`, no bursts, `playOnAwake false` and call `Emit(80)` from a script |
| Pickup sparkle | sphere, `radius .4`, `speed .3`, `size .1`, `emitOverTime 15`, additive, gold color, alpha fade; child of the coin |
| Dust trail behind the player | child of the player, `simulationSpace 1` (World: particles stay behind), hemisphere, `speed .3`, `size .3`, Alpha, light brown, set `EmitOverTime` from script by speed (0 when airborne) |

Rules that matter:
- **Additive** for anything that glows (fire, sparks, magic, coins) — it never needs sorting and brightens what is behind it. **Alpha** for smoke and dust; Alpha particles sort within their system, but two overlapping Alpha systems can sort wrongly — keep big smoke columns apart.
- **Always fade out** with `colorOverLifetimeAlphaKeys` ending at `a 0`, otherwise particles pop out of existence.
- **World vs Local:** World (default) for trails and smoke from moving things; Local for effects that must stay attached (a shield aura, a jet flame on a moving ship).
- No texture = soft round dots, which already look good for fire/sparks/dust. A texture (`"texture": "<GUID>"`) is drawn as-is on the billboard; use a soft-edged PNG with transparent background, not a hard square.
- Size is in world units: 0.05–0.15 for sparks, 0.3–0.6 for flames, 0.5–2 for smoke (grow it with the scale curve).
- One-shot effects from scripts: `GetComponent<ParticleSystem3D>()!.Emit(n)` works even while stopped; `Stop()` lets live particles finish, `Clear()` removes them.
- **`Emit` uses the emitter pose from the last particle update.** A shared burst emitter that a script moves to the event position and emits right away spawns the burst where the previous one was. Move it, then emit two frames later (a small pending queue in an `EffectsPlayer`); bursts at destroyed things (a collected coin) must come from such a shared emitter anyway.
- Particles are unlit and drawn after all meshes; very bright additive effects next to emissive materials read as "glowing" without bloom.
- **Seeing them:** in the Editor View particles only move while a selected system is previewed (Play overlay bottom right) or in play mode. For screenshots run the Runtime (`Run.ps1 -App Runtime … -Seconds 4 -Shots 2 -Interval 1.5`): the second shot shows the steady state, the first catches the first burst.

## 7. UI for PC and mobile (landscape + portrait)

- `project.json`: `"uiReferenceWidth": 1200, "uiReferenceHeight": 1200, "uiScaleMode": "Fit"`, `"mobileOrientation": "Portrait"` for a vertical phone game.
- Every UI root and full-screen panel: `RectTransform2D` with `"fillParent": true` (it spans the safe area). Anchor HUD items to corners/edges; keep centred panels ≤ ~900 wide so they fit portrait.
- **Text above panels:** a `TextRenderer` inside a panel needs a higher `sortOrder` than the panel's `Image` (e.g. panel 15–16, text 17, button 20, button label 21), or it draws behind it.
- **Touch controls only on mobile:** wrap the decision in `#if INDEX_ANDROID || INDEX_IOS` (the build defines them per platform) and add a `ShowOnDesktop` bool field for testing in the editor. Disabling the controls' own entity in `OnAwake` hides them on PC.
- **Two thumbs = read touches.** UI Buttons/Toggles react to every finger (two can be held at once), but sliders, scroll views and `Input.MousePosition` only see the first finger. For a joystick plus a jump button, loop `Input.GetTouch(i)`: a finger whose `Began` lands on the jump button (hit-test `RectTransform.Position`/`Size`) jumps, the first other finger drives the stick until its `Ended`. Keep a mouse path for desktop testing.
- Menus: house rule 9 (title + Play/Quit, separate level select with cards, unlock progression via the saved results).
- **Sprites, not flat rectangles:** there is no 9-slice, so generate each button/panel/card sprite (white, rounded, vertical gradient, darker outline and bottom lip; PIL with 4× supersampling) at the size it is shown and tint it with the `Button` colours / `Image` colour. Icons are white on transparent; coins, stars and the logo are coloured. Put a screen's content in a centred 1200×1200 child so title, grid and back button stay together in portrait.
- **Shop/skins:** coins banked into a `Wallet` (PlayerPrefs) at the goal; skins listed in a generated catalog; the materials are wired in the scene as disabled children of a "Skin Library" entity (catalog order) and the marble copies `children[i].MeshRenderer.Material` — `Material.FromAssetUUID` in game code is what the lint (rule 2) flags.
- **Polish:** a `ScreenFader` (full-screen overlay child, switched off when transparent) that `SceneFlow.Load` fades out through; a `PopIn` (scale 0.86 → 1 with overshoot) on every panel; three stars per level (finished / all coins / no falls) on the result panel and the level cards.
- **Shipping:** build with the Release profile (`index build --release`); the statistics and log windows are then off unless `project.json` sets `showRuntimeStats` / `showRuntimeLogs` to `true` (on phones they cover the pause button).

## 8. Verify like this

- **Scripts:** the Runtime loads `<Project>/bin/<its config>/`. The default `Run.ps1` Runtime is **Release**, so compile with `dotnet build <Name>.csproj -c Release`. A Debug-only build makes the Runtime silently run the old DLL (no script markers in the log is the tell).
- `index validate --project <root>` (engine load test) + `index_game_tool.py validate <root>`: both must report 0 errors / 0 warnings.
- `scripts\dev\Run.ps1 -App Runtime -Project <root> -Seconds 7 -Shots 1 -ExtraArgs "--scene=Level3"`: start a specific scene; use ≥ 6 s, or the shot catches the splash screen.
- **Screenshot a specific spot:** temporarily edit the generated scene (move the player/camera there, or enable a hidden panel), run, then re-run the generator to restore it.
- **Win flow:** an env-var probe that teleports the player into the goal proves goal → result panel → saved best time. Afterwards delete both the probe and the fake results it saved (`%LOCALAPPDATA%\<Company>\<ProductName>\prefs.json`).
- None of this proves the game is fun or beatable; say so and give the user a short play-test checklist.
