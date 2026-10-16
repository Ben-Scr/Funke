# Animation

The **Animator** component plays keyframe animations on an entity and its children. A clip can animate any component property the Inspector shows (position, rotation, scale, colors, light intensity, UI rects, sprites ...) and the visible fields of C# scripts. An **Animator Controller** switches between clips with a state machine driven by parameters that scripts set.

For simple flip-book sprite animation the [Sprite Animator 2D](../Scripting/Scripting.md) component is still the lighter option.

## The pieces

| Asset / component | What it is |
|---|---|
| **Animation Clip** (`.anim`) | Tracks of keyframes over time, plus animation events. |
| **Animator Controller** (`.animctrl`) | States (each plays a clip), parameters and transitions with conditions. |
| **Animator** (component) | Plays a controller, or a single clip when it has no controller. Its entity is the root that track paths start from. |

Create clips and controllers from the Project panel (**Create > Animation > Animation Clip / Animator Controller**), or from the Animation window: select an entity and click **Create Clip...** (or **New Clip...** in the clip selector). A save dialog asks where to put the `.anim`; it opens next to the clip being edited, or in `Assets/`, and only accepts folders inside `Assets/`. The window then adds an Animator if needed and assigns the clip. A second clip on the same Animator creates a controller with one state per clip, saved next to the new clip. **Create Controller...** in the Animator window works the same way for `.animctrl` files.

### Animator fields

| Field | Meaning |
|---|---|
| **Controller** | The `.animctrl` to play. Wins over Clip. |
| **Clip** | A `.anim` played on its own (looping per its Loop setting) when there is no controller. |
| **Speed** | Playback speed multiplier. 0 freezes the pose. |
| **Update Mode** | **Normal** follows `Time.TimeScale`; **Unscaled Time** keeps playing while the game is paused; **Fixed** steps in the fixed update, in sync with physics. |
| **Apply In Edit Mode** | Plays the animation in the editor without pressing Play. The scene keeps its authored values: saving never stores an animated value. |

The Animator runs after the scripts each frame, so parameters a script sets in `OnUpdate` take effect in the same frame, and before 3D physics reads kinematic bodies.

## What can be animated

- **Component properties:** every property with a field in the Inspector. Numbers, booleans, vectors and colors use curves, and each vector or color channel is its own track (`Position.x`, `Color.a`). Booleans and integers step from key to key.
- **Assets, text and enums** (a sprite texture, a material, a text) use **discrete keys** that switch the value at each key time.
- **Sprite-sheet slices:** the **Texture** property of a Sprite Renderer or Image holds the texture together with its slice, so a Texture track switches between slices of one sheet. With **Record** on, pick another slice in the Inspector's Texture field (it shows `Sheet > Slice`); each pick becomes a key. For a plain looping flip-book the Sprite Animator 2D is simpler.
- **Rect Transform 2D:** Anchored Position, Size, Pivot, anchors, Rotation (degrees) and Scale.
- **C# script fields:** the public or `[ShowInEditor]` fields of scripts on the animated entities (`float`, `int`, `bool`, `Vector2/3/4`, `Color`). The script must be on the entity when the Animator starts.
- **Rotations** are animated as Euler angles in degrees, per axis.

Tracks address their entity by a path of child names from the Animator's entity (`"Arm/Hand"`; empty for the Animator's own entity). Renaming a child breaks the path: the Animator then skips that track and logs one warning, and the Animation window shows the track in the warning color.

## Animation window

**Tools > Animation**, or double-click a `.anim` in the Project panel. It edits the clips of the selected Animator, or of the nearest parent that has one. Choosing a time in the window previews that pose in the Editor View and the Inspector without changing the scene.

**Toolbar:** Record, previous key, preview playback (Space), next key, the current time and frame, the clip selector (with **New Clip...**), **Dopesheet / Curves**, the clip's **Length**, **Samples** (the frame rate of the key grid) and **Loop**.

**Creating keys**

- **Record** (the red dot): while it is on, every change to an entity below the Animator becomes a key at the current time. The change can come from the Inspector or a gizmo drag. A property that is keyed for the first time also gets a key with its old value at time 0. Record changes go into the clip; the scene keeps its values.
- **Add Property...** lists the animatable properties and script fields of the Animator's entities and adds tracks with keys at the start and end.
- Drag the value next to a track to key it at the current time.
- Double-click a track row to add a key there; **K** keys the selected tracks (or all) at the current time.

