# Backlog-Quickfixes vom 3. November 2026

Ausgangspunkt: Engine `ed9552a2e0a8269dd6d5322271fae0983c3b5ad9`, Paketregistry `0fa5a15540073926e3784e1eb3d92eb011ccf6f7`. Bearbeitet auf den bestehenden Branches `master` und `main`, mit drei parallelen Agenten und gemeinsamem Review. Keine Commits oder Veröffentlichungen.

Auswahl: die kleinen Aufgaben mit oberer Schätzgrenze bis 90 Minuten sowie die unmittelbar zugehörige Ergebnisübersicht QAL-007. QAL-031 ist wegen der offenen Releasevorbereitung kein unabhängiger Quickfix. Größere Features, Hardwareabnahmen, Langzeitmessungen und Veröffentlichungen bleiben außerhalb dieses Durchlaufs. Die ursprüngliche Backlogdatei bleibt unverändert.

Die vorhandenen Änderungen an `Graphics/CustomShader.cpp`, `Graphics/PostProcessing/PostProcessor.cpp`, den External-Submodulen sowie den Input-Asset-Dateien/Metadaten im Paketrepository wurden erhalten. „Umgesetzt“ bezeichnet den geänderten Quellstand; fehlende echte Plattform- oder Geräteläufe stehen ausdrücklich dabei.

## Status je Task

