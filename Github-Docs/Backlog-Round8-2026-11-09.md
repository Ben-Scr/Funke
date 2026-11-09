# Backlog-Runde 8 – 9. November 2026

**Zehn Aufgaben abgeschlossen:** EDT-014, GFX-033, GFX-045, WLD-004, INP-006,
EDT-015, EDT-023, EDT-026, GFX-004 und INP-004. Der offene Backlog sinkt von
139 auf 129 Aufgaben; die elf bereits teilweise erledigten Aufgaben bleiben offen.

Ausgewählt wurden die schnellsten unabhängig lokal abnehmbaren Arbeiten.
Kürzere Auditpositionen benötigen unter anderem echte Linux-/macOS-Läufe,
Remote-CI, eine installierte Standaloneumgebung, Releasevoraussetzungen oder
längere Lastmessungen. Diese Voraussetzungen wurden nicht durch Mocks ersetzt.
Vorhandene Änderungen wurden geprüft und weiterverwendet. Kein Branch, Commit,
Push oder Paketrelease wurde erstellt. Input-Änderungen liegen im benachbarten
Repository `Index-PackageRegistry`, nicht in einem veröffentlichten ZIP.

## Ergebnisse je Aufgabe

| Aufgabe | Ergebnis und Ursache | Nachweis / Grenze |
|---|---|---|
| EDT-014 | Die vorhandene Vorabprüfung von ScriptCore-Identität und Feldlayout verhindert einen scheinbar erfolgreichen inkompatiblen Hot Reload. Neu: Export der tatsächlich kompilierten nativen Feldverträge für den direkten C#-Vergleich. | Native Transform2D-/SpriteRenderer-Verträge passen nach dem inkrementellen Neubau zu den C#-Spiegeln. Geänderte Offsets bei unveränderter Größe werden abgelehnt. Kein pauschaler Größenbeweis für alle Komponenten. |
| GFX-033 | Vorhandene Modi `EulerMultiTurn`, `ShortestAngle` und `Quaternion` einschließlich Metadaten, Dateiformat und kompatiblem Euler-Default abgenommen. | Native Animationsfälle für 350→10, −179→179, Mehrfachdrehung, gemischte Achsen und Skeletal-Crossfades bestanden. Editor-Prüfprojekt zeigt alle drei Varianten im Play-Modus. |
| GFX-045 | Vorhandene Slot-/Callback-Lebensdauerkorrektur und Diagnose für fehlende GPU-Werte abgenommen. | Vier echte Dawn-Geräteläufe: fehlendes Timestamp-Feature, fehlgeschlagenes Mapping, Shutdown mit ausstehendem Callback, injizierter Device-Loss. Kein physischer Treiberreset; Windows/D3D12-Abnahme. |
| WLD-004 | Float-Reichweiten, gerenderte Relativbewegung und Physikkontakte vermessen; Koordinatenvertrag und atomarer Origin-Shift geplant. | Sechs Physikabstände und zwölf GPU-Bilder. Ab 100.000 m verschwindet ein direkt addierter Millimeter im Float. WLD-005 bleibt die Implementierung. |
| INP-006 | Tastatur-/Maus-Chords mit physischen Modifiern, Konfliktprüfung, JSON-Profilen und expliziter Rebinding-Aufnahme ergänzt. | Beide Druckreihenfolgen, Loslassen, mehrere Modifier, alte IDs/Profile, ungültige Kombinationen und gehaltene Starttasten geprüft. Keine beliebigen verschachtelten Chords oder Gamepad-Chords. |
| EDT-015 | Bestehende Undo-/Recovery-Absicherung kartiert; fehlenden Zwischenzustand einer nur halb wiederhergestellten Linked-Undo-Verbindung ergänzt. | Undo bleibt diagnostiziert blockiert, Inhalte/Dirty-Flags/Historien bleiben erhalten; nach Wiederherstellung des Partners funktionieren Undo und Redo. Keine zusätzliche Undo-Implementierung nötig. |
| EDT-023 | Async-Ownership und Reload-Übergänge geprüft; native Neustartzyklen und tatsächlichen Managed-Assembly-Unload mit verspäteter Completion ergänzt. | Alte IDs/Callbacks können neue Requests nicht abschließen oder abbrechen. Geteilte Vorbereitung und observerbezogener Abbruch bleiben erhalten. Kein zweites Ladesystem. |
| EDT-026 | Mausfokus nutzte bisher eine eigene Registry-Reihenfolge. Er verwendet jetzt denselben Picker wie UI-Events; Tastaturfokus folgt der gemeinsamen Hierarchie und überspringt deaktivierte Eltern. | Sichtbarer GPU-Repro plus Masken-, Canvas-, Fokus- und Touchtests. Capture nach Layoutverschiebung löst am alten Ort keinen Klick aus. Kein physischer Mobilgerätetest. |
| GFX-004 | Ein zu großer Sprite-Pass wird in begrenzte Uploadfenster zerlegt, statt vollständig verworfen. Globale Reihenfolge und Material-/Maskenruns bleiben erhalten. | Sieben alphaüberlagerte Sprites sind bei künstlichen Limits von eins und zwei pixelgleich zum ungeteilten Bild; Instanzzählung bleibt sieben. Echte Speichererschöpfung wurde nicht provoziert. |
| INP-004 | Optionaler `InputActionRouter` priorisiert Maps und konsumiert deren physische Controls deterministisch. Ohne Router bleiben Maps unabhängig. | Abfragefolge, Prioritätsgleichstand, Menü/Gameplay, globale Shortcuts, Composites, lokale Spieler und Rebinding geprüft. Ein noch gehaltener Menü-Confirm wird erst nach Neutralstellung wieder für Gameplay freigegeben. |

