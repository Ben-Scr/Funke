# Scripting

Funke supports game scripting in **C# and native C++**, and a project can use both. Write scripts to move the player, spawn enemies, handle UI and manage game state in the language that fits your project. The native engine's C++20 implementation does not restrict game development to C++.

The first sections cover C# with `EntityScript`, `SceneSystem` and `GlobalSystem` (.NET 10 on desktop). For C++ gameplay in the editor, jump to [Native C++ scripts](#native-c-scripts), which covers `NativeScript`, Inspector fields and native compilation/reload. C++ is a supported gameplay workflow, not limited to performance optimizations. For a C++ application using the SDK directly, see the [native application guide](../../../Funke-Engine/README.md). Platform runtime differences are documented in [Known Limitations](../Known-Limitations.md).

Visual Studio IntelliSense for Funke game projects requires Visual Studio 2026 (18.0+) because Visual Studio 2022 cannot evaluate `net10.0` projects. VS Code or Rider can use the installed .NET 10 SDK instead.

**Script editor:** Editor Preferences › Scripting › External Script Editor lists every detected Visual Studio, VS Code, JetBrains Rider and IntelliJ IDEA installation (standalone installer, JetBrains Toolbox 1.x and 2.x), each version on its own. Double-clicking a script or a console error opens the file at that line; Rider opens the project's `.sln`, IntelliJ IDEA the project folder. IntelliJ IDEA has no C# language support, so you get editing without code completion; use Rider for that.

If you just want the short version: make a class that inherits from `EntityScript`, override `OnUpdate`, attach it to an entity in the editor, and press Play.

---

## The three kinds of script

Funke's C# API has three script base classes, each with a different **scope**:

|Base class|Scope|Use it for|
|-|-|-|
|**EntityScript**|One entity|Behavior for a specific object — a player, an enemy, a pickup.|
|**SceneSystem**|One scene|Logic over a whole scene — spawning waves, scoring, scene-wide rules.|
|**GlobalSystem**|The whole app|State that survives scene changes — a game manager, audio settings, save data.|

Pick the smallest scope that fits. Most gameplay is `EntityScript`; reach for `SceneSystem` when you need to work with *many* entities at once, and `GlobalSystem` for things that must persist between scenes.

---

## Your first script

Here's a complete `EntityScript` that moves its entity with WASD:

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

        var transform = Entity.Transform;
        if (transform == null) return;
        transform.Position += velocity.Normalized() * speed * Time.DeltaTime;
    }
}
```

Notice three things:

* It inherits from `EntityScript`.
* `Entity` refers to the object this script is attached to.
* `Input`, `Time`, and `Transform` are part of the scripting API (covered below).

### Attaching a script to an entity

A script doesn't run until it's attached. In the editor, select an entity, open **Add Component**, and choose your script class, or drag its source file onto the Inspector. The engine stores the attachment in a `ScriptComponent`, creates an instance, and calls its lifecycle methods when the game plays.

---

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

> The split between `OnUpdate` and `OnFixedUpdate` matters: put **rendering/input-rate** logic in `OnUpdate`, and **physics** logic in `OnFixedUpdate` (which runs at a steady rate regardless of frame rate). See [Startup Processes](../Engine-Core/Startup-Processes.md#stage-3--the-main-loop).

---

## Exposing fields to the Inspector

Public fields can be shown and edited in the editor's Inspector using **attributes**. This lets designers tweak values without touching code:

```csharp
using Funke;

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

---

## What scripts can do (the API)

The `Funke` namespace gives you a broad API. The main categories:

**Entities & components**

```csharp
Entity e = Entity.FindByName("Boss");
e.GetComponent<SpriteRenderer>();
e.AddComponent<Rigidbody2D>();
Entity copy = Entity.Instantiate(prefab);
e.Destroy();
```

**Transform** — position, rotation, scale:

```csharp
Entity.Transform.Position += Vector2.Up * Time.DeltaTime;
```

**Hierarchy** — parenting lives on the entity, not the transform:

```csharp
Entity.SetParent(other);          // 'other' is an Entity — keeps world position, rotation and scale (default; 2D and 3D)
Entity.SetParent(other, false);   // adopt the parent's frame instead (local transform kept)
Entity.SetParent(null);           // detach to the scene root
```

**Input** — keyboard, mouse, axes:

The examples below use the core's direct input API. Named actions, control maps and saved keyboard/mouse/gamepad rebinding are available through the optional [Funke.Input](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/main/packages/Funke.Input) package. Install it through the Package Manager and follow its README for the API and version-specific authoring tools. Its namespace is `Funke.InputActions`, distinct from the core `Funke.Input` class.

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

**Window & frame rate**:

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

