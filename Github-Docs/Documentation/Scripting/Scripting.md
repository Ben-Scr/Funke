# Scripting

Funke uses **C#** as its primary scripting language (running on **.NET 10 LTS**). You write scripts to give your game behavior — move the player, spawn enemies, handle UI, manage game state. Funke also supports **native C++ scripts** for performance-critical code.

Visual Studio IntelliSense for Funke game projects requires Visual Studio 2026 (18.0+) because Visual Studio 2022 cannot evaluate `net10.0` projects. VS Code or Rider can use the installed .NET 10 SDK instead.

**Script editor:** Editor Preferences › Scripting › External Script Editor lists every detected Visual Studio, VS Code, JetBrains Rider and IntelliJ IDEA installation (standalone installer, JetBrains Toolbox 1.x and 2.x), each version on its own. Double-clicking a script or a console error opens the file at that line; Rider opens the project's `.sln`, IntelliJ IDEA the project folder. IntelliJ IDEA has no C# language support, so you get editing without code completion; use Rider for that.

If you just want the short version: make a class that inherits from `EntityScript`, override `OnUpdate`, attach it to an entity in the editor, and press Play.

\---

## The three kinds of script

Funke has three script base classes, each with a different **scope**:

|Base class|Scope|Use it for|
|-|-|-|
|**EntityScript**|One entity|Behavior for a specific object — a player, an enemy, a pickup.|
|**SceneSystem**|One scene|Logic over a whole scene — spawning waves, scoring, scene-wide rules.|
|**GlobalSystem**|The whole app|State that survives scene changes — a game manager, audio settings, save data.|

Pick the smallest scope that fits. Most gameplay is `EntityScript`; reach for `SceneSystem` when you need to work with *many* entities at once, and `GlobalSystem` for things that must persist between scenes.

\---

## Your first script

Here's a complete `EntityScript` that moves its entity with the arrow keys / WASD:

```csharp
using Funke;

public class PlayerController : EntityScript
{
    private float speed = 5.0f;

    public override void OnStart()
    {
        Print("PlayerController attached to: " + Entity.Name);
    }

    public override void OnUpdate()
    {
        var velocity = Vector2.Zero;

        if (Input.GetKey(KeyCode.W)) velocity.Y += 1.0f;
        if (Input.GetKey(KeyCode.S)) velocity.Y -= 1.0f;
        if (Input.GetKey(KeyCode.A)) velocity.X -= 1.0f;
        if (Input.GetKey(KeyCode.D)) velocity.X += 1.0f;

        if (velocity == Vector2.Zero) return;

        Entity.Transform.Position += velocity.Normalized() \* speed \* Time.DeltaTime;
    }
}
```

Notice three things:

* It inherits from `EntityScript`.
* `Entity` refers to the object this script is attached to.
* `Input`, `Time`, and `Transform` are part of the scripting API (covered below).

### Attaching a script to an entity

A script doesn't run until it's attached. In the editor, select an entity, **Add Component → Script** (a `ScriptComponent`), and choose your script class from the list. The engine finds your class, creates an instance, and starts calling its lifecycle methods when the game plays.

\---

## Lifecycle methods

The engine calls these methods at the right moments — you override the ones you need. The exact set varies slightly by script type.

**EntityScript** (per entity):

```csharp
public override void OnAwake() { }          // created, before OnStart
public override void OnStart() { }           // first frame, after everything is loaded
public override void OnUpdate() { }          // every frame
public override void OnFixedUpdate() { }     // every fixed (physics) step
public override void OnDestroy() { }         // entity/script destroyed
public override void OnEnable() { }          // enabled
public override void OnDisable() { }         // disabled

// Physics collisions (need a Rigidbody2D + collider):
public override void OnCollisionEnter2D(Collision2D c) { }
public override void OnCollisionStay2D(Collision2D c) { }
public override void OnCollisionExit2D(Collision2D c) { }

// Trigger overlaps (one collider has "Sensor" enabled; sensors don't block movement):
public override void OnTriggerEnter2D(Collision2D other) { }
public override void OnTriggerStay2D(Collision2D other) { }
public override void OnTriggerExit2D(Collision2D other) { }
```

Triggers fire on both entities involved, so a pickup can react in its own script (`other.Other` is the entity that touched it). At least one of the two needs a Rigidbody2D, as with collisions.

An entity that starts disabled has no script instances yet. When it is enabled during play (`entity.IsEnabled = true`, or its parent turns on), its scripts are created on the spot: scene field values first, then `OnAwake` and `OnEnable`; `OnStart` follows before its first `OnUpdate`. So code that enables an entity can use `GetComponent<MyScript>()` right away, and values it sets there are not overwritten by the scene.