## Änderungen und Verträge

### Eingabe

In `Index-PackageRegistry/packages/Funke.Input/Source/` sind `InputModifiers.cs`
und `InputActionRouter.cs` neu. `InputBinding`, `InputBindingIdentity`,
`InputBindingConflicts`, `RebindOperation`, `InputActionMap` und `InputAction`
integrieren die Funktionen. Die README dokumentiert Semantik und Nutzung;
`tests/Funke.Input.Tests/ChordRoutingTests.cs` ergänzt 13 gezielte Fälle.

Ein Chord verlangt Hauptcontrol und alle gewählten Links-/Rechts-Modifier;
zusätzliche Modifier verhindern ihn nicht. Leere Modifier erzeugen über die
normale API eine einfache Belegung; die explizite Chord-Factory lehnt sie ab.
Unbekannte Flags, Gamepad-Kombinationen und der Hauptmodifier als sein eigener
Modifier werden abgewiesen. Bestehende anonyme Binding-IDs bleiben unverändert.

Router-Maps teilen dieselbe Inputquelle. Höhere Priorität gewinnt, Gleichstand
folgt Registrierungsreihenfolge. Ein aktives Binding konsumiert alle Teile seines
Composites/Chords; untergeordnete Maps überspringen das gesamte überlappende
Binding. Aktionen innerhalb einer Map bleiben unabhängig. Globale Shortcuts
können oberhalb mit `consume: false` stehen. Gamepad-Controls sind an den
Spielerslot gebunden. Dispose/Remove lösen die Routerzugehörigkeit wieder auf.

### Rendering und UI

`Funke-Engine/src/Graphics/Renderer2D.cpp` begrenzt die Uploadkapazität auf
`maxBufferSize / sizeof(SpriteInstance)` und den darstellbaren Draw-Indexbereich.
Fenster rechnen globale Instanzindices in lokale Bufferindices um. Vor dem
Überschreiben noch nicht eingereichter Draws werden die Befehle eingereicht.
Die vorhandenen Fehlerpfade bleiben erhalten. Zusätzliche Fenster erzeugen
zusätzliche Draw Calls, keine zusätzlichen Instanzen.

`UIFocusSystem.cpp/.hpp` verwendet `UIEventSystem::PickInteractable` für Pointer
und `UIDrawOrder::Build` für die Navigation. Nicht fokussierbare interaktive
Overlays lassen den Fokus nicht durch; stale Fokusflags werden auch bei
deaktivierten Feldern gelöscht. Kindreihenfolge im Hierarchiebaum bestimmt die
Tabfolge ausdrücklich unabhängig vom Bild-SortOrder. Details stehen im
[UI-Vertrag](Documentation/Engine-Core/UI-Focus-Order.md).

