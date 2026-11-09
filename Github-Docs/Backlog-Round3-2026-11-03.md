# Backlog-Runde 3 — 3. November 2026

Vier weitere kleine, unabhängige Backlogpunkte wurden in vier parallelen Chats umgesetzt und zentral auf Windows geprüft. Native Builds und GPU-Läufe liefen nacheinander. Engine-Branch `master` und Paket-Branch `main` blieben bestehen; keine Commits, Pushes oder Veröffentlichungen.

| ID | Ergebnis | Abnahme |
|---|---|---|
| EDT-008 | Tatsächliche ECB-Fähigkeitsmatrix, ausgewählter Pfad und konkrete Fallbackgründe über das bestehende Diagnoseflag; gezielte Lücken bei Modusgrenzen, Returnwerten und Fehlern geschlossen. | 54 native Tests und 21 Managedtests bestanden. Reale Worker-Ausführung und geordnete/inline Fallbacks im Log nachgewiesen. |
| QAL-006 | Verpflichtender GPU-Runner mit Adapterprüfung, vollständigen RGBA-Readbacks, Referenz-/Ist-/Diffbildern, XML-Ausführungsbeleg und gezielter Negativeprobe; in bestehende Linux-CI integriert. | Hardware und WARP lokal bestanden. Linux/Lavapipe-CI ist implementiert, aber noch nicht auf dem Remote-Runner ausgeführt. |
| GFX-005 | Reproduzierbarer Material-Benchmark und dokumentierte Wiederverwendungsempfehlungen mit tatsächlichen Draw Calls, CPU-Submit- und GPU-Pass-Zeiten. | 48 Messläufe / 2.880 gepaarte Samples; Sichtbarkeit, Transparenz, Materialänderung und identische Bilder geprüft. |
| GFX-008 | Stabile Licht-/Occluderauswahl bei gleichen Distanzen; begrenzte Budgets über vorhandene Ambient-Komponente; Inspector- und C#-Zähler für den letzten View. | 8 native Tests, reale Runtime-Szenarien, Inspector-Sichtung sowie Play/Stop und Reload bestanden. |

## Verhalten und Belege

### EDT-008

Die Diagnose unterscheidet `path=ordered` von `path=staged` und bei letzterem `execution=workers|inline`. Gründe umfassen z. B. doppelte Komponenten, Lifecycle-Signale, fehlende Worker und Auto-Schwellen. Scheduler, Schwellen und Interop-ABI bleiben erhalten. Die [ECS-Dokumentation](Documentation/Engine-Core/ECS.md) beschreibt zulässige Operationen und die Grenzen der Fehlerbehandlung: Ausführungsfehler garantieren keinen allgemeinen Rollback.

`*ECB*` mit `FUNKE_ECB_PARALLEL_DIAGNOSTICS=1` und `FUNKE_ECB_SERIAL_KERNELS=0`: **54/54 Tests, 2.850.086/2.850.086 Assertions**. Managed-Filter `EntityCommandBufferTests`: **21 bestanden**. [Native Logdatei](../artifacts/backlog-round3/playback-native.log) belegt echte Worker-Arbeit in Auto/Parallel sowie die Fallbackgründe. [Detailbericht](../artifacts/backlog-round3/playback-report.md).

### QAL-006

Je Adapter wurden vier Fälle mit **46 positiven Assertions** ausgeführt: Preflight, vorhandene Postprocess-Regression, feste 2D-/3D-Tiefenszene und asynchron geladene Textur mit echtem GPU-Readback. Die **sieben positiven Bilder** pro Adapter stimmen bytegenau mit den analytischen Referenzen überein; erlaubt sind maximal 2/255 Kanalabweichung und keine Fehlerpixel. Hardware- und Softwarebilder sind ebenfalls identisch.

