# Presentation and coroutines

## Audio (`Funke.Audio`)

For headphone binaural playback, enable `source.Spatial = true` and `source.Hrtf = true`
(Inspector: Audio Source → 3D Sound → HRTF (Headphones)). One-shots:
`clip.PlayOneShotAt(position, hrtf: true)` or `Audio.PlayOneShotAt(clip, position, hrtf: true)`.
HRTF replaces `Panning`, follows camera yaw/pitch/roll, and mixes stereo clips to a mono
point source. Distance and Doppler still apply. It is off by default. Use headphones;
generic KEMAR localization varies per listener. See `Github-Docs/Documentation/Engine-Core/Spatial-Audio.md`.

Three paths:
- **Component:** add an `AudioSource`, set `Audio`, call `Play()`.
- **Fire-and-forget:** `Audio sfx = Audio.FromAssetUUID(guid); sfx.PlayOneShot(volume = 1f);` — or expose a `public Audio? Sfx;` field and assign it in the Inspector.
- **Master volume:** `Audio.MasterVolume` (0..1) scales everything the game plays. Automated runs (`Application.IsAutomatedRun`: `--input-script`, `--screenshot-at`, `--quit-at`, `--exec`, `--fixed-step`, `--background`, `--prefs-profile=<name>`) are muted by the engine on top of it (`--sound` keeps them audible), so games need no mute switch of their own.
- **3D sound:** `source.Spatial = true` (Inspector: Audio Source › 3D Sound) plays it from the entity's Transform3D (Transform2D at z 0): quieter with distance from the main Camera3D (2D games: Camera2D) and panned to its side. `MinDistance`, `MaxDistance`, `Rolloff` (`AudioRolloff.Logarithmic` = min/distance, default; `Linear` = silent at max), `Panning` (0..1, default 0.8), `DopplerLevel` (0 = off). One-shots from a point: `clip.PlayOneShotAt(position, volume, minDistance, maxDistance, rolloff, panning)` or `Audio.PlayOneShotAt(clip, position)`; silent ones aren't started. Sources are 2D by default. Check sounds in muted test runs from the log: console `audio.spatial` (listener + playing 3D sounds with gain/pan) or `audio.spatial x y z [min] [max] [log|linear] [panning]` (how a sound there would be heard, nothing plays).
- **Background audio:** `Audio.PlayInBackground = true` keeps audio playing and scripts updating (~10 Hz, no rendering) while minimized/backgrounded — on while playing, off when paused. Android needs build profile option `"backgroundAudio": "true"` (foreground media service + notification; request `android.permission.POST_NOTIFICATIONS` on 13+).
- **Any file on disk (music players, user files):** `AudioStream? s = AudioStream.Open(path);` streams MP3/WAV/FLAC from disk — `Play/Pause/Stop`, `Time` (get = position, set = seek), `Length`, `IsPlaying`, `IsAtEnd`, `Volume`, `Loop`. `Dispose()` it in `OnDestroy`; leaving Play mode closes every stream. On Android, reading shared storage first needs `Application.RequestPermission("android.permission.READ_MEDIA_AUDIO")` (poll `GetPermissionState`) plus the Android build profile option `"platformOptions": {"permissions": "READ_MEDIA_AUDIO, READ_EXTERNAL_STORAGE:32"}`.

## Animation (`Funke.Animator`)

