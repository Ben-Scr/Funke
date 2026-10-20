# Funke Editor-Icons — Designsprache

Verbindliche Vorgabe für alle Icons unter `Funke-Runtime/FunkeAssets/Textures/Editor/` (General, FileIcons).
Referenzbild: `Planning/Editor-Icon-Style-Reference.png` (je Icon 64 px Dark | 16 px Dark | 16 px Light, so wie der Editor sie zeichnet).

## Kurzfassung

Funke-Icons sind **flache Linien-Glyphen**: runde Linienenden, eine Strichstärke, eine Farbe, kein Licht.
Sie gehören zur Editor-Oberfläche (dunkles Graphit, Google Sans, Rundungen 4–6 px, blauer Akzent) und sind keine Illustrationen.
Vorbild sind die ursprünglichen grauen Icons (`plus`, `search`, `pen`, `eye_open`, `grid`, `link` …) und die 2D-Collider-Icons, also eine Linie im Stil von Lucide/Material Symbols Rounded.

**Verboten:** Glanz, Spiegelung, Verlauf, Bevel/3D-Kante, Schlagschatten, Glow, dunkle Außenkontur, perspektivische 3D-Objekte und mehrfarbige Glyphen.
Die bunten, glänzenden Icons der letzten Runde (z. B. `arrow_up`, `delete`, `play`, `stop`, `search`, `settings_gear`, `undo`/`redo`, `copy`, `rotation`, `particles`, `rigidbody_3d`, `info`) passen nicht und werden ersetzt.

## Warum: so zeichnet der Editor die Icons

- `EditorUI::IconButton` und die Toolbar **multiplizieren** das Icon mit einer Tint-Farbe: Dark `Text #EBECEF`, Light `Text #1E2025`, aktiv `Highlight #9DBCF7`.
  Ein farbiges Icon wird im Light-Theme deshalb zu einem schwarzen Klumpen, und Glanzdetails verschwinden (siehe NEIN-Zeile im Referenzbild).
- Play und Stop liegen auf einem **Accent-Button `#5B8DEF`** mit weißem Tint. Ein blaues Play-Icon ist dort unsichtbar.
- Komponenten-Header, Menüs und das Add-Component-Popup zeichnen Icons **ungetintet**. Nur dort dürfen sie eigene Farbe haben.
- Die meisten Icons erscheinen mit 15–16 px (Font × 0.95). Was bei 16 px nicht lesbar ist, ist falsch.

## Raster und Geometrie

| | |
|---|---|
| Canvas | quadratisch, RGBA, transparenter Hintergrund, upright gespeichert (der Editor flippt die UVs selbst) |
| Größen | `<name>_16/32/64/128.png`, optional `_256` (wird nicht geladen) |
| Raster | 24 Einheiten (wie Lucide). Live-Area 20×20 (2 Einheiten Rand); runde/breite Formen (Zahnrad, Auge, Status-Kreise) bis 22×22 |
| Strich | 2 Einheiten = 1/12 der Kantenlänge (16 px → 1,5–2 px, 32 → 3, 64 → 5–6, 128 → 10–11); `round` caps und joins |
| Ecken | Rechtecke mit Radius ≈ 2 Einheiten; keine spitzen Innenwinkel unter 1 px bei 16 px |
| Füllung | nur Kontur. Ausnahmen: die Media-Glyphen `play`, `pause`, `stop`, `step_forward` und `settings_gear` sind gefüllt (wie im Original) |
| Ansicht | frontal und flach. Würfel (`entity`, 3D-Collider) als isometrische **Linienzeichnung**, nicht schattiert |
| 16 px | jede Größe aus Vektoren rendern, nicht aus einem 1024-px-Bild herunterskalieren. Bei 16 px auf das Pixelraster snappen, Details unter 1 px weglassen |

Alle Icons einer Gruppe sollen optisch gleich groß und gleich schwer wirken (Kreise etwas größer als Quadrate).

## Drei Familien

### 1. Glyph: einfarbig, wird getintet (der Normalfall)

Farbe **#BEBEBE** (RGB 190), Form nur über Alpha mit Kantenglättung, keine zweite Graustufe.
Gilt für alles, was ein Button, Tool, Toggle, Navigationselement oder Toast ist, auch wenn es zusätzlich als Komponenten-Icon dient:

`arrow_up/down/left/right` (Chevrons, keine Pfeile mit Schaft) · `aspect_ratio` · `bounding_box` · `caret` (I-Beam) · `circle_dot` · `clear` (×) · `copy` · `delete` · `download` · `entity` · `eye_open/eye_closed` · `file_manager` · `grid` · `launch_standalone` · `link` · `lock_closed/lock_open` · `minus` · `move` · `rotation` · `scale` · `pause/play/stop/step_forward` · `pen` · `pin` · `plus` · `post_processing` · `redo/undo` · `reset` · `save` · `search` · `settings` (Kontur-Zahnrad) · `settings_gear` (gefülltes Zahnrad) · `zoom_in/zoom_out`

Doppelt genutzt (Button **und** Komponenten-Header), deshalb zwingend einfarbig: `move` (Transform), `bounding_box` (RectTransform), `eye_open` (Sprite/Image), `grid` (Tilemap/Layout), `caret` (Text), `post_processing`, `entity`.

### 2. Komponente: Linien-Glyph in **einer** flachen Kategoriefarbe, ungetintet