| ID | Ergebnis / relevante Dateien | Prüfung und verbleibende Abnahme |
|---|---|---|
| INP-001 | Erledigt: Frame-Vertrag an `InputAction.WasPressed/WasReleased` dokumentiert. Vorhandenes gepuffertes FixedUpdate-Beispiel war korrekt. | 164 Input-Tests bestanden. Veraltete Lifecycle-Eventnamen im Paket auf aktuelle API migriert. |
| PRF-001 | Erledigt: [versionierter Ergebnisvertrag](Documentation/Editor/Benchmark-Results.md), kompatibel zum vorhandenen FunkeBench-CSV; Sidecar bindet Build, Workload und Rohdaten. | FunkeBench-Releasearchiv per SHA-256 geprüft, Export-/Statistikquellen gelesen. Keine neuen Leistungswerte behauptet. |
| AIN-001 | Erledigt: [Inventar der Navigation](Documentation/Engine-Core/Navigation-and-Worlds.md). Voxel-A*, Gegnersteuerung und Zustandsautomaten in Beispielen konkret belegt. | Core, ScriptCore, Paketquellen und hashgeprüfte Voxtau-/Aetherwild-Archive inventarisiert. Kein allgemeines Navmesh-/Behavior-Tree-System behauptet. |
| PKG-001 | Erledigt: vier fehlende Katalog-Mindestversionen ergänzt; `Index-PackageRegistry/docs/package-compatibility.md` dokumentiert Matrix und Engine-Prerelease-Normalisierung. | Acht veröffentlichte ZIPs: Name, Version, SHA-256 und Größe korrekt. Acht Quellpakete gegen echte ScriptCore-API gebaut; native Package-/Registry-/SemVer-Tests bestanden. Keine native Paket-Gerätefreigabe. |
| QAL-025 | Erledigt: Clone-/Security-Ziele auf Funke korrigiert. | GitHub-API bestätigt `Ben-Scr/Index` als eigenes älteres Repo, nicht als Funke-Redirect. Beide privaten Reporting-URLs führen zum Login; authentifizierte Meldungsabgabe nicht ausgeführt. 47 relative Linkziele in den betroffenen Einstieg-/neuen Dokumenten geprüft. |
| MEM-001 | Erledigt: Legacy-Accessor deprecated, Snapshot-Migration dokumentiert; exportiertes Symbol bleibt. | Keine produktiven Legacy-Aufrufer im Enginecheckout gefunden. Echte `Memory.cpp` unter MSVC ASan mit konkurrierenden Snapshot-/Allocate-/Free-Zugriffen geprüft. |
| QAL-010 | Erledigt: README, Getting Started und Installertexte führen zum tatsächlichen Source-Build und unterscheiden temporäre CI-Artefakte. | Öffentliche Releases-API liefert `[]`; vorhandene Buildskripte und Links geprüft. Windows-Projektneuanlage mit frisch gebauter CLI erfolgreich. Kein neuer kompletter Linux-/macOS-Source-Build. |
| WLD-001 | Erledigt: [Weltfähigkeitsmatrix](Documentation/Engine-Core/Navigation-and-Worlds.md) mit Chunkstreaming, Meshjobs, Terrain-LOD, Persistenz und Präzisionsgrenzen. | Veröffentlichte Voxtau-/Aetherwild-ZIPs hashgeprüft. Spielcode von Engine-API getrennt; allgemeine World-Partition-/Origin-Features und reale Lastgrenzen bleiben unbewiesen. |
| GFX-001 | Erledigt: AudioMixer in beide Richtungen synchronisiert, stille native UI-Updates verhindern verzögerte/quantisierte Rückkopplung; gleichzeitige Slider-/Toggle-Änderungen bleiben erhalten; Unbind idempotent. | Unitregressionen und echte API-Kompilierung bestanden. Echter Windows-Release-Editor: 17 Checks über 110 Scriptupdates, 0 Scriptfehler, Exit 0; Script/Fade/Snapshot, native UI-Events, Unbind und zerstörte Widgets geprüft. |
| GFX-049 | Erledigt: nur WGSL als Shader auswählbar; Legacyformate bleiben Quelldateien mit „Source only“-Kennzeichnung und Zuweisungsdiagnose. | Registry-Regressionen inkl. Großschreibung, GUID-/Inhaltserhalt bestanden; Editor gebaut und sichtbare Browsermarkierung im echten Fenster geprüft. Drag/drop-Diagnose kompiliert und im Code geprüft, nicht durch Mausbedienung ausgeführt. |
| NET-001 | Erledigt: Varint-Endbyte-Überläufe, nichtkanonische Encodings und overflowanfällige Boundschecks korrigiert. | Networking-Suite mit Grenzwerten, ungültigen Längen und echter UDP-Session besteht; gültige Nachricht nach fehlerhaftem Paket bleibt nutzbar. |
| QAL-001 | Bereits korrigierter Verursacher bestätigt: Tracys `SymError` schreibt auf stderr; kein External-Code geändert. | Frischer Windows-Agentworkflow einschließlich Recompile: kompletter stdout aller JSON-Kommandos direkt parsebar. Originaler reduzierter Standalone-PATH-/SymInitialize-Fall und Windows-CI bleiben offen. |
| QAL-002 | Fix umgesetzt: vor Recompile explizites PID-/Status-/Busy-Polling mit Gesamtbudget, Crash- und Fehlererkennung. | 26 Standalone-/Readiness-Tests und echter Windows-Agentworkflow bestanden. Verzögerter Start, dauerhaft busy und echte Fehler gezielt geprüft. Originaler Linuxlauf bleibt offen. |
| NET-002 | Fix umgesetzt: ungültige Transformrecords/Spawns/Zeitstempel früh verworfen; Double-Zwischenwerte verhindern Interpolationsüberlauf. | 73 Networking-Tests mit echten UDP-/WebSocket-Loopbacks bestanden; Float-Extrema, NaN/Inf und Session-Erholung geprüft. Nativer Mehrprozesslauf bis Physik/Renderer bleibt offen. |
| QAL-004 | Teilweise: eigene Sanitizer-Matrixausführung unabhängig von Editor/Installer und Coverage; Logs/Buildidentität erhalten, Releasegate fordert Erfolg. | Workflowstruktur und negative Gatefälle geprüft. Echter Linux-ASan/UBSan/LeakSanitizer-Lauf dieses Arbeitsstands fehlt; lokaler Memory-ASan-Test ersetzt ihn nicht. |
| QAL-007 | Teilweise: `ci-evidence` bilanziert success/failure/skipped/cancelled/missing; Buildertests sind ausdrücklich `unit_mock`. | Pflichtprüfungen können bei fehlendem Erfolg keinen grünen Gesamtcheck liefern. Vollständige Bilanz einzelner GPU-/Interop-Testfälle und echte QAL-006-Ausführung fehlen. |
| QAL-023 | Teilweise: Evidenzstufen, Exporthashinventare vor/nach Lauf, Tool-/Zieldaten, blockierte Voraussetzungen und Mindestmatrix ergänzt. | 28 gezielte Builder-/Plattformtests bestanden. Ein Mock-Pass bleibt ohne Plattformfreigabe. Build-Commit-/Compiler-Provenienz und echte Geräteabnahmen bleiben offen. |
| GFX-044 | Erledigt: alle acht zuvor unregistrierten Renderer3D-Scopes registriert und in CPU-Panelgruppe aufgenommen; leere Buffer zeigen „No samples“. | Vollständiger Registrierungsabgleich, Shutdown/Reinit-Regression und Editorbuild bestanden. Panel-Start ohne Fehler; das Hauptfenster-Screenshot erfasst das separate Profilerfenster nicht. |
| QAL-005 | Teilweise: unabhängiger Coveragejob, Roh-/HTML-/JSON-Berichte samt Commit, Toolversionen und Scope; leere/unbenutzte/Third-Party-Berichte schlagen fehl. | Parser-/Gate-Tests und YAML-Prüfung bestanden. Echte Linux-Baseline sowie eine daraus abgeleitete Regressionstoleranz fehlen; kein Prozentwert erfunden. |
| MEM-002 | Erledigt: Debug-Threadbindung der globalen Arenas, einschließlich Zugriff über gespeicherte Arena-Referenzen; Release/Dist ohne zusätzliche Felder/Checks. | 9 isolierte Debug-Tests, 48 Assertions bestanden. Worker-Lokalarenen und neuer Lifecycle-Thread geprüft. Debug-Verbraucher müssen wegen des Debug-Klassenlayouts neu gebaut werden. |
| MEM-004 | Teilweise geprüft, kein spekulativer Lebensdauerumbau: einmalige Prozessretention und dauerhaft abgeschaltetes Tracking nach finalem Shutdown dokumentiert. | MSVC-ASan: paralleler Shutdown und 100 zusätzliche Init-/Shutdown-Zyklen ohne Befund. Voller Engine-/Projekt-Lifecycle-Verlauf, genaue Retention in Bytes und Linux-LeakSanitizer fehlen. |