Keyframe animation lives in assets, not code: `.anim` clips animate any Inspector property of the entity and its children (and visible script fields); an `.animctrl` controller switches clips with parameters. Author them in JSON (`funke-make-a-game/references/scene-json.md`, *Animation*) or in the editor's Animation / Animator windows. Scripts only drive parameters:
```csharp
private Animator? m_Animator;
public override void OnStart() => m_Animator = GetComponent<Animator>();
public override void OnUpdate()
{
    m_Animator!.SetBool("IsRunning", Input.GetAxis().X != 0f);   // SetFloat / SetInteger / SetBool by name
    if (Input.GetKeyDown(KeyCode.Space)) m_Animator.SetTrigger("Jump"); // consumed by the transition that uses it
}
public void OnFootstep(string foot) { }   // called by an animation event named "OnFootstep" (public, 0 or 1 param)
```
- `Play(state, normalizedTime = 0)` jumps, `CrossFade(state, seconds)` blends; `CurrentStateName`, `NextStateName`, `NormalizedTime`, `IsInTransition`, `TransitionProgress`, `Speed`.
- `Controller` / `Clip` take `AnimatorController` / `AnimationClip` assets; `public AnimatorController? Skin;` fields are Inspector asset slots.
- Unknown parameter or state names log one warning and do nothing. The Animator runs after scripts each frame, so values set in `OnUpdate` apply the same frame; don't also move an animated property from a script (the Animator overwrites it).
- Prefer a clip over a coroutine for authored motion (bobbing pickups, door swings, UI pop-ins, flashing colors); keep scripts for gameplay-driven motion.
- Many characters: `CullingMode` (`AlwaysAnimate` / `CullPose` / `CullCompletely`, what happens off screen) and `DistanceRate` (`ProjectSetting` / `Off` / `Custom` with `HalfRateDistance`, `QuarterRateDistance`) exist on `Animator` and `SkinnedMeshRenderer`; `RenderSettings.AnimationDistanceRateEnabled` (or `"animationDistanceRate": true` in `project.json` `graphics`) turns the rate on for every component left on `ProjectSetting`. Far or culled animations then write their pose only every 2nd / 4th frame (time is caught up), so don't drive gameplay from an animated property of a distant entity.

## Coroutines (`Funke.Coroutines`)

Start from an `EntityScript`, `SceneSystem`, or `GlobalSystem` with `RunCoroutine(async () => { … })`. Awaiters (each takes an optional `CancellationToken`):
```csharp
await new WaitForSeconds(1.5f);     // scaled time
await new WaitForNextFrame();
await new WaitForFrames(10);
await new WaitForFixedUpdate();
await new WaitUntil(() => done);
```
Funke awaiters inherit the `DestroyToken` from `RunCoroutine`; teardown cancels them and the runner swallows `OperationCanceledException`. A supplied token with `CanBeCanceled == true` replaces the inherited token; `default` and `CancellationToken.None` still inherit it. Pass `DestroyToken` explicitly to external async APIs such as `Task.Delay`. Cancellation does not stop arbitrary work that ignores it.

```csharp
public override void OnStart() => RunCoroutine(SpawnWaves);

private async Task SpawnWaves()
{
    for (int i = 0; i < 3; i++)
    {
        Instantiate(EnemyPrefab);
        await new WaitForSeconds(2f);
    }
}
```

## Other useful statics

- **`Log`** — `Log.Info/Warn/Error/Trace(string|object)`; output goes to the Console. `Print(x)` on a script == `Log.Info(x)` (`Log.Print` is internal).
- **`Localization`** — the game's strings from `Assets/Localization/<code>.json` (`meta` + flat `strings`): `Get(key)`, `Format(key, args)` (.NET `{0}`/`{0:N1}`, language culture), `Has`, `CurrentLanguage`, `FallbackLanguage`, `SystemLanguage`, `AvailableLanguages` (`LanguageInfo(Code, DisplayName)`), `SetLanguage(code)` (stored in PlayerPrefs `Funke.Language`, restored next start), event `LanguageChanged`. Static labels: put a `LocalizedText` component (`Key`) next to the `TextRenderer` instead of code. For glyph coverage, see [asset documentation](../../../../Github-Docs/Documentation/Assets/Assets.md#font-coverage).
- **`Window`** — `Width/Height`, `Size`, `Title`, `IsFullScreen`, `Position`, `Minimize/Maximize/Restore/Focus()`, events `Resized`, `FocusChanged`.
- **`Cursor`** — `Mode` (`Default`/`Locked`), `Visible`, `Texture`.
- **`Gizmo`** (namespace `Funke.Graphics`; debug draw, visible in editor) — `DrawLine`, `DrawWireSquare`, `DrawWireCircle`, `DrawSquare`, `DrawCircle`, `DrawText`; `Color`, `LineWidth`.