`Rendering3DTests.cpp` enthält die GPU-Fenster-, UI- und Präzisionsrepros;
`UIHitTestTests.cpp` ergänzt Fokus und Capture bei Layoutänderung.
`scripts/ci/gpu_render_test.py` verlangt die neuen Bilder zusätzlich zu den
bisherigen Gates; `test_gpu_render.py` erwartet die erweiterten sechs Stufen
und deren Artefakte. Die [GPU-Dokumentation](Documentation/Editor/GPU-Regression-Testing.md)
beschreibt Injektion und Aussagegrenzen.

### Reload, Async und Undo

`EcbMutationTests.cpp` kann die echten nativen Layoutverträge über
`FUNKE_TEST_COMPONENT_LAYOUT_REPORT` exportieren. Der kleine Abnahme-Probe unter
`artifacts/backlog-round8/layout/` ruft die produktive C#-Validierung damit auf,
einschließlich negativer Offsetmutation. Die bereits vorhandenen Tests prüfen
Größen-, Feldreihenfolge-, Typ- und Assemblyabweichungen. Layout-/Bindingänderungen
benötigen weiterhin einen passenden nativen und Managed-Build mit Codegen sowie
einen Editorneustart; der reine Managed-Hot-Reload deaktiviert Codegen absichtlich.
Die Integritätsprüfungen wurden nicht abgeschwächt.

