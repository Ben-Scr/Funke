# Engine-Backlog aus der MarbleSky-Session (14.10.2026)

Beim Bau von MarbleSky (`C:\Users\benso\Index\Projects\MarbleSky`) gefundene Schwächen im Funke-Workflow, nach Nutzen sortiert.
Jeder Punkt nennt das Problem, wo es auftrat, den Workaround im Spiel (falls vorhanden) und einen Vorschlag für den Fix.

Bereits behoben in derselben Session (nur zur Einordnung):
- Szene neu starten invalidierte die C#-Komponenten der neuen Kopie (`Scene.cpp`, `NotifyScriptsEntityDestroyed`).
- Builds lieferten den veralteten `FunkeAssets`-Schnappschuss des Projekts aus, z. B. das alte Icon im Splash (`GameBuilder.cpp`).
- `RectTransform.Entity` verlangte ein `Transform2D` (`UIComponents.cs`).

---

## 1. 9-Slice für UI-Images

**Status (15.10.2026):** erledigt — `Image` hat `Type` (Simple/Sliced), `BorderLeft/Bottom/Right/Top` (Texturpixel) und `BorderScale` am Component statt in der `.meta`; `GuiRenderer` zeichnet bis zu 9 Quads (`Gui/NineSlice.hpp`), zu große Ränder schrumpfen. JSON `imageType`/`border`/`borderScale`, Inspector, C# `Image.Type/Border/BorderScale`. Doctest `NineSliceTests.cpp` (64×64, 24 px Rand auf 500×80 und 80×500), Runtime-Screenshot geprüft. Nachtrag: Ränder in der `.meta` (ganze Textur im Textur-Inspector, pro Slice im Sprite-Editor mit Hilfslinien, CLI `funke sprite border`), genutzt wenn die Image-Ränder 0 sind; `Image Type = Tiled` (`BuildTiled`, Kachelzahl begrenzt). Doctests für Tiled und den `.meta`-Roundtrip, Runtime-Screenshot geprüft.

**Problem:** `ImageComponent` hat nur `SpriteName` (Teilbereich aus einem Atlas, `Funke-Engine/src/Components/Graphics/ImageComponent.hpp`), keine Ränder, die beim Strecken fest bleiben. Abgerundete Buttons, Panels und Slider verzerren, sobald sie nicht in Originalgröße gezeigt werden.

**Workaround in MarbleSky:** Jedes Sprite wird in exakter Zielgröße generiert (`Tools/generate_art.py`). Die Slider-Füllung ist ein Balken voller Länge, der über `Mask` abgeschnitten wird (`Tools/generate_scenes.py`, Settings).

**Vorschlag:**
- Ränder (`border` links/rechts/oben/unten in Pixeln) in der Textur-`.meta` bzw. pro Sprite-Slice.
- `Image` bekommt `type`: `Simple` / `Sliced` (optional `Tiled`), dazu `pixelsPerUnitMultiplier`.
- `GuiRenderer` zeichnet bei `Sliced` 9 Quads.
- Dazu Inspector, Serialisierung (`"imageType"`), C#-API und Doku.

**Fertig, wenn:** ein 64×64-Rundrechteck mit 24-px-Rand auf 500×80 und 80×500 ohne verzerrte Ecken rendert, Doctest für die Quad-Aufteilung.

## 2. Klare Fehler bei DLL-Mismatch und Log-Fluten

**Problem A:** `funke.exe validate` endete ohne jede Ausgabe mit Exit-Code `-1073741512` (`0xC0000138`, Ordinal fehlt). Ursache: `Build.ps1` konnte `bin/Release-windows-x86_64/Funke-CLI/Funke-Engine.dll` nicht ersetzen (Datei in Benutzung) und meldete das nur als Hinweis.

**Vorschlag A:**
- `Build.ps1` schlägt fehl (oder meldet laut am Ende), wenn ein Deploy in einen Consumer-Ordner nicht klappt.
- Die Executables prüfen beim Start den Build-Identifier der Engine-DLL (`Engine::GetVersionLong()`) gegen den eigenen und geben eine verständliche Meldung aus.
- Für den Ordinal-Fehler, der vor `main` passiert, die Engine-DLL verzögert laden oder einen Mini-Launcher-Check einbauen.

**Problem B:** Eine Exception pro Frame (`[Script] Exception in OnUpdate()`) flutet das Log. `Funke-Runtime.log` rotiert nach 10 MB (`.1`, `.2`), und der erste, eigentliche Fehler ist weg. Auch der Log-Ausschnitt von `Run.ps1` enthielt ihn nicht mehr.

**Vorschlag B:**
- Gleiche Script-Exceptions drosseln: erste N vollständig, danach „… x weitere Male in den letzten n s“.
- `Run.ps1` liest seinen Ausschnitt über rotierte Dateien hinweg.

**Problem C:** Jeder Lauf meldet `[ScriptLoader] User assembly load context is still alive after unload` beim Beenden und verdeckt so echte Warnungen.

**Vorschlag C:** Beim Herunterfahren der App auf Debug herabstufen, nur bei Hot-Reload als Warnung melden.

**Status (15.10.2026):** B und C erledigt — `ScriptExceptionLog.cs` (3× voll, dann Zusammenfassung alle 5 s, Flush beim Play-Stopp); ALC-Meldung beim Beenden als Trace, Warnung nur, wenn der alte Kontext beim nächsten Laden noch lebt. A teilweise: `Build.ps1` bricht mit `DEPLOY FAILED` (Exit 1) ab, wenn eine Consumer-Kopie veraltet bleibt; `Run.ps1` liest über rotierte Logs (`<exe>.1.log` …). Nachtrag (A erledigt): eigene ABI-Kennung `FNK_ENGINE_ABI` (Hash über alle Engine-Header, `generate_build_metadata.py` → `EngineAbi.generated.hpp`); Editor/Launcher/Runtime (`EntryPoint.hpp`) und CLI vergleichen sie beim Start mit `Engine::GetAbiKey()` und loggen eine lesbare Meldung. Loader-Fehler vor `main`: `Process::LaunchDetachedChecked` + `DescribeLoaderFailure` in Launcher, Build & Run und CLI; `Run.ps1`/`Test.ps1` übersetzen 0xC0000135/138/139/7B/142 (`LoaderExitCodes.ps1`). `EngineAbiTests.cpp`.

## 3. Asset-Felder für Scripts (Material, Mesh, Prefab)

**Status (15.10.2026):** erledigt — `Material`- und `Mesh`-Felder (auch `List<…>`/Arrays) als gefilterte Asset-Slots (`material`/`mesh` → `AssetRef`), GUID-Roundtrip, Pfad→GUID-Normalisierung in Szenen; xUnit `ScriptAssetFieldTests.cs`. Prefab-Felder gab es schon über `Entity` (`prefab:<guid>`).

**Problem:** `ScriptInstanceManager.MapFieldType` kennt `texture` und `audio`, aber kein `Material`, kein `Mesh` und kein Prefab-Asset. Der Lint (`funke_game_tool.py`, Regel 2) verbietet `Material.FromAssetUUID` im Spielcode zu Recht.

**Workaround in MarbleSky:** Eine „Skin Library“-Entity mit deaktivierten Kindern, die je ein `MeshRenderer`-Material tragen. `MarbleSkin.cs` kopiert das Material des ausgewählten Kinds.

**Vorschlag:**
- Feldtypen `material`, `mesh` und `prefab` in `MapFieldType`, `PropertyType`, im Inspector-Picker und in `SceneSerializerShared.hpp` (GUID-Normalisierung wie bei `texture`/`audio`).
- Listen davon (`Material[]`) für Kataloge wie Skins.

## 4. Texture-Skybox für `Camera3D`

**Status (15.10.2026):** erledigt — Skybox-Texturen kamen mit Punkt 21 (Panoramic / 6-Sided Sky). Neu: `Camera3D.AmbientFromSkybox` leitet das Sky-Ambient aus dem Skybox-Material ab (Gradient-Farben, Bänder des Panoramas, Würfelseiten; `Graphics/SkyAmbient`), Texturmittel einmal pro Textur gecacht. Nachtrag: der Asset-Watcher des Editors leert den Mittelwert-Cache bei geänderten Bildern.

**Problem:** `Camera3DComponent` kann als Hintergrund nur einen Farbverlauf (`SkyTop`/`SkyHorizon`/`SkyGround` + Sonne).

**Workaround in MarbleSky:**
- Nach innen gewandte Kugel (`sky_dome.obj`) mit unbeleuchtetem Material und Panorama-Textur.
- `SkyDome.cs` hält sie auf der Kamera.
- Sonne bzw. Mond sind passend zum `DirectionalLight` gemalt.

**Vorschlag:**
- `background`-Modus `Texture` mit equirektangularer Textur (optional Cubemap), `skyRotation` und `skyExposure`.
- Gerendert als Fullscreen-Pass hinter allem, damit nichts geclippt wird und keine Kuppel-Geometrie nötig ist.
- Ambient optional aus der Textur ableiten (niedrig aufgelöste Mittelwerte).

## 5. `ParticleSystem3D.Emit` nutzt die alte Emitter-Position