Die separate Negativeprobe ersetzt das rote Sprite-Sollbild durch Grün: **1.024 Fehlerpixel**, genau eine erwartete Assertion fehlgeschlagen, nativer Exit 1. Der Runner akzeptiert diesen Erfolg der Fehlererkennung nur bei diesem konkreten Fehler. Fehlende Adapter, übersprungene Fälle und unvollständige Belege lassen das Pflichtgate scheitern.

- Hardware: AMD Radeon RX 7800 XT, Direct3D12/DiscreteGPU, Treiber `32.0.32015.7029`. [Summary](../artifacts/backlog-round3/gpu-hardware/summary.json), [Bildergalerie](../artifacts/backlog-round3/gpu-hardware/index.html).
- Software: Microsoft Basic Render Driver/WARP, Direct3D12/CPU, Treiber `10.0.26100.9711`; Auswahl über WARP-LUID ohne Hardware-Fallback. [Summary](../artifacts/backlog-round3/gpu-software/summary.json), [Bildergalerie](../artifacts/backlog-round3/gpu-software/index.html).

Neun Runner-Regressionen und fünf bestehende CI-Gate-Tests bestanden; Workflow-YAML geprüft. [Workflow und Bildverträge](Documentation/Editor/GPU-Regression-Testing.md). Die konfigurierte Linux/Lavapipe-Lane wurde hier nicht gestartet; WARP ersetzt deren Plattformabnahme nicht.

### GFX-005

10.000 sichtbare Sprites bei 1280×720, Release, RX 7800 XT: 16 Konfigurationen mit je drei Wiederholungen, 20 Warmups und 60 Messungen. Gemeinsame Materialreferenzen liefern dieselben Pixel; getrennte Material-IDs bleiben unabhängig änderbar. Transparenz-/Sortierreihenfolge wurde erhalten. Es wurde keine spekulative Zusammenfassung im Produktionsrenderer eingeführt.

| Fall bei 1.000 angefragten Materialien | Tatsächliche Draw Calls | CPU-Submit Mittelwert | CPU-Submit P95 |
|---|---:|---:|---:|
| Gleiche Zustände, gruppierte IDs | 1.000 | 1,326 ms | 1,764 ms |
| Gemeinsame Materialreferenz | 1 | 0,147 ms | 0,308 ms |
| Gleiche Zustände, abwechselnde IDs | 10.000 | 10,389 ms | 17,201 ms |

GPU-Zeitstempel sind auf diesem Dawn-Pfad auf **65,536 µs** quantisiert; viele kleine Passzeiten erscheinen deshalb als null. GPU-P95 beträgt hier 0,065536 ms für die gruppierten Fälle und 0,393216 ms für 10.000 abwechselnde Draws. Kleine GPU-Mittelwertunterschiede sind unter dieser Auflösung nicht belastbar. CPU-Streuung und Wiederholungen sind vollständig dokumentiert. Die isolierten Messbereiche schließen u. a. Sortierung, Upload, Präsentation und Wartezeiten aus; daraus folgt keine allgemeine FPS-Zusage.

[Dauerhaftes Messprotokoll](Documentation/Engine-Core/Material-Batching-Measurements.md), [vollständige lokale Messdaten](../artifacts/backlog-round3/batching-measured/measurement.md), [Harness](../scripts/benchmarks/material-batching/README.md). Fünf Exporttests bestanden. Die einzige Produktionshilfe neben der Adapterdiagnose ist eine kleine GPU-Timer-Erweiterung: öffentliche Supportabfrage und Verarbeitung ausstehender Map-Callbacks beim Polling.

### GFX-008

Das Paket `Funke.Lighting2D` begrenzt weiterhin auf **32 Lichter / 64 Occluder**. Die Auswahl sortiert nach Distanz, Szenen-ID, persistenter Entity-ID und Handle; Tests prüfen Registry-Umsortierung, Kamerabewegung, additive Szenen und identische Eingaben. Budgets werden beim Laden, im Inspector und über C# begrenzt. Sie liegen auf der vorhandenen Ambient-Komponente; ein gemeinsames Prefab oder eine persistente Settings-Szene liefert ein Projektbudget. Mehrere aktive Settings verwenden das kleinste Budget.