**Post-processing** — add **Post Processing** to the entity with the main **Camera 2D** or **Camera 3D**, and keep that camera's *Post Processing* toggle on. The shared `PostProcessing2D` component exposes each effect as a value struct: read it, change fields, assign it back.

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

For an opt-in game-specific schema, use `SaveData.SaveVersioned("slot1", inventory, schemaVersion: 2)` and `TryLoadVersioned`. Existing `Save` / `TryLoad` slots stay plain JSON and are never automatically upgraded. The envelope has `$funkeSave: 1`, `schemaVersion`, and `payload`; the application schema version is independent of the engine version.

```csharp
bool loaded = SaveData.TryLoadVersioned<Inventory>("slot1", 2, out var inventory, out var error,
    migrate: (fromVersion, data) =>
    {
        if (fromVersion != 1) throw new InvalidOperationException("Missing migration");
        data["Gold"] = data["Coins"]?.GetValue<int>() ?? 0;
        return data;
    },
    validate: value => value.Gold >= 0,
    persistMigration: true);
```

The callback advances exactly one version at a time and receives detached JSON. An unversioned slot requires an explicit `unversionedVersion`; otherwise it is rejected. Missing migration steps, malformed or future versions, deserialization errors, callback exceptions and failed validation return `false` with `error`, without publishing a value or replacing the original slot. A successful load leaves disk unchanged unless `persistMigration: true` is supplied. When enabled, the migrated JSON (including unknown fields) is saved atomically only after deserialization and validation succeed. Keep callbacks free of unrelated external side effects: those cannot be rolled back by SaveData. Pass migration delegates per load instead of retaining script types in a global registry across hot reload.

**Audio**:

```csharp
Audio sfx = Audio.FromAssetUUID(clipGuid);
sfx.PlayOneShot();
Audio.MasterVolume = 0.8f;   // 0..1, everything the game plays (a settings menu's master slider)
```

Automated runs (`--input-script`, `--screenshot-at`, `--quit-at`, `--exec`, `--fixed-step`, `--background` or `--prefs-profile=<name>`, see [Testing](../Editor/Testing.md)) are muted by the engine whatever `MasterVolume` says; `--sound` keeps them audible. `Application.IsAutomatedRun` tells a game it runs under such a test, e.g. to skip an intro or an ad.

**3D sound** — sounds that come from a place in the world. Turn on **Spatial** in an Audio Source's **3D Sound** section: the source then plays from its entity's position (Transform 3D, or Transform 2D at z 0), gets quieter with the distance to the listener and moves to the side it is on. The listener is the main Camera 3D (in 2D games the main Camera 2D), so a sound left of the view is heard on the left. Audio Sources are 2D by default, so existing scenes sound as before.

Enable `AudioSource.Hrtf` for headphone rendering with direction-dependent filters. Distance attenuation and Doppler still apply; stereo panning remains available when HRTF is off. See [Spatial Audio](../Engine-Core/Spatial-Audio.md) for listener selection, one-shots and limitations.

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
if (song != null) song.Time = 42f;                                 // seek; Length, IsPlaying, IsAtEnd, Volume, Loop, Pause(), Stop()
song?.Dispose();                                  // e.g. in OnDestroy; leaving Play mode closes every stream
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

**Whole-text style** — the TextRenderer inspector has independent **Bold**, **Italic**, and **Rich Text** switches. Bold and Italic style the entire text and work with Rich Text off. In C#:

```csharp
label.Bold = true;
label.Italic = true;
label.RichText = false;
```

With Rich Text off (the default), the renderer skips tag parsing, size-tag scanning and rich-style runs; it uses the plain-text layout even with Bold or Italic enabled. With Rich Text on, tags inherit the component style, and closing a tag restores it. The scene/prefab keys `bold`, `italic`, and `richText` default to false. InputField labels support whole-text styles while retaining literal markup and matching caret/selection metrics.

**Rich text** — enable **Rich Text** in the TextRenderer inspector, or set `RichText = true` in C#. It is off by default, including when loading older scenes. The scene/prefab key is `richText`.

```csharp
var label = GetComponent<TextRenderer>()!;
label.RichText = true;
label.Text = "<b>Warning:</b> <i>Only</i> <color=#FFCC00><size=48>3</size> lives</color> left";
```

| Tag | Effect |
| --- | --- |
| `<b>text</b>` | Synthetic bold using the selected font |
| `<i>text</i>` | Synthetic italic using the selected font |
| `<color=#RRGGBB>text</color>` | RGB color override |
| `<color=#RRGGBBAA>text</color>` | Color and opacity; alpha multiplies the component's alpha |
| `<size=48>text</size>` | Absolute size in the same text pixels as `FontSize`; finite values from 1 through 1024 |