## Gemeinsame Verifikation

- `scripts/dev/Build.ps1 -Project EngineTests,Editor,CLI -Config Release -NoRegenCheck`: erfolgreich, inkrementell, bestehende Caches/Projektkonfiguration erhalten. Vorhandene Compilerwarnungen bleiben dokumentiert in den Logs.
- `scripts/dev/Test.ps1 -Suite Engine -Config Release -NoBuild -Filter '*Arena*,*Profiler*,*Asset registry*,*Package*,*Registry*,*SemanticVersion*,*VersionRange*,*Install plans*'`: **115 Tests, 1.319 Assertions bestanden**; 742 nicht ausgewählte Fälle sind kein zusätzlicher Testnachweis.
- `python -m unittest discover -s scripts/ci -p 'test_*.py'`: **243 ausgeführt, 233 bestanden, 10 übersprungen**, kein Fehler. Die zehn Plattform-Skips betreffen sieben Linux-/POSIX-Installerfälle, zwei macOS-Nativfälle und einen POSIX-SDK-Symlinkfall; Namen und Gründe stehen in `bin-int/_agent/backlog-evidence/python-skips.json`.
- Pakettests: **287 bestanden, 0 übersprungen** — Input 164, AudioMixer 21, Networking 73, Tween 29. Input enthält bereits vorhandene Nutzer-Testarbeit.
- Isolierter Debugtest mit echter `Arena.cpp`/`FrameArenas.cpp`: **9 Tests, 48 Assertions bestanden**.
- Echte `Memory.cpp` mit MSVC AddressSanitizer: vier Worker, 80.000 Allocate/Free-Paare, 12.638.152 konsistente Snapshots sowie 100 weitere Lifecycle-Schleifen/100.000 späte Allokationen; kein ASan-Fehler. Vier Arena-Ownership-Logs und die gezielt gehaltenen Allokationen beim Shutdown sind erwartete Negativtestdiagnosen.
- Windows-Agentworkflow: **20 Schritte, 8 Suites** mit erwarteten Fehlerfällen erfolgreich. Readiness bei `state=edit`; Recompile, Spieltests und zweite Scriptrevision bestanden. Absichtlich fehlerhafte Assertions, Timeout, Property, Script-Exception und Compile-Fehler wurden korrekt erkannt. JSON wurde aus vollständigem stdout gelesen, ohne Textzeilen wegzufiltern.
- Echter Editor-Smoke: Projekt geöffnet/geschlossen (Exit 0), Browserkennzeichnung visuell geprüft, keine Warnungen/Fehler in diesen Läufen.
- Echter AudioMixer-Smoke mit nativen Slider-/Toggle-Komponenten: 17 Checks über 110 Updates bestanden, 0 Scriptfehler. Dabei wurde zusätzlich die gegenseitige Überschreibung gleichzeitiger UI-Änderungen gefunden, korrigiert und durch eine Regression abgesichert.
- Zwei Workflowdateien mit PyYAML geprüft; Evidence-Gate zusätzlich gegen alle Plattformauswahlen/core-only und fehlende/abgebrochene Prüfungen getestet. `actionlint` war nicht installiert.
- `git diff --check`: ohne Whitespacefehler. Keine neuen Branches, Commits oder Releaseassets.