**Status (15.10.2026):** erledigt — `Funke_ParticleSystem3D_Emit` setzt vorher die aktuelle Weltmatrix (`Transform3DHierarchySystem::ComputeWorldMatrix`, inkl. Eltern); Doctest in `ParticleSystem3DTests.cpp`.

**Problem:** `ParticleSystem3DComponent::Emit` verwendet `m_EmitterWorld` aus dem letzten `Simulate`. Ein Script, das den Emitter verschiebt und sofort `Emit` ruft, erzeugt den Burst an der vorherigen Stelle.

**Workaround in MarbleSky:** `EffectsPlayer.cs` verschiebt den Emitter und emittiert zwei Frames später.

**Vorschlag:** In `Funke_ParticleSystem3D_Emit` (`ScriptBindings3D.cpp`) vor dem Emittieren die aktuelle Weltmatrix aus dem `Transform3D` (inklusive Eltern) setzen. Dazu ein Doctest: verschieben, emittieren, Partikelposition prüfen.

## 6. Validierung von Komponenten-Verweisen in Szenen

**Problem:** Script-Felder verweisen über `"<uuid>:<Anzeigename>"` (z. B. `"…:Particle System 3D"`, `"…:Rect Transform 2D"`). Ein falscher Name oder ein fehlendes Component fällt erst zur Laufzeit als `null` auf. Bei Verweisen innerhalb eines Prefabs (Kind-Entity im selben Prefab) war unklar, ob sie beim Instanziieren umgebogen werden. MarbleSky hat deshalb Pads und Bumper ohne Prefabs gebaut.

**Vorschlag:**
- `funke validate` prüft jedes `component:`/`entity`-Feld: Ziel-Entity existiert und hat das Component (serialisierten Namen zusätzlich zum Anzeigenamen akzeptieren).
- Prefab-interne Verweise per Test absichern und in `scene-json.md` dokumentieren.

**Status (15.10.2026):** erledigt — `funke validate` prüft Entity-/Komponenten-Verweise (inkl. Prefab-Instanzen und Overrides), Test in `SceneSerializerPrefabRefTests.cpp` deckt Komponenten-Verweise ab, `scene-json.md` ergänzt.

## 7. Release-Builds ohne Debug-Overlay als Standard

**Problem:** `showRuntimeStats` und `showRuntimeLogs` sind standardmäßig `true`, auch bei `funke build --release`. Das gebaute Spiel zeigt dann Statistik- und Log-Fenster, auf dem Handy über dem Pause-Button.

**Vorschlag:** Bei Release-Builds standardmäßig aus (in `project.json` bzw. im Build-Profil überschreibbar), oder eine Option im Build-Panel.

**Status (15.10.2026):** erledigt — ungesetzt folgen beide Overlays dem Build-Profil (`FunkeProject::ResolveShowRuntimeStats/Logs`), explizite Werte in `project.json` gewinnen.

## 8. Nahtstellen zwischen Physik-Collidern

**Problem:** Eine rollende Kugel hüpft an jeder Naht zwischen getrennten Box-Collidern, auch bei Kindern eines Compounds und bei exakt bündigen Oberkanten.

**Workaround in MarbleSky:** Die Fahrfläche jedes Levels ist ein verschweißtes Dreiecks-Mesh (`MeshCollider3D`, gemeinsame Kanten, generiert).

**Vorschlag:**
- Prüfen, ob Joltʼs `EnhancedInternalEdgeRemoval` auch zwischen getrennten statischen Bodies greift.
- Sonst ein Hilfsmittel „statische Collider zu einem Mesh zusammenfassen“ (Editor-Aktion oder Build-Schritt) und das Muster in der Physics-3D-Doku beschreiben.

## 9. Multi-Touch — teilweise erledigt (15.10.)

**Erledigt:** `Input.TouchCount` / `Input.GetTouch(i)` (Id, Position, StartPosition, Phase), gespeist aus den SDL-Finger-Events (`TouchTracker` in `Core/Input.hpp`, Doctests in `TouchTests.cpp`, Interop-Version 20). MarbleSky liest damit Joystick und Sprung-Button getrennt.

**Erledigt (Nachtrag):** `UIEventSystem::UpdateFingerPresses` drückt/klickt Interactables mit jedem weiteren Finger; zwei Buttons gleichzeitig gehen. Drag-Widgets (Slider, Scrollbar, ScrollRect, InputField, Dropdown) bleiben beim Maus-Finger. `UIMultiTouchTests.cpp`.

## 10. Debug-Werkzeuge und eigene Test-Builds