The same holds for spawning: when `Entity.Instantiate(prefab, position, ...)` returns, the new entity and its children already have their script instances with the prefab's field values, and `OnAwake` and `OnEnable` have run after the clone was placed and parented (so `OnAwake` sees the spawn position). The spawner can configure the copy at once, and `OnStart` still runs before the copy's first `OnUpdate`, so it sees those values:

```csharp
Entity enemy = Entity.Instantiate(EnemyPrefab, spawnPoint, 0f);
enemy.GetComponent<EnemyAI>()!.Target = player;   // not null, also in the same frame
```

`IsEnabled` is the entity's own switch (the Inspector checkbox, Unity's `activeSelf`); `IsEnabledInHierarchy` tells whether it is actually active, i.e. it and all its parents are enabled (`activeInHierarchy`). Enabling a child while its parent is still off keeps the child off until the parent is enabled, so a panel's parts can be switched on before or after the panel itself.

`OnEnable` and `OnDisable` run after the whole hierarchy has switched: inside `OnEnable` both `IsEnabled` and `IsEnabledInHierarchy` read `true`, and in a parent's `OnDisable` its children already read `IsEnabledInHierarchy == false`. A script may switch its own entity off again in `OnEnable` (for example a list row with nothing to show): `Entity.IsEnabled = false` there takes effect, `OnDisable` follows, and children that were switched on in the same step are switched off again before their own `OnEnable` runs.

**SceneSystem** (per scene) has the same `OnAwake` / `OnStart` / `OnUpdate` / `OnFixedUpdate` / `OnDestroy` / `OnEnable` / `OnDisable`, plus a `Scene` property for querying entities — but no collision callbacks.

**GlobalSystem** (whole app) starts with `OnInitialize()` instead of Awake/Start, then `OnUpdate` / `OnFixedUpdate`. It is created at startup and persists across scene loads.

All three also receive application events like `OnApplicationPaused`, `OnApplicationQuit`, and `OnFocusChanged(bool focused)`.

Static fields and static classes keep their values across Play sessions in the editor, because scripts are not reloaded when Play stops. Code that keeps state there (a service, a package) resets it in `Application.PlayModeExited`, which the editor raises after it has restored the scenes.