## Lokale Belege und Auslieferungsstand

Die folgenden lokalen Dateien liegen im git-ignorierten Arbeitsbereich:

- `bin-int/_agent/backlog-python-tests.log`, `test-Engine.log`, `EngineTests.log`, `Editor.log`, `CLI.log`.
- `bin-int/_agent/backlog-agent-evidence/agent-75a21993c8e6/report.json` samt vollständigen stdout-/stderr- und Suite-Artefakten.
- `bin-int/_agent/backlog-evidence/shader-source-browser.png` und `.log`.
- `bin-int/_agent/backlog-core-test/core-report.md`, Debug-/ASan-Build-/Runlogs und reproduzierbare Probes.
- `artifacts/backlog-quickfixes/packages.md`, `package-release-audit.json` und API-Buildprojekte.
- `artifacts/backlog-quickfixes/mixer-native-runs/test-ada7d71aed7a5ba8e21ab4a8/report.json` und `editor.log`; Fixture/Ergebnis unter `bin-int/_agent/backlog-mixer/MixerBindingSmoke/Assets/`.

Paketänderungen liegen im benachbarten **Index-PackageRegistry**-Repository. Unveröffentlichte Sourceversionen: AudioMixer **1.1.2**, Networking **1.0.1**, Tween **1.0.1**. Indexeinträge behalten die tatsächlich veröffentlichten ZIP-Versionen und Hashes; Installer-Locks sind unverändert. Die Fixes sind daher noch nicht automatisch in einem heruntergeladenen Paket enthalten. Bestehende Input-0.2.0-Arbeit stammt vom Nutzer.

Nächste notwendige Nachweise sind der Linux-Agentlauf, die unabhängigen Linux-Sanitizer-/Coveragejobs und ein nativer Mehrprozess-Transform-Smoke. Gerätefreigaben, quantitative Leistungsbaselines und Paketveröffentlichungen benötigen eigene tatsächliche Ausführung; dieser Bericht markiert sie nicht als bestanden.