`AssetLoaderAsyncTests.cpp` prüft vier Shutdown-/Neustartzyklen mit alten und neuen
Operationen. `AssetLoadingTests.cs` entlädt tatsächlich die User-Assembly, prüft
Task-Cancellation/Release und liefert danach einen alten Callback. Der neue Task
bleibt bis zu seiner eigenen Completion offen. Alle nativen Aufrufe bleiben auf
dem Engine-Thread. Das gemeinsame [Lifecycle-Diagramm](Documentation/Assets/Assets.md#asynchronous-loading)
trennt `NotSupportedException`, abgebrochene Tasks und `null` bei Fehlschlag.

`StructureUndoTests.cpp` erweitert die Wiederherstellung beider Linked-Undo-
Szenen um den Zwischenzustand nach nur einer Restore-Operation. Die vorhandenen
Tests decken folgende Übergänge ab:

| Übergang | Bestehender Nachweis |
|---|---|
| Edit → Play → Stop | `SceneUndoHistory: edit branches survive play mutations and restored scene ownership`; Linked-Rebinding in `StructureUndoTests` |
| Szenenwechsel / Schließen / Reload | Unabhängige Branches, keine Übernahme stale Szenenbesitzer, expliziter Historienverlust bei echtem Reload |
| Prefab-Kontext / Szenentransfer | Prefab-/Source-Identität erhalten; Inspector-Geste wird im ursprünglichen Kontext abgeschlossen; Komponenten-/Hierarchiesnapshots bleiben konsistent |
| Fehler während Transfer / Recovery | Werfender Deserialisierer rollt frühere Teiltransfers zurück; fehlgeschlagener Snapshot überschreibt den letzten gültigen nicht |
| Externe Dateiänderung / GUID-Kollision | Spätere Dateien werden nicht überschrieben; Konflikt benennt den blockierten Pfad und erhält Undo-/Redo-Schritt für einen sicheren Retry |

Diese Zustands- und Fault-Injection-Tests ersetzen keinen Nachweis für jeden
interaktiven Prefab-Editorablauf oder Prozessabbruch mitten in beliebigen
Dateisystemoperationen. Der neue konkrete Zwischenzustand ist abgedeckt; größere
Agenten-Transaktionsprüfungen bleiben EDT-038.

### Weltpräzision

`Physics3DTests.cpp` liefert die CSV-Messung. Die
[Präzisionsentscheidung](Documentation/Engine-Core/Coordinate-Precision.md)
benennt Welttransforms, Rendering, Physik, Audio, Navigation, Save und Networking.
Für ein Spiel mit Meter als Einheit und Submillimeteranforderung schlägt sie eine
aktive lokale Zone um ±1.024 m plus globale Chunk-/Double-Koordinaten vor.
Das atomare Shift-Protokoll behandelt auch Geschwindigkeiten, Interpolation,
Historien, verspätete Jobs/Packets und Rollback. Dies ist kein implementiertes
Floating Origin und kein allgemeines Stabilitätsversprechen für beliebige Welten.

## Ausgeführte Abnahme

Belege liegen unter `artifacts/backlog-round8/`; relative Angaben in dieser
Tabelle beziehen sich darauf. Die ursprünglichen Dateien vor den Änderungen
liegen unter `baseline/`. Der Arbeitsbaum enthielt bereits umfangreiche fremde
Änderungen; die Ergebnisse beziehen sich auf den gemeinsam vorliegenden Stand.

| Prüfung | Ergebnis | Beleg |
|---|---|---|
| Release/x64 EngineTests und EditorTests, inkrementell, ohne Regeneration/Clean/Deployment | Erfolgreich; Compilerwarnungen bleiben sichtbar | `build-engine-final.log`, `build-editor-final.log` |
| Animation, Skelett, ECB/Layout, Async, UI/Touch/Canvases, Physik3D, Rendering und GPU-Timer-CPUfall | 154 bestanden, 6.823 Assertions; nicht ausgewählte/GPU-Opt-in-Fälle explizit übersprungen | `engine-final.xml` |
| Undo einschließlich Struktur- und Recoveryfälle | 63 bestanden, 750 Assertions | `editor-final.xml` |
| AssetLoading, InteropHandshake, ScriptAssemblyReload | 53 bestanden, keine übersprungen | `managed/assets-layout-final.trx` |
| Gesamtes Funke.Input-Paket | 313 bestanden, keine übersprungen | `input/input-final.trx` |
| GPU-Harness-Selbsttests | 9 bestanden; erwartete negative Fixture-Ausgaben sind Teil der Tests | `gpu-harness-tests.log` |
| GPU-Renderer | Alle sechs Stufen plus erwartete negative Pixelprobe bestanden | `gpu-final/summary.json`, Rohpixel und PNGs je Stufe |
| GPU-Timer | Alle vier Fehler-/Lifecycle-Szenarien bestanden | `timer/timer-tsteeprr/summary.json` |
| Direkter nativer → Managed-Layoutvergleich nach Neubau | Zwei echte Verträge akzeptiert, zwei gleich große Offsetmutationen abgewiesen | `layout/native.xml`, `layout/managed.log`, exportierte JSON-Verträge |
| Editor mit RotationModesFixture im Play-Modus | Drei Varianten sichtbar; reguläres Beenden mit Exit 0 | `editor-smoke.log`, `editor-profiler.png` (Hauptfenster mit Rotationsszene; kein Profiler-Panelnachweis) |

Die GPU-Messungen liefen auf AMD Radeon RX 7800 XT, D3D12, Treiber
32.0.32015.7029. Geräteverlust wurde über Dawn injiziert. Die Darstellung von
unbekanntem GPU-Speicher als `Unavailable` und die separate Timerdiagnose wurden
in `ProfilerPanel.cpp` nachvollzogen; ein fehlender Wert wird nicht als gemessene
Null ausgegeben. Die Native-Binärdateien wurden zur kollisionsfreien Ausführung
kopiert und mit Hashes in `native-final-binaries.json` festgehalten. Für den später
ergänzten Layout-Export liegt das aktualisierte Testprogramm unter `layout-bin/`.

Zwischenzeitliche Prüfprobleme sind behoben: ein falscher Test erwartete
`CanUndo=true` trotz noch ungebundenem Partner, eine GPU-Zählprüfung verwendete
den falschen Zähler, ein Include fehlte und der Harness erwartete die alte Anzahl
von Bildern. Parallele Builds sperrten zeitweise Objektdateien; der abschließende
inkrementelle Build gelang ohne Beenden fremder Prozesse. Ein erster Editorlauf
mit einer alten Fixture ohne `engineVersion` zählt nicht als erfolgreiche
Abnahme; die korrigierte eigene Kopie wurde anschließend erfolgreich ausgeführt.

Nicht ausgeführt: vollständiger Solution-Neubau, physische Mobileingaben,
Linux-/macOS-/Web-Läufe, Remote-CI und Paketveröffentlichung. Keine dieser
plattformgebundenen Restabnahmen wurde aus dem offenen Backlog entfernt.