Gleicher Strich und gleiches Raster wie bei den Glyphen, nur farbig. Erlaubt ist höchstens eine zweite Fläche in derselben Farbe mit Alpha ≈ 0,45 (z. B. Lichtkegel), aber keine zweite Farbe.
Die Palette stammt aus `scripts/assets/generate_component_icons.py`, das genau diesen Stil zeichnet. **Diesen Generator erweitern statt Bilder zu malen.**

| Kategorie | Farbe | Icons |
|---|---|---|
| Physik | Grün `#7CC47F` | `collider_*` (nur Kontur der Form), `rigidbody_2d/3d` (Körper + Bewegungsstriche) |
| Code | Grün `#7CC47F` | `script` |
| Licht | Gelb `#F2CF63` | `light_sun`, `light_point`, `light_spot` |
| Audio | Pink `#E07BB4` | `audio_source` |
| Effekte | Orange `#F0A44B` | `particles` |
| Animation | Lila `#A87CF0` | `animator` |
| Kamera | Blau `#5DA9E9` | `camera` (auch Viewport-Billboard, 24 px) |
| Prefab | Blau `#5DA9E9` / Amber `#F0B000` | `prefab` / `prefab_missing` |

### 3. Fläche + weißes Symbol: Status, Dateien, Ordner (`keepIconColors`, ungetintet)

Eine flache Grundform in einer Farbe mit Symbol in `#F8F8F8`. Das Symbol folgt dem Strich-System (runde Enden).

- **Status** (Kreis, Warnung als abgerundetes Dreieck, Farben exakt wie im Original): `info #3890F0` (i), `success #58C080` (✓), `warning #F0B000` (!), `error #E03830` (×).
- **Ordner** `open_folder`, `folder_zip`: Amber `#F2B33D`, Lasche in derselben Farbe + 25 % Weiß. `folder_zip` bekommt zusätzlich einen weißen Reißverschluss.
- **Dateien** (`FileIcons/`): Blatt mit Eselsohr oben rechts. Blatt in der Kategoriefarbe, Eselsohr in derselben Farbe + 25 % Weiß, Symbol weiß, kein Rand, kein Schatten. Gleiche Kategorie, gleiche Farbe wie bei den Komponenten:

| Icon | Farbe | Symbol |
|---|---|---|
| `file_cs` | Grün `#7CC47F` | `C#` bzw. `</>` |
| `file_shader` | Indigo `#8A86E8` | Raute/Funke |
| `file_anim` | Lila `#A87CF0` | Kurve mit Keyframes |
| `file_audio` | Pink `#E07BB4` | Wellenform |
| `file_prefab` | Blau `#5DA9E9` | Würfel-Kontur |
| `file_scene` | Dunkelblau `#3E80BA` | Klappe/Bühne |
| `file_dataasset` | Türkis `#4CB8A8` | Datenbank-Zylinder |
| `file_json` | Amber `#F2B33D` | `{ }` |
| `file_xml` | Indigo `#8A86E8` | `< >` |
| `file_font` | Rot `#F26B66` | `A` |
| `file_txt`, `file_bin`, `file_fallback` | Grau `#A0A4AD` | Zeilen / `01` / `?` |

**Nicht anfassen:** `PlatformIcons/` (Markenlogos) und `Textures/Default/` (Sprite-Formen). Die Marken-Orangetöne des Funke-Logos (`#FFD166 … #B83250`) sind **nur** für Branding gedacht und gehören in kein Editor-Icon.

## Regeln für Dateien und Namen

- Pfad: `Funke-Runtime/FunkeAssets/Textures/Editor/<Group>/<name>/<name>_<size>.png`. Namen nicht ändern, der Code lädt per String (`EditorIcons::Get("rotation", px)`).
- Neue Icons bekommen denselben Ordneraufbau. Bei jedem neuen Komponenten-Icon zusätzlich `BuiltInComponentRegistration.cpp` (AssignBuiltInIcons) bzw. `ImGuiUtils.cpp` (ComponentIconName) prüfen.
- Quellen reproduzierbar halten: Python-Generator(en) unter `scripts/assets/` (Vorlage `generate_component_icons.py`) oder SVG-Quellen außerhalb von `FunkeAssets` (das Verzeichnis wird in Projekte und Builds kopiert). Keine KI-Bildgenerierung für Glyphen.
- Original-Referenz der grauen Glyphen: `git show c9d43bda:Index-Runtime/IndexAssets/Textures/Editor/General/<name>/<name>_64.png`.

## Abnahme

1. Vorschaublatt rendern wie `Editor-Icon-Style-Reference.png`: Glyphen multipliziert mit `#EBECEF` auf `#2A2C30` **und** mit `#1E2025` auf `#F2F3F5`, Play/Stop weiß auf `#5B8DEF`, alles zusätzlich bei 16 px 1:1.
2. Jedes Icon ist bei 16 px in beiden Themes eindeutig erkennbar, und keine zwei Icons sind verwechselbar (`settings` vs. `settings_gear`, `reset` vs. `redo`, `clear` vs. `delete`).
3. Ein Icon pro Familie neben ein Original-Icon legen: gleiche Strichstärke, gleiche Größe, gleiche Helligkeit.
4. `scripts\dev\Run.ps1` ausführen und Toolbar, Inspector (Komponenten-Header), Asset Browser und Log ansehen. Danach Editor Preferences auf Light stellen und dieselben Stellen erneut prüfen.