> The split between `OnUpdate` and `OnFixedUpdate` matters: put \*\*rendering/input-rate\*\* logic in `OnUpdate`, and \*\*physics\*\* logic in `OnFixedUpdate` (which runs at a steady rate regardless of frame rate). See \[Startup Processes](../Engine-Core/Startup-Processes.md#stage-3--the-main-loop).

\---

## Exposing fields to the Inspector

Public fields can be shown and edited in the editor's Inspector using **attributes**. This lets designers tweak values without touching code:

```csharp
public class Enemy : EntityScript
{
    [Header("Stats")]
    [ShowInEditor("Move Speed")] public float Speed = 3.0f;
    [ClampValue(0f, 100f)]       public float Health = 100f;

    [Space(8f)]
    [ToolTip("Seconds between attacks")]
    public float AttackCooldown = 1.5f;

    [EnabledIf("UseCustomColor", true)] public Color Tint = Color.White;
    public bool UseCustomColor = false;

    [HideFromEditor] public int internalCounter;  // public but hidden
}
```

The main attributes:

|Attribute|Effect|
|-|-|
|`[ShowInEditor("Label")]`|Show the field (optionally with a custom label, and read-only if you want).|
|`[ClampValue(min, max)]`|Limit a numeric field to a range.|
|`[Header("Text")]`|A bold section header above the field.|
|`[ToolTip("Text")]`|Hover text explaining the field.|
|`[Space(pixels)]`|Vertical gap for grouping.|
|`[EnabledIf("Field", value)]`|Only enable this field when another field has a given value.|
|`[EditorReadOnly]`|Visible but not editable.|
|`[HideFromEditor]`|Hide a public field.|
|`[EditorIgnore]`|Hide all fields of a whole type.|

You can also expose **asset slots** (a texture, audio clip, etc.) using reference types — drag an asset onto the slot in the Inspector. See [Assets](../Assets/Assets.md#referencing-assets-from-c-scripts).

\---

## What scripts can do (the API)

The `Funke` namespace gives you a broad API. The main categories:

**Entities \& components**

```csharp
Entity e = Entity.FindByName("Boss");
e.GetComponent<SpriteRenderer>();
e.AddComponent<Rigidbody2D>();
Entity copy = Entity.Instantiate(prefab);
e.Destroy();
```

**Transform** — position, rotation, scale:

```csharp
Entity.Transform.Position += Vector2.Up \* Time.DeltaTime;
```

**Hierarchy** — parenting lives on the entity, not the transform:

```csharp
Entity.SetParent(other);          // 'other' is an Entity — keeps world position, rotation and scale (default; 2D and 3D)
Entity.SetParent(other, false);   // adopt the parent's frame instead (local transform kept)
Entity.SetParent(null);           // detach to the scene root
```

**Input** — keyboard, mouse, axes:

```csharp
Input.GetKey(KeyCode.Space);        // held
Input.GetKeyDown(KeyCode.Space);    // pressed this frame
Input.GetAxis();                    // WASD/arrows, else first gamepad's stick/D-pad, -1..1
Input.MousePosition;

// Mouse look: hide the cursor and read the motion. A locked or hidden cursor stays at the centre of the game view,
// so UI under the crosshair receives hover and clicks.
Cursor.Mode = CursorMode.Locked;   // or Cursor.Visible = false
Vector2 look = Input.GetMouseDelta();
```

In the editor the game holds the mouse only while the focused Game View plays (Play focuses it). **Escape** or switching apps gives the editor its normal OS cursor back and stops mouse-look; `Cursor.Mode` and `Cursor.Visible` keep returning what the game set. Clicking into the Game View (or focusing it) applies the game's cursor mode and custom cursor again, as does a new lock from the game, e.g. when it closes its pause menu.

`Input.MousePosition` and touch positions are pixels of the game's screen, origin top-left. `Application.ScreenWidth` and `ScreenHeight` are that screen's size: in the editor the Game View, not the editor window. To map the pointer onto UI, prefer a RectTransform's resolved `Position` (its centre on that screen) and `Scale` (reference points to pixels) over rebuilding the UI scaling yourself; they include the safe area and the scale mode.

```csharp
// Gamepads (Xbox layout; other pads are mapped automatically). Index 0-3, default 0.
Input.IsGamepadConnected();
Input.GetGamepadButtonDown(GamepadButton.A);
Vector2 move = Input.GetLeftStick();              // deadzone applied (Input.GamepadDeadzone)
float throttle = Input.GetGamepadTrigger(GamepadAxis.RightTrigger); // 0..1

// Touchscreens: every finger (the mouse only follows the first one). Track fingers by Id, not by index.
for (int i = 0; i < Input.TouchCount; i++)
{
    Touch touch = Input.GetTouch(i);   // Id, Position, StartPosition (viewport pixels), Phase
    if (touch.Phase == TouchPhase.Began && IsOverJumpButton(touch.Position)) Jump(); // your own hit test
}

// Phone/tablet motion sensors. Axes follow the rotated screen: +X right, +Y up, +Z out of the screen.
Vector3 tilt = Input.Acceleration;   // in g, toward gravity: (0, -1, 0) upright, (0, 0, -1) lying face up
Vector3 spin = Input.Gyroscope;      // rad/s, counter-clockwise positive
if (Input.IsAccelerometerAvailable) steer = tilt.X; // right edge down → positive
```

UI does not need any of this: Buttons, Toggles and other Interactables respond to every finger, so two on-screen buttons can be held at the same time. Sliders, scrollbars, scroll views, input fields and dropdowns follow the first finger.

The sensors are switched on by the first read (so the first frame can still be zero) and report zero on desktop and Web without such a sensor; Android and iOS need no permission.

**Time**:

```csharp
Time.DeltaTime;        // seconds since last frame (scaled)
Time.FixedDeltaTime;   // physics step length
Time.TimeScale = 0.5f; // slow motion (or 0 to pause game time); clamped to [0, 100]
```

**Window \& frame rate**:

```csharp
Window.IsFullScreen = true;            // also Window.Position, Window.Title, Minimize/Maximize/Restore()
Application.VsyncEnabled = false;      // present without waiting for the monitor refresh
Application.TargetFrameRate = 240;     // frame cap while VSync is off (0 = uncapped)
```

Window changes only apply in the built game: in the editor the game view lives inside the editor's own window, so these calls are ignored there (the Console says so once per call type). With VSync off on Direct3D 12 the game may tear; that is what lets it run above the monitor's refresh rate.

**Physics 2D** — raycasts and overlap checks:

```csharp
RaycastHit2D hit = Physics2D.Raycast(origin, direction, maxDistance);
Entity? hitEntity = Physics2D.OverlapCircle(center, radius);

// Only hit colliders on layers 0 and 3:
RaycastHit2D ground = Physics2D.Raycast(origin, Vector2.Down, 2f, PhysicsLayers.Mask(0, 3));
```

Each collider has a **Layer** (what it is) and **Collides With** (what it touches), both set in the Inspector or from code via `GetComponent<BoxCollider2D>().Layer` / `.CollisionMask`. Two colliders interact only when each one's Collides With includes the other's Layer. `IsSensor`, `Friction`, `Bounciness` and `Density` are available on every collider the same way. `Physics2D.Gravity` sets the world gravity; joints (`RevoluteJoint2D`, `PrismaticJoint2D`, `WeldJoint2D`) and multi-path polygon colliders are described in [Physics 2D](../Engine-Core/Physics-2D.md).

**Sprite animation** — add an **Animator** next to a Sprite Renderer, slice the texture in the Sprite Editor, and record the slices as discrete keys on the clip's **Sprite Renderer > Texture** track. Assign the looping clip to the Animator's **Clip** field. Scripts can change its playback speed:

```csharp
var anim = GetComponent<Animator>();
anim.Speed = 1.5f;
```

Use an Animator Controller to switch between idle, run and other clips; see [Animation](../Engine-Core/Animation.md).

**Tweens** — the **Funke.Tween** package (Package Manager) animates positions, rotations, scales, colors, alpha and any value with a getter and setter over time, DOTween-style: Penner easing, sequences, loops (restart, yoyo, incremental), callbacks, unscaled time for pause menus, shakes and punches. Tweens update themselves and die with their entity:

```csharp
using Funke.Tween;
transform.MoveTo(new Vector2(3f, 0f), 0.5f).SetEase(Ease.OutBack);
panel.ScaleTo(1f, 0.25f).From(0.8f).SetEase(Ease.OutBack).SetUnscaledTime();   // UI pop-in, also while paused
Tween.Sequence().Append(toast.MoveTo(shown, 0.3f)).AppendInterval(2f).Append(background.FadeTo(0f, 0.4f));
Camera2D.Main!.Shake(0.4f, 0.3f);
```

The full API is in the package's [README](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.Tween).

**Multiplayer** — the **Funke.Networking** package (Package Manager) hosts and joins games on every platform: desktop, Android and iOS over UDP, browsers over WebSocket, all on the same server. Messages are structs implementing `INetMessage`; prefabs spawned by the server appear on every client, and a **NetworkTransform2D** / **NetworkTransform3D** script on them keeps their movement smooth on the other screens:

```csharp
using Funke.Networking;
NetworkManager.StartHost();                                    // or StartServer(), StartClient("192.168.0.5")
NetworkServer.ClientConnected += client => NetworkServer.Spawn(PlayerPrefab, Vector3.Zero, 0f, ownerId: client.Id);
NetworkServer.RegisterHandler<Chat>((from, chat) => NetworkServer.SendToAll(chat));
NetworkClient.Send(new Chat { Text = "hi" });
if (NetworkObject.Of(Entity) is { IsOwner: true }) Move();     // scripts run everywhere; only the owner moves
```

Scripts that ship in a package are in **Add Component › Packages**; scenes and prefabs store them by their full class name (`"className": "Funke.Networking.NetworkObject"`). The full API, browser hosting (`wss://`) and internet play are in the package's [README](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.Networking).

**Post-processing** — add **Post Processing** to the entity with the main **Camera 2D** (its *Post Processing* toggle must stay on). `PostProcessing2D` exposes each effect as a value struct: read it, change fields, assign it back.

```csharp
var pp = GetComponent<PostProcessing2D>();

var bloom = pp.Bloom;                 // copy
bloom.Enabled = true;
bloom.Intensity = 3f;
pp.Bloom = bloom;                     // write back — editing pp.Bloom.Intensity directly changes only a copy

var vignette = PostProcessing2D.VignetteSettings.Default;   // Inspector defaults
vignette.Enabled = true;
vignette.Color = new Color(0.4f, 0f, 0f, 1f);
pp.Vignette = vignette;
```

Effects: `ColorGrading`, `Vignette`, `ChromaticAberration`, `Grain`, `Bloom`, `LensDistortion`, `Pixelated`, `GaussianBlur`. Values outside the Inspector ranges are clamped (for example `Bloom.Taps` to 7..500, `ColorGrading.Saturation` to 0..4); the ranges are in each field's XML doc.

**Saving data** — per game, stored under `Application.PersistentDataPath` (`%LOCALAPPDATA%/<Company>/<Name>`, from Project Settings):

```csharp
PlayerPrefs.SetInt("HighScore", 1200);          // small settings and progress values
int best = PlayerPrefs.GetInt("HighScore");     // saved automatically on quit / when Play stops; PlayerPrefs.Save() to force

SaveData.Save("slot1", myInventory);            // any class or struct, written as JSON
if (SaveData.TryLoad("slot1", out Inventory? inv)) { ... }
```

Writes are atomic: a crash while saving leaves the previous file intact.

**Audio**:

```csharp
Audio sfx = Audio.FromAssetUUID(clipGuid);
sfx.PlayOneShot();
Audio.MasterVolume = 0.8f;   // 0..1, everything the game plays (a settings menu's master slider)
```

Automated runs (`--input-script`, `--screenshot-at`, `--quit-at`, `--exec`, `--fixed-step`, `--background` or `--prefs-profile=<name>`, see [Testing](../Editor/Testing.md)) are muted by the engine whatever `MasterVolume` says; `--sound` keeps them audible. `Application.IsAutomatedRun` tells a game it runs under such a test, e.g. to skip an intro or an ad.

**3D sound** — sounds that come from a place in the world. Turn on **Spatial** in an Audio Source's **3D Sound** section: the source then plays from its entity's position (Transform 3D, or Transform 2D at z 0), gets quieter with the distance to the listener and moves to the side it is on. The listener is the main Camera 3D (in 2D games the main Camera 2D), so a sound left of the view is heard on the left. Audio Sources are 2D by default, so existing scenes sound as before.

| Field | Does |
|---|---|
| **Min Distance** | Full volume up to this distance |
| **Max Distance** | **Linear**: silent from here on. **Logarithmic**: the fade stops here |
| **Rolloff** | **Logarithmic** (default): `min / distance` like real sound, half as loud at twice the distance. **Linear**: evenly from full at Min Distance to silent at Max Distance |
| **Panning** | 0 = no left/right, 1 = a sound straight to the side is silent on the other ear (default 0.8) |
| **Doppler Level** | Pitch shift when the source or the listener moves (0 = off, the default) |

```csharp
blast.PlayOneShotAt(center, volume: 1f, minDistance: 8f, maxDistance: 64f);       // a one-shot from a point, Logarithmic
creak.PlayOneShotAt(door, 0.7f, minDistance: 2f, maxDistance: 16f, rolloff: AudioRolloff.Linear);
Audio.PlayOneShotAt(clip, position);                                               // does nothing for a null clip
source.Spatial = true;                                                             // MinDistance, MaxDistance, Rolloff, Panning, DopplerLevel
```

A one-shot that would be silent (Linear, beyond Max Distance) isn't started. To check sounds without listening, for example in muted test runs, the console command `audio.spatial` lists the listener and every playing 3D sound with its distance, gain, pan and left/right volume, and `audio.spatial x y z [min] [max] [log|linear] [panning]` tells how a sound at that point would be heard, without playing it.

**Music and files outside the project** — `AudioStream` plays an MP3, WAV or FLAC file from any path (a music folder, a download) and streams it from disk instead of loading it whole:

```csharp
AudioStream? song = AudioStream.Open(path);      // null if the file is missing or not audio
song?.Play();
song.Time = 42f;                                 // seek; Length, IsPlaying, IsAtEnd, Volume, Loop, Pause(), Stop()
song.Dispose();                                  // e.g. in OnDestroy; leaving Play mode closes every stream
```

**Music / SFX volume settings** — the **Funke.AudioMixer** package (Package Manager) adds volume groups (`Master` → `Music`, `SFX`, plus your own) with mute, fades, snapshots, saved settings and one-line slider binding:

```csharp
using Funke.AudioMixer;
AudioMixer.Attach(musicSource, AudioMixer.Music);        // AudioSource or AudioStream follows the group
AudioMixer.PlayOneShot(clip, AudioMixer.Sfx);
AudioMixer.BindSlider(musicSlider, AudioMixer.Music);    // settings screen; saved to PlayerPrefs
AudioMixer.TransitionTo("Paused", 0.4f);                 // after DefineSnapshot("Paused", (AudioMixer.Music, 0.3f))
```

The full API is in the package's [README](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.AudioMixer).

**Media controls** — for music and podcast apps on Android: a "now playing" notification with cover, previous / play-pause / next and a seek bar, also on the lock screen and for headset buttons. It belongs to the background-audio service, so keep `Audio.PlayInBackground` on while a track is loaded (also when paused) and enable `backgroundAudio` in the Android build profile:

```csharp
MediaSession.SetNowPlaying(track.Title, track.Artist, track.Album, stream.Length, coverRgba, w, h); // on track change
MediaSession.SetPlaybackState(isPlaying, stream.Time);                                            // on play / pause / seek
while (MediaSession.TryGetCommand(out MediaCommand cmd, out float seek)) { /* Play, Pause, Next, Previous, Seek */ }
```

**Images from bytes** — decode a PNG/JPEG you read yourself (a download, cover art embedded in an MP3) into a texture. Decoding is safe on a worker thread; create the texture on the main thread:

```csharp
byte[]? pixels = Texture.DecodeImage(bytes, maxSize: 256, out int w, out int h);
Texture? cover = pixels != null ? Texture.Create(w, h, pixels) : null;   // Dispose() it when done
```

**Background audio** — keep playing when the player switches apps or locks the phone:

```csharp
Audio.PlayInBackground = true;    // while music plays; set false when the user pauses
```

While it's on, sounds keep playing and scripts keep updating (about ten times a second, nothing is rendered) when the game is minimized or in the background. On Android a "playing" notification keeps the app alive; turn on the Android build profile option `backgroundAudio`, and ask for `android.permission.POST_NOTIFICATIONS` on Android 13+ so the notification is visible.

**Permissions on Android** — reading the phone's music or other shared files needs a runtime permission. Ask for it, then poll until the user has answered:

```csharp
Application.RequestPermission("android.permission.READ_MEDIA_AUDIO");
if (Application.GetPermissionState("android.permission.READ_MEDIA_AUDIO") == PermissionState.Granted) { ... }
```

The permission must also be declared in the APK with the Android build profile's `permissions` option ([Platform Packages](../Editor/Platform-Packages.md#android)). Every other platform reports `Granted` right away.

**Logging** — prints to the editor Console:

```csharp
Log.Info("Hello");
Log.Warn("Careful");
Log.Error("Something broke");
```

**Scene queries** — process many entities at once (great in a `SceneSystem`):

```csharp
foreach (ref var t in Scene.QueryRef<NativeTransform2D>())
    t.LocalRotation += Time.DeltaTime;      // spin everything
```

**Math** — `Vector2/3/4`, `Quaternion`, `Color`, `Mathf` (sin, cos, clamp, lerp…), and `Random`. Palette colors go in as written: `Color.FromHex("#3A7BD5")` (also `#RRGGBBAA`, `#RGB`, `#RGBA`; `TryFromHex` doesn't throw, `ToHex()` writes one back).

**UI text:** labels use `Funke.TextRenderer` (`using Funke;`), the same component used for world text. Import `using Funke.UI;` for widgets such as `Button` and `InputField`.

**Text outline and shadow** — a Text Renderer's **Effects** draw an outline and a drop shadow under the glyphs, for world and UI text, sized in text pixels (Font Size units):

```csharp
var label = GetComponent<TextRenderer>()!;
label.OutlineWidth = 3f;                         // 0 = none
label.OutlineColor = Color.FromHex("#1A1026");
label.ShadowOffset = new Vector2(2f, -2f);       // +Y up: down-right; (0, 0) = none
label.ShadowColor = new Color(0f, 0f, 0f, 0.5f);
```

In scene files the keys are `outlineWidth`, `outlineR/G/B/A`, `shadowX`, `shadowY`, `shadowR/G/B/A`. Both effects fade with the text color's alpha.

For heavy parallel work over many entities, combine queries with the [Job System](../Engine-Core/Job-System.md).

\---

## Your own components

Data that several scripts share belongs in a component of its own. Both kinds below are added with **Add Component** (or from code) and saved with the scene. Name the `.cs` file like the type; the editor finds them by file name.

**Class components** derive from `Component`: Inspector-editable data with strings, references and lists, but no lifecycle (pair them with an `EntityScript` or `SceneSystem` that reads them).

```csharp
public class Health : Component { public int Max = 10; [ShowInEditor] private int current; }

var h = Entity.GetComponent<Health>() ?? Entity.AddComponent<Health>();
```

**Struct components** implement `IComponent`: blittable data in the engine's own component storage, the fastest option for many entities and usable in `Scene.QueryRef<T>()` and jobs. Allowed fields: `bool`, the integer types, `float`, `double`, enums and structs built from those (`Vector2`, `Vector3`).

```csharp
[StructLayout(LayoutKind.Sequential)]
public struct Velocity : IComponent { public Vector2 Value; public float Drag; }

Entity.AddNativeComponent<Velocity>();
ref var v = ref Entity.GetNativeComponent<Velocity>();     // writes go straight to the component
foreach (ref var vel in Scene.QueryRef<Velocity>()) vel.Value *= 0.99f;
```

The Inspector edits struct fields in place, also during Play. Struct data is saved as raw bytes, so only **append** fields to a struct that existing scenes already use; reordering or inserting fields shifts saved values.

\---

## A scene-wide script

When you need to operate on lots of entities, a `SceneSystem` with a query is the natural tool:

```csharp
using Funke;
using Funke.Native;

public class SpinSystem : SceneSystem
{
    \[ShowInEditor("Spin Speed")] public float SpinSpeed = 1.0f;

    public override void OnUpdate()
    {
        float dt = Time.DeltaTime;
        foreach (ref var t in Scene.QueryRef<NativeTransform2D>())
            t.LocalRotation += SpinSpeed \* dt;
    }
}
```

`QueryRef<T>` gives you a direct reference to each entity's component, so you can read and write it with no copying — ideal for hot loops.

\---

## Native C++ scripts

Create a native script with **Asset Browser → Create → Scripting → C++ Script**. The `.cpp` file lives in the selected `Assets/` folder. Double-click it to edit that same file in your configured external editor. Save it, wait for the native compilation indicator to finish, then attach its class with **Add Component** or drag the file onto an entity's Inspector. Press Play to run it. Compiler errors appear in the Console.

A native script inherits from `NativeScript` and registers its class with `REGISTER_SCRIPT`. The creation template contains empty lifecycle callbacks:

```cpp
#include <Scripting/NativeScript.hpp>

class MyScript : public Funke::NativeScript {
public:
    void Start() override {}
    void Update() override {}
    void FixedUpdate() override {}
    void OnDestroy() override {}
};

REGISTER_SCRIPT(MyScript)
```

Call `Print("Started")` inside a native script to log an Info message to the editor Console and engine log. `Update()` and `FixedUpdate()` take no arguments. Include `<Core/Time.hpp>` and use `Funke::Time::GetDeltaTime()` or `Funke::Time::GetFixedDeltaTime()` when a callback needs its time step. Existing overrides with a `float` parameter must be updated and the native scripts recompiled.

Inspector fields are optional. Add members and `FNK_NATIVE_FIELDS(...)` inside the class when needed:

```cpp
float Speed = 1.0f;
FNK_NATIVE_FIELDS(
    Funke::NativeFields::Make<&MyScript::Speed>("Speed", "Movement Speed")
        .WithClamp(0.0, 100.0).WithTooltip("Units per second"))
```

`FNK_NATIVE_FIELDS(...)` takes one or more `NativeFields::Make<&Class::Member>("stableName", "Display Name")` descriptors. Exposed values can be edited in the Inspector and saved with scenes and prefabs. Keep the stable name when renaming a C++ member to preserve saved values. Supported member types include booleans, integers, floating-point numbers, `std::string`, enums, GLM float/integer vectors and `Funke::Color`. Use `.AsReadOnly()` for a displayed value that should not be edited. Fields are registered explicitly; arbitrary C++ members are not reflected automatically.

Saving native source or header changes triggers an incremental CMake build and DLL reload when automatic recompilation is enabled. The Editor Bridge command `funke editor recompile --project <project>` also rebuilds both C# and C++ scripts. A successful reload recreates script instances; `Start()` runs again in Play mode. Registered field values can be restored, but other C++ object state is recreated. A failed build leaves the previous library available and reports the error. Renaming or moving a `.cpp` asset keeps its registered class name, so existing scene attachments remain valid.

C++ scripts provide `Start`, `Update`, `FixedUpdate` and `OnDestroy`; C# collision, enable/disable and application callbacks are not part of this native lifecycle. Put scene access, resource acquisition and other game initialization in `Start()`. The editor may construct a temporary instance to inspect its fields without invoking lifecycle callbacks.

For component access kept between calls, use `ComponentRef<T>`. Raw pointers and references returned by `GetComponent`, `TryGetComponent` or `AddComponent` can become invalid after structural changes. EnTT owning groups may move components when another component is added, even while the original entity remains alive. A cached transform pointer can then refer to a different entity's transform.

`ComponentRef<T>` stores the entity and resolves its component on every access. This example acquires the handle **before** adding the sprite component and safely uses it afterwards:

```cpp
#include <Scripting/NativeScript.hpp>
#include <Components/Graphics/SpriteRendererComponent.hpp>

class SpawnSprite : public Funke::NativeScript {
public:
    void Start() override {
        auto spawned = GetScene()->CreateRuntimeEntity("Mein Objekt");
        m_Transform = spawned.GetComponentRef<Funke::Transform2DComponent>();
        spawned.AddComponent<Funke::SpriteRendererComponent>();
        m_Transform->SetPosition({2.0f, 2.0f});
    }

    void Update() override {
        if (m_Transform) m_Transform->SetPosition({2.0f, 2.0f});
    }

    void OnDestroy() override {
        auto spawned = m_Transform.GetEntity();
        if (spawned.IsValid()) spawned.Destroy();
        m_Transform.Reset();
    }

private:
    Funke::ComponentRef<Funke::Transform2DComponent> m_Transform;
};

REGISTER_SCRIPT(SpawnSprite)
```

Use handles for nonempty ECS component types; `NativeScript` subclasses and empty tag components are excluded. The handle starts empty. Construct one explicitly from an `Entity`, use `entity.GetComponentRef<T>()`, or call `GetComponentRef<T>()` in a native script for its own entity. `IsValid()` and `operator bool` test whether the entity and component still exist. `TryGet()` returns a nullable pointer; `Get()`, `operator->` and `operator*` throw `FunkeException` with `InvalidHandle` when access is invalid. `GetEntity()` returns the owning entity and `Reset()` clears the handle. Removing the component makes access invalid; adding that type back to the same live entity makes it valid again. Destroying the entity or its scene invalidates access, and recycling an entity slot does not retarget the handle.

Use these handles on the runtime main thread; they do not provide synchronization or keep an entity or scene alive. A pointer borrowed from `TryGet()` or reference from `Get()` still expires on a structural change, so keep the handle between frames. A `const ComponentRef<T>` behaves like a const pointer variable and still permits modifying `T`; use `ComponentRef<const T>` for read-only component access. Calling `GetComponentRef<T>()` on a const entity or const native script returns a read-only handle. There is no implicit conversion to a raw pointer. Handles are runtime state and cannot be registered with `FNK_NATIVE_FIELDS` or saved as Inspector fields. The example destroys its spawned entity in `OnDestroy()` so a script reload does not leave another copy behind.

Script and managed-component class names must be unique on each entity, including across C# and C++. Conflicting attachments are rejected with a Console warning because their saved field keys would overlap. Use distinct class names or qualified namespaces, such as `REGISTER_SCRIPT(NativeGame::Player)`.

Native builds require CMake, a compatible desktop C++ compiler and matching Funke engine headers/libraries. `FUNKE_DIR` selects the engine root when it cannot be found from the project. On Windows the editor can also find CMake bundled with Visual Studio's C++ tools; `FUNKE_CMAKE_PATH` can override its location. `NativeScripts/Source/` remains available for manually maintained sources, while newly created assets compile directly from `Assets/` without a second editable copy. The native library uses the same build configuration as its engine.

Desktop game builds compile and include the native-script library. Mobile and Web exports with native scripts are currently rejected with an explicit error; they require a different static integration path.

\---

## Hot reload

A big convenience of C# scripting: you can change your scripts and reload them **without restarting** the editor. When you rebuild your game's script assembly, the engine swaps in the new code and keeps your scenes running. Entity state is preserved where it can be (matching script classes keep their instances; fields reset to their default/saved values).

The engine's *core* scripting library can't be reloaded mid-session (a .NET limitation), so changing engine-level scripting bits needs a restart — but your everyday game scripts reload freely.

### How fast a reload is

When you save a script, the editor compiles it **in-process** with Roslyn, the C# compiler from your installed .NET SDK, instead of starting `dotnet build`. Editing an existing script usually compiles in well under a second.

* The editor asks MSBuild once for the exact compiler settings `dotnet build` would use (references, defines, `Nullable`, language version, analyzer config, source generators) and reuses them. MSBuild runs again (about 1 s) only after you **add or delete** a `.cs` file or change the active build configuration or defines.
* A change to the `.csproj`, a `.props`/`.targets` file, `global.json` or an installed package still triggers one full `dotnet build`, because MSBuild may need to copy new dependencies into `bin/`. The reload after that is in-process again.
* Source generators (for example `[GeneratedRegex]`, `[LibraryImport]`, `[JsonSerializable]`) run as usual. Analyzer warnings (`CAxxxx`) appear only in a full `dotnet build` or your IDE.
* Compile errors appear in the Console in the usual `File.cs(line,col): error CSxxxx` format.
* If the in-process compiler can't reproduce the build (an unusual project setup, a different .NET SDK, the compiler isn't installed next to the editor), the editor falls back to `dotnet build` and logs why under **ScriptSystem**. Set the environment variable `FUNKE_SCRIPT_COMPILER=dotnet` before starting the editor to always use `dotnet build`.

\---

## How it works under the hood (brief)

You don't need this to write scripts, but it helps to know the shape:

* At startup, the engine boots the **.NET runtime** (via hostfxr) and loads two assemblies: **`Funke-ScriptCore.dll`** (the scripting API you call) and **your game's script assembly**.
* A bridge connects the two worlds: C# calls into the engine through **InternalCalls** (P/Invoke), and the engine calls back into your scripts (e.g. to invoke `OnUpdate`) through a table of function pointers.
* This bridge is set up once during the scripting-host startup step described in [Startup Processes](../Engine-Core/Startup-Processes.md#start-scripting).

\---

## Summary

* Write game logic in **C#**: `EntityScript` (per entity), `SceneSystem` (per scene), or `GlobalSystem` (whole app).
* Override lifecycle methods (`OnStart`, `OnUpdate`, `OnFixedUpdate`, …); attach scripts via a `ScriptComponent`.
* Expose fields to the Inspector with attributes like `\[ShowInEditor]` and `\[ClampValue]`.
* Use the rich API — `Input`, `Time`, `Physics2D`, `Audio`, `Log`, scene queries, math.
* Drop to **native C++ scripts** only for measured performance needs.
* **Hot reload** lets you iterate on C# without restarting.

## Related pages

* [ECS](../Engine-Core/ECS.md) — entities and components your scripts work with.
* [Scenes](../Engine-Core/Scenes.md) — SceneSystems and the play-mode lifecycle.
* [Assets](../Assets/Assets.md) — referencing textures, audio, and fonts from scripts.
* [Job System](../Engine-Core/Job-System.md) — parallelizing heavy script work.