**8/8 native Tests, 5.288/5.288 Assertions**, einschließlich 0/Cap/Cap+1/4.096 Kandidaten, deaktivierter/ungültiger Komponenten, Puffergrenzen, Serialisierung und Bindings. Drei Python-Vertragsprüfungen und Managed-API-Kompilierung bestanden. Native Paket-DLL, Managedpaket und Beispielprojekt wurden gegen die aktuelle Engine gebaut.

Der echte Runtime-Lauf prüfte acht Stufen, einschließlich Nullbudget, komplett umgangenem Lichtpass, Kamerabewegung und deaktivierten Entities. Alle Stufen und `LIGHTING BUDGET TESTS PASSED` stehen im [finalen Runtime-Log](../artifacts/backlog-round3/lighting-runtime-final.log). [Screenshots](../artifacts/backlog-round3/lighting-runtime-shots/at-1.5s.png) wurden gesichtet. Der [Inspector](../artifacts/backlog-round3/lighting-inspector.png) zeigt Budget 8/16, 8 verwendete/120 verworfene Lichter und 16 verwendete/112 verworfene Occluder samt Caps. Play/Stop setzte die transienten Nullbudgets zurück; Reload und Neustart zeigten wieder die gespeicherten 8/16. [Paket-Detailbericht](../artifacts/backlog-round3/lighting-report.md).

## Buildstand und Grenzen

Inkrementell bestanden: **EngineTests samt Engine**, **Runtime**, **Editor**, isolierter Lighting2D-Doctest-Harness, native/managed Paketziele und Beispielprojekt, jeweils Release. Einmalige Engine-Premake-Regenerierung war wegen der neuen Benchmark-TU erforderlich. Keine Clean-/Rebuild-Läufe; Caches und Modul-/Profiler-/Entity-Bit-Konfiguration erhalten.

Während der ersten Versuche aktualisierte ein bereits laufender Visual-Studio-Installer das Windows-SDK; vorübergehend fehlten Systemheader. Nach dessen erfolgreichem Abschluss wurde weitergebaut. Ein nicht defaultkonstruierbares Entity-Array im neuen Benchmark und fehlende Dawn-Includes im isolierten Testharness wurden korrigiert. Der erste Runtime-Probelauf zeigte einen alten Headerfingerprint; die finalen Runtime-/Editor-Läufe nach Verbraucher-Builds sind ohne diesen Fehler. In den finalen Paketläufen blieb nur der Hinweis auf einen nicht installierten CJK-Font. Keine WebGPU-Validierungsfehler oder Device-Loss-Meldungen.

Basis: Engine `ed9552a2e0a8269dd6d5322271fae0983c3b5ad9`, Pakete `0fa5a15540073926e3784e1eb3d92eb011ccf6f7`, beide mit bestehenden lokalen Änderungen. [Baseline und Delta](../artifacts/backlog-round3/delta.json) trennen diese Runde von früherer Arbeit. Keine Ausgangsdateien entfernt; 79 zuvor geänderte Engine-Dateien und alle 65 zuvor geänderten Paketdateien bleiben bytegleich. Die bestehenden Änderungen in CI und Rendering3DTests wurden gezielt erweitert. Whitespaceprüfung für das Delta bestanden.

Offen bleiben der echte Linux/Lavapipe-CI-Lauf und allgemeine Plattform-/Treiberabdeckung. Die Materialwerte gelten für das genannte Gerät und Experiment; keine Mobile-/Web-Leistungsfreigabe. Alle Testprojekte und Nutzerdaten lagen isoliert unter `artifacts/backlog-round3`; gestartete Anwendungen wurden beendet.
