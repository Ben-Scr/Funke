# FunkeBench

Editierbares Benchmark-Werkzeug für Entity-Operationen und 2D-/3D-Rendering. Benötigt .NET 10, eine gebaute Funke-Runtime und die Module Funke.2D und Funke.3D.

## Starten

- `Start FunkeBench.cmd` baut die Skripte in Release und startet die Runtime.
- `Open in Funke Editor.cmd` öffnet das Projekt; anschließend Play starten.
- Die Startskripte erkennen den umgebenden Engine-Checkout. Außerhalb davon `FUNKE_ENGINE_ROOT` setzen oder `Tools/Start.ps1 -EngineRoot <Pfad>` übergeben. Für das ursprüngliche lokale Projekt bleibt der bisherige Checkout-Pfad als Fallback erhalten.
- Die Skripte kompilieren gegen die ScriptCore-DLL der ausgewählten Anwendung. Bei einem manuellen Build lässt sich deren Pfad mit `-p:FunkeScriptCorePath=<DLL>` angeben; ansonsten wird `Packages/Funke-ScriptCore/Funke-ScriptCore.dll` verwendet.

## Bedienung

1. Szenario und 1 bis 100.000 Entities auswählen.
2. Jobs, Animation und VSync einstellen. Operationen verwenden 1 bis 1.000 Samples, Rendering 1 bis 120 Sekunden. Das jeweils andere Eingabefeld beeinflusst den Lauf nicht.
3. **Run benchmark** verwendet Warmup. **Once** misst eine Operation ohne Warmup beziehungsweise eine Sekunde Rendering nach dem Verwerfen des Setup-Frames.
4. **Stop** verwirft unvollständige Samples und lässt den Workload stehen. **Clear workload** entfernt ihn; fertige Ergebnisse bleiben erhalten.
5. **Export CSV** schreibt die letzten maximal 64 Ergebnisse in `Results` unter den Anwendungsdaten. Während einer Messung ist der Export gesperrt, damit Dateizugriffe keine Framezeiten verfälschen. Jeder Export erhält einen eigenen Dateinamen.

Jeder Lauf ersetzt den bisherigen Workload. Änderungen an Auflösung, VSync oder Framelimit brechen eine aktive Messung ab. Große synchrone Entity-Operationen können ihren jeweiligen Frame blockieren.

## Messumfang

| Test | Gemessene Arbeit |
|---|---|
| Create | Bulk-Aufzeichnung von Transform2D-Entities plus ein `PlaybackInto`; Reservierung und nachträgliche Entity-Wrapper liegen außerhalb. Dies misst nicht einzelne Aufrufe von `Entity.Create()`. |
| Destroy | Aufzeichnung der Löschbefehle plus ein `Playback`; Vorbereitung der Entities liegt außerhalb. |
| Move | Auflösen nativer Transform2D-Referenzen auf dem Hauptthread plus Positionsänderungen, seriell oder in verbundenen Jobs. |
| Render 2D | Reale Frameintervalle mit sichtbaren Sprites und optionaler Bewegung; Setup und Warmup liegen außerhalb. |
| Render 3D | Reale Frameintervalle mit beleuchteten Würfeln ohne Schatten; Erstellung erfolgt vorab in Blöcken. Animierte Jobs berechnen Positionen, Transform3D-Eigenschaften werden auf dem Hauptthread gesetzt. |

**Jobs** steuert Aufzeichnung beziehungsweise Bewegung. Bei Create verwenden beide Varianten `CreateEntitiesWith`: seriell einen Batch, mit Jobs Blöcke von 1.024 Entities. Die Jobzeit enthält Scheduling und Warten auf Abschluss. Kleine Batches können deshalb weiterhin seriell schneller sein. Die Workerzahl im CSV bezeichnet den verfügbaren Pool, nicht die tatsächlich beteiligten Threads.

Die native ECB-Ausführung ist davon unabhängig. `bench.playback Automatic`, `Sequential` oder `Parallel` wählt den angeforderten Modus für den nächsten Lauf; Standard ist `Automatic`. Playback wird auf dem Hauptthread aufgerufen, darf aber geeignete native Arbeit an Worker verteilen. `Parallel` erzwingt keine Parallelisierung nicht unterstützter Befehle. Der angeforderte Modus wird für Create/Destroy in `timing_scope` gespeichert. Für einen Vergleich ausschließlich der Aufzeichnung denselben Playback-Modus verwenden.

Zeiten stammen aus `Stopwatch`. Renderwerte enthalten Engine, UI und Präsentation und sind keine isolierten GPU-Zeiten. Median und P95 beziehen sich auf komplette Operationen beziehungsweise Frames; P95 verwendet Nearest Rank. Setup ist eine separat aufsummierte Vorbereitung. CSV-Spalten und Dezimalpunkte bleiben kompatibel. Animation wird nur für Render-Ergebnisse als aktiv ausgewiesen.

Für Vergleiche identische Einstellungen verwenden, mehrfach aufwärmen und serielle/Jobs-Läufe abwechseln. Editor und Runtime separat betrachten. Automatisierte Funktionsprüfungen mit Screenshots sind keine Performance-Referenz. Das vorhandene CSV allein erfüllt noch nicht den vollständigen Metadatenvertrag unter `Github-Docs/Documentation/Editor/Benchmark-Results.md`.

## Entwicklung und Prüfungen

- `Assets/Scripts/Benchmarks`: Workload, Ablauf, Statistik und Export.
- `Assets/Scripts/UI`: Bedienung und Anzeige.
- `Tools/build_ui.py`: erzeugt `Assets/Scenes/Bench.scene` und die Palette. Projekteinstellungen und andere Szenen werden dabei beibehalten; eigene Änderungen an Bench.scene sollten in den Generator übernommen werden.
- `Tests/Statistics`: Statistik, Eingabegrenzen und CSV; native Umgebungsdaten sind durch Stubs ersetzt.
- `Tests/Input/Smoke.json`: alle fünf Szenarien seriell und mit Jobs, Stop, Clear und Export.
- `Tests/Input/Creation.json`: Blockgrenzen 1/1.023/1.024/1.025, drei Playback-Modi und 100.000 Entities.
- `Tests/Input/Interaction.json`: UI-Klicks, Animation und Export.
- `Tools/Verify.ps1`: startet eine isolierte Runtime-Kopie mit eigenem Profil, prüft Fehler und die vollständige Anzahl erwarteter Assertions. `bench.check` validiert außerhalb aktiver Messungen auch eindeutige IDs, Transform-Komponenten und zerstörte Entities.

```powershell
dotnet run --project Tests/Statistics/Statistics.csproj -c Release
python Tests/test_generator.py
./Tools/Verify.ps1
./Tools/Verify.ps1 -InputScript Tests/Input/Creation.json
./Tools/Verify.ps1 -InputScript Tests/Input/Interaction.json
```

Dev-Konsole: `bench.run Render2D 10000 true 5 10`, `bench.once Create 10000 false`, `bench.playback Sequential`, `bench.animate true`, `bench.vsync false`, `bench.state`, `bench.cancel`, `bench.clear`, `bench.export`.