**Editing keys:** click to select (Ctrl adds), drag to move them (they snap to the sample grid; hold Shift to move freely), drag on empty space for a box selection, **Delete** removes them. Right-click a key for **Smooth**, **Flat**, **Linear** and **Constant** interpolation.

**Curves** shows the curves of the selected tracks, or all of them when no track is selected. Drag keys in time and value. Drag a smooth key's tangent handles to shape it; hold Ctrl to break the two tangents apart. **F** frames everything. The mouse wheel zooms time, Shift+wheel zooms values, and the middle mouse button pans.

**Navigation:** Ctrl+wheel zooms the dopesheet, Shift+wheel scrolls in time, a click on the ruler moves the playhead, `,` / `.` step one frame, and Home / End jump to the start / end. Ctrl+Z / Ctrl+Y undo and redo clip edits while the window has focus. Every edit is saved to the `.anim` file at once.

### Animation events

The row above the tracks holds events. Double-click it (or right-click > **Add Event**) to add one, then drag it in time. Double-click or right-click an event to set its **Function** and **Argument**. During playback the event calls that public method on every script of the Animator's entity:

```csharp
public class Footsteps : EntityScript
{
    public Audio? Step;

    // Event: Function "OnFootstep", Argument "left"
    public void OnFootstep(string foot) => Step?.PlayOneShot();
}
```

The method may take no parameter or one `string`, `int`, `float` or `bool`; the Argument text is converted to it. An event whose method no script has logs one warning. Events fire on every loop, and during a crossfade they fire for both clips.

## Animator window

**Tools > Animator**, or double-click a `.animctrl`. It shows the controller of the selected Animator, or the one you opened.

- **States:** right-click the canvas > **Create State**, or drop a `.anim` from the Project panel onto it. Select a state to edit its name, clip, speed and loop mode (**From Clip**, **Loop**, **Once**) on the right. The orange arrow from **Entry** marks the default state; change it with right-click > **Set as Default State**. Double-click a state to edit its clip in the Animation window.
- **Transitions:** right-click a state (or **Any State**) > **Make Transition**, then click the target. Select a transition to edit it:
  - **Has Exit Time / Exit Time:** waits until the source state has played that far, in clip lengths (0.75 = three quarters through; with looping states it fires each time the loop passes that point).
  - **Duration:** crossfade length in seconds. Properties that both clips animate blend; properties only one of them animates blend from or to their current value.
  - **Conditions:** all must hold. Floats and ints compare with Greater / Less (ints also Equals / Not Equal); bools test True / False; triggers fire when set and are reset by the transition that used them.
  - **Can Transition To Self** (Any State only) lets the transition restart the state that is already playing.
  - A transition without conditions fires at its exit time, or immediately when it has none.
- **Any State** transitions are checked before the current state's own, every frame, except while a crossfade is running.
- **Parameters** (left): **+** adds Float, Int, Bool or Trigger; the value next to each is its start value. Renaming a parameter updates the conditions that use it; deleting it removes them.
- In Play mode, select the playing entity: the window marks the active state with its progress, highlights a running transition, and the parameter column shows live values you can change.

Drag nodes to arrange them, drag the empty canvas (or use the middle mouse button) to pan, use the wheel to zoom, and press **F** to frame everything. **Delete** removes the selection; Ctrl+Z / Ctrl+Y undo and redo.

## Scripting

```csharp
public class Player : EntityScript
{
    private Animator? m_Animator;

    public override void OnStart() => m_Animator = GetComponent<Animator>();

    public override void OnUpdate()
    {
        m_Animator!.SetBool("IsRunning", Input.GetAxis().X != 0f);
        if (Input.GetKeyDown(KeyCode.Space)) m_Animator.SetTrigger("Jump");
    }
}
```

| Member | |
|---|---|
| `SetFloat/GetFloat`, `SetInteger/GetInteger`, `SetBool/GetBool` | Parameters by name. A name that doesn't exist (or has another type) logs one warning. |
| `SetTrigger(name)`, `ResetTrigger(name)` | A trigger stays set until a transition that checks it fires. |
| `HasParameter(name)` | |
| `Play(state, normalizedTime = 0)` | Jumps to a state without blending. |
| `CrossFade(state, duration)` | Blends from the current pose to a state. |
| `CurrentStateName`, `NextStateName` | The playing state, and the target of a running crossfade (empty when none). |
| `NormalizedTime` | Time in the current state in clip lengths (1.5 = halfway through the second loop). |
| `IsInTransition`, `TransitionProgress` | The running crossfade and its 0..1 progress. |
| `Speed`, `Controller`, `Clip` | The component's fields; `Controller` / `Clip` take `AnimatorController` / `AnimationClip` assets. |