Tags are case-sensitive and can nest; close them in reverse order to restore the enclosing style. Color also accepts `red`, `green`, `blue`, `white`, `black`, and `yellow`. Values may be quoted, such as `<color="red">`. Use `&lt;`, `&gt;`, and `&amp;` for literal angle brackets and ampersands. Unknown or invalid tags and unmatched closing tags remain visible. An unclosed valid opening tag applies to the rest of the text. Nesting is limited to 64 levels.

UI word/character wrapping, alignment and automatic sizing use the visible styled text, including changing sizes. Outline and shadow retain their effect colors and follow glyph shapes and inline opacity. The font atlas resolution accounts for the largest inline size, within the renderer's existing 192-pixel bake limit; bold/italic do not switch to separate font faces. Rich text works for UI and world text, but InputField text remains literal even if its label has Rich Text enabled, preserving caret and selection behavior.

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

---

## Your own components

Data that several scripts share belongs in a component of its own. Both kinds below are added with **Add Component** (or from code) and saved with the scene. The editor discovers types from the active compiled assembly; filenames do not determine component identity. See [script identities](#script-identities-and-inspector-limits) for source navigation and multiple types per file.

**Class components** derive from `Component`: Inspector-editable data with strings, references and lists, but no lifecycle (pair them with an `EntityScript` or `SceneSystem` that reads them).

```csharp
public class Health : Component { public int Max = 10; [ShowInEditor] private int current; }

var h = Entity.GetComponent<Health>() ?? Entity.AddComponent<Health>();
```

**Struct components** implement `IComponent`: blittable data in the engine's own component storage, the fastest option for many entities and usable in `Scene.QueryRef<T>()` and jobs. Allowed fields: `bool`, the integer types, `float`, `double`, enums and structs built from those (`Vector2`, `Vector3`).

```csharp
using System.Runtime.InteropServices;
using Funke;
using Funke.Components;

[StructLayout(LayoutKind.Sequential)]
public struct Velocity : IComponent { public Vector2 Value; public float Drag; }

Entity.AddNativeComponent<Velocity>();
ref var v = ref Entity.GetNativeComponent<Velocity>();     // writes go straight to the component
foreach (ref var vel in Scene.QueryRef<Velocity>()) vel.Value *= 0.99f;
```

The Inspector edits struct fields in place, also during Play. Struct data is saved as raw bytes, so only **append** fields to a struct that existing scenes already use; reordering or inserting fields shifts saved values.

---

## A scene-wide script

When you need to operate on lots of entities, a `SceneSystem` with a query is the natural tool:

```csharp
using Funke;
using Funke.Native;

public class SpinSystem : SceneSystem
{
    [ShowInEditor("Spin Speed")] public float SpinSpeed = 1.0f;

    public override void OnUpdate()
    {
        float dt = Time.DeltaTime;
        foreach (ref var t in Scene.QueryRef<NativeTransform2D>())
            t.LocalRotation += SpinSpeed * dt;
    }
}
```

`QueryRef<T>` gives you a direct reference to each entity's component, so you can read and write it with no copying — ideal for hot loops.

---

## Native C++ scripts

Create a native script with **Asset Browser → Create → Scripting → C++ Script**. The `.cpp` file lives in the selected `Assets/` folder. Double-click it to edit that same file in your configured external editor. Save it, wait for the native compilation indicator to finish, then attach its class with **Add Component** or drag the file onto an entity's Inspector. Press Play to run it. Compiler errors appear in the Console.

A native script inherits from `NativeScript` and registers its class with `REGISTER_SCRIPT`. New scripts include the gameplay umbrella and a source-local namespace directive; the generator supplies empty `Start`, `Update`, `FixedUpdate` and `OnDestroy` callbacks. For example, fill in `Update` like this (2D module):

```cpp
#include <Funke/Scripting.hpp>

using namespace Funke;

class MyScript : public NativeScript {
public:
    void Update() override {
        if (Input::GetKeyDown(KeyCode::Space)) Print("Jump");
        if (GetScene() && HasComponent<Transform2DComponent>())
            GetComponent<Transform2DComponent>().SetPosition({1.0f, 2.0f});
    }
};

REGISTER_SCRIPT(MyScript)
```

`Funke/Scripting.hpp` supplies `NativeScript`, field descriptors, `Input`, `KeyCode`, mouse buttons, `Time`, color/vector/math helpers, `Entity`, `Scene`, `SceneManager` and `Funke/ScriptComponents.hpp`. The component umbrella covers all built-in component headers: general metadata/transforms/animation/tags, UI, graphics, audio, 2D/3D physics, and `ScriptComponent`. It reuses `General.hpp` and `UI.hpp`; it does not revive the deprecated `Components/Components.hpp`.

Availability follows the linked engine's `FUNKE_WITH_APPLICATION`, `_RENDER`, `_AUDIO`, `_PHYSICS`, `_SCRIPTING`, `_2D` and `_3D` compile definitions. UI/RectTransform and the shared `PostProcessing2DComponent` remain available without the 2D module, matching the engine's registration rules. Runtime project module activation is a separate setting. Neither umbrella enables modules or registers components. These are the original types and calls, without new wrappers, copies or dispatch.

`using namespace Funke;` belongs in the script `.cpp`, after its includes. Public headers and the optional PCH never import the namespace globally. If a name conflicts with your own `Game::Input`, use `Funke::Input` or specific declarations such as `using Funke::NativeScript;`. Existing scripts need no migration: targeted includes such as `<Scripting/NativeScript.hpp>`, `<Core/Input.hpp>` and `<Components/General/Transform2DComponent.hpp>` with fully qualified types remain supported. Changing an existing script to the umbrella is optional.

Specialized APIs still use explicit headers, for example `<Physics/Physics2D.hpp>`, `<Assets/AssetLoader.hpp>`, `<Audio/AudioManager.hpp>` or `<Jobs/JobSystem.hpp>`. The umbrella does not explicitly include editor UI, renderer backends or component registration code. The existing scene/registry API does bring some engine infrastructure transitively.

Call `Print("Started")` inside a native script to log an Info message to the editor Console and engine log. `Update()` and `FixedUpdate()` take no arguments. `Time::GetDeltaTime()` and `Time::GetFixedDeltaTime()` are available through the umbrella. Existing overrides with a `float` parameter must be updated and the native scripts recompiled.

The generated CMake target already exports a compile database with Ninja/Makefiles (`NativeScripts/build/compile_commands.json`); Visual Studio uses the generated native solution. Both source roots, `Assets/` and `NativeScripts/Source/`, share the target's includes, C++20 standard and module/ABI definitions. Reconfigure after adding files; the editor's native rebuild does this automatically. Point clangd at that build directory or use the IDE's CMake integration. Completion and definition navigation for `Input`, `KeyCode`, `SceneManager` and `Transform2DComponent` were verified with clangd and VS Code's Microsoft C/C++ extension against generated Windows build configurations. The VS Code checks also covered a new source file and switching between Debug and Release compile databases. Other IDE interfaces were not exercised.

PCH is optional and defaults to off: configure with `-DFUNKE_NATIVE_USE_PCH=ON` to precompile `Funke/Scripting.hpp`, or `OFF` to disable it. Keep the explicit include in each script. Add per-file exceptions after the generated module include in your custom `CMakeLists.txt`, for example `set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/../Assets/ThirdParty.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)`. Scripts with per-source SDK macros should disable PCH. No PCH is shared across targets, and the generated build does not enable Unity/Jumbo compilation.

The SDK integration check is `python scripts/ci/native_scripting_test.py --sdk <EngineSDK> --test-exe <Funke-Editor-Tests> --output <fresh-test-directory>`. It compiles actual generator output, mixes old and new include styles in one library, loads/reloads it, checks fields and component mutation, and compiles full/2D/3D/no-module/core headers without PCH. Add `--benchmark` for local baseline/no-PCH/PCH timings (four translation units, initial build, one source edit, public header edit). Use a fresh output directory for initial-build measurements. This does not replace editor Play Mode, Inspector save/reopen or desktop export validation.

See [native scripting verification](Native-Scripting-Verification.md) for the Editor/export procedure, recorded Windows/Linux and IDE results, evidence locations and the measurements behind the optional PCH default.

Native replacement validates the exported `FunkeNativeScriptAbiVersion` and required create/destroy exports before unloading the current library. A mismatch reports the expected and supplied versions (or the missing export), and instructs you to rebuild the project's `NativeScripts` CMake target with the matching SDK and configuration. The same host diagnostic reaches Editor logs and command-line output. A failed replacement keeps the loaded library, its source path and class configuration; rebuilding a compatible library at that path allows the next reload to succeed without restarting the host. Each failed candidate's shadow copy is removed, and successful replacement/unload removes the prior shadow.

`python scripts/ci/native_abi_boundary_test.py --test-exe <Funke-Engine-Tests> --output <fixture-directory>` builds actual shared libraries and tests old/future/missing ABI, each missing required export, retained configuration, recovery and shadow cleanup. This boundary test passed on Windows x64 Release with ABI 7; it does not replace the broader lifecycle, Inspector or export checks above. `FunkeHasScript` and `FunkeInitialize` retain their existing optional compatibility behavior.

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

---

## Component access and native references

An `EntityScript` can call `HasComponent<T>()` and `TryGetComponent<T>(out T? component)` directly, alongside its existing Get/Add/Remove helpers. These protected helpers forward to the owning `Entity` and retain the `where T : Component, new()` constraint. `HasNativeComponent<T>()`, `RemoveNativeComponent<T>()` and the ref-returning `GetRefOrThrow<T>()` are also available directly, with `where T : unmanaged, IComponent`. Calls through `Entity` remain valid; the helpers add no separate component store or preliminary native lookup.

`Entity.TryGetComponent`, its `EntityScript` helper, `Scene.TryGetSingleton` and `Scene.TryGetSingletonEntity` annotate their output with `[NotNullWhen(true)]`. With nullable analysis enabled, a successful branch can use the result without `!` or another null check. A false result may still contain null; the annotation does not change runtime behavior.

Native refs borrow engine storage. `GetNativeComponent<T>()` and `GetNativeComponentReadOnly<T>()` return a null ref when the component is absent; check it with `Unsafe.IsNullRef` before dereferencing. `GetRefOrThrow<T>()`, `TransformRef` and `TransformRefReadOnly` instead throw `InvalidOperationException` when the required component is absent. Ordinary assignment, such as `var copy = entity.TransformRef`, copies the struct; `ref var transform = ref entity.TransformRef` writes through to live storage.

A writable transform ref marks transforms for updating; `TransformRefReadOnly` and `GetNativeComponentReadOnly<T>()` avoid that write-side effect. Reacquire borrowed refs each frame and after structural changes. Do not add/remove components or create/destroy entities during a `QueryRef` iteration; record those changes in an `EntityCommandBuffer` and play it back after the borrowed scope. These are caller obligations, not automatic lifetime checks. See [ECS](../Engine-Core/ECS.md#batched-structural-changes-in-c) and the complete [native ref example](../../../Examples/CSharpScripting/NativeRefs.cs).

## Explicit transforms and spawning

`Entity.Transform2D` and `Entity.Transform3D` (also available directly on `EntityScript`) return the requested component or `null`. They never add a transform. Use `LocalPosition` for parent-relative coordinates. `Transform2D.WorldPosition` is writable; `Transform3D.WorldPosition` reports the last hierarchy update and is read-only.

The complete [2D movement](../../../Examples/CSharpScripting/Move2D.cs), [3D movement](../../../Examples/CSharpScripting/Move3D.cs), [spawn](../../../Examples/CSharpScripting/SpawnExamples.cs), [native ref](../../../Examples/CSharpScripting/NativeRefs.cs) and [session state](../../../Examples/CSharpScripting/SessionState.cs) examples compile together with nullable checks and warnings as errors:

```powershell
dotnet build Examples/CSharpScripting/Examples.csproj -c Release
```

`Create2D`, `Create3D`, `Instantiate2D` and `Instantiate3D` accept **local** positions and rotations in **degrees**. 2D uses `Vector2` and a float angle; 3D uses `Vector3` and Euler angles. Parent assignment and local values are complete before spawned scripts receive `OnAwake`/`OnEnable`; `OnStart` remains deferred until normal startup. Without a parent, local coordinates equal world coordinates. Instantiation requires the source's matching transform and does not convert dimensions. An invalid source returns `Entity.Invalid`; an invalid parent or unsuitable transform throws and destroys any partially created clone.

| Existing API | Existing meaning, retained | Recommended explicit API |
|---|---|---|
| `Transform` | Nullable 2D wrapper | `Transform2D` or `Transform3D` |
| `Transform2D.LocalTransform` | Local position | `Transform2D.LocalPosition` |
| `Transform2D.Position` | World position | `Transform2D.WorldPosition` |
| `Transform3D.Position` | Local position | `Transform3D.LocalPosition` |
| `Create(name, Vector3, rotation)` | 2D X/Y; rotation is passed as **radians** | `Create2D(name, Vector2, degrees)` or `Create3D(name, Vector3, EulerDegrees)` |
| Positional `Instantiate` overloads | 2D position; rotation in **degrees**, legacy parenting behavior | `Instantiate2D/3D` with explicit local values and parent |

Empty project templates remain small; examples are opt-in. For borrowed component storage, follow the [native reference contracts](#component-access-and-native-references).

## Script identities and Inspector limits

Project pickers use actual types from the **active compiled assembly**, including indirect inheritance, namespaces and multiple types per file. Abstract, open generic and nonconstructible classes are excluded. Failed compilation or rejected preflight keeps the active catalog. Project identities are stored as `type:<FullName>`; the prefix distinguishes them from unmarked legacy names, including global classes with no namespace. The same identities apply to generic and string-based Entity APIs, managed components, references and systems. Native built-in names and existing package catalog names keep their contracts.

An unmarked name migrates only when it has one possible type. Attachment names and their prefixed field keys move together during normal scene/prefab editing and saving. Dotted namespaces and nested field paths are preserved; migration does not overwrite conflicting values already stored under the canonical name. Ambiguous legacy names (including both `Funke.Player` and global `Player`) are left unresolved with candidate diagnostics; explicitly canonical `type:Player` remains exact. Renamed or missing types keep their attachments and raw values. Resolve genuine type renames explicitly; a filename resemblance is not a migration rule. No project-wide rewrite runs.

Renaming or moving a C# file leaves class text and attachments unchanged. Source navigation uses portable PDB sequence points; when a partial type has several source files, the first ordinal path is the primary source. Unknown or unavailable sources are not guessed from filenames. Rebuild after moving a file to refresh the catalog's source path; undo restores the file path through the existing asset history.

Inspector scalars, supported one-dimensional arrays and `List<T>` remain editable. Two-level collections and string-key dictionaries follow the contract below. A folded warning lists unsupported visible members by field path, including omitted members such as `Items[].Children`; `[HideFromEditor]` and `[EditorIgnore]` remain silent. Missing types, an unavailable host and a valid type with no visible fields have distinct statuses. Unknown stored field values survive ordinary serialization.

### Nested lists and dictionaries

`List<List<T>>`, `T[][]` and `Dictionary<string, T>` support numeric primitives, booleans, strings and the existing enum representation as leaf values. This includes fields and writable properties inside ordinary nested classes or structs. Mixed array/list shapes, additional collection levels, dictionary values that are collections or objects, reference/resource leaves and collections inside object-list elements remain unsupported and retain their Inspector diagnostics.

Nested lists have independent outer and inner add/remove controls and the usual list drag handles for reordering. The **Null** controls distinguish a null collection, an empty outer collection, a null inner collection and an empty inner collection. String values also distinguish null from an empty string. These values survive normal scene/prefab persistence, Undo/Redo and edit-mode recompilation. Different collections in a multi-selection require selecting one object to edit; identical collections can be edited together.

Dictionary keys use case-sensitive ordinal equality: `A` and `a` are different keys. Empty and Unicode keys are valid; null keys and duplicates are rejected before replacing any value. Finish a key edit with Enter or by leaving the field. Add creates an unused key. The default comparer and `StringComparer.Ordinal` are supported. Case-insensitive, culture-sensitive and custom comparers display a read-only explanation; loading or editing does not silently replace them with the default comparer. Keys and string values containing an embedded null character retain their data but cannot be edited as text in the Inspector. Unicode glyph display depends on the editor font; missing glyphs do not change the stored strings.

The new fields use a version 1 JSON envelope inside the existing script-field string value. `kind` is `nestedList` or `dictionary`, `leaf` is the existing scalar wire tag, and leaf values are invariant strings (or JSON null for null strings). `items: null` denotes a null container. Nested `items` are arrays of null or scalar arrays. Dictionary `items` are `[key, value]` pairs with `comparer: "ordinal"`, stored in UTF-8 byte order for deterministic output; ordering does not change lookup semantics. For example:

```json
{"version":1,"kind":"nestedList","leaf":"int","items":[null,[],["1","2"]]}
{"version":1,"kind":"dictionary","leaf":"float","comparer":"ordinal","items":[["","1.25"],["speed","3.5"]]}
```

Malformed schemas, invalid scalar values and duplicate keys leave the previous collection intact and produce a diagnostic. Existing simple-list and object-list formats are unchanged; no scene-format migration or project-wide rewrite is needed. As with existing list fields, these collection widgets currently have no Editor Bridge property-edit representation.

## Hot reload

C# scripts can reload **without restarting** the editor. The defaults are **Auto-recompile on file changes: on**, **Recompile before Play Mode: off**, and **Exit Play Mode on Recompilation: on**. With automatic recompilation off, use the manual compile action or enable compilation before Play. A Play request waits for an already running build; a failed build or assembly swap blocks entry and keeps its diagnostics in the Console.

| Situation | Effect |
|---|---|
| Script edit in Edit mode | Auto-recompile, when enabled, replaces instances and restores compatible Inspector values. |
| Script edit during Play, stop-on-recompile enabled | Play stops when recompilation begins; the pre-Play scene is restored. |
| Script edit during Play, stop-on-recompile disabled | A successful reload replaces instances while Play continues. Unselected runtime field values are lost; opt-in `IHotReloadState` values are restored separately from saved Inspector values. |
| Compiler error | No replacement is loaded. If Play already stopped for the rebuild, it stays stopped. |
| Candidate rejected during preflight | The currently loaded assembly and still-live instances remain intact; the Console reports the validation failure. |
| Failure after the swap boundary | The exchange failed. Keeping an assembly does not guarantee that old instances or their state survived. |

A successful exchange cancels old coroutines and recreates script instances; their normal Awake/Enable/Start lifecycle runs again (Start can be deferred to the next update). Tasks, call stacks and arbitrary resources are not resumed. Inspector values and running instance fields are different kinds of state: Edit-mode exposed values can be restored, but Play-mode runtime edits are discarded on Stop.

Static game fields can survive Stop and the next Play session when the assembly was not replaced. Use `Application.PlayModeExited` for an explicit cleanup/reset hook; no automatic reset of all statics is promised. An assembly replacement creates new game static state, but does not reset every external library.

Compiler errors **and warnings** include their diagnostic code and available source location in Console. Double-click a diagnostic to open its source line. The fast Roslyn path runs compiler diagnostics and source generators, but deliberately skips regular analyzers; use a full `dotnet build` for analyzer diagnostics. Each build produces its own output; old Console entries remain history until cleared. Build diagnostics are bounded to 200 lines (errors first), with an omitted-count notice for the rest.

A failed Play request preserves those diagnostics, including when Play was waiting for a background build. Missing last-build details can be restored after clearing the Console; repeated Play requests do not append duplicate copies of already visible diagnostics. The usual Console clearing occurs only when Play actually starts successfully.

The replacement assembly is loaded and its type signatures and native component contracts checked **before managed scene instances are torn down**. A rejected candidate leaves the working assembly and its registrations intact; the Console and rebuild status report failure. The validated assembly bytes are retained until the swap, so a file change between preflight and teardown cannot substitute an unchecked assembly. A first load with no working assembly requires a successful build. Failures after a successful preflight can still recreate instances.

The engine's *core* scripting library cannot be reloaded mid-session. If the ScriptCore DLL next to the candidate or at the running core's path has a different module identity (MVID), reload is refused even when the assembly version is unchanged. Stop the editor, build Funke-Engine and Funke-ScriptCore together, deploy matching binaries, then restart.

### Selected runtime state

Implement `IHotReloadState` on an `EntityScript` to capture named values with `ReloadState.Set` and restore them with `TryGet<T>`. Version 1 supports `bool`, `int`, `uint`, `long`, `ulong`, `float`, `double` and `string` only. Store stable entity/scene/asset IDs explicitly as `ulong` and resolve them again in game code. Objects, wrappers, delegates, tasks, coroutines and pointers are rejected. The neutral snapshot contains strings, never old instances, `Type` objects or handles.

The sequence is candidate validation → capture → normal teardown → new Awake/Enable/Start → restore → first regular update. Restore follows a delayed `OnStart`, so startup cannot overwrite restored values afterward. Use restore itself to rebuild derived data. Earlier lifecycle side effects still happen; this is not a continuation of the old call stack. Missing or type-changed keys return `false`; unread keys and restore exceptions produce an incomplete-restore diagnostic. State is matched by scene GUID, entity UUID and complete type identity. Disabled instances can wait for their startup; Stop, unload, a new capture or a failed exchange clears retained state. It never enters `PendingFieldValues`, scene/prefab files or the pre-Play snapshot. Scripts that do not opt in use ordinary reload behavior.

### Registered state before Play

The **Reset registered game state before Play** preference is off by default. With it enabled, `[ResetOnPlay]` methods in the loaded game assembly run immediately before each successful new Play entry, even if no `OnStart` has ever run. Each method must be static, parameterless, nongeneric, synchronous and return `void`. All signatures are validated before any method runs. Execution order is ordinal declaring full type name, then method name. An exception reports the method and aborts Play; changes made by earlier reset methods are not rolled back.

Pause/resume and failed builds do not invoke these resets. Reload replaces registrations. Unregistered statics and external libraries are untouched. The [session example](../../../Examples/CSharpScripting/SessionState.cs) resets `Score`, keeps `SessionsPlayed` between sessions in the same assembly, and separately transfers selected instance state during Play reload. `Application.PlayModeExited` remains available for cleanup on exit.

### Native component layouts and bindings

Method changes in the game assembly remain hot reloadable. Ordinary user `IComponent` structs use dynamic native storage; their storage is recreated from the replacement schema, so field reorder/size changes can reload. Live arbitrary state is not preserved; exposed values follow the existing reload rules.

A struct that mirrors a **compiled native component** has a stricter contract. Both initial load and reload compare the native mirror size and every primitive leaf's path, type, byte offset and size. Swapping two equally sized fields fails even though `sizeof` is unchanged. Native trailing fields are permitted only outside the explicitly declared mirror prefix. Transform2D and SpriteRenderer publish contracts from their compiled native members. An unsupported or missing native contract fails explicitly; a size-only match is never reported as proof of layout compatibility.

Native/package/codegen registrations can supply `ComponentInfo.managedSize` and `managedFields` (`Path`, `Type`, `Offset`, `Size`). These describe the full managed prefix, including private fields and explicit padding. Derive offsets and sizes from the compiled native type; do not copy numbers from the C# file. Paths use managed field names with dotted nested paths, and leaf types use `bool`, `i8/u8`, `i16/u16`, `i32/u32`, `i64/u64`, `f32/f64`, `char`, or pointer-width `ptr/uptr`. Enums use their underlying integer type. The managed mirror must have a supported sequential or explicit unmanaged layout; the computed layout is also checked against its runtime size. A custom native name must be a `const string NativeName` for metadata-only preflight, so validation does not execute user static initialization.

For an intentional native layout change, stop the editor, update the native type/contract and managed mirror together, then run the full affected project/engine build with codegen enabled (`FunkeCodegenEnabled=true`) and restart with the matching binaries. In a source checkout use `scripts/dev/Build.ps1 Engine,Editor,ScriptCore -Config Release -NoRegenCheck` after any required generation, preserving the active module/profiler configuration. Generated native component code must actually be compiled. A restart or managed-only rebuild cannot fix a mismatched native layout. This tree currently uses dynamic user components and has no active CodegenComponents generator; external codegen integrations must supply the contract. Both hot-reload compiler paths deliberately keep `FunkeCodegenEnabled=false`.

The positional NativeBindings/ManagedCallbacks tables retain their existing size plus explicit layout-version handshake. Any added, removed, reordered or signature-changed slot requires matching native/managed table definitions, a coordinated layout-version bump, a build of both sides and an editor restart. Equal table sizes alone cannot detect a slot reorder; changing the version is part of that contract. Component preflight uses dedicated runtime-host entry points and does not add or reorder table slots. Package-specific native ABIs and native source files that have not been rebuilt are outside the component metadata check.

### How fast a reload is

When you save a script, the editor compiles it **in-process** with Roslyn, the C# compiler from your installed .NET SDK, instead of starting `dotnet build`. Editing an existing script usually compiles in well under a second.

* The editor asks MSBuild once for the exact compiler settings `dotnet build` would use (references, defines, `Nullable`, language version, analyzer config, source generators) and reuses them. MSBuild runs again (about 1 s) only after you **add or delete** a `.cs` file or change the active build configuration or defines.
* A change to the `.csproj`, a `.props`/`.targets` file, `global.json` or an installed package still triggers one full `dotnet build`, because MSBuild may need to copy new dependencies into `bin/`. The reload after that is in-process again.
* Source generators (for example `[GeneratedRegex]`, `[LibraryImport]`, `[JsonSerializable]`) run as usual. Analyzer warnings (`CAxxxx`) appear only in a full `dotnet build` or your IDE.
* Compile errors appear in the Console in the usual `File.cs(line,col): error CSxxxx` format.
* If the in-process compiler can't reproduce the build (an unusual project setup, a different .NET SDK, the compiler isn't installed next to the editor), the editor falls back to `dotnet build` and logs why under **ScriptSystem**. Set the environment variable `FUNKE_SCRIPT_COMPILER=dotnet` before starting the editor to always use `dotnet build`.

---

## How it works under the hood (brief)

You don't need this to write scripts, but it helps to know the shape:

* At startup, the engine boots the **.NET runtime** (via hostfxr) and loads two assemblies: **`Funke-ScriptCore.dll`** (the scripting API you call) and **your game's script assembly**.
* A bridge connects the two worlds through matched **function-pointer tables**: `InternalCalls` invokes the native table, and the engine invokes managed callbacks (for example `OnUpdate`). Packages can additionally expose their own P/Invoke APIs.
* This bridge is set up once during the scripting-host startup step described in [Startup Processes](../Engine-Core/Startup-Processes.md#start-scripting).

---

## Summary

* Write game logic in **C#**: `EntityScript` (per entity), `SceneSystem` (per scene), or `GlobalSystem` (whole app).
* Override lifecycle methods (`OnStart`, `OnUpdate`, `OnFixedUpdate`, …); attach scripts via a `ScriptComponent`.
* Expose fields to the Inspector with attributes like `[ShowInEditor]` and `[ClampValue]`.
* Use the rich API — `Input`, `Time`, `Physics2D`, `Audio`, `Log`, scene queries, math.
* Write game logic in **C++** with `NativeScript`; create and attach native scripts in the editor, expose registered fields to the Inspector, and combine them with C# scripts as needed.
* C# **hot reload** and C++ **native rebuild/reload** support iteration in the editor; see their respective sections for state-restoration behavior and toolchain requirements.

## Related pages

* [ECS](../Engine-Core/ECS.md) — entities and components your scripts work with.
* [Scenes](../Engine-Core/Scenes.md) — SceneSystems and the play-mode lifecycle.
* [Assets](../Assets/Assets.md) — referencing textures, audio, and fonts from scripts.
* [Job System](../Engine-Core/Job-System.md) — parallelizing heavy script work.