**Erledigt (15.10.2026), alle fünf Punkte:**
- **Konfiguration:** `BuildProfile::Debug` (Debug-Runtime + PDBs von Runtime, Engine, ScriptCore und User-Assembly, Define `FUNKE_BUILD_DEBUG`, braucht Engine-Quellen bzw. `Runtime-Debug`). Build-Fenster „Configuration“, `funke build --debug`. C# `Engine.BuildConfiguration`: `Editor`/`Development`/`Release`/`Debug`.
- **Profile:** `.build` mit `configuration`, `defines`, `showRuntimeStats/Logs`, `devConsole`, `isolatedPrefs` (`FunkeBuildProfile::ApplyTo`, nur im Build-Snapshot). UI unter Build Profiles › Configuration & Testing. CLI-Flags schlagen das Profil.
- **Dev-Konsole:** `DevTools/DevConsole` (Engine) + `RuntimeConsoleLayer` (` bzw. Drei-Finger-Tipp, Tab, Verlauf). Eingebaut: `help`, `scene`, `timescale`, `physics.debug/contacts`, `stats`, `logs`, `screenshot`, `prefs.clear/path`, `echo`, `clear`, `quit`. C#: `[DevCommand]` (typisierte und optionale Parameter), `DevConsole.Register/Execute`. Interop-Layout 25. Spiel-Eingaben sind blockiert, solange sie offen ist.
- **Isolierte Spielstände:** `--prefs-profile=`, `"prefsProfile"`, `isolatedPrefs` → `PersistentDataPath/Profiles/<name>`. `Run.ps1` nutzt standardmäßig `agent`.
- **Physik-Overlay:** F8 / `physics.debug`. 2D (`Physics/PhysicsDebugDraw2D`, 2 px Umrisse, Box2D-Kontakte) und 3D (`Physics3D/PhysicsDebugDraw3D`, aus dem Editor in die Engine verschoben; Kontakte über `PhysicsWorld3D::ForEachActiveContact`). Neue Gizmo-Ebene `Debug`. Runtime flusht jetzt die 3D-Linien. Der Gizmo-Pass läuft vor den ImGui-Overlays.
- **Automatisierte Läufe:** `DevTools/RunAutomation`: `--input-script` (Tasten, Maus, Touch über SDL-Events; Befehle; Screenshots; Log; Quit), `--screenshot-at`, `--fixed-step`, `--quit-at`, `--exec`. Die Uhr läuft nur in nicht pausierten Spiel-Frames. Testläufe pausieren nicht im Hintergrund, und Vollbild minimiert nicht bei Fokusverlust. `ScreenCapture` liest die Swapchain (CopySrc). `funke run` reicht alles durch. `Run.ps1 -ScreenshotAt/-InputScript/-FixedStep/-Exec/-QuitAt` schreibt Screenshots pro Lauf nach `run\shots-*`.
- **Getestet:** `DevToolsTests.cpp`, `GameBuilderTests.cpp` (Prepare), xUnit `DevCommandRegistryTests.cs`; im Runtime mit `bin-int/_agent/DevToolsSandbox` (Input-Script: Tasten frame-genau, Klick, `[DevCommand]`, Prefs-Profil, Screenshots, Quit bei Frame 120), `MarbleSkyCopy` und `SandboxPhysics3D` (Collider, Trigger, Kontakte).
- **Doku:** `Editor/Testing.md`, CHANGELOG, Skills.
- **Debug-Build geprüft:** `funke build` mit einem QA-Profil (`configuration: Debug`) baut die Debug-Runtime (bei 2D-only als Modul-Variante) und liefert `Funke-Runtime.pdb` + `Funke-Engine.pdb` + Script-PDBs aus. Das gebaute Spiel meldet `config=Debug` und das Profil `QA`. Editor-UI (Build-Fenster, Build Profiles) per `Run.ps1 -OpenWindow` gescreenshottet.
- **Offen:** Gamepads lassen sich nicht skripten. Kein `--seed` für Zufall. UI-Klicks unter dem offenen Konsolenfenster erreichen weiterhin das Spiel-UI. Die Konsole selbst (Tippen, Tab, Verlauf) ist nicht per Hand durchgeklickt; die Befehle laufen über dieselbe `DevConsole::Execute` wie die Input-Scripts.

**Ist-Zustand (vorher):** Ein Game-Build ist entweder `Development` (Release-Binaries, `Runtime-Development`, Define `FNK_RELEASE`) oder `Release` (Dist-Binaries, `FUNKE_BUILD_RELEASE`). Es gibt keinen Debug-Runtime für Spiele (Asserts, Symbole, C#-Debugging), und keine eigenen Varianten. `CustomDefines` gelten projektweit, ein Build-Profil (`BuildProfiles/*.build`) wählt nur Plattform und Render-Backend (`GameBuilder.cpp`, `BuildManagedDefinesForSnapshot`).

**Was beim Testen von MarbleSky gefehlt hat:**
- **Testhilfen ohne Code-Eingriff:** Um Sprungpad, Bumper und Ziel zu prüfen, musste ich eine Probe per Umgebungsvariable vorübergehend in `LevelTimer.cs` einbauen (Murmel schieben, ins Ziel teleportieren, Pfad loggen) und danach wieder löschen.
- **Getrennte Spielstände für Testläufe:** Testläufe schreiben in die echten `PlayerPrefs` des Spielers (`%LOCALAPPDATA%\DefaultCompany\MarbleSky\prefs.json`). Ich musste sichern und wiederherstellen, sonst wären Münzen, Sterne und Bestzeiten verfälscht.
- **Physik-Debug im laufenden Spiel:** Collider, Trigger und Kontakte sind nur im Editor sichtbar. Die Nahtstellen-Hüpfer und Trigger-Größen hätte man im Runtime sofort gesehen.
- **Screenshots zu einem Zeitpunkt im Spiel:** `Run.ps1 -Shots/-Interval` arbeitet nach Wanduhr. Ladezeiten verschieben, was auf dem Bild ist.

**Vorschlag:**
1. **Build-Konfiguration im Profil:**
   - `BuildProfiles/<Name>.build` bekommt `"configuration": "Debug" | "Development" | "Release"`, eigene `"defines"` (z. B. `FUNKE_TEST`, `DEMO`) und Schalter wie `showRuntimeStats`, `devConsole`, `isolatedPrefs`.
   - So lassen sich beliebige Test-, Demo- oder QA-Builds anlegen (`funke build --profile AndroidTest`).
   - Ein **Debug**-Runtime (Debug-Binaries, C#-Debug-Symbole, Debugger zum Anhängen) als dritte Konfiguration.
2. **Entwickler-Konsole im Runtime** (nur in Development/Debug/Test):
   - Taste bzw. Drei-Finger-Tipp öffnet sie.
   - Eingebaute Befehle: `scene <name>`, `timescale`, `physics.debug`, `stats`, `prefs.clear`.
   - Spiele registrieren eigene Befehle über ein C#-Attribut (`[DevCommand] void Teleport(string target)`), z. B. für Münzen geben oder zum Checkpoint springen.
3. **Isolierte Spielstände:** `--prefs-profile <name>` bzw. `isolatedPrefs` im Test-Profil leitet `PlayerPrefs` in einen eigenen Ordner um. `Run.ps1` nutzt das standardmäßig.
4. **Physik-Debug-Overlay im Runtime:** Collider-Wireframes, Trigger farbig, Kontaktpunkte und -normalen. Umschaltbar per Konsole oder Taste.
5. **Automatisierte Testläufe:**
   - Eingaben skriptbar machen (`--input-script <datei>` mit Achsen/Tasten pro Zeitpunkt).
   - Screenshots nach Spielzeit oder Frame (`--screenshot-at 3.5s`).
   - Deterministischer fester Zeitschritt für reproduzierbare Läufe.
   - Mit `Run.ps1` gekoppelt: wiederholbare Smoke-Tests („Level 5 bis zum ersten Checkpoint“) statt Proben im Spielcode.

## 11. View Settings: Toolbar-Elemente im Editor View ein-/ausblenden

**Erledigt (15.10.2026):** Zahnrad am Ende der rechten Pill bzw. Rechtsklick auf eine Pill, ein Schalter pro Element plus ganze Pills, gespeichert in `EditorPreferences.json` (`EditorViewToolbar`), Reset to Default, alle Aktionen zusätzlich in der Command Palette. Nachtrag: dasselbe für die Game-View-Leiste (Zahnrad, Rechtsklick, Edit › View, Command Palette; `GameViewToolbar` in den Preferences), im Editor durchgeklickt.

**Problem:** Die beiden Pills im Editor View (`ImGuiEditorLayerViewport.cpp`, `drawToolsPill` / `drawOptionsPill`) zeigen immer alles: Gizmo-Modi (None/Move/Rotate/Scale/Rect), 3D-Umschalter, Draw-Mode-Dropdown (Default/Triangle/Mixed), Gizmos, Post Processing, Grid-Snap mit Einstellungs-Popup. Wer z. B. den Draw-Mode nie braucht, kann das Dropdown nicht ausblenden, und die Leiste verdeckt Teile der Szene.

**Vorschlag:**
- Neuer Eintrag „View Settings“ (Zahnrad am Ende der rechten Pill und/oder Rechtsklick auf eine Pill) mit je einem Schalter pro Element: Gizmo-Modi, 3D-Umschalter, Draw Mode, Gizmos, Post Processing, Grid Snap.
- Ganze Pills ausblendbar (links/rechts), das View-Settings-Menü selbst bleibt immer erreichbar.
- Speichern in den `EditorPreferences` (pro Benutzer, nicht im Projekt), mit „Reset to Default“.
- Ausgeblendete Funktionen bleiben per Shortcut bzw. Command Palette nutzbar (z. B. Gizmo-Modus über Tastatur).
- Optional dasselbe für die Game-View-Leiste (Stats/Log-Overlay usw.).

**Fertig, wenn:** jedes Toolbar-Element einzeln ausblendbar ist, die Auswahl einen Editor-Neustart überlebt und die Pills ohne Lücken neu layouten. Doku in `Github-Docs/Documentation` und `CHANGELOG.md`.

## 12. Custom Shader, die der User selbst schreibt

**Ist-Zustand:**
- `.wgsl` (und `.vert`/`.frag`/`.glsl`) wird im Projekt schon als `AssetKind::Shader` erkannt (`AssetRegistry.hpp`), aber nirgends benutzt.
- Shader kommen nur aus dem Engine-Bestand (`Shader.cpp`, eingebaut oder `FunkeAssets/Shaders/webgpu/<name>.wgsl`).
- `Material` (`Graphics/Material.hpp`) ist ein fester PBR-Parametersatz (BaseColor, Metallic, Roughness, Emissive, Lit, Surface) ohne Shader-Feld.
- 2D (`SpriteRenderer`, UI-`Image`) und Post-Processing haben gar keine Material-/Shader-Wahl.

**Vorschlag:**
1. **Shader-Asset im Projekt:** `.wgsl`-Datei mit kleinem Kopf-Block für Metadaten (Name, Ziel `Mesh3D` / `Sprite2D` / `UI` / `PostProcess`, Blend-Modus, Cull-Mode, Depth Write).
2. **Eingeschränkte Schnittstelle statt komplettem Pipeline-Shader:** Der User schreibt nur Funktionen wie `fn surface(in: SurfaceInput) -> SurfaceOutput` (Farbe, Normal, Emissive, Alpha) bzw. optional `fn vertex(...)` für Vertex-Offset. Die Engine fügt das in ihre Vorlage ein (Bind Groups, Licht, Schatten, Tone Mapping), damit User-Shader Backend-Änderungen überleben.
3. **Eigene Parameter:** `@property`-Annotationen (float, vec2–4, color, texture, mit Range/Default) werden geparst und landen als zusätzliche Felder im `.material` (`"shader": <guid>`, `"properties": {…}`), gepackt in einen eigenen Uniform-Buffer und Bind Group.
4. **Material-Erweiterung:** `Material` bekommt `Shader` (GUID, 0 = Standard-PBR) und eine Property-Map; Inspector zeigt die Felder des gewählten Shaders. Für 2D bekommt `SpriteRenderer` (und optional `Image`) ein Material-Feld, Batching nur über gleiche Material-GUID.
5. **Custom Post-Process-Pass:** Fullscreen-Shader als Effekt in `PostProcessing2DComponent` bzw. der 3D-Kamera einhängbar, mit Reihenfolge.
6. **Hot Reload und Fehler:** Datei-Watcher kompiliert neu; Compiler-Fehler von Dawn/Tint mit Datei und Zeile in die Console, bei Fehler bleibt der letzte gültige Stand bzw. ein Magenta-Fallback-Shader aktiv.
7. **C#-API:** `material.SetFloat/SetColor/SetTexture("name", …)` für Laufzeitänderungen, plus Builtins wie `time`, `deltaTime`, Kamera- und Objektmatrizen in der Vorlage.
8. **Build:** Shader-Assets werden mit ins Spiel gepackt; optional beim Build vorkompilieren bzw. validieren (`funke validate` meldet Shader-Fehler).

**Fertig, wenn:** ein User-Shader (z. B. animiertes Wasser mit `time` und zwei Farb-Properties) auf einem Mesh und auf einem Sprite im Editor und im gebauten Spiel läuft, Änderungen per Hot Reload sofort sichtbar sind, ein Syntaxfehler mit Zeilennummer in der Console landet und nichts abstürzt. Doctests für Property-Parsing und Material-Serialisierung, Doku-Seite „Custom Shaders“ mit Beispielen, `CHANGELOG.md`.

**Stand 15.10.2026 – umgesetzt (uncommitted):**
- Kern: `Graphics/CustomShader.{hpp,cpp}` (Kopf-Direktiven, `@property`, WGSL-Uniform-Layout, Vorlagen Mesh3D/Sprite2D/PostProcess, Magenta-Fallback), `Graphics/CustomShaderLibrary.{hpp,cpp}` + `CustomShaderGpu.hpp` (Laden per GUID, Kompilieren im Error-Scope mit Zeilen-Mapping auf die User-Datei, letzter gültiger Stand, Hot Reload per Datei-Stempel im Editor, `CustomMaterialBindings` = Gruppe 1 je Renderer, Headless-Validierung). `Material` hat `Shader` + `Properties` (`"shader"`/`"properties"`), `Material.*` ist dafür aus dem Funke.3D-Modul in den Kern gewandert.
- Renderer: Mesh3D in `Renderer3D` (Sortierung/Batching nach Material, transparente User-Shader in der Rückwärts-Sortierung), Sprite2D in `Renderer2D` (`SpriteRendererComponent::Material`, `"materialAsset"`, Batching nur bei gleichem Material), PostProcess in `PostProcessor` (`PostProcessing2DComponent::CustomEffects[4]`, `"customEffects"`, nach den eingebauten Effekten).
- Editor: Shader-Feld + Property-Felder + Fehlerliste im Material-Inspector (`Inspector/MaterialShaderInspector.cpp`), Material-Slot am Sprite Renderer, 4 Effekt-Slots an Post Processing.
- C#: `Material.SetFloat/SetVector/SetColor/SetTexture`, Getter, `HasProperty`, `ShaderUUID`, `SpriteRenderer.Material` (Interop-Layout 17).
- Build/CLI: Shader werden über die GUID-Referenzen der Materialien mitgepackt; `funke validate` kompiliert jede `.wgsl` (Headless-Device) und prüft `"shader"`-Verweise.
- Getestet: Doctests (`CustomShaderTests.cpp`), Editor/Runtime/gebautes Spiel mit Wasser-Shader auf Mesh und Sprite, Hot Reload, Syntaxfehler mit Zeile, Post-Effekt, C#-API. Doku `Engine-Core/Custom-Shaders.md`, CHANGELOG.

**Nachtrag – Reste erledigt:**
- Ziel `UI`: `Image::Material` (`"materialAsset"`, Inspector-Slot, C# `Image.Material`, Interop-Layout 21); `CustomSpriteDrawer` (Kern) teilt die Pipeline-Logik zwischen Renderer2D und GuiRenderer.
- Post-Processing für 3D: `PostProcessing2DComponent`, `PostProcessor` und die PP-Bindings sind Kern (nicht mehr Funke.2D); Renderer3D rendert bei aktivem Effekt in ein Zwischen-FBO und lässt die Kette laufen (Runtime und Editor-Views, Editor-View-Schalter gilt auch für 3D).
- Vertex-Offset: Mesh3D-Vorlage hat `vs_shadow`, der Schatten-Pass nutzt dafür eine eigene Pipeline; Picking über `// @displacement` (Bounds werden vergrößert).
- Material-Inspector im Editor durchgeklickt (Shader-Feld, Properties, live Änderung, Reset).
- Skybox-Ziel: mit Punkt 21 umgesetzt (`ShaderTarget::Skybox`).
- Prüfprojekt: `bin-int/_agent/PartialsCheck`.

## 13. Animator: Keyframe-Animationen (`.anim`) und State Machine

**Erledigt (15.10.2026):**
- Kern (`Funke-Engine/src/Animation/`): `AnimationClip`/`AnimatorController` mit JSON und Libraries, `AnimatorStateMachine` mit Parametern, Exit Time, Any State, Trigger-Verbrauch und Crossfade, `AnimationBinding` über die `PropertyDescriptor`s bzw. C#-Felder (`SetScriptField`) und `AnimatorRuntime` (Blending, Events per `InvokeScriptMethodByName`).
- `AnimatorComponent` (Kern, Icon `animator`) und `AnimatorSystem` (nach `ScriptSystem`, Update oder Fixed). RectTransform2D hat dafür jetzt Descriptoren.
- C#: `Animator`, `AnimationClip`, `AnimatorController`, Asset-Felder, `AssetManager`.
- Editor: Animation-Fenster (Dopesheet, Kurven mit Tangenten, Events, Record per Werte-Diff, Undo) und Animator-Fenster (Graph, Parameter, Transition-Details, Live-Ansicht). Dazu `AnimationPreview`: Posen werden nur zwischen Panel-Aufbau und Viewport-Render geschrieben, so dass Speichern und Play-Snapshot die Autorenwerte sehen. Außerdem Content-Browser-Create/Open und Hot Reload.
- `funke validate` prüft `.anim`/`.animctrl` und Animator-Verweise.
- Getestet: Doctests (`AnimationTests.cpp`), Play-Modus in `bin-int/_agent/AnimatorSandbox` (Transitionen und Events im Log), beide Fenster im Editor durchgeklickt, gespeicherte Szene ohne Vorschauwerte.
- Doku `Engine-Core/Animation.md`, CHANGELOG, Skills.

**Offen:** Blend Trees, Layers und Root Motion. Felder von C#-`IComponent`-Structs sind nicht animierbar. Ein beim Test gefundener Altfehler: Der erste Klick in ein schwebendes OS-Fenster über dem Editor View landet als Picking im Viewport, was die Auswahl aufhebt. Zweiter Altfehler: `funke build` eines reinen 3D-Projekts scheitert auf diesem Rechner. `FunkeProject::GetMSBuildPath` wählt VS „18“, dessen Header nicht zur VS2022-PCH der Runtime-Variante passen (C2375 `__builtin_assume_aligned`). Gespielt wurde deshalb im Runtime-Player direkt mit dem Projekt.

**Ist-Zustand:** Es gibt nur `SpriteAnimator2D` (`SpriteAnimationSystem.cpp`, Frame-Wechsel eines Sprites). Beliebige Komponentenwerte (Position, Farbe, Light-Intensität, Script-Felder …) lassen sich nur per eigenem C#-Code animieren, und es gibt keine Übergänge zwischen Animationen.

**Vorschlag:**
1. **Animation Clip als Asset (`.anim`, JSON, GUID über `.meta`):**
   - Liste von Tracks: Pfad zur Ziel-Entity (relativ zum Animator, z. B. `""` oder `"Arm/Hand"`), Komponente (serialisierter Name oder Script-Klasse), Feld (inkl. Unterfeld wie `position.x`, `color.a`).
   - Pro Track Keyframes `{ time, value, interpolation: Linear | Step | Bezier, inTangent, outTangent }`.
   - Clip-Einstellungen: Länge, Loop, Sample-Rate fürs Editieren, optional Events (`{ time, function: "OnFootstep" }`, ruft eine Methode auf Scripts der Entity).
2. **Generischer Wertezugriff:** Tracks laufen über die vorhandenen `PropertyDescriptor`s der Komponenten (dieselbe Reflection wie der Inspector) und bei C#-Scripts über die exponierten Felder. Damit ist jede Komponente animierbar, ohne Sonderfälle. Interpolierbar: float, int (gerundet), bool/enum (Step), vec2/3/4, Color, Quaternion (Slerp); Asset-Verweise (z. B. Sprite/Textur) als Step-Track, was Sprite-Animationen mit abdeckt.
3. **`Animator`-Komponente:** Verweis auf einen Animator Controller (oder direkt einen Clip), `Speed`, `UpdateMode` (Normal / Unscaled Time / Fixed), `ApplyInEditMode` für die Vorschau. Ein `AnimatorSystem` sampelt nach den Scripts und vor Physik-Sync bzw. Rendering.
4. **Animator Controller als Asset (`.animctrl`):**
   - States (je ein Clip, Speed, Loop), Default-State, Any-State.
   - Parameter: `Float`, `Int`, `Bool`, `Trigger`.
   - Transitions mit Bedingungen (`Greater`/`Less`/`Equals`/`NotEquals`/`If`/`IfNot`), `HasExitTime`/`ExitTime`, Übergangsdauer (Crossfade, zwischen beiden Clips interpoliert), Trigger werden beim Auslösen verbraucht.
5. **C#-API wie in Unity:** `animator.SetTrigger("Jump")`, `ResetTrigger`, `SetBool/GetBool`, `SetFloat/GetFloat`, `SetInteger/GetInteger`, `Play("State", normalizedTime)`, `CrossFade("State", duration)`, `CurrentStateName`, `NormalizedTime`, `IsInTransition`, plus `OnAnimationEvent`.
6. **Editor:**
   - **Animation-Fenster:** Clip der ausgewählten Entity, Timeline mit Tracks, Record-Modus (Änderungen im Inspector setzen Keyframes an der Abspielposition), Kurven-Editor für Tangenten, Abspielen/Scrubben in der Szene ohne Play-Modus.
   - **Animator-Fenster:** Node-Graph der States mit Pfeilen für Transitions, Parameter-Liste links, Bedingungen im Inspector der Transition, im Play-Modus aktiver State und Übergangsfortschritt live sichtbar.
7. **Robustheit:** Fehlende Ziel-Entity/Komponente/Feld → Track wird übersprungen, einmalige Warnung (nicht pro Frame), Track im Animation-Fenster gelb markiert. Umbenennen von Entities passt die Pfade an.

**Abgrenzung:** `SpriteAnimator2D` bleibt für einfache Frame-Animationen; ein Konverter „Sprite-Animation → `.anim`“ ist optional.

**Fertig, wenn:**
- ein `.anim` mit Tracks auf `Transform`, `SpriteRenderer.Color` und einem C#-Script-Feld im Editor erstellt, gespeichert, neu geladen und abgespielt werden kann,
- ein Controller mit `Idle → Run` (Bool `IsRunning`) und `Any → Jump` (Trigger `Jump`) per `SetBool`/`SetTrigger` aus C# umschaltet, inkl. Crossfade,
- beides im gebauten Spiel genauso läuft.
Doctests für Interpolation/Sampling, `.anim`-/Controller-Serialisierung und Transition-Auswertung (Trigger-Verbrauch, Exit Time), xUnit für die C#-API, Doku-Seite „Animation“, `CHANGELOG.md`.

## 14. Umbenennung Funke → Funke

**Entscheidung (14.10.2026):** Komplettes Rebranding vor 2026.1.0 final, solange ein Kompatibilitätsbruch am wenigsten kostet. Harter Bruch: alte Projekte müssen nicht mehr laden, Migration später.

**Umfang:**
- Makros: `FNK_*` → `FNK_*`, `FUNKE_*` → `FUNKE_*` (inkl. `FUNKE_API`, `FUNKE_PROFILE_*`, `FUNKE_BUILD_RELEASE`, `FNK_RELEASE`).
- C++-Namespace `Funke` → `Funke`, C#-Namespace und Assemblies (`Funke-ScriptCore` → `Funke-ScriptCore`, `Funke.*` → `Funke.*`).
- Projektordner, premake-Projekte, DLLs und Executables: `Funke-Engine` → `Funke-Engine`, `Funke-Editor`, `Funke-Runtime`, `Funke-Launcher`, `Funke-EditorRuntime`, `Funke-CLI` (`funke.exe` → `funke.exe`), `Funke-ScriptCompiler`, Test-Projekte, `Funke.sln`.
- Pakete: `funke-package.lua` → `funke-package.lua`, `Pkg.Funke.*` → `Pkg.Funke.*`, `FUNKE_PACKAGE_API`, `FunkePackage_OnLoad`, Package-Tools und -Registry-Namen.
- Dateiformate und Pfade: Magic-Strings wie `FNKSCNB`, `FunkeAssets`, `%LOCALAPPDATA%\Funke\` (Logs, CrashDumps, Einstellungen), Projekt-Dateinamen und Format-Felder.
- Tooling: `scripts/dev/*.ps1`, CI, `Setup.py`, Skills (`index-*` → `funke-*` in `.claude/skills` und `.agents/skills`), `CLAUDE.md`, Memory.
- Doku, `CHANGELOG.md`, README, Fenstertitel, About-Dialog, Splash und Icons.

**Ablauf:**
1. Vorher: großen uncommitteten Arbeitsstand committen, parallele Agents pausieren.
2. Umbenennung in einem Durchgang per Skript (Dateinamen + Inhalte, mit Ausnahmeliste für `External/` und echte Wörter wie „index“ in Arrays/Suchindizes), danach premake regenerieren.
3. `Build.ps1 All`, `Test.ps1 -Suite All`, `Run.ps1` für Editor, Launcher und Runtime, Spiel-Build eines Testprojekts.
4. Rest-Suche nach `Funke`/`FNK_`/`FUNKE_` und manuell prüfen.

**Beim User:** GitHub-Repo, Submodul `External/Index-Physics`, Package-Registry, Logo.

**Fertig, wenn:** kein `FNK_`/`FUNKE_`/`Funke-` mehr außerhalb von `External/` vorkommt (bis auf bewusste Ausnahmen), alles baut, alle Tests grün sind und ein neu angelegtes Projekt im Editor und als gebautes Spiel läuft.

## 15. Performance und Stabilität unter Last (gesamte Engine)

**Ziel:** Spiele laufen gleichmäßig (keine Ruckler), über lange Sitzungen ohne wachsenden Speicher und auch auf schwachen Handys flüssig; Editor bleibt bei großen Szenen bedienbar. Erst messen, dann optimieren — jede Optimierung mit Vorher/Nachher-Zahlen.

**1. Messbasis schaffen (zuerst):**
- Benchmark-Szenen im Repo: 2D (10k Sprites, viel Text/UI, viele Partikel), 3D (viele Meshes, Schatten, Lichter, Jolt mit vielen Bodies), Script-lastig (1k `EntityScript`s mit `OnUpdate`).
- Headless-Benchmark-Modus im Runtime (`--benchmark <szene> --frames N`), Ausgabe als JSON: Mittelwert, P95/P99, schlimmster Frame, Allokationen, Speicher am Anfang/Ende, Draw Calls.
- Messung auf Referenzgeräten: Desktop (Release + Dist), ein schwaches Android-Gerät (Mali), optional iOS.
- Budget pro Bereich festlegen (z. B. 60 FPS auf Referenz-Handy, P99 < 20 ms) und in CI bzw. per Skript gegen gespeicherte Werte vergleichen (Warnung ab z. B. +10 %).

**2. CPU pro Frame:**
- Profiler-Abdeckung prüfen (`FUNKE_PROFILE_*`, Tracy): jede Systemstufe, Scripts, Physik, Render-Vorbereitung, UI, Audio als eigene Zone.
- C#-Interop: Anzahl Native↔Managed-Übergänge pro Frame senken (Batch-Calls statt pro Entity, `OnUpdate` nur für Scripts, die es überschreiben).
- Managed-Allokationen pro Frame auf ~0 bringen (keine LINQ/Closures/Boxing in Engine-API-Pfaden), GC-Pausen messen.
- ECS: Hot-Path-Queries auf Cache-Freundlichkeit prüfen, unnötige Kopien/`std::string`/`std::function` im Frame entfernen.
- Arbeit auf den `JobSystem` verteilen, wo unabhängig (Culling, Partikel-Simulation, Animation-Sampling, Transform-Hierarchie).

**3. Rendering:**
- Draw Calls/Batching (2D und 3D, Instancing), Pipeline- und Bind-Group-Caching, keine Neuanlage pro Frame.
- Buffer-Uploads bündeln (Ring-Buffer statt vieler kleiner `WriteBuffer`), Text-/UI-Mesh nur bei Änderung neu bauen.
- Culling (Frustum, optional Distanz), Schatten-Auflösung/Kaskaden nach Qualitätsstufe.
- Qualitätsstufen (Low/Medium/High) für Mobile: Render-Scale, Schatten, Post-Processing, MSAA; automatische Wahl beim ersten Start.
- Frame Pacing auf Mobile (Present-Modus, konstante 30/60 statt Schwanken), Thermik über längere Läufe beobachten.

**4. Ruckler (Hitches) vermeiden:**
- Asset-Laden asynchron (Texturen, Meshes, Audio-Decoding) mit Upload-Budget pro Frame; Szenenwechsel mit Ladebildschirm statt Freeze.
- Shader/Pipelines vorwärmen (beim Laden der Szene bzw. Pipeline-Cache auf Platte), damit das erste Auftauchen eines Materials nicht ruckelt.
- Erstes Instanziieren großer Prefabs, erstes Abspielen von Sounds und JIT-Kosten der C#-Scripts messen (ggf. ReadyToRun/Tiered-Compilation-Einstellungen).

**5. Speicher und Langzeitstabilität:**
- Soak-Test: Szene 30–60 min laufen lassen bzw. Szenen in Schleife wechseln, Speicher (nativ, managed, GPU) darf nicht wachsen. Gilt besonders für Android (vgl. gefundenes Mali-Leck bei Dynamic Rendering).
- Leak-Prüfung beim Szenenwechsel und Hot Reload (Texturen, Buffer, Physik-Bodies, Script-Instanzen, `AssemblyLoadContext`).
- Memory-Tracking-Ansicht im Profiler: Textur-/Mesh-/Audio-Speicher pro Asset.
- Verhalten unter Druck: `Device lost`, wenig Speicher (Android `onTrimMemory`), App im Hintergrund/zurück — sauber erholen statt Absturz oder schwarzer Bildschirm.

**6. Editor-Performance:**
- Große Szenen (10k+ Entities): Hierarchy virtualisiert, Inspector nur sichtbare Felder, Gizmos/Picking nicht O(n) pro Frame.
- Asset-Browser und Import im Hintergrund, Thumbnails gecacht.
- Play/Stop und Hot Reload messen und beschleunigen (Szenen-Kopie, Script-Recompile).

**7. Absicherung gegen Rückschritte:**
- Benchmarks aus Punkt 1 in `scripts/dev` (z. B. `Bench.ps1`) mit Vergleich gegen letzte Baseline.
- Doctests für kritische Datenstrukturen bleiben, zusätzlich Mikro-Benchmarks (doctest oder eigenes Mini-Harness) für Hot Paths.
- Kurze Doku-Seite „Performance“ für Spielentwickler: Profiler benutzen, Budget, typische Fallen (Allokationen in `OnUpdate`, zu viele Lichter, große Texturen auf Mobile).

**Fertig, wenn:** für jede Benchmark-Szene eine Baseline existiert und die vereinbarten Budgets auf den Referenzgeräten eingehalten werden, der Soak-Test ohne Speicherwachstum und ohne Absturz durchläuft, beim Szenenwechsel und ersten Erscheinen neuer Inhalte keine Frames über dem Budget liegen und Rückschritte per Skript auffallen. Ergebnisse (vorher/nachher) in `Planning/` dokumentieren, user-sichtbare Änderungen (Qualitätsstufen, Benchmark-Modus) in `CHANGELOG.md` und Doku.

## 16. Iterative (inkrementelle) Game-Builds

**Ist-Zustand (geprüft 14.10.2026):** Nur teilweise inkrementell.
- **Schon inkrementell:**
  - Kompilieren über die Toolchains selbst: `dotnet build` für `Funke-ScriptCore` und die User-Scripts, MSBuild für `Funke-Runtime` bzw. die Runtime-Variante und für Paket-Projekte (`GameBuilder.cpp`).
  - Android/Web: Der native Player liegt pro Engine-Stand, Konfiguration und Modulen im `PlatformCache` (`player-<engine>-<config>-<modules>`). Dazu kommen gecachte Java-Klassen (Hash über die Quellen), Mono-AOT-Images pro Assembly-Hash und das Emscripten-/Dawn-Cache (`platforms/*/Builder/build.py`, `mono_aot.py`).
- **Nicht inkrementell:**
  - Jeder Build schreibt in ein **neues, leeres Staging-Verzeichnis** (`BuildStagingDirectory::Prepare`, `MakeUniqueSibling("staging")`) und tauscht es danach gegen die alte Ausgabe. `CopyDirIncremental*` vergleicht deshalb immer gegen nichts. Alle Assets, `FunkeAssets`, Runtime-Binaries, Lizenzen und Pakete werden bei jedem Build komplett neu kopiert, die alte Ausgabe wird gelöscht.
  - Asset-Pruning (`BuildAssetPruning::ComputeShippedAssets`) und Fingerprint laufen jedes Mal komplett.
  - App-Icon wird jedes Mal neu dekodiert und skaliert.
  - Bei Plattform-Paketen (APK/Web) wird das Paket immer komplett neu gepackt und auf dem Gerät komplett neu installiert.
  - Es gibt keine Zeitmessung pro Build-Schritt, also keine Zahlen, wo die Zeit hingeht.

**Vorschlag:**
1. **Zeit pro Schritt loggen** (Snapshot, Pruning, C#, Runtime, Kopieren, Packen, Installieren) und am Ende eine Zusammenfassung ausgeben. Das ist die Grundlage für alles Weitere.
2. **Build-Manifest in der Ausgabe** (`.funke-build/manifest.json`): pro Ausgabedatei Quelle, Größe, Zeitstempel bzw. Hash, dazu Engine-Version, Konfiguration, Profil, Defines und Module. Ändert sich einer der globalen Schlüssel, folgt ein vollständiger Build.
3. **Staging gegen die vorherige Ausgabe:**
   - Unveränderte Dateien nicht neu kopieren, sondern per Hardlink bzw. Rename aus der alten Ausgabe übernehmen. Nur Geändertes kopieren, Entferntes weglassen.
   - Der atomare Tausch und die Sicherheitsprüfungen (`BuildOutputSafety`) bleiben erhalten.
   - Alternative: direkt in die Ausgabe schreiben mit Manifest-Abgleich, alte Dateien aufräumen, Ausgabe bei Abbruch als „unvollständig“ markieren.
4. **Teure Schritte cachen:** Pruning-Plan, Icon-Varianten und spätere Asset-Verarbeitung (Textur-Kompression, Mesh-Konvertierung, Shader-Vorkompilierung aus Punkt 12) mit Schlüssel über den Inhalt in `<project>/.funke/build-cache/`.
5. **Schnelle Teil-Builds:**
   - „Nur Scripts“ bzw. „nur Content“, wenn sich Runtime und Engine nicht geändert haben (Erkennung über das Manifest).
   - Im Editor als Option „Build & Run (schnell)“ und in der CLI als `funke build --incremental` (Standard) bzw. `--clean`.
6. **Mobile-Deploy:**
   - Android: unverändertes APK nicht neu installieren. Bei reinen Content-/Script-Änderungen optional nur geänderte Dateien per `adb push` in einen Dev-Content-Ordner schieben, den der Development-Runtime bevorzugt lädt (nur in Dev/Test-Builds, vgl. Punkt 10).
   - Web: nur geänderte Dateien in den Ausgabeordner schreiben.
7. **Clean Build:** Button im Build-Panel bzw. `--clean` löscht Manifest und Cache. Bei Fehlern im Manifest automatisch auf einen vollständigen Build zurückfallen.

**Fertig, wenn:**
- ein zweiter Build ohne Änderungen nur noch einen Bruchteil der Zeit braucht (Zielwert nach der Messung aus Schritt 1 festlegen),
- nach einer geänderten Textur bzw. einem geänderten Script nur diese Datei bzw. die User-Assembly neu in der Ausgabe landet,
- gelöschte Assets aus der Ausgabe verschwinden,
- ein Wechsel von Konfiguration oder Profil einen vollständigen Build auslöst,
- inkrementelle und Clean-Ausgabe byte-gleich sind (Doctest bzw. Skript-Vergleich).

Doku im Build-Kapitel, `CHANGELOG.md`.

---

# Sammlung 15.10.2026 (Editor-Feedback)

Sofort erledigt (15.10.2026), hier nur zur Einordnung:
- **F-Fokus ohne Hover:** F fokussiert im 3D-Modus jetzt auch, wenn Hierarchy/Inspector fokussiert sind (`FocusSelectedEntity` → `FocusSelectedEntity3D`, vorher nur bei Maus über der Editor View).
- **Durchklicken in 3D:** wiederholter Klick auf dieselbe Stelle wählt die nächste Entity dahinter (`EntityPicker3D::PickAll`, gleiche Logik wie in 2D).
- **Inspector-Icons:** nicht mehr blau (Highlight-Tönung) eingefärbt, sondern in Textfarbe (`BeginComponentSection`).
- **Game View Aspect „Project (…)“:** Option im Dropdown, die das Aspect Ratio aus den Projekteinstellungen (Build) übernimmt.
- **Skills:** `funke-make-a-game` / `funke-make-a-mobile-app` um „Entities ohne unnötigen Name/Transform“ und „eigene C#-Komponenten (Struct und Klasse)“ ergänzt.

## 17. Play Mode sofort verlassen

**Erledigt (15.10.2026):** Stop 300 → ~135 ms (Level mit ~200 Entities): Die Play-Szene wird ohne Probe- und Rücklade-Deserialisierung abgebaut (`SceneManager::SuspendLoadedSceneForRelease`), die Edit-Szene ohne zweite Validierung aktiviert. Log: `[PlayMode] Stopped in … ms`.

**Problem:** Stop im Play Mode dauert spürbar, statt sofort zurückzukehren.

**Vorschlag:**
- Stop-Pfad mit `FUNKE_PROFILE_SCOPE` zerlegen (Script-`OnDestroy`/`OnDisable`, Freigabe der Play-Szene, Physik-Welten 2D/3D, Audio, Wiederherstellen der Edit-Szene, `AssemblyLoadContext`, GC) und messen.
- Edit-Szene nicht neu laden/deserialisieren, sondern die vor Play gehaltene Kopie direkt wieder aktivieren; Play-Szene im Hintergrund bzw. über mehrere Frames abbauen, wo sicher.
- Keine vollständige GC/Unload pro Stop erzwingen, wenn kein Hot Reload ansteht.

**Fertig, wenn:** Stop bei MarbleSky und einer großen Testszene in < 100 ms zurück im Edit-Modus ist (Messwert im Log), ohne Leaks über 20 Play/Stop-Zyklen.

## 18. Absturz nach Entfernen des 2D-Moduls

**Erledigt (15.10.2026):** Ursache war ein thread-lokaler `ComponentInfo*`-Cache in `ScriptBindings.cpp`, der nur über `registry.GetGeneration()` invalidiert wurde. Die nach dem Reload neue Registry konnte denselben Zähler erreichen → Aufruf in freigegebenen Speicher (CFG-Fast-Fail, daher kein Dump). Jede Registry zählt jetzt in einem eigenen Bereich. UI-only (ohne beide Module) läuft. Test-Hook: `Run.ps1 -ReloadAfter 3 -RemoveModule Funke.2D`.

**Problem:** In einem 2D-Projekt das 2D-Modul entfernen und neu laden → Editor stürzt ab. Projekte ohne 2D und ohne 3D (reine UI-Apps) müssen aber möglich sein.

**Vorschlag:**
- Absturz reproduzieren (Crash-Dump in `%LOCALAPPDATA%\Funke\CrashDumps\`), Ursache beheben: vermutlich Szenen mit `Transform2D`/`SpriteRenderer`/`Camera2D`, deren Komponenten nicht mehr registriert sind, oder Editor-Code, der Renderer2D/`Camera2DComponent::GetMain()` ohne Modul-Prüfung nutzt.
- Unbekannte Komponenten beim Laden behalten (als „fehlendes Modul“-Platzhalter im Inspector mit Hinweis), statt zu verwerfen oder abzustürzen.
- UI-only-Modus prüfen: UI (`RectTransform`, `Image`, Text) muss ohne 2D- und 3D-Modul rendern; Editor View dann im UI-Modus.

**Fertig, wenn:** 2D-Projekt ohne 2D-Modul lädt mit Warnung statt Absturz, eine UI-only-App ohne beide Module im Editor und im Build läuft, Doctest für das Laden einer Szene mit Komponenten eines deaktivierten Moduls.

## 19. Einheitlicher „Neustart nötig“-Dialog

**Erledigt (15.10.2026):** `Editor/EditorRestart` plus Popup „Reload the editor?“ im Hauptfenster und „Reload pending“ in der Statusleiste. Module, Paketliste, Render-Backend (auch über Build-Profil oder project.json) und Editor-Schrift laufen darüber. Package-Manager-Banner und Settings-Restart-Dialog sind entfernt.

**Problem:** Änderungen, die einen Neustart brauchen (z. B. 2D-Modul im 3D-Projekt hinzufügen), zeigen den Hinweis im Package-Manager-Fenster. Heute gibt es mehrere eigene Dialoge (`EditorPreferencesPanel.cpp` „Restart Editor?“ mit Restart Now/Later, `ImGuiEditorLayerPanels.cpp` „Restart Editor“ mit Restart Now/Save For Later/Cancel).

**Vorschlag:**
- Ein zentraler Dienst `EditorRestart::Request(reason)`: zeigt ein eigenes modales Popup im Hauptfenster („Engine neu laden?“ – „Jetzt neu laden“ / „Später“), sammelt mehrere Gründe, „Später“ zeigt einen dauerhaften Hinweis in der Statusleiste mit Button zum Neustarten.
- Alle Stellen (Module, Pakete, Render-Backend, IDE-Wechsel, Preferences …) darauf umstellen; keine Neustart-Popups mehr innerhalb einzelner Panels.
- Vor dem Neustart ungespeicherte Szenen abfragen.

**Fertig, wenn:** jede neustartpflichtige Änderung denselben Dialog auslöst, „Später“ einen sichtbaren Hinweis hinterlässt und der Neustart Projekt, Szene und Layout wiederherstellt.

## 20. 2D-Entities in der 3D-Ansicht auswählbar

**Erledigt (15.10.2026):** Picking auf der Ebene z = 0 (Sprites/UI vor den Meshes im Durchklick-Stapel), Auswahlrahmen und F-Fokus für 2D-Entities in 3D.

**Problem:** Im 3D-Modus der Editor View lassen sich Entities mit `Transform2D` oder `RectTransform2D` nicht anklicken; `EntityPicker3D` betrachtet nur `Transform3D`.

**Vorschlag:**
- Picking um 2D-Entities erweitern: `Transform2D`-Quads (Sprites, Text, Collider) als dünne Box in der Z=0-Ebene mit ihrer Weltmatrix; UI (`RectTransform`) im Screen-Space-Overlay per 2D-Picking zuerst prüfen (UI gewinnt, wie in 2D).
- In den Durchklick-Stapel (`PickAll`) mit aufnehmen, Auswahlrahmen und Gizmo für 2D-Entities auch in 3D zeichnen, F-Fokus für 2D-Entities im 3D-Modus.

**Fertig, wenn:** Sprite, Text und UI-Button in einer gemischten Szene im 3D-Modus per Klick auswählbar und per F fokussierbar sind, Doctest für den Ray-Test gegen eine 2D-Entity.

## 21. Skybox über ein Material

**Erledigt (15.10.2026):** `Camera3D.SkyboxMaterial`, `@target Skybox`, Vorlagen Gradient / 6-Sided / Panoramic unter Create → Skybox Shader, im Runtime geprüft. C#-Property `Camera3D.SkyboxMaterial` nachgezogen (Binding im 3D-Modul, Interop-Layout 18, Doctest).

**Problem:** Die Skybox der `Camera3D` wird über einzelne Farben (`SkyTop`/`SkyHorizon`/`SkyGround` + Sonne) eingestellt. Gewünscht: Skybox als Material, das man zuweisen und wiederverwenden kann (ergänzt Punkt 4 und Punkt 12).

**Vorschlag:**
- `Camera3D` bekommt `background = Skybox` mit `skyboxMaterial` (GUID). Der bisherige Farbverlauf wird zu einem eingebauten Skybox-Shader „Gradient Sky“ mit genau diesen Parametern (Migration: alte Felder → automatisch erzeugtes Material bzw. Default-Material).
- Eingebaute Skybox-Shader: „Gradient Sky“, „6-Sided“ (je Würfelseite Textur oder Farbe: +X/−X/+Y/−Y/+Z/−Z), „Panoramic“ (equirektangular), später „Cubemap“. Parameter: Tint, Exposure, Rotation.
- Render als Fullscreen-Pass hinter allem (wie in Punkt 4), Ambient optional aus dem Material ableiten.
- Mit Punkt 12 (Custom Shader) können User eigene Skybox-Shader schreiben (Ziel `Skybox`).

**Fertig, wenn:** ein `.material` mit „6-Sided“ (je Seite Farbe oder Textur) als Skybox an der Kamera im Editor und im Build erscheint, alte Projekte mit Farbverlauf unverändert aussehen, Doku + `CHANGELOG.md`.

## 22. Komponenten-Icons

**Erledigt (15.10.2026):** 14 neue Icons (`scripts/assets/generate_component_icons.py`), zugeordnet in Inspector und Hierarchy. Zuordnung jetzt über `ComponentInfo::icon` (`AssignBuiltInIcons`, Pakete per `icon`-Argument bzw. `Package::SetComponentIcon<T>`), Namenssuche nur noch als Fallback; `ComponentIconTests.cpp`.

**Problem:** Viele Komponenten nutzen Platzhalter-Icons (`ComponentIconName` in `ImGuiUtils.cpp`: Audio → „play“, Particle → „light_sun“, Script → „link“, Rigidbody → „circle_dot“, Lichter/3D-Physik ohne eigenes Icon).

**Vorschlag:** Eigene Icons (gleicher Stil, `FunkeAssets/Textures/Editor/<Gruppe>/<name>/<name>_<size>.png` in 16/32/64/128) für:
- Audio (`AudioSource`, `AudioListener`): Musiknote.
- `ParticleSystem2D` / `ParticleSystem3D`: eigenes Partikel-Icon.
- Script-Komponenten (`EntityScript`, eigene C#-Komponenten): eigenes Script-Icon.
- `PointLight`, `SpotLight` (und `DirectionalLight`): je eigenes Icon.
- 3D-Physik: alle `Collider3D`-Arten (Box, Sphere, Capsule, Mesh …) und `Rigidbody3D`.
- 2D-Physik: alle `Collider2D`-Arten und `Rigidbody2D`.
- Zuordnung über Komponenten-Metadaten (`ComponentInfo::icon`) statt Textsuche im Anzeigenamen, damit Pakete eigene Icons angeben können.

**Fertig, wenn:** jede genannte Komponente im Inspector, im Add-Component-Popup und in der Hierarchy ihr eigenes Icon zeigt, in Dark und Light Theme lesbar.

## 23. JetBrains-IDE-Unterstützung (Rider / IntelliJ)

**Erledigt (15.10.2026):** IntelliJ IDEA als Editor-Typ; Erkennung beider IDEs über Program Files, Toolbox 1.x/2.x und Registry; Öffnen mit Projektordner bzw. .sln und `--line`. Doctest mit gefälschten Installationen.

**Ist-Zustand:** `ExternalEditor.cpp` erkennt Rider unter Windows nur in `%LOCALAPPDATA%\JetBrains\Toolbox\apps\Rider\<ver>\bin\rider64.exe` (altes Toolbox-Layout) und `C:\Program Files\JetBrains\JetBrains Rider\bin\rider64.exe` (echte Installationen liegen in versionierten Ordnern wie `JetBrains Rider 2025.2`). Aktuelle Toolbox-Installationen (`%LOCALAPPDATA%\Programs\Rider\`, Skripte unter `%LOCALAPPDATA%\JetBrains\Toolbox\scripts\rider.cmd`) werden nicht gefunden. IntelliJ IDEA selbst kann ohne Rider-Backend kein C#; „IntelliJ-Support“ heißt hier praktisch Rider (gleiche Plattform).

**Vorschlag:**
- Erkennung erweitern: versionierte Ordner unter `Program Files\JetBrains\*Rider*`, Toolbox 2.x (`Programs\Rider`, Toolbox-Skripte, `state.json`), Registry (`HKLM/HKCU\Software\JetBrains\Rider`), `PATH`; mehrere Versionen auflisten, neueste vorschlagen. Optional IntelliJ IDEA erkennen, mit Hinweis „C# braucht Rider“.
- Öffnen: Solution/`.csproj` des Projekts plus Datei und Zeile (`rider64.exe <sln> --line <n> <datei>`), vorhandene Instanz wiederverwenden.
- Projekt-Dateien so erzeugen, dass Rider IntelliSense hat (Referenz auf `Funke-ScriptCore.dll` + XML-Doku, `.sln` im Projekt-Root).
- Doppelklick auf Script, Fehler in der Console und „Open C# Project“ funktionieren mit Rider.

**Fertig, wenn:** Rider (Toolbox und Standalone) automatisch erkannt wird, Doppelklick auf ein Script die Datei an der richtigen Zeile im laufenden Rider öffnet und IntelliSense für die Funke-API funktioniert. Doku im Scripting-Kapitel.

## 24. Audio Mixer (als Paket)

**Erledigt (15.10.2026):** `Funke.AudioMixer` 1.0.0 (C#): Gruppen, Mute, Fades, Snapshots, dB, PlayerPrefs, BindSlider/BindMuteToggle; im Play Mode geprüft. Veröffentlicht in `Ben-Scr/Index-PackageRegistry` (Release `Funke.AudioMixer-1.0.0`, Quelle dort unter `packages/Funke.AudioMixer`; aus dem Engine-Repo entfernt, weil der Loader doppelte Paketnamen ablehnt). Nachtrag: Logik-Tests neu im Registry-Repo unter `tests/Funke.AudioMixer.Tests` (xUnit, 10 Tests, kompiliert `AudioMixerGraph.cs` direkt). Offen: Mixer-Fenster im Editor – C#-Pakete können heute keine Editor-Fenster beisteuern; braucht entweder eine C#-Editor-Fenster-API oder ein natives Fenster, das den Mixer-Zustand über eine Schnittstelle abfragt.

**Problem:** Es gibt keine zentrale Lautstärkeverwaltung. Jedes Spiel baut sich Master/Musik/SFX-Regler selbst.

**Vorschlag (als herunterladbares Paket, z. B. `Funke.AudioMixer`):**
- Asset `.mixer` mit Gruppen-Baum: `Master` → `Music`, `SFX` (frei erweiterbar, z. B. `UI`, `Voice`), je Gruppe Lautstärke (dB bzw. linear), Mute, Solo, optional Pitch.
- `AudioSource` bekommt ein Feld „Output“ (Mixer-Gruppe); `Audio.PlayOneShot` mit optionaler Gruppe.
- Mixer-Fenster im Editor mit Pegeln und Reglern, live im Play Mode.
- C#-API: `AudioMixer.SetVolume("Music", 0.5f)`, `GetVolume`, `SetMute`, Snapshots (`AudioMixer.TransitionTo("Paused", 0.5f)`), Speichern/Laden der Benutzer-Einstellungen über `PlayerPrefs` mit einem Aufruf (`AudioMixer.SaveVolumes()` / `LoadVolumes()`).
- Fertige UI-Vorlage: Settings-Prefab mit Slidern für Master/Musik/SFX.
- Engine-seitig nur, was nötig ist (Gruppen-Lautstärke im Audio-Backend); Rest im Paket.

**Fertig, wenn:** das Paket über den Package Manager installierbar ist, Musik und SFX getrennt regelbar sind (Editor + Build + Android), Einstellungen einen Neustart überleben, Doku + Beispiel.

## 25. Eigene C#-Struct-Komponenten: Inspector-Werte und Doku (beim Skill-Update gefunden)

**Erledigt (15.10.2026):** Feld-Offsets aus C# (`StructFieldLayout`); der Inspector liest und schreibt die Komponenten-Bytes (`Scripting/StructFieldBytes`). Veraltete `[NativeComponent]`-Verweise entfernt, Doku „Your own components“ ergänzt, Skill-Hinweis entfernt.

**Problem (aus Code-Lektüre, nicht im Editor nachgestellt):**
- Für `IComponent`-Structs zeigt der Inspector die Felder (Default-Instanz), Änderungen landen aber nur in `ScriptComponent::PendingFieldValues` → `ScriptFields` (`ScriptComponentInspector.cpp`). Es wurde keine Stelle gefunden, die diese Werte in die Struct-Bytes der Komponente schreibt; im Editor gesetzte Werte wirken zur Laufzeit vermutlich nicht.
- Kommentare in `NativeCallbacks.cs`/`InternalCalls.cs` und eine Fehlermeldung in `IJobQuery.cs` nennen ein Attribut `[NativeComponent(..., Generate=true)]`, das es nicht gibt.
- Keine Tests für die dynamische Registrierung (`DynamicComponentRegistrar`) und keine Doku-Seite zu eigenen C#-Komponenten (Struct und Klasse).

**Vorschlag:** Im Editor nachstellen; Inspector-Änderungen direkt in die Struct-Bytes schreiben (Edit- und Play-Mode, Undo) bzw. beim Laden aus `ScriptFields` übernehmen. Veraltete `NativeComponent`-Verweise entfernen. xUnit/Doctest für Registrierung, Serialisierung (Feld anhängen = Daten bleiben) und Hot Reload. Doku-Seite „Eigene Komponenten in C#“.

**Fertig, wenn:** ein im Inspector gesetzter Struct-Wert nach Speichern, Neuladen und Play stimmt, Tests grün, Doku vorhanden; danach den Hinweis in den Skills `funke-make-a-game`/`funke-make-a-mobile-app` entfernen.

---

## 26. Laufzeit-Spawning in 3D (beim Level-Builder gefunden, 15.10.)

- `Entity.Create(name)` gibt in Projekten mit 2D- und 3D-Modul eine **Transform2D**; für 3D muss man sie entfernen und Transform3D hinzufügen. Gebraucht: `Entity.Create3D(name, position, rotation, parent)` bzw. ein Parameter.
- `Entity.Instantiate(source, position, ...)`-Overloads setzen nur Transform2D; für 3D-Prefabs fehlen `Instantiate(prefab, Vector3 position, Vector3 rotation, parent)`.
- Direkt nach `Instantiate` liefert `GetComponent<MyScript>()` null (Instanzen entstehen erst im nächsten Script-Pass), und `AddComponent<T>()` ist ein No-Op, wenn das Prefab das Script schon hat. Der Builder fügt Scripts deshalb erst zur Laufzeit hinzu. Gebraucht: Script-Instanzen sofort beim Instantiate anlegen (vor OnAwake die Feldwerte setzen lassen), z. B. `Instantiate(prefab, init: e => ...)`.
- `PrefabTemplateCache` loggt Warnungen für normale Inhalte (deaktivierte Kinder, ParticleSystem3D, Scripts) – das sind erwartete Slow-Path-Fälle und sollten höchstens Info sein.
- ~~Kein `Camera3D.ScreenPointToRay` / Welt-Picking für 3D~~ — erledigt 15.10.: `Camera3D.ScreenPointToRay`, `WorldToScreenPoint`, `Physics3D.Raycast(Ray)`, `Physics3D.RaycastFromScreen`; der Builder wählt Teile per Antippen.
- Keine Vibration/Haptik-API (`Handheld.Vibrate`) für Mobile.

## 27. `SetEnabled(true)` lässt ausgeschaltete Kinder aus (beim UI-Redesign gefunden, 15.10.)

- `Entity::SetEnabled(true)` (`Funke-Engine/src/Scene/Entity.cpp`, ca. Z. 166–187) überspringt ein im Editor ausgeschaltetes Kind, wenn dessen Elternobjekt in dem Moment noch aus ist; der Kommentar am C#-Binding verspricht anderes. Folge: Panels mit vorab ausgeschalteten Unterelementen zeigen nach dem Einschalten Teile nicht an.
- Workaround im Spiel: Menü-Skripte aktualisieren sich beim ersten OnUpdate noch einmal; LevelCompletePanel schaltet erst das Panel, dann die Kinder ein.
- Gebraucht: klare, dokumentierte Semantik (activeSelf vs. activeInHierarchy wie in Unity) und ein Test dazu.
- Laufzeit-Prozesse mit `--quit-at` beenden sich nicht, wenn ein Skript die Frames extrem verlangsamt (hängender Endlos-Testlauf, 15.10.); ein Watchdog auf Wanduhrzeit (`--quit-after`) für Agenten-Läufe wäre hilfreich.

## 28. Editor überschreibt `project.json` beim Beenden (beim Android-Build gefunden, 16.10.)

- Der Editor hielt MarbleSky offen, während `Tools/build_game.py` die `project.json` neu schrieb (30 neue Build-Szenen, Version 2.0.0, App-Icon). Beim Schließen (00:12:28) schrieb er seinen alten Stand aus dem Speicher zurück: Build-Szenen Level1–Level25, die es nicht mehr gibt, Version 1.0.0, kein App-Icon. Ein Build danach hätte nur Menü, Builder und Endlos enthalten.
- Gebraucht: Der FileWatcher lädt eine von außen geänderte `project.json` neu (oder fragt nach). Gespeichert wird nur, was im Editor wirklich geändert wurde, nicht beim Beenden der ganze alte Stand.

## 29. 9-Slice-Ränder folgen nicht der UI-Skalierung (beim Level-Editor-Redesign gefunden, 16.10.)

- `GuiRenderer` rechnet den Rand eines geslicten Images (`Border*` × `BorderScale`) in Bildschirmpixeln, nicht in UI-Einheiten. In einem kleinen Fenster (480 px hoch, UI-Skalierung 0,4) wirken alle geslicten Sprites etwa 2,5-mal runder und dicker als entworfen: Kacheln, Badges, Panels, Pills. Auf dem Pixel 9a (Skalierung ≈ 0,9) fällt es kaum auf.
- Gebraucht: Ränder mit der UI-Skalierung des Canvas multiplizieren, so wie Positionen und Größen. Dazu ein Test mit zwei Fenstergrößen.

## Offen im Spiel selbst

- Die Slider-Lösung mit `Mask` ist ungetestet (auf Wunsch). Prüfen, ob die Füllung mitwandert. Mit 9-Slice (Punkt 1) wird sie überflüssig.
- Level 5–8 einmal komplett durchspielen: Schaffbarkeit, Sprungpad-Landungen, Bumper-Gassen.