Script fields of type `AnimationClip` and `AnimatorController` show as asset slots in the Inspector, so a script can swap controllers (e.g. per character skin): `GetComponent<Animator>().Controller = Skins[i];`. `AssetManager.LoadByGUID<AnimationClip>(guid)` and `FindAll<AnimatorController>(...)` work as for other assets.

## File formats

Both are JSON with a GUID in their `.meta`, like other assets, and can be written by hand or by tools. `index validate` checks them: tracks need a component and a property, controller states must point at `.anim` files, transitions at existing states, and conditions at parameters. It also checks that an Animator's `controllerAsset` / `clipAsset` point at the right kind of file.

```json
{
  "type": "AnimationClip", "version": 1,
  "length": 1.0, "loop": true, "sampleRate": 60,
  "tracks": [
    { "path": "", "component": "Transform3D", "property": "Position.y",
      "keys": [ { "t": 0, "v": 1, "interp": "bezier" }, { "t": 0.5, "v": 1.6, "interp": "bezier", "in": 0, "out": 0 }, { "t": 1, "v": 1 } ] },
    { "path": "Arm", "component": "SpriteRenderer", "property": "Texture",
      "discrete": [ { "t": 0, "v": "7300000000000000001|slice|arm_0" }, { "t": 0.5, "v": "7300000000000000001|slice|arm_1" } ] }
  ],
  "events": [ { "t": 0.5, "function": "OnFootstep", "argument": "left" } ]
}
```

- `component` is the serialized component name (`Transform3D`, `Transform2D`, `RectTransform2D`, `SpriteRenderer`, `MeshRenderer`, `Image`, `PointLight` ...) or a C# script class name. `property` is the Inspector property name (case-insensitive), with `.x/.y/.z/.w` or `.r/.g/.b/.a` for one channel.
- `interp` is `linear` (default), `step` or `bezier`; `in` / `out` are the bezier tangents (value per second) and default to 0 (flat).
- Discrete values are written as in the Inspector's text form: asset GUIDs as decimal strings, a sprite-sheet slice as `"<texture GUID>|slice|<slice name>"` (a bare GUID shows the whole texture), enum values as numbers, vectors as `"x,y"`. The value field next to a discrete track in the Animation window shows and edits the same text. Sprite Renderer and Image have no separate `SpriteName` property; the slice is part of `Texture`.

```json
{
  "type": "AnimatorController", "version": 1,
  "parameters": [ { "name": "IsRunning", "type": "bool", "default": false }, { "name": "Jump", "type": "trigger" } ],
  "states": [ { "name": "Idle", "clip": "7200000000000000001", "speed": 1, "position": [0, 0] },
              { "name": "Run", "clip": "7200000000000000002", "speed": 1, "loop": "loop", "position": [260, 0] } ],
  "defaultState": "Idle",
  "transitions": [
    { "from": "Idle", "to": "Run", "conditions": [ { "parameter": "IsRunning", "mode": "if" } ], "hasExitTime": false, "duration": 0.2 },
    { "fromAny": true, "to": "Jump", "conditions": [ { "parameter": "Jump", "mode": "if" } ], "duration": 0.1 }
  ]
}
```

- Parameter `type`: `float`, `int`, `bool`, `trigger`. Condition `mode`: `greater`, `less`, `equals`, `notEquals` (with `threshold`), `if`, `ifNot`.
- State `loop`: `clip` (default, use the clip's Loop), `loop`, `once`.
- Transitions reference states by name; `"fromAny": true` makes an Any State transition.

In a scene the component is written as `"Animator": { "controllerAsset": "<guid>", "clipAsset": "<guid>", "speed": 1, "updateMode": 0, "applyInEditMode": false }`. Game builds ship the clips and controllers that scenes, controllers and scripts reference.

## Limits

- One layer per controller; there are no blend trees, avatar masks or root motion yet.
- Fields of C# `IComponent` structs can't be animated (fields of `EntityScript`s and managed `Component`s can).
- Crossfades blend each rotation axis separately (Euler angles), which can take a long way around when the angles differ by more than 180°.
