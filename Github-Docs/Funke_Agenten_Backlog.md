# Funke – offene Aufgaben nach Komplexität

Stand 26. November 2026. **98 offene Aufgaben.** Bewertet wird die verbleibende Arbeit einschließlich ihrer Abnahme. Innerhalb jeder Stufe sind die IDs aufsteigend sortiert. P0–P3 geben die Dringlichkeit an. Abhängigkeiten und benötigte Geräte stehen beim Auftrag.

| Komplexität | Maßstab |
| --- | --- |
| K1 · Gering | Eine vorhandene Funktion oder einen vorbereiteten Prüfpfad mit klarem Sollwert abnehmen. |
| K2 · Mittel | Eine abgegrenzte Änderung oder einen durchgängigen Ablauf mit überschaubaren Schnittstellen und Fehlerfällen prüfen. |
| K3 · Hoch | Mehrere Systeme, Artefakte oder Zielumgebungen integrieren oder Messungen mit kontrollierten Vergleichsbedingungen auswerten. |
| K4 · Sehr hoch | Einen neuen systemübergreifenden Vertrag entwerfen, etwa für ABI, Nebenläufigkeit, Rendering, Netzwerkzustand oder Datenmigration, und seine Wechselwirkungen absichern. |

## Aufgabenübersicht

### K1 · Gering (2)

| ID | Thema | Aufgabe |
| --- | --- | --- |
| [CI-01](#task-ci-01) | CI und Tests | Erweiterte native SDK-Prüfung auf drei Desktop-Runnern belegen |
| [QAL-004](#task-qal-004) | CI und Tests | Unabhängige Sanitizer-Lane bei absichtlichem Editorfehler nachweisen |

### K2 · Mittel (8)

| ID | Thema | Aufgabe |
| --- | --- | --- |
| [EDT-030](#task-edt-030) | Game UI und Text | Font-Fallbacks auf weiteren Zielplattformen im Editor und Export prüfen |
| [INP-011](#task-inp-011) | Input | Touch-Gesten auf einem physischen Gerät abnehmen |
| [PM-26](#task-pm-26) | Physik | Physikkosten mit passendem Vorher-Stand vergleichen |
| [QAL-005](#task-qal-005) | CI und Tests | Regressionstoleranz für die Linux-Coverage kalibrieren |
| [QAL-007](#task-qal-007) | CI und Tests | Geänderte Pflichtgates in einem vollständigen CI-Bericht abnehmen |
| [QAL-011](#task-qal-011) | Auslieferung | Vorbereiteten Installerentwurf für die erste Vorschau abgleichen |
| [QAL-030](#task-qal-030) | Dokumentation | Unabhängigen Nutzungsbericht für die geprüften Referenzprojekte einholen |
| [QAL-031](#task-qal-031) | Auslieferung | Vorbereitete Desktop-Beta veröffentlichen und den echten Downloadweg prüfen |

### K3 · Hoch (49)

| ID | Thema | Aufgabe |
| --- | --- | --- |
| [ASSET-01](Backlog-Build-Asset-Validation.md#task-asset-01) | Auslieferung | Fehlerdarstellung und Export auf Desktop und Web abnehmen |
| [EDT-010](#task-edt-010) | Scripting | Nativen SDK-, Inspector- und Exportablauf auf sauberer Installation abnehmen |
| [EDT-012](#task-edt-012) | Scripting | Transaktionsgrenzen beim Managed-Assembly-Wechsel überprüfen |
| [EDT-021](#task-edt-021) | Assets und Szenen | Gestückelte Textur-Publikation auf echter GPU abnehmen |
| [EDT-041](#task-edt-041) | Editor | Drag-and-drop von Assets zu externen Anwendungen auf Linux und macOS ergänzen |
| [EDT-043](#task-edt-043) | Game UI und Text | Glyphen-Vorwärmung und Erstbenutzung auf der GPU messen |
| [GFX-002](#task-gfx-002) | Audio | AudioMixer: laufende OneShots auf Gruppenänderungen reagieren lassen |
| [GFX-006](#task-gfx-006) | Rendering 2D | Tilemap-Pinsel mit Löschen und zusammenhängendem Undo-Schritt bauen |
| [GFX-009](#task-gfx-009) | Beleuchtung 2D | Fullscreen-Lichtschleifen auf Zielgeräten vermessen und den größten Engpass reduzieren |
| [GFX-010](#task-gfx-010) | Beleuchtung 2D | Polygonale 2D-Schattenwerfer ergänzen |
| [GFX-011](#task-gfx-011) | Beleuchtung 2D | Weiche Schatten als optionale Qualitätsstufe implementieren |
| [GFX-012](#task-gfx-012) | Beleuchtung 2D | Normalmaps in den 2D-Beleuchtungspfad integrieren |
| [GFX-013](#task-gfx-013) | Beleuchtung 2D | Lichtmasken und selbstleuchtende Ausschlüsse für 2D einführen |
| [GFX-024](#task-gfx-024) | Shader und Postprocessing | Shader-/Pipeline-Warmup vor dem ersten sichtbaren Einsatz ermöglichen |
| [GFX-028](#task-gfx-028) | Physik | Weitere Box2D-Jointtypen als klar abgegrenzte Komponenten exponieren |
| [GFX-032](#task-gfx-032) | Physik | Fixed-, Hinge- und Distance-Joints für 3D exponieren |
| [GFX-035](#task-gfx-035) | Animation | 1D-Blendtrees für Animationen implementieren |
| [GFX-040](#task-gfx-040) | Partikel und VFX | Beleuchtete und depth-weiche 3D-Billboard-Partikel ergänzen |
| [GFX-041](#task-gfx-041) | Partikel und VFX | Stretched-Billboards und Mesh-Partikel als explizite Rendermodi ergänzen |
| [GFX-048](#task-gfx-048) | Rendering 3D | glTF-Szenenstruktur, Kameras und unterstützte Lichter optional als Prefab importieren |
| [GFX-050](#task-gfx-050) | Shader und Postprocessing | Picking bei Vertex-Displacement präzisieren und die Bounds-Approximation sichtbar machen |
| [GFX-051](#task-gfx-051) | Shader und Postprocessing | Vier feste Custom-Effect-Slots zu einer validierten geordneten Effektliste erweitern |
| [INP-007](#task-inp-007) | Input | Stabile Controllerzuordnung und Join-on-Press ergänzen |
| [INP-009](#task-inp-009) | Input | Visuellen Action-Map-Editor mit Rebindingvorschau bauen |
| [MEM-003](#task-mem-003) | Architektur und Speicher | Speicherdiagnostik auf Linux und macOS als verifizierten Weg anbieten |
| [MEM-004](#task-mem-004) | Architektur und Speicher | Beabsichtigte Allocator-Retention von wachsenden Leaks unterscheiden |
| [NET-002](#task-net-002) | Networking | Transform-Guards bis zur nativen Spielwelt abnehmen |
| [NET-011](#task-net-011) | Networking | Relay-Anbindung als austauschbaren Verbindungsweg vorbereiten |
| [PERF-15](#task-perf-15) | Performance | Native Allokationen den gemessenen Frames zuordnen |
| [PERF-16](#task-perf-16) | Performance | Regressionsschwellen auf kontrollierter CPU-/GPU-Hardware kalibrieren |
| [PM-24](#task-pm-24) | Physik | Physikmodulvarianten auf Geräten und im Release-SDK abnehmen |
| [PRF-002](#task-prf-002) | Performance | Drei reproduzierbare Lastszenen und einen belastbaren Ausgangswert erzeugen |
| [PRF-003](#task-prf-003) | Performance | Fünf Lade-Lastfälle auf echter GPU vermessen |
| [PRF-005](#task-prf-005) | Performance | Langzeittest für Szenen, Hot Reload und Ressourcenfreigabe einrichten |
| [QAL-008](#task-qal-008) | Plattformen | Desktop-Unterstützung mit einer reproduzierbaren Laufzeitmatrix belegen |
| [QAL-009](#task-qal-009) | Auslieferung | Installierte Editor-/SDK-/Playerabläufe ohne Source-Checkout prüfen |
| [QAL-012](#task-qal-012) | Auslieferung | Vorhandene Release-Signiergates tatsächlich verifizieren |
| [QAL-013](#task-qal-013) | Plattformen | Notarisierung exportierter macOS-Spiele als optionalen Ablauf ergänzen |
| [QAL-014](#task-qal-014) | Plattformen | Android-Export auf echtem arm64-Gerät end-to-end validieren |
| [QAL-016](#task-qal-016) | Plattformen | iOS-Export auf einem physischen Gerät beweisen |
| [QAL-017](#task-qal-017) | Plattformen | iOS-Simulator als getrenntes, tatsächlich baubares Exportziel ergänzen |
| [QAL-018](#task-qal-018) | Plattformen | Echten WebAssembly-Export regelmäßig im Browser testen |
| [QAL-021](#task-qal-021) | Plattformen | Mono-Interpretergrenzen mit Messungen und Kompatibilitätstests quantifizieren |
| [QAL-023](#task-qal-023) | CI und Tests | Exportbelege bis zum tatsächlich ausgeführten Zielartefakt prüfen |
| [QAL-027](#task-qal-027) | Pakete | Gesperrte Git- und lokale Pakete reproduzierbar wiederherstellen |
| [QAL-028](#task-qal-028) | Pakete | Paketupdate-Rollback und verbleibende Plattformpfade abnehmen |
| [QAL-029](#task-qal-029) | Pakete | Lizenzinventare der tatsächlichen Artefakte und unklare Beispielprojektlizenzen klären |
| [UTL-007](Backlog-CSharp-Utilities.md#task-utl-007) | Scripting | Kamera-Komfort |
| [WLD-003](#task-wld-003) | Große Welten | Einfaches geometrisches LOD mit Hysterese und Debuganzeige anbieten |

### K4 · Sehr hoch (39)

| ID | Thema | Aufgabe |
| --- | --- | --- |
| [AIN-003](#task-ain-003) | KI und Navigation | 3D-Navmesh-Laufzeitintegration und begrenzte Pfadabfragen aufbauen |
| [AIN-004](#task-ain-004) | KI und Navigation | Navmesh-Bake, Debugdarstellung und Aktualisierung in den Editor integrieren |
| [AIN-006](#task-ain-006) | KI und Navigation | Behavior-Tree-Assets und visuellen Debugger ergänzen |
| [EDT-024](#task-edt-024) | Plattformen | Definierten Ladepfad für threadlose Web-Builds anbieten |
| [EDT-031](#task-edt-031) | Game UI und Text | Glyphen-Shaping für komplexe Schriften ergänzen |
| [EDT-032](#task-edt-032) | Game UI und Text | Bidirektionales Layout einschließlich gemischter Textläufe ergänzen |
| [EDT-033](#task-edt-033) | Game UI und Text | Graphemcluster, kombinierende Zeichen und Caret-Bewegung korrekt behandeln |
| [EDT-034](#task-edt-034) | Game UI und Text | Optionales Rendering von Color-Font-/Emoji-Glyphen ergänzen |
| [EDT-035](#task-edt-035) | Agentensteuerung | Restoration-Vertrag für Paketkomponenten in Editor-Transaktionen einführen |
| [EDT-036](#task-edt-036) | Agentensteuerung | Dynamische Managed-Komponenten in Transaktionen kontrolliert unterstützen |
| [EDT-037](#task-edt-037) | Agentensteuerung | Native-Script-Teilbäume für sichere Edit-Batches freischalten |
| [GFX-003](#task-gfx-003) | Audio | Native Mixer-Busse und eine kleine DSP-Effektkette ergänzen |
| [GFX-018](#task-gfx-018) | Rendering 3D | FBX-Import über klar begrenzten, lizenzierten Importpfad ergänzen |
| [GFX-019](#task-gfx-019) | Rendering 3D | Transparenzgrenzen reproduzieren und einen begrenzten verbesserten Renderpfad anbieten |
| [GFX-020](#task-gfx-020) | Beleuchtung 3D | Punkt- und Spotlichtschatten mit begrenztem Schattenbudget implementieren |
| [GFX-021](#task-gfx-021) | Beleuchtung 3D | Cascaded Shadow Maps für das Hauptlicht ergänzen |
| [GFX-022](#task-gfx-022) | Beleuchtung 3D | Environment-Cubemap-Reflexionen mit vorgefilterten Roughness-Stufen ergänzen |
| [GFX-023](#task-gfx-023) | Kameras | Mehrere Spielansichten und Split Screen mit getrennten Viewzuständen unterstützen |
| [GFX-029](#task-gfx-029) | Physik | Compound-Collider für 2D mit eindeutigem Ownership-Vertrag ergänzen |
| [GFX-031](#task-gfx-031) | Physik | 3D-Character-Controller für einen definierten Basisspieler anbieten |
| [GFX-036](#task-gfx-036) | Animation | Animations-Layers und Avatar-Masks mit klarer Additivsemantik ergänzen |
| [GFX-037](#task-gfx-037) | Animation | Root Motion mit kontrollierter Physik-/Scriptübergabe implementieren |
| [GFX-038](#task-gfx-038) | Animation | glTF-Morph-Targets einschließlich animierter Gewichte importieren und rendern |
| [GFX-042](#task-gfx-042) | Partikel und VFX | Begrenzte Partikelkollisionen und ereignisbasierte Subemitter einführen |
| [GFX-043](#task-gfx-043) | Partikel und VFX | GPU-Partikelsimulation als zusätzlichen begrenzten Backendpfad bauen |
| [NET-006](#task-net-006) | Networking | Versionierte Szenensynchronisierung und Late-Join-Bereitschaft ergänzen |
| [NET-007](#task-net-007) | Networking | Interest Management für große Multiplayerwelten ergänzen |
| [NET-008](#task-net-008) | Networking | Prediction und Reconciliation für einen begrenzten Bewegungstyp umsetzen |
| [NET-009](#task-net-009) | Networking | Lag Compensation als getrennte serverseitige Erweiterung evaluieren |
| [NET-010](#task-net-010) | Networking | Sicheren Transportpfad mit ehrlicher TLS-/UDP-Abgrenzung bereitstellen |
| [NET-012](#task-net-012) | Networking | NAT-Punchthrough mit definiertem Fallback prüfen und integrieren |
| [QAL-015](#task-qal-015) | Plattformen | Android-ABI-Grenzen explizit machen und zusätzliche Architektur gezielt ausbauen |
| [QAL-020](#task-qal-020) | Plattformen | Web-Jobausführung transparent machen und optionalen Threadmodus entwickeln |
| [QAL-022](#task-qal-022) | Plattformen | Optionales Web-Trimming mit nachgewiesener Script-/Reflectionkompatibilität anbieten |
| [QAL-024](#task-qal-024) | Plattformen | Desktop-Crossexport über explizite Zielbuilder statt stiller Hostannahmen ermöglichen |
| [QAL-026](#task-qal-026) | Pakete | Beta-Kompatibilität, ABI und Datenmigration als überprüfbare Releaseverträge definieren |
| [UTL-008](Backlog-CSharp-Utilities.md#task-utl-008) | Scripting | 3D-Physikabfragen |
| [WLD-002](#task-wld-002) | Große Welten | Begrenzten Chunk-Streaming-Scheduler mit Abbruch und Speicherbudget entwickeln |
| [WLD-005](#task-wld-005) | Große Welten | Floating Origin konsistent über alle benötigten Subsysteme umsetzen |

## Arbeitsregeln

1. Bearbeite die konkret ausgewählten IDs. Lies vorhandene Projektanweisungen und Konventionen; erhalte laufende Nutzeränderungen.
2. Prüfe zunächst Iststand, Beleg und Voraussetzungen. Markiere bereits gelöste Punkte mit Fundstelle und Prüfergebnis, statt sie erneut umzubauen.
3. Benenne bei fehlenden Geräten, Compilern oder Zugangsdaten den exakten Blocker. Ein Mock, ein übersprungener Test oder eine vorhandene Builderklasse zählt nicht als bestandener echter Plattformlauf.
4. Setze bestehende Schnittstellen weiter ein. Änderungen an Komponenten/Feldern müssen bei Bedarf C++, C#, Layout/ABI, Serialisierung, Inspector, Export, Tests und Dokumentation konsistent berücksichtigen.
5. Halte Fehlerbehebung und umfangreiches Refactoring getrennt. Entferne keine Integritätsprüfungen, ABI-Gates oder Rollback-Ablehnungen lediglich, damit der schnelle Pfad funktioniert.
6. Validiere das tatsächlich verbleibende Risiko mit vorhandenen und gezielt ergänzten Tests. Für eine reine Text-/Linkkorrektur reichen passende Inhalts-/Linkprüfungen; dafür keine neue Testsuite erfinden.
7. Belege Codeänderung und Ausführung getrennt. Schreibe nicht „funktioniert auf iOS“, wenn nur Code kompiliert oder ein Simulator gestartet wurde.
8. Passe bei behobenen Grenzen die Dokumentation an, aber entferne Einschränkungen erst nach dem entsprechenden Nachweis. Performancebehauptungen brauchen reproduzierbare Messwerte.
9. Verwende für Funke-Pakete deren Repository und bestehende Releasekonventionen. Geänderter Quellcode auf main ist nicht automatisch in einem bereits veröffentlichten ZIP enthalten.
10. Führe externe Veröffentlichung nur aus, wenn sie zum aktuellen Auftrag gehört. Bereits erteilte Autorisierung bleibt gültig. QAL-011 ist Vorbereitung, QAL-031 ist die tatsächliche Veröffentlichung; die Erstellung dieser Datei führt beide nicht aus.
11. Bei umfangreichen Tasks nach sinnvollen nummerierten Schritten einen überprüfbaren Zwischenstand erzeugen. Eine teilweise implementierte Funktion bleibt „teilweise erledigt“, bis alle maßgeblichen Abnahmekriterien erfüllt sind.
12. Berichte nach jedem Task: geänderte Dateien, gelöste Ursache/Verhaltensänderung, ausgeführte Prüfungen, nicht ausgeführte Prüfungen mit Grund und verbleibende Risiken. Einen zielgerichteten abgeschlossenen Task nicht endlos mit optionalen Tests ausweiten.

Abgeschlossene Aufgaben erst nach Prüfung ihrer dauerhaften Dokumentation entfernen. Bei Teilfortschritt die Restarbeit neu einstufen. [Vertragsprüfungen](Documentation/Editor/Verified-Contracts-2026-11.md) · [CI-Nachweis für 67e570bf](Documentation/Editor/Testing.md#verified-desktop-ci-run).

## Aufträge mit Komplexität K1

<a id="task-ci-01"></a>

### CI-01 – Erweiterte native SDK-Prüfung auf drei Desktop-Runnern belegen

**Komplexität:** K1 · **Status:** Restabnahme offen · **Priorität:** P2

**Prüfung vom 26. November:** Windows lokal bestanden (PCH an/aus, alle Snippets, Recovery, Headermatrix). Der ältere WSL-Generator erzeugte keine Snippets. Für die erweiterten Prüfungen fehlen passende Linux-/macOS-Ergebnisse und die Hosted-Jobnachweise. [SDK-Prüfbericht](Documentation/Scripting/Native-Scripting-Verification.md#local-sdk-check-on-26-november-2026).

Die drei SDK-Schritte am veröffentlichten Stand waren erfolgreich. [CI-Nachweis für 67e570bf](Documentation/Editor/Testing.md#verified-desktop-ci-run)

- [ ] Die erweiterte Fassung von `scripts/ci/native_scripting_test.py` auf Windows, Linux und macOS ausführen. Pro Plattform Job, Commit, Toolchain und Protokoll des passenden Builds sichern. Die interaktive Abnahme bleibt [EDT-010](#task-edt-010).

<a id="task-qal-004"></a>

### QAL-004 – Unabhängige Sanitizer-Lane bei absichtlichem Editorfehler nachweisen

**Komplexität:** K1 · **Status:** teilweise erledigt; Restarbeit offen · **Priorität:** P1 · **Art:** Ausbau · **Belegstatus:** bestätigt

**Lokal geprüft (26.11.2026):** Der Fehlerzweig liefert Exitcode 1, der Sanitizer-Job ist vom Linux-Build unabhängig und alle 24 Evidenztests bestehen. Das XML aus dem normalen Lauf wurde erneut validiert. Offen bleibt der gemeinsame gehostete Fehlerversuch. [Lokale Prüfung](Documentation/Editor/Testing.md#local-sanitizer-independence-checks)

**Verbleibende Arbeit und Abnahme:**

- [ ] Den vorbereiteten Dispatch mit `fail_linux_editor_probe=true` auf dem integrierten Stand prüfen. Fehlgeschlagenen Editor-Schritt, unabhängigen erfolgreichen Sanitizer-Job und dessen XML aus demselben Versuch sichern. Der normale Sanitizerlauf ist belegt. [CI-Nachweis für 67e570bf](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Dateien und Einstiegspunkte:**

- `Funke:.github/workflows/ci.yml`
- `Funke:premake5.lua`
- `Funke:Tests/Funke-Engine-Tests/premake5.lua`

## Aufträge mit Komplexität K2

<a id="task-edt-030"></a>

### EDT-030 – Font-Fallbacks auf weiteren Zielplattformen im Editor und Export prüfen

**Komplexität:** K2 · **Status:** implementiert; Restabnahme offen · **Priorität:** P2

**Lokaler Prüfstand (26.11.2026):** D3D12 und Vulkan zeigten auf der Radeon RX 7800 XT gemischte Schrift, Zeilenumbruch und Ersatz für U+10FFFF. Der lokale Windows-Development-Export erhielt die Glyphen. Font-Auswahl und Umordnen im Settings-Fenster, gespeicherte GUID-Reihenfolge, Caret-Eingabe neben einem CJK-Zeichen, fehlende GUID sowie Austausch und Wiederherstellung einer Fontdatei im laufenden Editor wurden geprüft. Der Filter findet die Fallback-Sektion jetzt auch per Suche.

**Verbleibende Abnahme:**

- [ ] Den Ablauf auf den weiteren Zielplattformen mit passendem SDK und Backend wiederholen. Das ältere beta.1-SDK 67e570bf verpackte die Assets, zeigte im Windows-Export aber Ersatzzeichen. [Prüfstand und Fontvertrag](Documentation/Assets/Font-Fallbacks.md).


<a id="task-inp-011"></a>

### INP-011 – Touch-Gesten auf einem physischen Gerät abnehmen

**Komplexität:** K2 · **Status:** implementiert; Geräteabnahme offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Lokaler Prüfstand (26.11.2026):** Der lokale Paketcheckout enthält den optionalen TouchGestureAdapter mit Finger-IDs, relativer Drag-Schwelle, UI-Capture, unskalierter Tap-Zeit und Abbruch bei Fokus-/Viewportwechsel. SDL-Cancel entfernt den Finger ohne Tap. 317 Pakettests bestanden, einschließlich zweier schneller Berührungen mit derselben Finger-ID. Der Windows-Host meldet keine Touchkontakte als Gerätefähigkeit; der Test auf einem physischen Touchgerät bleibt offen.

**Verbleibende Abnahme:**

- [ ] Auf einem physischen Touchgerät Tap, Drag und Pinch mit vertauschter Fingerreihenfolge, UI-Capture, Rotation, Fokusverlust und abgebrochener Berührung prüfen. Gerät, Betriebssystem und Ergebnis festhalten.

Der optionale Adapter und sein Eingabevertrag stehen im lokalen Paketcheckout unter `packages/Funke.Input/README.md`. Drei native Touchtracker-Tests bestanden mit 21 Assertions.


<a id="task-pm-26"></a>

### PM-26 – Physikkosten mit passendem Vorher-Stand vergleichen

**Komplexität:** K2 · **Status:** Restabnahme offen · **Priorität:** P1

**Lokaler Prüfstand (26.11.2026):** Die lokalen Vergleichsdaten wurden anhand der Harness-, Binär- und Quellfingerprints erneut geprüft. Die beiden Messreihen stammen von unterschiedlichen uncommitteten Quellständen und unterschiedlichen Harnesses. Im Git-Verlauf ist der Adapter erstmals in 92e61d4a enthalten; dieser Integrationscommit ändert 444 Dateien. Beide Transformquellen und ihre Hashes sind lokal gesichert. Ein isolierter Vorherstand mit passender Binär-/Harness-Zuordnung und abwechselnde Messungen bleiben offen.

- [ ] Ausgangsstand und integrierten Stand mit identischem Harness, Compiler, Profil und Szenen einfrieren. Die gesicherte Basis enthält den ersten Transform-Adapter bereits. Für dessen Kosten den Stand davor heranziehen.
- [ ] Abwechselnde Messungen ohne parallele Builds durchführen. Binär- und Harness-Hashes, Rohdaten, Wiederholungen und Streuung aufbewahren. Die ältere Referenz mit abweichendem Harness eignet sich dafür nicht. Erst mit vergleichbaren Daten Regressionen bewerten.

[Modulverträge](Documentation/Engine-Core/Engine-Modules.md) · [Prüfbericht und Vergleichsgrenzen](Documentation/Engine-Core/Physics-Modules-Verification.md).

<a id="task-qal-005"></a>

### QAL-005 – Regressionstoleranz für die Linux-Coverage kalibrieren

**Komplexität:** K2 · **Status:** teilweise erledigt; Restarbeit offen · **Priorität:** P1 · **Art:** Ausbau · **Belegstatus:** bestätigt

**Lokaler Prüfstand (26.11.2026):** Die einzige lokal vorhandene gefilterte LCOV-Datei wurde erneut mit dem aktuellen Prüfer ausgewertet. Der Bericht stimmt mit dem archivierten JSON einschließlich aller Dateizähler überein. Weitere unabhängige Läufe mit derselben Linux-CI-Konfiguration fehlen; eine Prozenttoleranz lässt sich aus dieser einen Beobachtung nicht kalibrieren.

**Verbleibende Arbeit und Abnahme:**

- [ ] Die dokumentierte Linux-Baseline mit weiteren vergleichbaren Läufen auswerten, eine begründete Regressionstoleranz festlegen und das Gate daran prüfen. [CI-Nachweis für 67e570bf](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Dateien und Einstiegspunkte:**

- `Funke:.github/workflows/ci.yml`
- `Funke:premake5.lua`
- `Funke:CONTRIBUTING.md`

<a id="task-qal-007"></a>

### QAL-007 – Geänderte Pflichtgates in einem vollständigen CI-Bericht abnehmen

**Komplexität:** K2 · **Status:** teilweise erledigt; Restarbeit offen · **Priorität:** P1 · **Art:** Ausbau · **Belegstatus:** bestätigt

**Lokaler Prüfstand (26.11.2026):** Die zwölf archivierten Lanes des Versuchs 43692056280/1 haben dieselben geforderten Capability-Schritte wie der aktuelle Aggregator. Nach Anpassung einer veralteten Quelltextprüfung bestanden 58 lokale CI-Evidenz- und Release-Regressionstests, einschließlich Negativfällen. Die verschärften Gates auf dem unveröffentlichten Stand benötigen weiterhin einen vollständigen gehosteten Versuch.

**Abhängigkeiten:** [QAL-004](#task-qal-004), [QAL-005](#task-qal-005), [QAL-006](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Verbleibende Arbeit und Abnahme:**

- [ ] Den Gesamtbericht nach Integration der verschärften XML-, Skip- und Ausführungsprüfungen abnehmen. Der veröffentlichte Ausgangsstand hat alle sieben Pflichtgruppen bestanden. [CI-Nachweis für 67e570bf](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Dateien und Einstiegspunkte:**

- `Funke:.github/workflows/ci.yml`
- `Funke:.github/workflows/platform-smoke.yml`
- `Funke:scripts/ci/test_release_scripts.py`
- `Funke:Github-Docs/Documentation/Editor/Testing.md`

<a id="task-qal-011"></a>

### QAL-011 – Vorbereiteten Installerentwurf für die erste Vorschau abgleichen

**Komplexität:** K2 · **Status:** Restabnahme offen · **Priorität:** P1

**Lokaler Prüfstand (26.11.2026):** Windows-ZIP und Linux-Tarball wurden lokal gehasht und ihr BuildMetadata/Commit mit Bericht 43692056280/1 verglichen. Beide enthalten Build-ID 2026.1.0-beta.1+67e570bf2940 und Lizenzdateien; der Windows-Hash stimmt mit der Prüfsummendatei überein. Das eingebettete Linux-Metadatum nennt fälschlich Windows-Dateien. Der lokale Metadatengenerator verwendet jetzt den POSIX-Archivnamen und lässt den Verweis auf das dort fehlende Symbolarchiv leer. 37 Release- und Pakettests bestanden. Es fehlen ein vollständiges Inventar aller drei Installer einschließlich macOS sowie überprüfbare Symbolarchive und der Backupnachweis.

Für v2026.1.0-beta.1 existiert ein unveröffentlichter Vorbereitungsentwurf. Für genau diese Vorschau gilt die dokumentierte Ausnahme für unsignierte Installer. [Releaseablauf](Documentation/Editor/Release-Operations.md).

- [ ] Für den gewählten Tag die drei Installer, Build-IDs, Lizenzen, SHA-256, Gesamtbericht und privaten Symbolsatz demselben Commit und CI-Versuch zuordnen. Symbolbackup prüfen.
- [ ] Entwurf und Release Notes mit dem finalen Inventar, Signaturstatus sowie offenen manuellen Installationsprüfungen abgleichen. Die öffentliche Downloadabnahme folgt in [QAL-031](#task-qal-031).

<a id="task-qal-030"></a>

### QAL-030 – Unabhängigen Nutzungsbericht für die geprüften Referenzprojekte einholen

**Komplexität:** K2 · **Status:** offen · **Priorität:** P2 · **Art:** Prüfauftrag · **Belegstatus:** vorgeschlagen

**Lokaler Prüfstand (26.11.2026):** Zwei versionierte Prüfartefakte (FunkeBench 1.0.0 und der lokale Rust-&-Steel-Stand) wurden mit Hashes und [Anleitung für unabhängige Tester](Documentation/Editor/Testing.md#reference-project-acceptance-on-windows) festgehalten. Beide liefen im Editor und im Windows-Export. Im Export wurden 10.000 Entitäten bzw. New factory bis zur Freeplay-HUD geprüft. Der zweite Teststand ist lokal; die Übergabe an einen unabhängigen Tester und dessen eigener Bericht stehen aus.

**Verbleibende Abnahme:**

- [ ] Den zweiten Prüfstand zusammen mit der dokumentierten Prüfsumme bereitstellen, sobald eine Übergabe beauftragt ist.
- [ ] Einen unabhängigen Lauf mit Start, Play, Export, Aufwand und auftretenden Fehlern dokumentieren. Eigene Läufe und unabhängige Beobachtungen getrennt ausweisen.


<a id="task-qal-031"></a>

### QAL-031 – Vorbereitete Desktop-Beta veröffentlichen und den echten Downloadweg prüfen

**Komplexität:** K2 · **Status:** offen · **Priorität:** P1 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Lokaler Prüfstand (26.11.2026):** Der lokale Abgleich der beiden verfügbaren Kandidaten ist bei QAL-011 dokumentiert. Die Freigabe eines vollständigen Releasepakets, Veröffentlichung und Prüfung des öffentlichen Downloads stehen aus.

**Abhängigkeiten:** [QAL-011](#task-qal-011)

**Voraussetzungen:** Vorbereiteter Release aus QAL-011 und saubere Installation für die öffentliche Downloadprüfung.

**Ausgangslage:** Die erste Beta liegt als Vorbereitungsentwurf vor. Der öffentliche Download und die Installation aus dem finalen Release stehen aus. [Releaseablauf](Documentation/Editor/Release-Operations.md).

**Dateien und Einstiegspunkte:**

- `Funke:.github/workflows/ci.yml`
- `Funke:README.md`
- `Funke:CHANGELOG.md`
- `Funke:Github-Docs/Documentation/Getting-Started.md`

**Anweisungen:**

1. Führe diesen Task aus, wenn die Veröffentlichung Bestandteil des aktuellen Nutzerauftrags ist; verwende ausschließlich das bestandene Releasepaket aus QAL-011.
2. Erzeuge den vorgesehenen Tag/Release nach dem bestehenden Repositoryworkflow und ordne alle Assets exakt dem geprüften Commit zu.
3. Prüfe nach dem Upload Dateinamen, Plattform-/Architekturzuordnung, Prüfsummen und den Zugriff über die tatsächlichen öffentlichen Downloadlinks.
4. Teste den dokumentierten Einstieg aus diesen Downloads und korrigiere Release Notes/README bei einem abweichenden Ziel oder fehlenden Artefakt.
5. Dokumentiere veröffentlichte Version, Commit, Assets und die tatsächlich bestandenen Nachprüfungen; ein fehlgeschlagener Upload wird nicht als Veröffentlichung ausgegeben.

**Abnahme:**

- [ ] Der in der README genannte Downloadweg liefert die passende getestete Version.
- [ ] Veröffentlichte Asset-Hashes entsprechen dem vorbereiteten Releasepaket.
- [ ] Ein veröffentlichter Stand wird nicht nachträglich stillschweigend durch andere Binaries ersetzt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/releases) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/.github/workflows/ci.yml) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/README.md)

## Aufträge mit Komplexität K3

<a id="task-edt-010"></a>

### EDT-010 – Nativen SDK-, Inspector- und Exportablauf auf sauberer Installation abnehmen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** prüfen

**Voraussetzungen:** Passende Desktop-SDKs, Compiler und Editor-Binaries.

**Ausgangslage:** Windows-/Linux-Prüfungen und ihre SDK-Stände sind [dokumentiert](Documentation/Scripting/Native-Scripting-Verification.md). Auf macOS die erweiterte Header-Matrix, PCH an/aus, Compilerfehler und Wiederherstellung abnehmen. Im Editor ein registriertes Feld ändern, speichern und erneut öffnen. Den nativen Development-Export verschieben und mit gebündelter .NET-Laufzeit auf Feldwert, Eingabe, Destroy, Exitcode und ABI-Diagnosen prüfen. [CI-Nachweis für 67e570bf](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Editor/src/Gui/AssetBrowserActions.cpp`
- `Funke:Funke-Engine/src/Scripting/ScriptSystem.cpp`
- `Funke:Funke-Engine/src/Scripting/NativeScriptHost.cpp`
- `Funke:Tests/Funke-Engine-Tests/src/EditorScriptPlatformTests.cpp`

**Anweisungen:**

1. Nutze die vorhandenen Installer/SDKs und erstelle pro Desktopplattform ein minimales natives Skript ausschließlich über den vorgesehenen Editorworkflow.
2. Prüfe Compilerentdeckung, CMake-Generator, Quellverzeichnis, Inspectorfelder, erste Kompilierung und inkrementellen Reload.
3. Prüfe einen absichtlichen Compilerfehler, anschließend Korrektur und Wiederaufnahme, ohne Projektdateien von Hand zu reparieren.
4. Ordne jeden Fehler konkret Toolchain, SDK-Bundle oder Engine zu und ergänze nur dafür einen passenden Gate-/Regressionstest.
5. Verwende die Installations-/Relocation-Infrastruktur aus QAL-009 mit; dieser Task besitzt die Native-Script-Details, QAL-009 den vollständigen Installationsablauf.

**Abnahme:**

- [ ] Für jede geprüfte Plattform liegen Toolchainversionen, Befehle und Ergebnisartefakte vor.
- [ ] Die korrigierte Skriptänderung erreicht Editor und exportiertes Spiel; verbleibende Plattformblocker sind explizit benannt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Editor/src/Gui/AssetBrowserActions.cpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Scripting/ScriptSystem.cpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Scripting/NativeScriptHost.cpp) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Tests/Funke-Engine-Tests/src/EditorScriptPlatformTests.cpp)

<a id="task-edt-012"></a>

### EDT-012 – Transaktionsgrenzen beim Managed-Assembly-Wechsel überprüfen

**Komplexität:** K3 · **Status:** teilweise erledigt; Restarbeit offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** prüfen

**Verbleibende Arbeit und Abnahme:**

- [ ] Native Drain-/Teardown-/Recreate-Sequenz beim Managed-Assembly-Wechsel mit Fehlern und erfolgreichem Retry im vollständigen Host prüfen. Managed Registrierung sowie fehlschlagendes OnStart/OnDestroy über echte unmanaged Einstiegspunkte sind getestet; dies ersetzt den nativen Gesamtablauf nicht.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-ScriptCore/Source/Funke/Interop/ScriptInstanceManager.cs`
- `Funke:Funke-Engine/src/Scripting/ScriptEngine.cpp`
- `Funke:Funke-Engine/src/Scripting/ScriptSystem.cpp`
- `Funke:Tests/Funke-ScriptCore-Tests/ScriptAssemblyReloadTests.cs`

<a id="task-edt-021"></a>

### EDT-021 – Gestückelte Textur-Publikation auf echter GPU abnehmen

**Komplexität:** K3 · **Status:** implementiert; Restabnahme offen · **Priorität:** P1

**Dauerhafter Vertrag und gezielte Tests:** [Textur-Publikation](Documentation/Engine-Core/Cooperative-Loading.md#texture-publication). Die Implementierung und Prüfgrenzen stehen in der Engine-Dokumentation; sie müssen nicht erneut entwickelt werden.

**Verbleibende Arbeit und Abnahme:**

- [ ] Allokation, begrenzte Transfers, Mips und Veröffentlichung auf echter GPU vermessen; verbleibende maximale Hauptthreadblöcke an [PRF-003](#task-prf-003) liefern.
- [ ] Projektwechsel während eines Teiltransfers und echte Geräte-/Treiberfehler prüfen: keine veröffentlichten Teilressourcen, Ressourcenfreigabe und bestehende gültige Cacheeinträge erhalten.
- [ ] Sichtbare große Textur mit/ohne Mips sowie Sampler-/Metadatenwechsel im neu gebauten Runtime-Pfad prüfen. Null-Validierung ersetzt weder GPU-Inhalte noch Treibermessungen.

**Früherer Prüfstand:** [Build- und Backendgrenzen des Laufs vom 14. November](Documentation/Editor/Verified-Contracts-2026-11.md). Für die Restprüfung passende Build- und Geräteidentität festhalten.

<a id="task-edt-041"></a>

### EDT-041 – Drag-and-drop von Assets zu externen Anwendungen auf Linux und macOS ergänzen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Voraussetzungen:** Echte Desktopumgebungen/Dateimanager unter Linux und macOS erforderlich; Wayland/X11 unterscheiden und separat protokollieren.

**Ausgangslage:** Laut Known Limitations funktioniert das Ziehen aus dem Asset Browser zu anderen Anwendungen nur auf Windows.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Editor/src/Gui/AssetBrowserActions.cpp`
- `Funke:Funke-Engine/src/Core/Platform.hpp`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`

**Anweisungen:**

1. Verfolge den bestehenden Windows-Dragout-Pfad und definiere im Plattformlayer ein gemeinsames Dateiliste-/URI-Vertragsformat.
2. Implementiere den passenden nativen beziehungsweise SDL-gestützten Dragout-Pfad für die tatsächlich unterstützten Desktopumgebungen.
3. Behandle Unicode, Leerzeichen, mehrere Dateien, abgebrochenen Drag und nicht mehr vorhandene Dateien.
4. Erhalte internes Entity-/Asset-Drag-and-drop und verhindere versehentliches Verschieben der Projektquellen.
5. Prüfe Übergabe an Finder beziehungsweise einen passenden Linux-Dateimanager und eine zweite externe Anwendung.

**Abnahme:**

- [ ] Externe Empfänger erhalten gültige Dateiverweise ohne kaputte Kodierung.
- [ ] Abbruch verändert keine Assets.
- [ ] Nicht unterstützte Desktopvarianten werden korrekt ausgewiesen.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Editor/src/Gui/AssetBrowserActions.cpp)

<a id="task-edt-043"></a>

### EDT-043 – Glyphen-Vorwärmung und Erstbenutzung auf der GPU messen

**Komplexität:** K3 · **Status:** teilweise erledigt; Restarbeit offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** PRF-001 (erledigt; siehe Vertragsdokumentation)

**Verbleibende Arbeit und Abnahme:**

- [ ] Vollständigen Engine-Link, GPU-Ersttext-Spike vorher/nachher sowie reale Cache-, Fallback- und Sprachwechsel prüfen.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Text/Font.cpp`
- `Funke:Funke-Engine/src/Graphics/Text/TextRenderer.cpp`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`

<a id="task-gfx-002"></a>

### GFX-002 – AudioMixer: laufende OneShots auf Gruppenänderungen reagieren lassen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Dokumentierte Grenze · **Belegstatus:** bestätigt

**Ausgangslage:** PlayOneShot multipliziert den Gruppenpegel ausschließlich beim Start. Bereits laufende OneShots sind nicht in s_Outputs registriert und werden durch spätere Gruppenänderungen nicht nachgeführt.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.AudioMixer/Source/AudioMixer.cs`
- `Funke:Funke-Engine/src/Audio/AudioManager.hpp`
- `Funke:Funke-Engine/src/Audio/AudioManager.cpp`

**Anweisungen:**

1. Definiere einen generationsgesicherten Playback-Handle oder eine native Buszuordnung für OneShots; trenne Quellen-, Gruppen- und Masterlautstärke.
2. Erweitere native und verwaltete Schnittstellen so, dass laufende OneShots aktualisiert werden können, ohne wiederverwendete Voices zu treffen.
3. Führe Volume, Mute, Parent-Gruppen und Snapshot-Fades auf aktive OneShots nach.
4. Entferne beendete, gestoppte oder entladene Voices zuverlässig aus der Zuordnung und wahre den Main-Thread-Vertrag.
5. Prüfe lange OneShots, kurzlebige Voice-Recycling-Fälle und Play/Stop im Editor.

**Abnahme:**

- [ ] Ein bereits laufender langer SFX-OneShot reagiert sofort auf Mute und auf Fades.
- [ ] Alte Handles verändern nach Voice-Recycling keinen neuen Sound.
- [ ] Quellenpegel wird genau einmal mit Gruppen- und Masterpegel kombiniert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.AudioMixer/Source/AudioMixer.cs#L163-L168) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Audio/AudioManager.hpp)

<a id="task-gfx-006"></a>

### GFX-006 – Tilemap-Pinsel mit Löschen und zusammenhängendem Undo-Schritt bauen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Tilemap2D unterstützt bereits Rendering, Inspector, native Speicherung und C#/C++-APIs. Der README zufolge erfolgt Painting nur per API; ein Editor-Pinsel fehlt.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Tilemap2D/README.md`
- `Funke-PackageRegistry:packages/Funke.Tilemap2D/src/PackageEntry.cpp`
- `Funke-PackageRegistry:packages/Funke.Tilemap2D/csharp/Tilemap2D.cs`

**Anweisungen:**

1. Ergänze eine Tileset-Auswahl und einen Viewport-Pinsel für Einzelzelle, Radieren und Rechteckfüllung.
2. Nutze die bestehende WorldToCell-/Chunk-API und berücksichtige Transform, Anker, Zellabstand sowie negative Koordinaten.
3. Fasse einen Mausstrich in einen Undo-Schritt zusammen; speichere nur veränderte Zellen und deren vorherige Werte.
4. Integriere Scene-Dirty, Prefab-Editing und bestehende Editor-Shortcuts ohne einen zweiten Tile-Speicher anzulegen.
5. Prüfe Paint/Erase, Undo/Redo, Save/Load und Play/Stop an rotierten Tilemaps.

**Abnahme:**

- [ ] Tilemaps lassen sich ohne eigenen Spielcode im Editor bemalen.
- [ ] Ein Strich lässt sich vollständig rückgängig machen und wiederholen.
- [ ] Gespeicherte Tiles und Prefabzustände bleiben nach Reload/Play-Stop erhalten.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Tilemap2D/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Tilemap2D/src/PackageEntry.cpp)

<a id="task-gfx-009"></a>

### GFX-009 – Fullscreen-Lichtschleifen auf Zielgeräten vermessen und den größten Engpass reduzieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** bestätigt

**Voraussetzungen:** Zusätzlich Gerätebereitstellung, native/mobile Builds und Benchmark-Läufe; ohne Gerätezugriff bleibt die betreffende Plattform offen.

**Ausgangslage:** Der Fragmentshader läuft über sichtbare Lichter und für Schatten über Occluder. Frühe Distanz-/Opazitätsabbrüche existieren; die README nennt Zielgeräteperformance ausdrücklich ungemessen.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Lighting2D/src/LightingShader.hpp`
- `Funke-PackageRegistry:packages/Funke.Lighting2D/Examples/create_showcase.py`
- `Funke-PackageRegistry:packages/Funke.Lighting2D/Tests/verify_screenshots.py`

**Anweisungen:**

1. Erweitere die vorhandene Showcase-Szene um Auflösung, Licht-/Occluderzahl und Schattenüberlappung als Parameter.
2. Erfasse GPU-Zeit für 0/1/8/32 Lichter und 0/8/64 Occluder auf mindestens einem Desktop und einem verfügbaren schwächeren Zielgerät.
3. Wähle anhand der Messung genau eine begrenzte Verbesserung wie bessere Lichtrechtecke, Occluder-Vorauswahl oder niedrigere Beleuchtungsauflösung.
4. Vergleiche Bilder und Framezeit bei derselben Last; kennzeichne nicht verfügbare Geräte als ungeprüft.

**Abnahme:**

- [ ] Ergebnisse enthalten Hardware, Treiber/API, Auflösung, Commit und Warmup.
- [ ] Die Verbesserung erhält Schatten- und UI-Ergebnis innerhalb einer beschriebenen Toleranz.
- [ ] Mobile/Web werden nur als geprüft markiert, wenn dort tatsächlich ein Lauf stattfand.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/src/LightingShader.hpp#L21-L59) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/README.md)

<a id="task-gfx-010"></a>

### GFX-010 – Polygonale 2D-Schattenwerfer ergänzen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Der bestehende Occluder ist rechteckig. Freie Polygone sind ausdrücklich nicht unterstützt.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Lighting2D/include/Lighting2DComponents.hpp`
- `Funke-PackageRegistry:packages/Funke.Lighting2D/src/LightingShader.hpp`
- `Funke-PackageRegistry:packages/Funke.Lighting2D/src/PackageEntry.cpp`

**Anweisungen:**

1. Definiere einen versionierten Polygon-Occluder mit begrenzter Punktzahl und einem klaren Vertrag für konkave oder selbstschneidende Polygone.
2. Ergänze Datenvalidierung, Serialisierung, Inspector, Gizmo und C#-Binding.
3. Implementiere Segment-/Polygon-Schatten im bestehenden Beleuchtungsraum und beachte Rotation, negative Skalierung und Licht innerhalb des Polygons.
4. Ergänze visuelle Fälle für konvexe und unterstützte konkave Formen sowie fehlerhafte Eingaben.

**Abnahme:**

- [ ] Unterstützte Polygone erzeugen reproduzierbare passende Schatten.
- [ ] Nicht unterstützte Polygonformen führen zu einer Diagnose statt zu Absturz oder unklarer Darstellung.
- [ ] Alte rechteckige Occluder bleiben lesbar und funktionsfähig.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/src/LightingShader.hpp)

<a id="task-gfx-011"></a>

### GFX-011 – Weiche Schatten als optionale Qualitätsstufe implementieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [GFX-009](#task-gfx-009)

**Ausgangslage:** Lighting2D bietet harte Schatten. Eine weiche Variante wäre eine Erweiterung; der bestehende schnelle Pfad sollte erhalten bleiben.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Lighting2D/src/LightingShader.hpp`
- `Funke-PackageRegistry:packages/Funke.Lighting2D/Source/Lighting2D.cs`

**Anweisungen:**

1. Definiere Lichtquellengröße, Qualitätsstufen und ein begrenztes Samplingbudget.
2. Implementiere eine optionale Softshadow-Approximation mit stabiler räumlicher Abtastung.
3. Behalte Hard-Shadows als abschaltbaren Referenz- und Low-End-Pfad.
4. Miss zusätzliche GPU-Kosten und prüfe Bewegung, Kontaktschatten sowie Halbtransparenz.

**Abnahme:**

- [ ] Qualitätsstufen bewirken nachvollziehbare Bild-/Leistungsunterschiede.
- [ ] Kein temporales Flackern in einer definierten Kamerafahrt.
- [ ] Hardshadow-Ausgaben bleiben bei ausgeschalteter Erweiterung unverändert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/src/LightingShader.hpp)

<a id="task-gfx-012"></a>

### GFX-012 – Normalmaps in den 2D-Beleuchtungspfad integrieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Voraussetzungen:** Passende Renderer- und Paketbuilds für GPU-Vergleiche auf den Zielplattformen.

**Ausgangslage:** Der aktuelle Effekt beleuchtet ein bereits zusammengesetztes Weltbild auf einer Ebene; pro Sprite stehen dort keine Oberflächennormalen zur Verfügung. Normalmaps erfordern deshalb zusätzliche Renderdaten und sind kein kleiner Shaderpatch.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Lighting2D/src/LightingShader.hpp`
- `Funke:Funke-Engine/src/Graphics/Renderer2D.cpp`
- `Funke:Funke-Engine/src/Graphics/Material.hpp`

**Anweisungen:**

1. Lege Normalmap-Konvention, Fallback-Normale, Licht-Höhe und Verhalten bei Sprite-Flips/Rotation/Skalierung fest.
2. Erweitere Sprite-Materialien und Renderattachments um die notwendigen Normaldaten, ohne Screen-Space-UI zu beleuchten.
3. Passe den Lighting2D-Pass und Paket-/ABI-Vertrag an.
4. Prüfe Atlasslices, halbtransparente Sprites, überlappende Ebenen und fehlende Normalmaps.
5. Miss Speicherbandbreite und GPU-Kosten gegen den bisherigen Farbpass.

**Abnahme:**

- [ ] Ein Test-Sprite reagiert abhängig von seinen Normalen korrekt auf die Lichtposition.
- [ ] Rotation/Flip des Sprites drehen die Normalen konsistent.
- [ ] Bestehende Projekte ohne Normalmap behalten einen definierten kompatiblen Fallback.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/src/LightingShader.hpp)

<a id="task-gfx-013"></a>

### GFX-013 – Lichtmasken und selbstleuchtende Ausschlüsse für 2D einführen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [GFX-012](#task-gfx-012)

**Ausgangslage:** Die Beleuchtung wirkt auf das kombinierte Weltbild. Per-Layer-Masks und Emissive-Ausschlüsse fehlen ausdrücklich; aus der fertigen Farbe lassen sie sich nicht zuverlässig rekonstruieren.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Lighting2D/src/LightingShader.hpp`
- `Funke-PackageRegistry:packages/Funke.Lighting2D/include/Lighting2DComponents.hpp`
- `Funke:Funke-Engine/src/Graphics/Renderer2D.cpp`

**Anweisungen:**

1. Definiere Light-Layer, Receiver-Masken und einen Emissive-/Unlit-Vertrag für Sprites, Terrain, Text und Partikel.
2. Transportiere die nötigen Masken bzw. getrennten Beiträge durch den Renderer bis zum Lichtpass.
3. Ergänze Inspector, Serialisierung und C#/C++-Schnittstellen.
4. Prüfe sich überlappende Empfänger mit verschiedenen Masken, UI und halbtransparente Materialien.
5. Dokumentiere die Interaktion mit zusätzlichen Szenen und optionalen Normalmaps.

**Abnahme:**

- [ ] Ein Licht beeinflusst ausschließlich die vorgesehenen Empfänger.
- [ ] Emissive-/Unlit-Inhalte bleiben trotz dunklem Ambient sichtbar.
- [ ] Keine Änderung allein aufgrund der Renderreihenfolge gleichartiger transparenter Empfänger.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Lighting2D/src/LightingShader.hpp)

<a id="task-gfx-024"></a>

### GFX-024 – Shader-/Pipeline-Warmup vor dem ersten sichtbaren Einsatz ermöglichen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Shaders werden beim ersten Gebrauch auf der GPU kompiliert; die Dokumentation weist auf mögliche Hitches hin. Last-Good-Hotreload ist bereits implementiert und soll erhalten bleiben.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/CustomShaderLibrary.cpp`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Github-Docs/Documentation/Engine-Core/Custom-Shaders.md`

**Anweisungen:**

1. Definiere eine Warmup-API für referenzierte Materialien und die tatsächlich benötigten Pipelinevarianten.
2. Ermittle Build-/Scene-Abhängigkeiten und führe Warmup in einem ausdrücklich sichtbaren Ladeabschnitt durch.
3. Melde Fortschritt/Fehler und verhindere doppelte Kompilierungen.
4. Cache nur zulässige Ergebnisse mit passender Geräte-/Treiber-/Shader-/Pipelineidentität; keine portable Binärcache-Garantie erfinden.
5. Messe Erstframe-Spitzen vor/nach Warmup und prüfe fehlerhafte Shader sowie Hotreload.

**Abnahme:**

- [ ] Die vorbereiteten Varianten verursachen beim ersten sichtbaren Draw keinen erneuten Compilepfad.
- [ ] Fehler behalten die letzte funktionierende Fassung oder einen klaren Fallback.
- [ ] Warmup-Kosten werden ausgewiesen und nicht als verschwunden dargestellt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Custom-Shaders.md#L275-L289) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Custom-Shaders.md#L317-L330) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/CustomShaderLibrary.cpp)

<a id="task-gfx-028"></a>

### GFX-028 – Weitere Box2D-Jointtypen als klar abgegrenzte Komponenten exponieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Revolute, Prismatic und Weld sind bereits vorhanden. Andere Jointtypen werden nicht über die eingebauten Komponenten angeboten.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Physics/Joints2D.cpp`
- `Funke:Funke-Engine/src/Components/Physics/Joint2DComponents.hpp`
- `Funke:Funke-Engine/src/Scene/BuiltInComponentRegistrationPhysics.cpp`

**Anweisungen:**

1. Wähle für den ersten Ausbau Distance- und Wheel-Joint aus der tatsächlich eingebundenen Box2D-Version.
2. Ergänze Komponente, Lifecycle, native Bindings, C#-API, Inspector und versionierte Serialisierung.
3. Behandle fehlende/zerstörte Connected Bodies und Body-Rebuilds im selben Vertrag wie bestehende Joints.
4. Expose nur unterstützte Motor-/Limit-/Springparameter mit eindeutigen Einheiten.
5. Ergänze dynamische Verhaltenstests für Pendel/Seilabstand und Federung.

**Abnahme:**

- [ ] Die ausgewählten Jointtypen funktionieren nach Spawn, Save/Load und Body-Rebuild.
- [ ] Destroy eines beteiligten Bodys hinterlässt keinen ungültigen Joint.
- [ ] Weitere weiterhin fehlende Jointtypen werden konkret dokumentiert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Physics-2D.md#L39-L72) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Physics/Joints2D.cpp)

<a id="task-gfx-032"></a>

### GFX-032 – Fixed-, Hinge- und Distance-Joints für 3D exponieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Die 3D-Integration stellt bislang keine Joints bereit, obwohl der verwendete Physikbackend entsprechende Funktionen besitzt.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Physics3D/PhysicsWorld3D.cpp`
- `Funke:Funke-Engine/src/Scene/BuiltInComponentRegistrationPhysics3D.cpp`
- `Funke:Funke-Engine/src/Scripting/ScriptBindingsPhysics3D.cpp`

**Anweisungen:**

1. Lege zunächst drei gemeinsame Jointkomponenten samt ConnectedBody-/Anchor-Vertrag fest.
2. Erzeuge und entferne die Jolt-Constraints im sicheren Physik-Lifecycle.
3. Ergänze Inspector, Binding, Serialisierung und Debug-Gizmos einschließlich eindeutiger Winkel-/Längeneinheiten.
4. Behandle Body-Rebuild, fehlende Partner, Szenenwechsel und Play/Stop.
5. Teste Türscharnier, starre Verbindung und Distanzbegrenzung physikalisch.

**Abnahme:**

- [ ] Alle drei Joints reagieren nach Save/Load und Body-Rebuild korrekt.
- [ ] Ungültige Partner verursachen Diagnose statt Crash.
- [ ] Break-/Motor-/Ragdollfunktionen werden nur dann als vorhanden markiert, wenn sie tatsächlich umgesetzt wurden.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Physics-3D.md#L98-L101) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Physics3D/PhysicsWorld3D.cpp)

<a id="task-gfx-035"></a>

### GFX-035 – 1D-Blendtrees für Animationen implementieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Animator-State-Machines und Crossfades sind vorhanden, Blendtrees fehlen. Ein erster Task sollte auf 1D-Blending beschränkt bleiben, statt ein komplettes Animationssystem neu zu entwerfen.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Animation/AnimatorController.cpp`
- `Funke:Funke-Engine/src/Animation/AnimatorRuntime.cpp`
- `Funke:Funke-Editor/src/Gui/AnimationWindow.cpp`

**Anweisungen:**

1. Definiere einen 1D-Blendtree-Knoten mit Parameter, geordneten Thresholds und Clipreferenzen.
2. Implementiere deterministische Gewichtung, Zeit-/Loop-Synchronisierung und Umgang mit fehlenden Clips.
3. Erweitere Controllerformat, Validator, Editorvorschau und C#-Parametersteuerung.
4. Prüfe Idle/Walk/Run, identische Thresholds, Grenzwerte und Crossfade in einen bzw. aus einem Blendtree.
5. Dokumentiere 2D-Blendtrees als separaten noch offenen Ausbau.

**Abnahme:**

- [ ] Speedänderungen mischen die definierten Clips kontinuierlich.
- [ ] Save/Load und Hotreload erhalten Thresholds und Referenzen.
- [ ] Fehlerhafte Trees werden klar diagnostiziert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Animation.md#L211-L215) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Animation/AnimatorController.cpp)

<a id="task-gfx-040"></a>

### GFX-040 – Beleuchtete und depth-weiche 3D-Billboard-Partikel ergänzen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** 3D-Partikel sind derzeit unbeleuchtete Billboards. Lit- und Soft-Depth-Fade-Modi fehlen ausdrücklich.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Funke-Engine/src/Components/Graphics/ParticleSystem3DComponent.hpp`
- `Funke:Funke-Engine/src/Graphics/Shader.cpp`

**Anweisungen:**

1. Ergänze separate optionale Einstellungen für Lichtempfang und Soft-Particle-Distanz.
2. Stelle den Partikelshadern die nötigen Lichtdaten und lesbaren Scene-Depth bereit.
3. Behandle MSAA-Depth, RenderScale und 2D-/UI-Komposition korrekt.
4. Prüfe Boden-/Wanddurchdringung, nahe Kamera und halbtransparente Partikel.
5. Miss zusätzliche GPU-Kosten und erhalte den bisherigen Unlit-Pfad.

**Abnahme:**

- [ ] Soft-Partikel blenden an Opaque-Geometrie weich aus.
- [ ] Lit-Partikel reagieren auf die dokumentierten Lichttypen.
- [ ] Unlit ohne Soft Fade bleibt optisch und leistungsseitig nahe dem bisherigen Pfad.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L179-L179) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L270) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Renderer3D.cpp#L2773-L2834)

<a id="task-gfx-041"></a>

### GFX-041 – Stretched-Billboards und Mesh-Partikel als explizite Rendermodi ergänzen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Partikel werden kameraorientiert als Billboards gezeichnet; Stretched- und Mesh-Partikel sind nicht vorhanden.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Components/Graphics/ParticleSystem3DComponent.hpp`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`

**Anweisungen:**

1. Füge einen versionierten RenderMode sowie Stretch-/Meshasset-Felder hinzu.
2. Implementiere geschwindigkeitsorientierte Billboards mit stabilem Verhalten bei Geschwindigkeit null.
3. Implementiere instanziertes Mesh-Rendering mit begrenztem Material-/Drawbudget.
4. Passe Culling-Bounds, Shadow-/Lighting-Vertrag und Alpha-/Additive-Sortierung an.
5. Prüfe drehende Kamera, schnelle Partikel und unterschiedliche Meshgrößen.

**Abnahme:**

- [ ] Die drei Rendermodi sind klar getrennt und serialisieren korrekt.
- [ ] Partikel verschwinden bei Stretch/Rotation nicht durch zu kleine Bounds.
- [ ] Alte Partikelsysteme behalten Billboard als Default.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Components/Graphics/ParticleSystem3DComponent.hpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L270)

<a id="task-gfx-048"></a>

### GFX-048 – glTF-Szenenstruktur, Kameras und unterstützte Lichter optional als Prefab importieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [GFX-017 (erledigt)](Documentation/Engine-Core/3D-Asset-Workflow-Verification.md)

**Voraussetzungen:** Zusätzlich native Importtests und Editor-/Runtime-Smokes.

**Ausgangslage:** Der aktuelle Meshimport führt glTF-Geometrie zusammen und ignoriert Kameras/Lichter. Ein separater vollständiger Szenen-/Prefabimport ist dadurch nicht belegt.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/MeshAsset.cpp`
- `Funke:Github-Docs/Documentation/Engine-Core/3D.md`

**Anweisungen:**

1. Trenne den bisherigen Meshasset-Import vom optionalen Scene-/Prefabimport.
2. Erhalte Nodehierarchie und lokale Transformwerte sowie wiederverwendete Mesh-/Materialreferenzen.
3. Importiere orthografische/perspektivische Kameras und eine klar benannte unterstützte Light-Extension.
4. Verhindere, dass importierte Kameras ungefragt die aktive Spielkamera ersetzen.
5. Prüfe verschachtelte Nodes, mehrere Instanzen eines Meshes und nicht unterstützte Extensions.

**Abnahme:**

- [ ] Das importierte Prefab erhält die dokumentierte glTF-Hierarchie und Transformationen.
- [ ] Kameras und unterstützte Lichter können anschließend gezielt aktiviert werden.
- [ ] Der bestehende einfache Meshimport bleibt verfügbar.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L61-L65) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/MeshAsset.cpp)

<a id="task-gfx-050"></a>

### GFX-050 – Picking bei Vertex-Displacement präzisieren und die Bounds-Approximation sichtbar machen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-006](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Voraussetzungen:** Zusätzlich GPU-/Editor-Bildtests und native Buildzeit.

**Ausgangslage:** Editor-Picking führt den Vertexshader nicht aus, sondern erweitert Meshbounds anhand @displacement. Ohne deklarierte Displacementgrenze werden entsprechend veränderte Meshes nicht gecullt.

**Dateien und Einstiegspunkte:**

- `Funke:Github-Docs/Documentation/Engine-Core/Custom-Shaders.md`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Funke-Editor/src`

**Anweisungen:**

1. Erstelle ein Mesh mit deutlich bewegter Shaderoberfläche und dokumentiere den heutigen Unterschied zwischen sichtbarer Oberfläche und CPU-Picking.
2. Biete für betroffene Materialien einen GPU-ID-/Depth-Pickingpfad an, der denselben Vertex- und gegebenenfalls Alpha-Test ausführt.
3. Erhalte Bounds-Picking als günstigen Fallback und kennzeichne dessen Approximationsgrenze.
4. Diagnostiziere fehlende oder offensichtlich zu kleine @displacement-Angaben ohne unbekannte Shaderbewegung als sicher begrenzt zu behaupten.
5. Prüfe Pixel an ursprünglicher und verschobener Oberfläche, deformierte Schatten sowie Viewportskalierung.

**Abnahme:**

- [ ] Der neue Pfad wählt im Referenzfall die tatsächlich sichtbare deformierte Oberfläche.
- [ ] GPU-Readback blockiert nicht unbeschränkt den Editor.
- [ ] Fallback und Cullingvertrag bleiben ausdrücklich dokumentiert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Custom-Shaders.md#L159-L176) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Custom-Shaders.md#L326-L330)

<a id="task-gfx-051"></a>

### GFX-051 – Vier feste Custom-Effect-Slots zu einer validierten geordneten Effektliste erweitern

**Komplexität:** K3 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** bestätigt

**Abhängigkeiten:** [QAL-006](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Voraussetzungen:** Zusätzlich native/managed Builds und gezielte GPU-Regression.

**Ausgangslage:** PostProcessing2DComponent speichert CustomEffects als std::array<uint64_t,4>. Das begrenzt die Kette benutzerdefinierter Posteffekte auf vier Slots.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Components/Graphics/PostProcessing2DComponent.hpp`
- `Funke:Funke-Engine/src/Graphics/PostProcessing/PostProcessor.cpp`
- `Funke:Funke-ScriptCore/Source/Funke/Graphics/PostProcessing2D.cs`

**Anweisungen:**

1. Definiere eine geordnete versionierte Liste mit einem sinnvollen konfigurierten Sicherheitsbudget statt einer unbegrenzten Kette.
2. Migriere alte Vier-Slot-Daten kompatibel und aktualisiere native/verwaltete Interoprepräsentation.
3. Ergänze Editor-Reorder, Add/Remove, Aktivierung und klare Fehlermeldungen für falsche Shadertargets.
4. Plane Ping-Pong-Targets wiederverwendbar und verhindere Lesen und Schreiben derselben Texture im selben Pass.
5. Teste 0, 1, 4, 5 und Budget+1 Effekte sowie zwei Kameraviews.

**Abnahme:**

- [ ] Fünf unterstützte Effekte laufen nachweislich in der angegebenen Reihenfolge.
- [ ] Alte Szenen rendern nach Migration identisch.
- [ ] Ungültige/zu große Ketten werden kontrolliert behandelt und nicht still abgeschnitten.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Components/Graphics/PostProcessing2DComponent.hpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/PostProcessing/PostProcessor.cpp)

<a id="task-inp-007"></a>

### INP-007 – Stabile Controllerzuordnung und Join-on-Press ergänzen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Voraussetzungen:** Reale Controller für Hotplug und Mehrspielerzuordnung.

**Ausgangslage:** Das Paket verwendet Slots 0–3; stabile Geräteidentitäten und Join-on-Press fehlen laut README.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Core/Input.cpp`
- `Funke:Funke-Engine/src/Core/Input.hpp`
- `Funke-PackageRegistry:packages/Funke.Input/Source/InputActionMap.cs`

**Anweisungen:**

1. Prüfe, welche SDL-Gerätekennungen pro Lauf beziehungsweise über Neustarts tatsächlich stabil sind; behaupte keine dauerhafte Identität allein anhand gleicher Modellnamen.
2. Ergänze eine Player-/Device-Zuordnung oberhalb der vorhandenen Slots und definierte Join/Leave-Ereignisse.
3. Behandle Abziehen, Wiederanstecken, zwei gleiche Controller und Wechsel des primären Eingabegeräts.
4. Binde Rebindingprofile an Spieler/Profile; vermische keine Bindings verschiedener lokaler Spieler.
5. Prüfe den Ablauf mit echten Geräten und simulierbaren Hotplug-Ereignissen.

**Abnahme:**

- [ ] Ein Controllerwechsel verschiebt nicht unbemerkt die Steuerung zwischen Spielern.
- [ ] Hotplug hinterlässt keine dauerhaft gedrückten Actions.
- [ ] Fehlende physische Geräte werden als ausstehender Praxistest dokumentiert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Input/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Input/Source/InputActionMap.cs)

<a id="task-inp-009"></a>

### INP-009 – Visuellen Action-Map-Editor mit Rebindingvorschau bauen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** INP-002 (erledigt; siehe Vertragsdokumentation), INP-008 (erledigt; siehe Vertragsdokumentation)

**Voraussetzungen:** Editorbuild und tatsächliche Bedienprüfung zusätzlich; neue Fenster nicht nur per Unit-Test als fertig erklären.

**Ausgangslage:** Die fehlende visuelle Bearbeitung ist ausdrücklich dokumentiert; bestehende Eingabe- und Rebinding-APIs sollen wiederverwendet werden.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Input/ (Editor-Erweiterung neu)`
- `Funke:Github-Docs/Documentation/Packages/Writing-Packages.md`

**Anweisungen:**

1. Erstelle ein Paket-Editorfenster für Maps, Actions, Bindinglisten, Composites, Deadzones und Anzeigenamen auf Basis der serialisierbaren Definitionen.
2. Integriere Undo/Redo, Dirty-Markierung, Validierungsfehler und Speichern über vorhandene Editorservices.
3. Zeige Konflikte an und biete eine kontrollierte Test-/Capturefunktion; Eingaben im Editor dürfen nicht ungewollt das Spiel steuern.
4. Prüfe Tastatur, Maus, Controller und ein geladenes Profil an einem kleinen Beispielprojekt.

**Abnahme:**

- [ ] Eine vollständige WASD-/Controller-Map lässt sich ohne Quellcodeänderung erstellen und nutzen.
- [ ] Undo/Redo und Reload erhalten IDs und Defaults.
- [ ] Fehlende Controls beziehungsweise ungültige Typwechsel werden angezeigt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Input/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Packages/Writing-Packages.md)

<a id="task-mem-003"></a>

### MEM-003 – Speicherdiagnostik auf Linux und macOS als verifizierten Weg anbieten

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** MEM-001 (erledigt; siehe Vertragsdokumentation)

**Voraussetzungen:** Native Builds und Sanitizer-Läufe je Betriebssystem zusätzlich; ohne verfügbares Betriebssystem bleibt dessen Nachweis offen.

**Ausgangslage:** Der geprüfte Memory-Header bietet den globalen Tracking-Pfad nur für Windows-Debug. Das ist kein Nachweis eines Leaks auf anderen Plattformen.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Core/Memory.hpp`
- `Funke:Funke-Engine/src/Core/Memory.cpp`
- `Funke:Funke-Engine/src/Profiling/Profiler.hpp`

**Anweisungen:**

1. Definiere, welche Messwerte überall benötigt werden: native CPU-Allokationen, Arenas, GPU-Speicher und verwalteter Heap sind getrennte Kategorien.
2. Prüfe vorhandene Tracy-/Sanitizer-Integrationen und ergänze eine portable Diagnoseanbindung mit eindeutigen Capability-Angaben; überschreibe nicht unüberlegt jeden globalen Allocator.
3. Berücksichtige aligned/sized new/delete, Rekursion des Trackers und Modul-/DLL-Grenzen, falls ein globaler Hook tatsächlich benötigt wird.
4. Markiere nicht messbare Bereiche als unbekannt statt mit scheinbar vollständigen Nullwerten.
5. Vergleiche einen reproduzierbaren Allocate/Free-/Szenenwechsel-Fall auf Windows, Linux und macOS und dokumentiere Kosten und Grenzen.

**Abnahme:**

- [ ] Alle Desktopplattformen haben einen beschriebenen und ausprobierten Diagnoseweg.
- [ ] Statistiken behaupten keine vollständige Sicht auf uninstrumentierte Speicherbereiche.
- [ ] Tracking verursacht keine ABI-Konflikte und bleibt für normale Release-Builds abschaltbar.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Core/Memory.hpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Core/Memory.cpp)

Windows Debug mit globaler Allocation-Interception und den neuen Lifecycle-Fällen gesondert abgleichen. [Speicher-Prüfstand](Documentation/Editor/Memory-Lifecycle-Verification.md).

<a id="task-mem-004"></a>

### MEM-004 – Beabsichtigte Allocator-Retention von wachsenden Leaks unterscheiden

**Komplexität:** K3 · **Status:** teilweise erledigt; Restarbeit offen · **Priorität:** P2 · **Art:** Prüfauftrag · **Belegstatus:** prüfen

**Verbleibende Arbeit und Abnahme:**

- [ ] Verbleibende Verwaltungsdaten nach vollständigem Engine-/Projekt-Shutdown in Bytes quantifizieren. Wiederholte Lebenszyklen und späte konkurrierende Zugriffe unter Linux-LeakSanitizer prüfen. Der Windows-Editor-Mischlauf mit 120 Zyklen erfüllte bereits das Speicherbudget. [Speicherprüfungen](Documentation/Editor/Memory-Lifecycle-Verification.md).

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Core/Memory.cpp`
- `Funke:Tests/Funke-Engine-Tests/src/ApplicationLifecycleTests.cpp`

<a id="task-net-002"></a>

### NET-002 – Transform-Guards bis zur nativen Spielwelt abnehmen

**Komplexität:** K3 · **Status:** teilweise erledigt; Restarbeit offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** prüfen

**Verbleibende Arbeit und Abnahme:**

- [ ] Nativen Mehrprozesslauf einschließlich Entity-/Physik-/Renderer-Verarbeitung ungültiger Transformdaten ausführen. Managed Validierung und echte Transporttests sind vorhanden.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/NetworkObjects.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/Interpolation.cs`

<a id="task-net-011"></a>

### NET-011 – Relay-Anbindung als austauschbaren Verbindungsweg vorbereiten

**Komplexität:** K3 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** NET-004 (erledigt; siehe Vertragsdokumentation), [NET-010](#task-net-010)

**Voraussetzungen:** Providerzugang und Testdienst für die tatsächliche Relay-Verbindung.

**Ausgangslage:** Ein Relay ist nicht enthalten. Direkte Internetverbindungen erfordern erreichbare Ports beziehungsweise einen öffentlich erreichbaren Server.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/Transport.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/NetworkManager.cs`

**Anweisungen:**

1. Definiere eine Relay-/Session-Provider-Schnittstelle mit kurzlebigen Zugangsdaten, Limits und klaren Fehlercodes.
2. Implementiere einen lokalen Referenzadapter oder einen ausdrücklich ausgewählten verfügbaren Provider; binde die Engine nicht fest an einen kostenpflichtigen Dienst.
3. Erhalte den direkten UDP-/WebSocket-Pfad und biete eine explizite Verbindungsstrategie an.
4. Prüfe Ablauf/Erneuerung von Tokens, Serverwechsel, Relay-Ausfall und Bandbreitenlimits.
5. Dokumentiere Betriebsaufwand und trenne vorbereiteten Adapter von tatsächlicher externer Bereitstellung.

**Abnahme:**

- [ ] Ein Beispiel kann über den geprüften Relaypfad verbinden.
- [ ] Zugangsdaten sind kurzlebig und werden nicht im Projekt eingecheckt.
- [ ] Der direkte Verbindungsweg bleibt nutzbar.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/Source/Internal/Transport.cs)

<a id="task-perf-15"></a>

### PERF-15 – Native Allokationen den gemessenen Frames zuordnen

**Komplexität:** K3 · **Status:** Restabnahme offen · **Priorität:** P2

- [ ] Für Material, UI, Dispatch und Laden kurze native Heap-Profile mit Anzahl, Bytes und Aufrufstellen aufnehmen. Spitzen mit vorhandenen Frames und Phasen korrelieren, Managed-/GC-Daten getrennt ausweisen. Der frühere Windows-Aufnahmeversuch endete mit 0x80070005. [Messungen und Erfassungsgrenzen](Documentation/Editor/Performance-Remaining-2026-11.md#perf-15-and-perf-16-evidence-still-unavailable).
- [ ] Für die abschließende Allocator-Retention [MEM-004](#task-mem-004) verwenden.

<a id="task-prf-004"></a>
<a id="task-perf-16"></a>

### PERF-16 – Regressionsschwellen auf kontrollierter CPU-/GPU-Hardware kalibrieren

**Komplexität:** K3 · **Status:** Restabnahme offen · **Priorität:** P2

Hier ist die verbleibende Schwellenprüfung aus PRF-004 zusammengeführt. Der CI-Trend auf gemeinsam genutzter Hardware und die Erkennung eines künstlichen Rückschritts sind belegt. [Messvertrag](Documentation/Editor/Benchmark-Results.md) · [Prüfstand](Documentation/Editor/Performance-Remaining-2026-11.md#perf-15-and-perf-16-evidence-still-unavailable).

- [ ] Die Referenzfälle aus [PRF-002](#task-prf-002) auf einem kontrollierten Runner wiederholen. Build, Workload, Treiber und Geräteklasse binden, Rohdaten und Streuung auswerten.
- [ ] Relative und absolute Schwellen sowie Mindeststichprobe mit einem kontrollierten Rückschritt und unveränderten Wiederholungen prüfen. Baseline- und Hardwarewechsel im CI-Artefakt mit Grund festhalten.

<a id="task-pm-24"></a>

### PM-24 – Physikmodulvarianten auf Geräten und im Release-SDK abnehmen

**Komplexität:** K3 · **Status:** Restabnahme offen · **Priorität:** P0

- [ ] Die Linux-/macOS-CI-Artefakte für Rendering-only und Vollbuild mit dem vorgesehenen Integrationsstand abgleichen. Die entsprechenden Jobs am veröffentlichten Stand bestanden. [CI-Nachweis für 67e570bf](Documentation/Editor/Testing.md#verified-desktop-ci-run)
- [ ] Varianten mit und ohne Physik auf iOS bauen und ausführen. Die vorhandenen Android-APKs auf Gerät oder Emulator starten. Im Browser C#-Physikaufrufe einschließlich deaktivierter APIs und initialisierter Out-Parameter prüfen.
- [ ] Im frisch gebauten Dist-Installer/Release-SDK deklarierte Varianten mit den Dateien vergleichen. Aus der Installation exportieren, Full-Runtime-Fallback und Ablehnung bei `--exact-modules` prüfen.

[Modulverträge](Documentation/Engine-Core/Engine-Modules.md) · [Prüfbericht und Vergleichsgrenzen](Documentation/Engine-Core/Physics-Modules-Verification.md).

<a id="task-prf-002"></a>

### PRF-002 – Drei reproduzierbare Lastszenen und einen belastbaren Ausgangswert erzeugen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** prüfen

**Abhängigkeiten:** PRF-001 (erledigt; siehe Vertragsdokumentation)

**Voraussetzungen:** Reproduzierbare GPU-Lastfälle, Warmup und Wiederholungen auf identifizierter Hardware.

**Ausgangslage:** Für Sprites, 3D-Views und animierte Modelle liegen [Messungen mit definierten Grenzen](Documentation/Editor/Performance-Remaining-2026-11.md) vor. Für die drei Lastszenen vorhandene Fixtures, Rohdaten und Geräteidentitäten zuerst abgleichen. Fehlende Laststufen und gemeinsame FunkeBench-Berichte ergänzen.

**Dateien und Einstiegspunkte:**

- `Funke-AssetLibrary:index.json`
- `Funke:Funke-Engine/src/Graphics/Renderer2D.cpp`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Funke-Engine/src/Profiling/GpuTimer.cpp`

**Anweisungen:**

1. Verwende FunkeBench weiter und ergänze deterministische Szenen für viele Sprites/Tiles, beleuchtete 3D-Geometrie und animierte Charaktere.
2. Wähle mindestens drei Laststufen pro Szene und halte Anzahl, Materialvielfalt, Lichtanzahl, Animation und Sichtbarkeit konstant.
3. Führe Warmup und mehrere Wiederholungen mit derselben Buildkonfiguration aus; speichere Rohdaten, nicht nur den besten Lauf.
4. Erfasse CPU-/GPU-Limit, Frameverteilung und Speicher; ergänze einen Screenshot als Beleg, dass die erwartete Arbeitsmenge sichtbar ist.
5. Berichte Ergebnisse und Flaschenhälse ohne pauschale Vergleiche zu anderen Engines mit abweichendem Workload.

**Abnahme:**

- [ ] Jeder Lauf ist mit Konfiguration und Rohdaten wiederholbar.
- [ ] Median/P95/P99 und Speichermessung sind vorhanden oder ausdrücklich nicht verfügbar.
- [ ] Leere oder falsch konfigurierte Szenen bestehen den Benchmark nicht unbemerkt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-AssetLibrary/blob/555cc0fdae1f015fba9517ee197260c11a7bf403/index.json) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Renderer3D.cpp)

<a id="task-prf-003"></a>

### PRF-003 – Fünf Lade-Lastfälle auf echter GPU vermessen

**Komplexität:** K3 · **Status:** implementiert; Restabnahme offen · **Priorität:** P1

**Dauerhafter Vertrag und gezielte Tests:** [Loading capture und Messprotokoll](Documentation/Engine-Core/Cooperative-Loading.md). Die Implementierung und Prüfgrenzen stehen in der Engine-Dokumentation; sie müssen nicht erneut entwickelt werden.

**Verbleibende Arbeit und Abnahme:**

- [ ] Auf passendem Engine-/Editor-Build die fünf dokumentierten Lastfälle getrennt wiederholen: große Textur, viele kleine Assets, großer Prefabbaum, viele Lifecycle-Callbacks, Szenen-Teardown.
- [ ] Pro Fall Capture/CSV mit P95/P99/Maximum, tatsächlichem Budgetverbrauch, Roh-Frameintervallen, Inputgröße, Build-/Geräte-/Treiberidentität und Kalt-/Warmzustand sichern.
- [ ] Worker-Dekodierung, CPU-Treiberaufruf und echte GPU-Ausführung unterscheiden; dominante gemessene Ursachen und konkrete nächste Optimierung benennen.
- [ ] Keine Null-Backend-Zeiten oder bloße Instrumentierung als reale GPU-/Frametime-Abnahme ausgeben.

**Früherer Prüfstand:** [Build- und Backendgrenzen des Laufs vom 14. November](Documentation/Editor/Verified-Contracts-2026-11.md). Für die Restprüfung passende Build- und Geräteidentität festhalten.

<a id="task-prf-005"></a>

### PRF-005 – Langzeittest für Szenen, Hot Reload und Ressourcenfreigabe einrichten

**Komplexität:** K3 · **Status:** zurückgestellt · **Priorität:** P2 · **Art:** Prüfauftrag · **Belegstatus:** prüfen

**Abhängigkeiten:** PRF-001 (erledigt; siehe Vertragsdokumentation)

**Voraussetzungen:** Reproduzierbarer, sauber beendbarer Soak-Harness mit fortlaufender Zeitreihe.

**Ausgangslage:** Der [120-Zyklen-Mischlauf](Documentation/Editor/Memory-Lifecycle-Verification.md#completed-post-fix-mixed-run-20-november-2026) ist abgenommen. Für den zurückgestellten mehrstündigen Soak fehlen eine längere Zeitreihe, Ressourcen- und Frametime-Verlauf. Vorhandenen Harness und dieselben Lebenszyklusmarker weiterverwenden.

**Dateien und Einstiegspunkte:**

- `Funke:scripts/ci/runtime_smoke_test.py`
- `Funke:scripts/ci/agent_workflow_test.py`
- `Funke:Tests/Funke-ScriptCore-Tests/ScriptAssemblyReloadTests.cs`

**Anweisungen:**

1. Automatisiere einen wiederholbaren Ablauf aus Szene laden, spielen, Ressourcen erzeugen/freigeben, Szene wechseln und optional Hot Reload.
2. Speichere nach Warmup periodisch Speicher, offene Ressourcen, Frametime und Fehlerzähler.
3. Trenne erwartete Caches mit Plateau von monotonem Wachstum; beobachte nach Freigabe mehrere Zyklen.
4. Beende den Test sauber mit Fehlerartefakten; Timeout ist kein Erfolg und Logdrosselung darf Fehlerzähler nicht verstecken.

**Abnahme:**

- [ ] Ein mehrstündiger Lauf erzeugt eine auswertbare Zeitreihe.
- [ ] Wachstum nach Warmup ist erklärt oder als Folgefehler mit Repro dokumentiert.
- [ ] Scene-/Assembly-Lebensdauern werden über mehrere Zyklen überprüft.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/scripts/ci/runtime_smoke_test.py) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Tests/Funke-ScriptCore-Tests/ScriptAssemblyReloadTests.cs)

<a id="task-qal-008"></a>

### QAL-008 – Desktop-Unterstützung mit einer reproduzierbaren Laufzeitmatrix belegen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** [QAL-001](Documentation/Editor/Testing.md#windows-installed-package-under-reduced-path), [QAL-002](Documentation/Editor/Testing.md#linux-editor-startup-and-recovery-acceptance), [QAL-003](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Voraussetzungen:** Windows-, Linux- und macOS-Systeme mit den jeweils geprüften GPUs und Treibern.

**Ausgangslage:** Die aktuellen Desktop-Builds, Release-Tests und mehrere Laufzeit-/Exportprüfungen sind belegt; Dokumentation nennt Linux/macOS deutlich weniger getestet. Einzelne Runner ersetzen keine bekannte Hardware-/Betriebssystemmatrix.

**Dateien und Einstiegspunkte:**

- `Funke:.github/workflows/ci.yml`
- `Funke:scripts/ci/runtime_smoke_test.py`
- `Funke:scripts/ci/desktop_export_smoke_test.py`
- `Funke:Github-Docs/Documentation/Editor/Building-and-Shipping.md`

**Anweisungen:**

1. Lege eine kleine unterstützte Referenzmatrix mit OS-Version, CPU-Architektur, Grafikbackend, GPU/Treiber und .NET-Version fest.
2. Prüfe pro verfügbarer Kombination denselben versionierten Export mit Assetload, Scriptupdate, Input, Audioinitialisierung, Save-/Load und sauberem Beenden.
3. Ergänze Start ohne Grafiktreiber beziehungsweise mit fehlenden Laufzeitvoraussetzungen als klare Fehlermeldungstests.
4. Dokumentiere tatsächliche Laufzeit, Prüfsumme des Exports und beobachtete Grenzen statt allgemeiner Plattformversprechen.
5. Prüfe Langzeitsitzungen und Sleep-/Resume-/Fensterwechsel separat und ordne die Ergebnisse der getesteten Plattform zu.

**Abnahme:**

- [ ] Es gibt einen auf einen Commit/Export bezogenen Matrixreport mit eindeutigen Pass-/Fail-/Unbestätigt-Feldern.
- [ ] Behauptete Baselinekombinationen wurden tatsächlich ausgeführt.
- [ ] Neue Aussagen zu zusätzlichen Distributionen/Intel-Macs/Grafiktreibern werden nur bei vorliegendem Nachweis ergänzt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/.github/workflows/ci.yml) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Building-and-Shipping.md)

<a id="task-qal-009"></a>

### QAL-009 – Installierte Editor-/SDK-/Playerabläufe ohne Source-Checkout prüfen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** bestätigt

**Abhängigkeiten:** [QAL-001](Documentation/Editor/Testing.md#windows-installed-package-under-reduced-path), [QAL-002](Documentation/Editor/Testing.md#linux-editor-startup-and-recovery-acceptance), [QAL-003](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Voraussetzungen:** Saubere virtuelle oder physische Zielsysteme.

**Ausgangslage:** Der installierte Windows-Workflow hat auf dem Hosted Runner mit reduziertem PATH beide Scriptrevisionen und beide verschobenen Player bestanden. Die Abnahme auf sauberen Zielsystemen ohne Checkout oder native Toolchain bleibt offen. [CI-Nachweis für 67e570bf](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Dateien und Einstiegspunkte:**

- `Funke:scripts/ci/standalone_sdk_test.py`
- `Funke:scripts/ci/test_standalone_sdk.py`
- `Funke:Github-Docs/Documentation/Editor/Standalone-Verification.md`
- `Funke:installer/linux/smoke_test.py`
- `Funke:installer/macos/smoke_test.py`
- `Funke:scripts/ci/installer_smoke_test.ps1`

**Anweisungen:**

1. Erzeuge eine disposable Testumgebung ohne Engine-Checkout, lokale Buildartefakte und Paketcache; installiere ausschließlich deklarierte Voraussetzungen und das zu prüfende Bundle.
2. Führe Projektanlage, C#-Bearbeitung/Recompile, lokales Paket, Editor-Play und Spiel-Export mit dem ausgelieferten SDK aus.
3. Verschiebe Installation und exportiertes Spiel in Pfade mit Leerzeichen/Umlauten und starte aus fremdem cwd.
4. Prüfe Update und Deinstallation an einer ausschließlich für den Test angelegten Installation; kontrolliere, dass Benutzerprojekte erhalten bleiben.
5. Speichere Manifest, Logs und Exporthash als Abnahmebeleg und kennzeichne verbleibende Hostabhängigkeiten.
6. Übernimm die detaillierten C++-Scripting-Nachweise aus EDT-010, statt dieselbe Native-Toolchain-Prüfung unabhängig zu duplizieren.

**Abnahme:**

- [ ] Ein neuer Nutzer kann den dokumentierten Grundablauf aus dem installierten Produkt ohne Source-Checkout durchführen.
- [ ] Umgezogene Exporte finden Assets und gebündelte Laufzeit eigenständig.
- [ ] Der Report unterscheidet Host-Guard-Prüfung von echter sauberer Maschine; kein unbelegtes 'standalone verified'.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Standalone-Verification.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/scripts/ci/standalone_sdk_test.py) · [Beleg 3](https://github.com/Ben-Scr/Funke/actions/runs/41534529365/job/125630786622)

<a id="task-qal-012"></a>

### QAL-012 – Vorhandene Release-Signiergates tatsächlich verifizieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** prüfen

**Abhängigkeiten:** [QAL-003](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Voraussetzungen:** Zugriff auf den vorgesehenen Windows-Signierweg und Apple Developer ID/Notarisierung für einen signierpflichtigen Release.

**Ausgangslage:** Installer-Dokumentation fordert für signierpflichtige Windows-Releases Code Signing und für signierpflichtige macOS-Releases Developer ID plus Notarisierung. Unsigned Branch-Artefakte sind als Tests vorgesehen. Für die erste unsignierte Vorschau gilt die [versionsgebundene Ausnahmeregel](Documentation/Editor/Release-Operations.md).

**Dateien und Einstiegspunkte:**

- `Funke:scripts/SignInstaller.ps1`
- `Funke:scripts/VerifyRelease.ps1`
- `Funke:installer/macos/signing.py`
- `Funke:scripts/BuildMacInstaller.py`
- `Funke:.github/workflows/ci.yml`
- `Funke:installer/README.md`

**Anweisungen:**

1. Prüfe die vorhandenen Signier-/Verifikationspfade, ohne private Schlüssel oder Passwörter auszugeben oder in Artefakte zu schreiben.
2. Bestätige, dass signierpflichtige Releaseprofile bei fehlendem Zertifikat/Notarisierungsnachweis früh und eindeutig scheitern, während Testartefakte sichtbar unsigniert bleiben dürfen.
3. Prüfe auf Windows Signaturkette, erwarteten Herausgeber, Zeitstempel und Signatur nach finalem Packen.
4. Prüfe auf macOS Bundle-Signaturen, eingebettete Bibliotheken, Hardened Runtime soweit erforderlich sowie nach autorisiertem Notarylauf Stapling/Gatekeeper.
5. Bewahre ausschließlich öffentliche Signatur-/Verifikationsdaten und den Artefakthash im Freigabereport auf.

**Abnahme:**

- [ ] Ein Testartefakt kann nicht versehentlich als vollständig signiertes öffentliches Release passieren.
- [ ] Manipulation nach dem Signieren wird erkannt.
- [ ] Fehlende Konten/Zertifikate ergeben einen klaren blockiert-Status statt eines fingierten Signing-Pass.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/installer/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/scripts/SignInstaller.ps1) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/installer/macos/signing.py) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/.github/workflows/ci.yml)

<a id="task-qal-013"></a>

### QAL-013 – Notarisierung exportierter macOS-Spiele als optionalen Ablauf ergänzen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Ausbau · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-012](#task-qal-012)

**Voraussetzungen:** Benötigt einen Mac und eingerichteten Apple-Developer-/Signingzugang. Ohne Zugang bleiben externe Nachweise blockiert. Einen Notarisierungsupload ausführen, wenn er Bestandteil des aktuellen Auftrags ist; bereits erteilte Autorisierung nicht erneut anfordern.

**Ausgangslage:** Known Limitations erklärt, dass macOS-Spiele derzeit ad-hoc signiert, aber nicht notarisiert sind. Das ist von der bereits vorgesehenen Notarisierung des Engine-Installers zu trennen.

**Dateien und Einstiegspunkte:**

- `Funke:Github-Docs/Documentation/Editor/Building-and-Shipping.md`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`
- `Funke:Funke-Editor/src/Build/GameBuilder.cpp`
- `Funke:scripts/ci/desktop_export_smoke_test.py`

**Anweisungen:**

1. Prüfe die aktuellen Spiel-Exportpfade und wähle einen optionalen Releaseprofil-/CLI-Ablauf für Developer-ID-Signierung und Notarisierung.
2. Halte ad-hoc signierte lokale Entwicklungsbuilds weiterhin nutzbar und kennzeichne ihren Status korrekt.
3. Signiere zuerst alle eingebetteten nativen Komponenten und anschließend das finale App-Bundle; ändere nach dem Signieren keine Inhalte mehr.
4. Implementiere einen sicheren Notary-/Stapling-Ablauf mit nachvollziehbaren Fehlern und führe den Upload aus, wenn der aktuelle Auftrag diesen Schritt umfasst.
5. Dokumentiere Voraussetzungen und prüfe ein exportiertes Spiel auf einem anderen Mac mit normalen Gatekeeper-Einstellungen.

**Abnahme:**

- [ ] Entwicklungs- und notarisiertes Releaseprofil sind klar getrennt.
- [ ] Das tatsächliche notarierte Spielartefakt ist auf einem zweiten Mac geprüft, sofern Zugang vorhanden ist; andernfalls bleibt die Geräteabnahme blockiert.
- [ ] Der vorhandene Editor-Installerstatus wird nicht als Nachweis für die Spieleexports verwendet.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Building-and-Shipping.md)

<a id="task-qal-014"></a>

### QAL-014 – Android-Export auf echtem arm64-Gerät end-to-end validieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-023](#task-qal-023)

**Voraussetzungen:** Autorisiertes arm64-Ziel, Android-SDK/NDK und adb. Ein dediziertes Testpaket für die Installation verwenden.

**Ausgangslage:** Androidexport existiert, zielt derzeit auf arm64-v8a und wurde laut Dokumentation wenig auf Geräten getestet. Builder-Unit-Tests ersetzen externe Compiler und belegen allein keine startende APK.

**Dateien und Einstiegspunkte:**

- `Funke:platforms/Funke.Platform.Android/Builder/build.py`
- `Funke:platforms/Funke.Platform.Android/Native/AndroidMain.cpp`
- `Funke:platforms/Funke.Platform.Android/Java/dev/funke/player/FunkeActivity.java`
- `Funke:scripts/ci/platform_export_smoke.py`
- `Funke:Github-Docs/Documentation/Editor/Platform-Export-Testing.md`

**Anweisungen:**

1. Baue eine kleine versionierte Testszene mit vorher festgelegten Bildmerkmalen und unabhängigen Script-/Asset-/Save-Markern als echte APK.
2. Installiere auf einem explizit ausgewählten Testgerät und erfasse Androidversion, GPU/Treiber, ABI, Exporthash und SDK/NDK-Versionen.
3. Prüfe Start, C#-Updates, Touch, Assetload, Audioinitialisierung, Save-/Load, Hintergrund/Vordergrund und sauberes Beenden.
4. Führe den vorhandenen Smoke-Harness mit Log- und Pixelassertions aus und ergänze fehlende Prüfungen ohne Erfolgsmarker zu simulieren.
5. Bewahre Logs, Bild und Ergebnisreport auf; führe tatsächlich gefundene Fehler getrennt als belegte Bugs.

**Abnahme:**

- [ ] Eine aus dem geprüften Commit gebaute APK läuft auf dem angegebenen Gerät und erfüllt alle vorbereiteten Assertions.
- [ ] Scriptabsturz, fehlender Marker oder falsches Bild führt zum Fehler.
- [ ] Ohne verfügbares Gerät lautet der Status blockiert/unverifiziert, nicht bestanden.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Platform-Export-Testing.md) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/scripts/ci/platform_export_smoke.py) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.Platform.Android/Builder/build.py)

<a id="task-qal-016"></a>

### QAL-016 – iOS-Export auf einem physischen Gerät beweisen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-023](#task-qal-023)

**Voraussetzungen:** Mac mit Xcode, Provisioning und physischem iOS-Testgerät.

**Ausgangslage:** Die iOS-Plattform ist vorhanden, laut Known Limitations aber noch nicht auf einem Gerät verifiziert. Ein Simulator-Smoke allein bestätigt weder Gerätesigning noch den physischen Export.

**Dateien und Einstiegspunkte:**

- `Funke:platforms/Funke.Platform.iOS/Builder/build.py`
- `Funke:platforms/Funke.Platform.iOS/Native/iOSMain.mm`
- `Funke:platforms/Funke.ScriptRuntime.Mono/Native/MonoScriptRuntime.cpp`
- `Funke:Github-Docs/Documentation/Editor/Platform-Export-Testing.md`

**Anweisungen:**

1. Erzeuge einen echten gerätespezifischen Export der versionierten Plattformtestszene mit korrektem Bundle-Identifier und Provisioning.
2. Installiere auf dem explizit freigegebenen Testgerät und erfasse Modell, OS, Architektur, Exporthash und Toolchain.
3. Prüfe Start, interpretiertes C# inklusive relevanter Interopaufrufe, Assets, Touch, Audioinitialisierung, Speichern sowie Hintergrund-/Vordergrundwechsel.
4. Sammle Geräte-/Crashlogs und vorab festgelegte Script-/Bildbelege; behebe bestätigte Plattformfehler mit gezielten Regressionstests.
5. Aktualisiere den Verifizierungsstatus nur anhand dieses tatsächlichen Gerätereports.

**Abnahme:**

- [ ] Ein echter physischer iOS-Export aus dem geprüften Commit startet und erfüllt die definierten Laufzeitassertions.
- [ ] Signing-/Provisioningfehler sind klar diagnostiziert; Simulatorerfolg wird nicht als Geräteerfolg gezählt.
- [ ] Bei fehlendem Gerät/Konto bleibt der Task blockiert und die Dokumentation weiterhin offen.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Platform-Export-Testing.md) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.Platform.iOS/Builder/build.py)

<a id="task-qal-017"></a>

### QAL-017 – iOS-Simulator als getrenntes, tatsächlich baubares Exportziel ergänzen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-023](#task-qal-023)

**Voraussetzungen:** Mac mit Xcode, Simulator und passenden nativen und Mono-Builds. Der physische Gerätelauf bleibt QAL-016.

**Ausgangslage:** Die vorhandene Smoke-Anleitung kann Simulator-Apps ausführen, erklärt aber ausdrücklich, dass der aktuelle iOSexport physische Geräte adressiert und einen getrennten Simulatorbuild samt passender Toolchain benötigt.

**Dateien und Einstiegspunkte:**

- `Funke:platforms/Funke.Platform.iOS/Builder/build.py`
- `Funke:platforms/Funke.Platform.iOS/Native/CMakeLists.txt`
- `Funke:platforms/Funke.ScriptRuntime.Mono/Builder/mono_runtime.py`
- `Funke:scripts/ci/platform_export_smoke.py`
- `Funke:scripts/ci/test_ios_builder.py`

**Anweisungen:**

1. Trenne iphoneos und iphonesimulator als explizite Zielkonfigurationen einschließlich Architektur, SDK und nativer Abhängigkeiten.
2. Verhindere, dass Gerätebibliotheken in ein Simulatorbundle gemischt werden.
3. Erzeuge passende Mono-/Engineartefakte und einen echten Simulator-App-Export.
4. Starte auf einem ausdrücklich ausgewählten Simulator und führe die vorhandenen Marker-/Pixel-/Crashprüfungen aus.
5. Dokumentiere unterstützte Host-/Simulatorarchitekturen und die Grenze zur physischen Gerätevalidierung.

**Abnahme:**

- [ ] Ein Simulator-Export lässt sich ohne manuelle Binärersetzung bauen und starten.
- [ ] Falsche SDK-/Architekturkombinationen werden vor oder beim Linken eindeutig erkannt.
- [ ] Simulator und physisches iOS besitzen getrennte Statusfelder und Nachweise.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Platform-Export-Testing.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.Platform.iOS/Builder/build.py) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.Platform.iOS/Native/CMakeLists.txt)

<a id="task-qal-018"></a>

### QAL-018 – Echten WebAssembly-Export regelmäßig im Browser testen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-023](#task-qal-023)

**Voraussetzungen:** Benötigt die echte .NET-/wasm-/Emscripten-Toolchain sowie Browser mit funktionierendem WebGPU. Software- und Hardware-GPU-Prüfungen getrennt; Builds/Browserinstallation zusätzlich. Ohne Adapter bleibt der Lauf fehlgeschlagen oder blockiert.

**Ausgangslage:** Ein echter Browserharness existiert, wird aber opt-in mit bereits exportiertem Artefakt gestartet. Die regulären Buildertests ersetzen Compiler und beweisen keine ausführbare WebAssembly-Anwendung.

**Dateien und Einstiegspunkte:**

- `Funke:.github/workflows/platform-smoke.yml`
- `Funke:platforms/Funke.Platform.Web/Builder/build.py`
- `Funke:platforms/Funke.Platform.Web/Builder/preview.py`
- `Funke:scripts/ci/platform_export_smoke.py`
- `Funke:Github-Docs/Documentation/Editor/Platform-Export-Testing.md`

**Anweisungen:**

1. Erzeuge aus einer versionierten Testszene einen tatsächlichen Webexport statt eines Dummy-Artefakts.
2. Binde dessen Hash/Commit an den Browserjob und prüfe Scriptmarker, Canvaspixel, Fehlerkonsole und Beenden/Reload.
3. Beziehe fehlende WebGPU-Unterstützung, Assetfehler und Script-Exceptions als Negativfälle ein.
4. Lasse zunächst eine softwarebasierte CI-Lane regelmäßig laufen und ergänze einen gesonderten Hardware-GPU-Browsernachweis für Releases.
5. Speichere Browser-/Adapterversion, Bild, Konsole und Resultat; akzeptiere keine Screenshots aus älteren Läufen.

**Abnahme:**

- [ ] Der Job führt die echten erzeugten WebAssembly-/C#-Artefakte aus.
- [ ] Falsche Pixel, fehlende Marker und Browserfehler führen zuverlässig zum Fehler.
- [ ] Ein bestandener Software-GPU-Test wird nicht als Hardwarekompatibilitätsbeweis ausgegeben.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Platform-Export-Testing.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/.github/workflows/platform-smoke.yml) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/scripts/ci/platform_export_smoke.py) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.Platform.Web/Builder/build.py)

<a id="task-qal-021"></a>

### QAL-021 – Mono-Interpretergrenzen mit Messungen und Kompatibilitätstests quantifizieren

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Prüfauftrag · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-016](#task-qal-016), [QAL-018](#task-qal-018)

**Voraussetzungen:** Laufzeitmessungen benötigen echte Zielbrowser/iOS-Geräte und repräsentative Gerätehardware. Keine Smartphone-/Desktop-Zeiten erfinden; fehlende Ziele bleiben unvermessen. Wiederholte Messläufe zusätzlich.

**Ausgangslage:** iOS und Web verwenden laut Dokumentation interpretiertes C#; kultursensitive Formatierung nutzt außerhalb des Desktops invariante Regeln. Der Audit liefert keine belastbaren plattformspezifischen Geschwindigkeitswerte.

**Dateien und Einstiegspunkte:**

- `Funke:platforms/Funke.ScriptRuntime.Mono/Native/MonoScriptRuntime.cpp`
- `Funke:platforms/Funke.ScriptRuntime.Mono/Builder/mono_aot.py`
- `Funke:platforms/Funke.ScriptRuntime.Mono/Builder/dotnet_wasm.py`
- `Funke:Github-Docs/Documentation/Editor/Platform-Packages.md`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`

**Anweisungen:**

1. Erstelle wenige identische repräsentative Workloads für Scriptupdates, Interop, Allokation und Asset-/Serializepfade.
2. Messe Desktop-JIT und verfügbare Mono-Ziele mit warmem/cold start, Laufzeit, Peak-Speicher und vollständiger Hardware-/Buildbeschreibung.
3. Prüfe kultursensitive Anzeige und Datenpersistenz mit festgelegten erwarteten invarianten Ergebnissen.
4. Untersuche einen zulässigen AOT-/Hybridpfad nur für ein konkret unterstütztes Target und dokumentiere Toolchain-/Reflectiongrenzen.
5. Leite aus Messungen praktische Budgets und priorisierte Optimierungen ab statt pauschaler Geschwindigkeitsversprechen.

**Abnahme:**

- [ ] Vergleiche nennen reale Hardware, Buildtyp, Datengröße und Streuung.
- [ ] Dokumentation unterscheidet gemessene Interpretergrenzen von optionalen noch nicht gelieferten Optimierungen.
- [ ] Formatierung und persistierte Daten bleiben auf den geprüften Zielen konsistent.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.ScriptRuntime.Mono/Native/MonoScriptRuntime.cpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.ScriptRuntime.Mono/Builder/mono_aot.py)

<a id="task-qal-023"></a>

### QAL-023 – Exportbelege bis zum tatsächlich ausgeführten Zielartefakt prüfen

**Komplexität:** K3 · **Status:** teilweise erledigt; Restarbeit offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** dokumentiert

**Verbleibende Arbeit und Abnahme:**

- [ ] Produktionsbuilder mit `--builder-request` in einem sauberen Job ausführen. Commit, Toolchain, Ziel und Hash des Exports mit dem Runtime-Bericht verbinden. Auf echten Zielen abnehmen und belegen, dass ein Mock oder ein fremdes Artefakt die Prüfung nicht erfüllt.

**Dateien und Einstiegspunkte:**

- `Funke:scripts/ci/test_android_builder.py`
- `Funke:scripts/ci/test_ios_builder.py`
- `Funke:scripts/ci/test_web_builder.py`
- `Funke:scripts/ci/platform_export_smoke.py`
- `Funke:.github/workflows/platform-smoke.yml`
- `Funke:Github-Docs/Documentation/Editor/Platform-Export-Testing.md`

<a id="task-qal-027"></a>

### QAL-027 – Gesperrte Git- und lokale Pakete reproduzierbar wiederherstellen

**Komplexität:** K3 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Voraussetzungen:** Gitrestore benötigt erlaubten Zugang zum referenzierten Repository; lokale Pfade müssen bewusst vom Nutzer verfügbar gemacht werden. Private Credentials nicht ins Lockfile schreiben. Netzwerkdownloads zusätzlich.

**Ausgangslage:** Known Limitations erklärt: Restore locked versions rekonstruiert Registryreleases anhand URL/SHA-256, Git- und lokale Pakete müssen derzeit manuell neu installiert beziehungsweise kopiert werden.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Packages/PackageLock.cpp`
- `Funke:Funke-Engine/src/Packages/PackageLock.hpp`
- `Funke:Funke-Engine/src/Packages/PackageManager.cpp`
- `Funke:Funke-Editor/src/Gui/PackageManagerPanel.cpp`
- `Funke:Github-Docs/Documentation/Packages/Package-System.md`

**Anweisungen:**

1. Prüfe Lockfileschema und Source-Type-Modell; erhalte bestehende Registryrestore-Semantik.
2. Speichere für Gitquellen einen unveränderlichen Commit sowie die überprüfte Quelle statt nur eines beweglichen Branchnamens.
3. Definiere für lokale Pakete einen expliziten Pfad-/Snapshot-/Hashvertrag; fehlende lokale Inhalte müssen klar benannt werden und dürfen nicht still durch andere Quellen ersetzt werden.
4. Implementiere Restore mit Zwischenverzeichnis, Versionsprüfung und konsistenter Fehlerbehandlung ohne bereits gültige Installationen zu zerstören.
5. Teste frischen Checkout, nicht mehr vorhandenen Commit, geänderte lokale Inhalte, Offlinefall und erfolgreiche reproduzierbare Wiederherstellung.

**Abnahme:**

- [ ] Ein Git-Lockfile stellt denselben Commit wieder her und driftet nicht auf einen neueren Branchstand.
- [ ] Lokale Pakete sind entweder reproduzierbar verfügbar oder mit konkreter Handlungsanweisung als fehlend markiert.
- [ ] Fehlgeschlagener Restore lässt keine halb aktualisierte Paketmenge als erfolgreich installiert erscheinen.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Packages/PackageLock.cpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Packages/Package-System.md)

<a id="task-qal-028"></a>

### QAL-028 – Paketupdate-Rollback und verbleibende Plattformpfade abnehmen

**Komplexität:** K3 · **Status:** teilweise erledigt; Restarbeit offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** prüfen

**Verbleibende Arbeit und Abnahme:**

- [ ] Vollständigen Rollback über den Editor-Updatedialog mit aktuellem Build sowie die vorgesehenen POSIX-/Netzwerkpfade prüfen. Für den neuen Stand bei Bedarf die vorhandenen Transport- und Junction-Fixtures wiederverwenden. [Bereits geprüfte Windows-Fälle und Grenzen](Documentation/Packages/Download-Verification-2026-11.md).

**Dateien und Einstiegspunkte:**

- `Funke:Funke-PackageTool/Program.cs`
- `Funke:Funke-Engine/src/Packages/GitHubSource.cpp`
- `Funke:Funke-Editor/src/Editor/FunkePackageInstaller.cpp`
- `Funke:Funke-Editor/src/Project/BuildOutputSafety.hpp`
- `Funke:Tests/Funke-Editor-Tests/src/BuildOutputSafetyTests.cpp`
- `Funke:Tests/Funke-Editor-Tests/src/FunkePackageInstallerTests.cpp`
- `Funke:installer/windows/dependencies.lock.json`

<a id="task-qal-029"></a>

### QAL-029 – Lizenzinventare der tatsächlichen Artefakte und unklare Beispielprojektlizenzen klären

**Komplexität:** K3 · **Status:** offen · **Priorität:** P1 · **Art:** Prüfauftrag · **Belegstatus:** prüfen

**Voraussetzungen:** Fehlende Rechte-/Lizenzentscheidungen des jeweiligen Rechteinhabers blockieren eine öffentliche Weiterverteilung; der Agent darf keine fremde Lizenz erfinden. Prüfung des fertigen Artefakts benötigt dessen Build.

**Ausgangslage:** MIT-Lizenz und Third-Party-/Fontinventare sind vorhanden. Root fand im Asset-Library-Katalog einen Aetherwild-Eintrag mit NOASSERTION; das ist eine ungeklärte Lizenzangabe, kein Nachweis unerlaubter Inhalte. Finale Binärinventare wurden nicht vollständig rechtlich geprüft.

**Dateien und Einstiegspunkte:**

- `Funke:LICENSE.txt`
- `Funke:THIRD_PARTY_LICENSES.md`
- `Funke:Funke-Runtime/FunkeAssets/Fonts/LICENSES.md`
- `Funke:installer/README.md`
- `Funke-AssetLibrary:index.json`

**Anweisungen:**

1. Erzeuge aus tatsächlichen Engine-/Spiel-/Installerartefakten eine Liste verteilter nativer, verwalteter, Font- und Assetbestandteile samt vorhandenen Lizenz-/NOTICE-Dateien.
2. Vergleiche diese Liste mit THIRD_PARTY_LICENSES und den Quellversionen; prüfe insbesondere transitive Dawn-/Mono/.NET-Bestandteile anhand ihrer tatsächlichen Bedingungen.
3. Untersuche für NOASSERTION-Projekte die vorhandenen LICENSE-Dateien und kläre fehlende Angaben mit dem Rechteinhaber, statt automatisch MIT einzutragen.
4. Verhindere die als freigegeben beworbene Veröffentlichung eines ungeklärten Pakets, bis die jeweilige Nutzungs-/Weitergabebasis dokumentiert ist.
5. Bewahre die geprüfte Lizenzinventarliste zusammen mit Version und Artefakthash auf.

**Abnahme:**

- [ ] Die veröffentlicht werden sollenden Artefakte enthalten die für ihre tatsächlichen Bestandteile erforderlichen Lizenzhinweise nach überprüfter Grundlage.
- [ ] NOASSERTION ist entweder durch belegte Lizenzangaben ersetzt oder bleibt sichtbar ungeklärt und von einer Freigabe ausgenommen.
- [ ] Keine automatisch erfundene Rechteübertragung oder pauschale Aussage vollständiger rechtlicher Unbedenklichkeit.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/THIRD_PARTY_LICENSES.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Runtime/FunkeAssets/Fonts/LICENSES.md) · [Beleg 3](https://github.com/Ben-Scr/Funke-AssetLibrary/blob/555cc0fdae1f015fba9517ee197260c11a7bf403/index.json)

<a id="task-wld-003"></a>

### WLD-003 – Einfaches geometrisches LOD mit Hysterese und Debuganzeige anbieten

**Komplexität:** K3 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** WLD-001 (erledigt; siehe Vertragsdokumentation), [PRF-002](#task-prf-002)

**Voraussetzungen:** Neue Engine-/Paketfunktionen benötigen zusätzlich passende native Builds und praktische Szenenprüfung.

**Ausgangslage:** Die Engine bietet Culling und Animation-Distance-Rate. Daraus folgt noch keine allgemeine geometrische LOD-Pipeline für große Welten.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Funke-Engine/src/Components/Graphics/MeshRendererComponent.hpp`
- `Funke:Funke-Editor/src/Gui/ (LOD-Inspector neu)`

**Anweisungen:**

1. Prüfe vorhandene spielspezifische LODs und definiere zunächst explizite Meshstufen statt automatische Vereinfachung als Nebenaufgabe.
2. Wähle Stufen nach nachvollziehbarer Bildschirmgröße/Distanz mit Hysterese; definiere Schatten- und Materialverhalten.
3. Ergänze Serialisierung, Inspector, Boundsvalidierung und Debugfarben für aktive Stufen.
4. Prüfe Instancing-Gruppen, Kamerawechsel und schnelle Bewegungen; miss Drawcalls, Dreiecke und Wechselkosten.

**Abnahme:**

- [ ] LOD-Wechsel flackern nicht an einer festen Grenze.
- [ ] Bounds/Culling passen zu allen Stufen.
- [ ] Die Szene zeigt einen messbaren Nutzen unter dokumentierter Last.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Renderer3D.cpp)

## Aufträge mit Komplexität K4

<a id="task-ain-003"></a>

### AIN-003 – 3D-Navmesh-Laufzeitintegration und begrenzte Pfadabfragen aufbauen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** AIN-001 (erledigt; siehe Vertragsdokumentation)

**Voraussetzungen:** Geeignete Navmesh-Bibliothek und Buildunterstützung für die gewählten Zielplattformen.

**Ausgangslage:** Eine allgemeine 3D-Navigationslaufzeit wurde nicht nachgewiesen. Der Auftrag ist ein Ausbauprojekt für Spiele mit navigierenden Figuren.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Physics3D/PhysicsWorld3D.hpp`
- `Funke:Funke-Engine/src/Jobs/JobSystem.hpp`
- `Funke-PackageRegistry:packages/ (3D-Navigationspaket neu)`

**Anweisungen:**

1. Wähle nach Bestandsprüfung eine passende gepflegte Navmesh-Lösung oder bereits vorhandenen Projektcode mit geeigneter Lizenz; halte sie hinter einer Funke-API.
2. Definiere Agentradius/-höhe, begehbare Flächen, Pfadstatus und Ergebnislebensdauer.
3. Integriere zunächst Laden eines vorbereiteten Navmeshs, Punktprojektion und Pfadabfrage mit Abbruch und Budget.
4. Trenne Navigation von Bewegung/Physikkollision und behandle veraltete Ergebnisse nach Szenenwechsel.
5. Teste mehrere Agentengrößen, unerreichbare Ziele, schmale Durchgänge und das sichere Entladen der Szene.

**Abnahme:**

- [ ] Ein vorbereitetes Navmesh ist über C++ und die vorgesehene C#-Schicht nutzbar.
- [ ] Keine ungültigen Ergebnisse nach Entladen/Wechsel der Szene.
- [ ] Ein Headless-Test und eine sichtbare Debugszene prüfen den gleichen Pfadvertrag.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Physics-3D.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Packages/Writing-Packages.md)

<a id="task-ain-004"></a>

### AIN-004 – Navmesh-Bake, Debugdarstellung und Aktualisierung in den Editor integrieren

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** [AIN-003](#task-ain-003)

**Voraussetzungen:** Neue Engine-/Paketfunktionen benötigen zusätzlich passende native Builds und praktische Szenenprüfung.

**Ausgangslage:** Eine Laufzeitbibliothek allein bietet noch keinen produktiven Navigationsworkflow.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Editor/src/Gui/ (Navigationspanel neu)`
- `Funke:Funke-Engine/src/Graphics/GizmoRenderer.cpp`
- `Funke-PackageRegistry:packages/ (Navigations-Editorerweiterung neu)`

**Anweisungen:**

1. Ergänze Bakeeinstellungen pro Szene/Asset und wähle ausdrücklich die Geometriequellen; rendere Debugflächen und Pfade.
2. Führe teure Bakearbeit abbrechbar im Hintergrund aus und speichere Ergebnisse mit Geometrie-/Einstellungsfingerprint.
3. Erkenne veraltete Daten und verhindere unbemerkte Nutzung eines Navmeshs zu einer anderen Geometrieversion.
4. Definiere dynamische Hindernisse zunächst eng begrenzt; verspreche kein vollständiges Streaming-/Realtime-Rebake ohne Umsetzung.
5. Prüfe Speichern/Neuladen, Assetumbenennung, Export und einen abgebrochenen Bake.

**Abnahme:**

- [ ] Ein Nutzer kann ohne externe Handarbeit eine Szene für die geprüften Agenttypen backen.
- [ ] Veraltete Bakes werden sichtbar gemeldet.
- [ ] Abbruch oder Fehler beschädigt keinen vorherigen gültigen Bake.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Editor.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/GizmoRenderer.cpp)

<a id="task-ain-006"></a>

### AIN-006 – Behavior-Tree-Assets und visuellen Debugger ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** [AIN-005 (erledigt)](Documentation/Engine-Core/Navigation-and-Worlds.md#running-a-behavior-tree-from-c)

**Voraussetzungen:** Neue Engine-/Paketfunktionen benötigen zusätzlich passende native Builds und praktische Szenenprüfung.

**Ausgangslage:** Ein allgemeiner Behavior-Tree-Editor wurde nicht nachgewiesen; er baut auf einer klaren Runtime auf.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Editor/src/Gui/ (AI-Editor neu)`
- `Funke:Github-Docs/Documentation/Assets/Data-Assets.md`

**Anweisungen:**

1. Serialisiere Trees mit stabilen Node-IDs, Parametern und Blackboard-Schema.
2. Baue einen Editor mit Validierung von Zyklen, fehlenden Actions und Typfehlern; verwende Undo/Redo und existierende Assetservices.
3. Zeige im Playmodus aktive Knoten und Blackboardwerte an, ohne versehentlich Laufzeitwerte in authored Assets zu speichern.
4. Prüfe Kopieren, Referenzumbenennung, Reload und einen Versionswechsel des Schemas.

**Abnahme:**

- [ ] Ein Tree ist als Asset reproduzierbar speicherbar und exportierbar.
- [ ] Ungültige Graphen werden vor Ausführung erklärt.
- [ ] Debuganzeige und Editzustand bleiben sauber getrennt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Assets/Data-Assets.md)

<a id="task-edt-024"></a>

### EDT-024 – Definierten Ladepfad für threadlose Web-Builds anbieten

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-018](#task-qal-018)

**Voraussetzungen:** Threadloser Emscripten-/Mono-/WebGPU-Build und Browserrunner.

**Ausgangslage:** Im aktuellen threadlosen Webpfad führt asynchrone Vorbereitung zu Unsupported bzw. Ladefehler. Die Funktion darf nicht als vorhanden gewertet werden, solange dieser Pfad nicht praktisch funktioniert.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Assets/AssetLoader.cpp`
- `Funke:Funke-Engine/src/Scene/SceneManager.cpp`
- `Funke:Funke-ScriptCore/Source/Funke/Core/AssetManager.cs`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`
- `Funke:Funke-Engine/src/Scene/SceneManager.hpp`
- `Funke:Funke-ScriptCore/Source/Funke/Scene/SceneManager.cs`
- `Funke:Tests/Funke-Engine-Tests/src/SceneManagerAsyncTests.cpp`
- `Funke:Tests/Funke-Engine-Tests/src/SceneManagerFailureTests.cpp`

**Anweisungen:**

1. Definiere für Web explizit entweder kooperative Hauptthreadvorbereitung mit möglicher Restblockzeit oder einen plattformeigenen asynchronen IO-/Decodepfad.
2. Nutze dieselben Operation-IDs, Fortschritts-, Cancellation- und Fehlerverträge wie auf Desktop, ohne Threadunterstützung vorzutäuschen.
3. Führe Asset- und Szenen-Ladephasen über die Browser-Eventloop fort; vermeide blockierendes Warten auf nie gestartete Worker.
4. Prüfe in einem echten threadlosen Browserbuild Erfolg, Abbruch, fehlende Datei und Szenenwechsel mit einer überschaubaren Testszene.
5. Prüfe auch PreloadSceneAsync und stelle sicher, dass nach einem fehlgeschlagenen Ladeversuch die zuvor aktive Szene weiterhin gültig ist.

**Abnahme:**

- [ ] LoadAsync/LoadSceneAsync besitzen im unterstützten Webmodus ein dokumentiertes funktionierendes Verhalten.
- [ ] Der Browser bleibt während kooperativer Phasen bedienbar; verbleibende Decode-/GPU-Blockzeiten und Plattformgrenzen werden benannt.
- [ ] Erfolg, Fehler, Abbruch und Fortschritt sind im echten threadlosen Browserbuild belegt; der Implementierungsauftrag enthält die frühere Plattformaufgabe QAL-019.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Assets/AssetLoader.cpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Scene/SceneManager.cpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-ScriptCore/Source/Funke/Core/AssetManager.cs) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 5](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Tests/Funke-Engine-Tests/src/SceneManagerAsyncTests.cpp)

<a id="task-edt-031"></a>

### EDT-031 – Glyphen-Shaping für komplexe Schriften ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [EDT-030](#task-edt-030)

**Voraussetzungen:** Für die gewählte Shapingbibliothek werden Plattformbuilds und geeignete Schrift- und Gerätefixtures benötigt.

**Ausgangslage:** OpenType-Shaping fehlt laut Known Limitations. Einzelne Unicode-Glyphen oder Kerning ersetzen kein Shaping für komplexe Schriften; das ist eine neue größere Textlayoutfähigkeit. Für internationale Veröffentlichungen priorisieren; ein bewusst deutsch/englisch begrenztes Spiel kann diesen Ausbau zurückstellen.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Text/TextRenderer.cpp`
- `Funke:Funke-Engine/src/Graphics/Text/TextRenderer.hpp`
- `Funke:Funke-Engine/src/Graphics/Text/Font.cpp`
- `Funke:Funke-Engine/src/Systems/UIEventSystem.cpp`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`

**Anweisungen:**

1. Wähle anhand aktueller Primärdokumentation eine geeignete Shapingbibliothek und kläre Lizenz-/Buildintegration für die tatsächlich unterstützten Plattformen.
2. Erzeuge Glyphenruns mit Clusterzuordnung, Font, Sprache, Schreibrichtung, Advances und Offsets statt rein codepointbasierter Positionierung.
3. Verwende dieselben geformten Runs für Messung, Zeilenumbruch und Rendern und definiere Cacheinvalidierung bei Font-/Größen-/Textänderung.
4. Prüfe Ligaturen und mindestens eine komplexe Schrift mit bekannten Referenzausgaben; dokumentiere Grenzen, die erst durch Bidi und Graphem-Caret-Aufgaben geschlossen werden.

**Abnahme:**

- [ ] Geformte Glyphen mit korrekten Advances/Offsets werden konsistent gemessen und gerendert.
- [ ] Referenzfälle komplexer Schriften bestehen visuell und geometrisch; RTL-Reihenfolge und Editing gelten erst nach ihren separaten Tasks als vollständig.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Text/TextRenderer.cpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Text/TextRenderer.hpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Text/Font.cpp) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Systems/UIEventSystem.cpp) · [Beleg 5](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md)

<a id="task-edt-032"></a>

### EDT-032 – Bidirektionales Layout einschließlich gemischter Textläufe ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [EDT-031](#task-edt-031)

**Ausgangslage:** Bidirektionale Textanordnung fehlt ausdrücklich. Eine einfache Umkehr der Zeichenfolge wäre inkorrekt, insbesondere bei Zahlen, Satzzeichen und gemischtem RTL/LTR. Für internationale Veröffentlichungen priorisieren; ein bewusst deutsch/englisch begrenztes Spiel kann diesen Ausbau zurückstellen.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Text/TextRenderer.cpp`
- `Funke:Funke-Engine/src/Systems/UIEventSystem.cpp`
- `Funke:Funke-ScriptCore/Source/Funke/Utility/TextUtility.cs`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`

**Anweisungen:**

1. Nutze eine dokumentierte Unicode-Bidi-Implementierung und definiere Basisrichtung sowie Sprach-/Richtungsoptionen der Textkomponente.
2. Trenne logische Stringreihenfolge von visuellen Runs und wende Shaping mit der richtigen Richtung auf jeden Run an.
3. Integriere Bidi-Ergebnisse in Umbruch, Alignment, Auswahl und Hit-Testing mit einer nachvollziehbaren Mappingtabelle.
4. Prüfe gemischte arabische/hebräische und lateinische Texte, Zahlen, Satzzeichen und mehrzeilige Eingaben anhand von Referenzfällen.

**Abnahme:**

- [ ] Gemischte RTL/LTR-Ausgabe erscheint korrekt, ohne den gespeicherten String umzuordnen.
- [ ] Caret-/Auswahlpositionen lassen sich zwischen logischem Text und sichtbaren Runs eindeutig abbilden.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Text/TextRenderer.cpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Systems/UIEventSystem.cpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-ScriptCore/Source/Funke/Utility/TextUtility.cs) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md)

<a id="task-edt-033"></a>

### EDT-033 – Graphemcluster, kombinierende Zeichen und Caret-Bewegung korrekt behandeln

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [EDT-031](#task-edt-031), [EDT-032](#task-edt-032)

**Ausgangslage:** Kombinierende Zeichen werden laut Dokumentation nicht korrekt positioniert; der bestehende Textfeldcode arbeitet mit UTF-8-Bytepositionen und Codepoints. Darstellung und Editiergrenzen müssen gemeinsam weiterentwickelt werden. Für internationale Veröffentlichungen priorisieren; ein bewusst deutsch/englisch begrenztes Spiel kann diesen Ausbau zurückstellen.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Systems/UIEventSystem.cpp`
- `Funke:Funke-Engine/src/Graphics/Text/TextRenderer.cpp`
- `Funke:Funke-ScriptCore/Source/Funke/Utility/TextUtility.cs`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`

**Anweisungen:**

1. Definiere anhand Unicode-Graphemgrenzen, was ein editierbarer sichtbarer Cluster ist; trenne Byteoffsets, Codepoints, Grapheme und Shapingcluster.
2. Nutze Shaping-Offets für kombinierende Zeichen statt manuellem Sonderfallzeichnen.
3. Passe links/rechts, Backspace/Delete, Auswahl, Maus-Hit-Test und Zeilenwechsel an die Cluster-/Bidi-Mappings an.
4. Prüfe vor- und nachkomponierte Akzente, mehrere kombinierende Zeichen, Surrogat-/Nicht-BMP-Eingaben und Emoji-ZWJ-Sequenzen auch dann, wenn Farbdarstellung noch fehlt.

**Abnahme:**

- [ ] Caret und Löschen zerlegen keine sichtbaren Graphemcluster unabsichtlich.
- [ ] Akzentposition und gemessene Breite stimmen mit dem Shapingrun überein; gespeicherter Unicode-Text bleibt unverfälscht.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Systems/UIEventSystem.cpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Text/TextRenderer.cpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-ScriptCore/Source/Funke/Utility/TextUtility.cs) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md)

<a id="task-edt-034"></a>

### EDT-034 – Optionales Rendering von Color-Font-/Emoji-Glyphen ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [EDT-030](#task-edt-030), [EDT-031](#task-edt-031), [EDT-033](#task-edt-033)

**Ausgangslage:** Farbemoji-Rendering fehlt. Das ist für viele Spiele optional und soll nach den grundlegenden Font-/Shaping-/Eingabefähigkeiten priorisiert werden.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Text/Font.cpp`
- `Funke:Funke-Engine/src/Graphics/Text/FontManager.cpp`
- `Funke:Funke-Engine/src/Graphics/Text/TextRenderer.cpp`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`

**Anweisungen:**

1. Wähle explizit unterstützte Color-Fontformate anhand einer verfügbaren Font-/Rasterizerbibliothek und dokumentiere nicht unterstützte Formate.
2. Erweitere Atlas, Material-/Blendpfad und Glyphenmetadaten für farbige Raster ohne Regression bei monochromen Texten.
3. Verbinde Variation-Selector-/ZWJ-Ergebnisse mit den vorhandenen geformten Runs und Fallbackfonts.
4. Prüfe Skalierung, gemischte Text-/Emoji-Zeilen, fehlende Glyphen und Speichergrenzen großer Color-Atlanten.

**Abnahme:**

- [ ] Mindestens das ausdrücklich gewählte Color-Fontformat rendert korrekt.
- [ ] Monochrome Fonts bleiben unverändert; Format-, Plattform- und Fontlizenzgrenzen sind dokumentiert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Text/Font.cpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Text/FontManager.cpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Text/TextRenderer.cpp) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md)

<a id="task-edt-035"></a>

### EDT-035 – Restoration-Vertrag für Paketkomponenten in Editor-Transaktionen einführen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P1 · **Art:** Dokumentierte Grenze · **Belegstatus:** bestätigt

**Voraussetzungen:** Ein Referenzpaket für den Enginevertrag.

**Ausgangslage:** EditorSceneEdit lehnt Komponenten mit ownerId und betroffene Teilbäume mit Paketkomponenten ab, weil deren Wiederherstellung nicht garantiert ist. Diese Grenze darf nicht bloß entfernt werden.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Editor/src/Bridge/EditorSceneEdit.cpp`
- `Funke:Funke-Engine/src/Scene/ComponentInfo.hpp`
- `Funke:Funke-Engine/src/Packages/PackageRegistration.hpp`
- `Funke:Funke-Editor/src/Undo/StructureEditCommand.cpp`
- `Funke:Tests/Funke-Editor-Tests/src/EditorSceneEditTests.cpp`

**Anweisungen:**

1. Definiere eine opt-in Fähigkeit für Snapshot, Validierung, Restore und Ressourcen-/Lifecycle-Behandlung von Paketkomponenten.
2. Prüfe vor einer Batchmutation, ob sämtliche betroffenen Komponenten den benötigten Vertrag erfüllen; lasse unsichere Pakete weiterhin nachvollziehbar ablehnen.
3. Implementiere den Vertrag zunächst für eine konkrete offizielle Paketkomponente gemeinsam mit deren Paketcode.
4. Integriere Undo/Redo und Fehler-Rollback und prüfe Version-Mismatch, Paket entladen, Restore wirft und externe Ressourcen.

**Abnahme:**

- [ ] Die ausgewählte Paketkomponente kann per apply, Undo und Redo vollständig und identitätserhaltend verändert werden.
- [ ] Nicht optierte Komponenten bleiben sicher abgewiesen; ein Restorefehler wird als eigener Status mit Recoveryinformation ausgegeben.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Editor/src/Bridge/EditorSceneEdit.cpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Scene/ComponentInfo.hpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Packages/PackageRegistration.hpp) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Editor/src/Undo/StructureEditCommand.cpp) · [Beleg 5](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Tests/Funke-Editor-Tests/src/EditorSceneEditTests.cpp)

<a id="task-edt-036"></a>

### EDT-036 – Dynamische Managed-Komponenten in Transaktionen kontrolliert unterstützen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P1 · **Art:** Dokumentierte Grenze · **Belegstatus:** bestätigt

**Abhängigkeiten:** [EDT-012](#task-edt-012), [EDT-035](#task-edt-035)

**Ausgangslage:** EditorSceneEdit lehnt isDynamic-Komponenten derzeit ab. Scripting-Daten und dynamischer Storage existieren bereits; die Lücke ist ihre sichere transaktionale Wiederherstellung.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Editor/src/Bridge/EditorSceneEdit.cpp`
- `Funke:Funke-Engine/src/Scene/DynamicComponentStorage.hpp`
- `Funke:Funke-ScriptCore/Source/Funke/Interop/DynamicComponentRegistrar.cs`
- `Funke:Tests/Funke-ScriptCore-Tests/ManagedComponentStoreTests.cs`
- `Funke:Tests/Funke-Editor-Tests/src/EditorSceneEditTests.cpp`

**Anweisungen:**

1. Erfasse unterstützte dynamische Feldtypen, Identitäten, Registrierungsrevisionen und Abhängigkeiten zum geladenen Scriptkontext.
2. Implementiere Snapshot-/Restore-Adapter über den gemeinsamen Restoration-Vertrag und verweigere nicht serialisierbare Zustände gezielt.
3. Fixiere eine Transaktion auf dieselbe Script-/Schemaversion oder breche vor Mutation ab, wenn währenddessen Hot Reload beginnt.
4. Prüfe Apply/Undo/Redo über Entfernen/Readd, Typänderung und fehlgeschlagene Registrierung hinweg.

**Abnahme:**

- [ ] Unterstützte dynamische Komponenten lassen sich verlässlich transaktional bearbeiten.
- [ ] Ein Schemawechsel während der Operation erzeugt einen klaren Konflikt statt Restore in einen falschen Speicherlayoutstand.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Editor/src/Bridge/EditorSceneEdit.cpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Scene/DynamicComponentStorage.hpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-ScriptCore/Source/Funke/Interop/DynamicComponentRegistrar.cs) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Tests/Funke-ScriptCore-Tests/ManagedComponentStoreTests.cs) · [Beleg 5](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Tests/Funke-Editor-Tests/src/EditorSceneEditTests.cpp)

<a id="task-edt-037"></a>

### EDT-037 – Native-Script-Teilbäume für sichere Edit-Batches freischalten

**Komplexität:** K4 · **Status:** offen · **Priorität:** P1 · **Art:** Dokumentierte Grenze · **Belegstatus:** bestätigt

**Abhängigkeiten:** EDT-011 (erledigt; siehe [Vertragsdokumentation](Documentation/Editor/Verified-Contracts-2026-11.md)), [EDT-035](#task-edt-035)

**Ausgangslage:** Betroffene Teilbäume mit ScriptType::Native werden ausdrücklich abgewiesen. Inspectorfelder existieren, garantieren aber allein noch keine vollständige Wiederherstellung von nativen Callback-/Ressourcenzuständen.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Editor/src/Bridge/EditorSceneEdit.cpp`
- `Funke:Funke-Engine/src/Scripting/NativeScriptHost.cpp`
- `Funke:Funke-Engine/src/Scripting/NativeScriptField.hpp`
- `Funke:Funke-Engine/src/Scripting/ScriptSystem.cpp`
- `Funke:Tests/Funke-Editor-Tests/src/EditorSceneEditTests.cpp`

**Anweisungen:**

1. Definiere zunächst einen Editmode-Vertrag für authored native Felder, Scriptzuordnung und Referenzen; behandle beliebigen Laufzeitspeicher nicht als serialisierbar.
2. Verhindere oder versioniere Native-DLL-Reload während eines laufenden Apply/Undo-Restore.
3. Nutze die gemeinsamen Restoration-Fähigkeiten, sodass native Skripte ohne vollständigen Vertrag weiterhin abgewiesen werden.
4. Prüfe Entity-Löschen, Wiederherstellen, Native-Konstruktor-/Destruktorfehler, ABI-Wechsel und Undo nach neuem DLL-Build.

**Abnahme:**

- [ ] Ein klar definierter nativer Skripttyp ist in Edit-Batches mit stabiler Identität und authored Feldern unterstützt.
- [ ] Kein Batch lädt alte Funktionspointer nach DLL-Unload wieder; nicht rekonstruierbarer nativer Zustand bleibt ausdrücklich nicht unterstützt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Editor/src/Bridge/EditorSceneEdit.cpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Scripting/NativeScriptHost.cpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Scripting/NativeScriptField.hpp) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Scripting/ScriptSystem.cpp) · [Beleg 5](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Tests/Funke-Editor-Tests/src/EditorSceneEditTests.cpp)

<a id="task-gfx-003"></a>

### GFX-003 – Native Mixer-Busse und eine kleine DSP-Effektkette ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** [GFX-002](#task-gfx-002)

**Ausgangslage:** Das verfügbare Mixer-Paket verwaltet Gruppenpegel, Mutes, Fades und Snapshots. Eine echte DSP-Buspipeline mit Effektrouting ist in der geprüften Implementierung nicht vorhanden.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.AudioMixer/Source/AudioMixerGraph.cs`
- `Funke:Funke-Engine/src/Audio/AudioManager.cpp`
- `Funke:Funke-Engine/src/Audio/IAudioBackend.hpp`

**Anweisungen:**

1. Lege als ersten Umfang Master/Music/SFX-Busse, Parent-Routing und einen Low-Pass-Effekt fest; verschiebe komplexe Reverb-/Sidechain-Systeme in eigene Folgetasks.
2. Integriere die Busstruktur in das Audiobackend und halte Allokationen, Dateizugriffe und blockierende Locks aus dem Audiocallback.
3. Definiere sichere Main-Thread-zu-Audiothread-Parameterupdates sowie Verhalten bei Geräte-Neustart und Play/Stop.
4. Verbinde die vorhandene Gruppen-API mit der nativen Struktur und speichere Routing/Parameter versioniert.
5. Prüfe Kanalformate, stille Busse, schnelle Parameteränderungen und Fehler beim Effektaufbau.

**Abnahme:**

- [ ] Alle Quellen, Streams und OneShots können auf einen Bus geroutet werden.
- [ ] Der definierte Low-Pass beeinflusst nur den zugeordneten Bus und dessen korrekten Parentpfad.
- [ ] Kein Audiothread-Deadlock und keine verlorenen Zuordnungen bei wiederholtem Start/Stop; verbleibende DSP-Lücken sind dokumentiert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.AudioMixer/Source/AudioMixerGraph.cs) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Audio/AudioManager.cpp)

<a id="task-gfx-018"></a>

### GFX-018 – FBX-Import über klar begrenzten, lizenzierten Importpfad ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [GFX-017 (erledigt)](Documentation/Engine-Core/3D-Asset-Workflow-Verification.md)

**Voraussetzungen:** Lizenzierte Importbibliothek, Plattformbuilds und GPU-/Importfixtures.

**Ausgangslage:** FBX wird nicht unterstützt. Das ist eine Importlücke, keine Aussage über bereits vorhandene OBJ-/glTF-Funktionalität.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/MeshAsset.cpp`
- `Funke:Github-Docs/Documentation/Engine-Core/3D.md`
- `Funke:THIRD_PARTY_LICENSES.md`

**Anweisungen:**

1. Vergleiche für einen festgelegten Minimalumfang einen direkten FBX-Importer mit einem dokumentierten Konvertierungspfad nach glTF.
2. Wähle eine mit Funke-Verteilung und Zielplattformen kompatible Abhängigkeit und halte deren Lizenz fest.
3. Implementiere zunächst statische Meshes, Achsen-/Einheitenumrechnung, UVs und Materialzuordnung; kennzeichne Skelett-/Animationsumfang explizit.
4. Erstelle kleine selbst erzeugte FBX-Fixtures und prüfe Scale, Handedness, Pivot und normale/negative Skalierung.
5. Dokumentiere reproduzierbare Fehler für nicht unterstützte FBX-Varianten.

**Abnahme:**

- [ ] Der definierte FBX-Minimalumfang funktioniert auf der vorgesehenen Desktopmatrix.
- [ ] Material-/Transformzuordnung stimmt für die Referenzdateien.
- [ ] Nicht implementierte Animation-/Formatvarianten werden nicht als unterstützt beworben.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L265-L269) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/MeshAsset.cpp)

<a id="task-gfx-019"></a>

### GFX-019 – Transparenzgrenzen reproduzieren und einen begrenzten verbesserten Renderpfad anbieten

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [GFX-016 (erledigt)](Documentation/Engine-Core/3D-Asset-Workflow-Verification.md)

**Ausgangslage:** Transparente Meshes werden nach Objektzentrum sortiert, nur Frontfaces gezeichnet und bei Cast Shadows opak verschattet. Sich schneidende/konkave transparente Objekte sind dadurch ausdrücklich eingeschränkt.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Funke-Engine/src/Graphics/Material.hpp`
- `Funke:Github-Docs/Documentation/Engine-Core/3D.md`

**Anweisungen:**

1. Erstelle Referenzszenen für zwei sich schneidende Flächen, ein konkaves Mesh, Rückseiten und Glasschatten.
2. Lege je Fall einen expliziten Vertrag fest: Double-Sided-Option, Shadow-Modus und ein opt-in verbessertes Transparenzverfahren.
3. Implementiere einen begrenzten Pfad wie Depth-Peeling mit festen Layern oder dokumentierte Order-Independent-Approximation; behaupte keine universell korrekte Sortierung.
4. Erhalte den bisherigen günstigen Alpha-Sortierpfad als Default für einfache Szenen.
5. Vergleiche Bildfehler, GPU-Zeit und Speicherverbrauch anhand der Referenzszenen.

**Abnahme:**

- [ ] Unterstützte Problemfälle verbessern sich reproduzierbar und bleiben bei Kamerabewegung stabil.
- [ ] Rückseiten- und Schattenverhalten sind explizit einstellbar statt zufällig.
- [ ] Unvermeidbare Grenzen des gewählten Verfahrens sind dokumentiert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L55-L57) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Renderer3D.cpp#L2551)

<a id="task-gfx-020"></a>

### GFX-020 – Punkt- und Spotlichtschatten mit begrenztem Schattenbudget implementieren

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [GFX-016 (erledigt)](Documentation/Engine-Core/3D-Asset-Workflow-Verification.md)

**Ausgangslage:** Aktuell wirft ausschließlich das Haupt-Directional-Light Schatten. Point-/Spot-Lights sind vorhanden, werfen aber keine Schatten.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Funke-Engine/src/Graphics/Lighting3D.hpp`
- `Funke:Funke-Engine/src/Graphics/Shader.cpp`

**Anweisungen:**

1. Definiere ein Schattenbudget und Auflösungsstufen für lokale Lichter; starte mit einem Spot und einem Punktlicht.
2. Implementiere Spot-Shadowmaps und einen geeigneten Punktlichtpfad einschließlich Culling und stabiler Ressourcenverwaltung.
3. Ergänze Bias, Reichweite, Quality-Fallback und pro-Licht-Einstellungen.
4. Binde opaque/cutout/skinned/custom-displaced Casters konsistent ein.
5. Prüfe bewegte Lichtquellen, Schattenatlas-Recycling und Budgetüberschreitungen.

**Abnahme:**

- [ ] Ein Punkt- und ein Spotlicht werfen in Referenzszenen korrekte Schatten.
- [ ] Keine ungültige Ressourcennutzung beim Ein-/Ausschalten oder Wiederverwenden von Lichtern.
- [ ] Leistungs- und Speichermehrkosten sind für den definierten Budgetfall gemessen.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L127-L134) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L269)

<a id="task-gfx-021"></a>

### GFX-021 – Cascaded Shadow Maps für das Hauptlicht ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Es existiert eine einzelne texelstabilisierte Directional-Shadowmap mit begrenzter Shadow Distance, aber keine Cascaded Shadow Maps.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Lighting3D.hpp`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Funke-Engine/src/Graphics/Shader.cpp`

**Anweisungen:**

1. Ergänze eine konfigurierbare kleine Cascade-Anzahl und einen klaren Split-/Blend-Vertrag.
2. Berechne stabile texelgesnappte Lichtmatrizen pro Cascade und erhalte Offscreen-Caster.
3. Erweitere GPU-Daten, Shaderauswahl, Bias und Cascade-Übergänge.
4. Prüfe weite Kamerafahrten, große Koordinaten und nahe sowie ferne Details.
5. Miss Draw- und GPU-Mehrkosten gegen die bestehende Ein-Map-Option.

**Abnahme:**

- [ ] Nahe Details bleiben bei großer Schattenreichweite sichtbar.
- [ ] Cascade-Grenzen flackern nicht in der definierten Kamerafahrt.
- [ ] Einzelmap-Modus bleibt als günstige Option verfügbar.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Lighting3D.hpp) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L269)

<a id="task-gfx-022"></a>

### GFX-022 – Environment-Cubemap-Reflexionen mit vorgefilterten Roughness-Stufen ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [GFX-015 (erledigt)](Documentation/Engine-Core/3D-Asset-Workflow-Verification.md)

**Ausgangslage:** Skybox-Materialien und vereinfachtes Sky-Ambient existieren. Environment-Cubemap-Reflexionen werden ausdrücklich als noch nicht verfügbar genannt.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Shader.cpp`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Github-Docs/Documentation/Engine-Core/3D.md`

**Anweisungen:**

1. Definiere ein Environmentasset mit Cubemap bzw. Panoramaimport und einer linearen HDR-Farbkonvention.
2. Berechne oder importiere vorgefilterte Roughness-Mips und die nötige BRDF-Hilfsfunktion/-LUT.
3. Integriere Specular-IBL und optionalen Diffuse-Anteil in das Standard-PBR.
4. Lege Fallback, Assetpruning und Qualitätsbudgets fest; starte ohne dynamische Reflection-Probes.
5. Prüfe metallische und raue Referenzkugeln bei verschiedenen Environmentrichtungen.

**Abnahme:**

- [ ] Roughness verändert die Reflektionsunschärfe nachvollziehbar.
- [ ] Reflexionen passen zu Orientierung und Farbraum des Environments.
- [ ] Dynamische lokale Probes werden nicht fälschlich als mit implementiert ausgewiesen.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L127-L149) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L269)

<a id="task-gfx-023"></a>

### GFX-023 – Mehrere Spielansichten und Split Screen mit getrennten Viewzuständen unterstützen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Mehrere Kameras können existieren, aber Runtime wählt eine Haupt-2D-Kamera bzw. die erste aktivierte 3D-Kamera. Split Screen ist nicht unterstützt.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/Renderer3D.hpp`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Funke-Engine/src/Graphics/Renderer2D.cpp`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`

**Anweisungen:**

1. Definiere Kamera-Viewport, Renderreihenfolge, Clear-Modus und RenderTarget als öffentliche Schnittstelle.
2. Isoliere per-View-Uniforms, Depth-, Shadow-, Postprocess- und temporäre Bufferzustände.
3. Render zwei Ansichten in getrennte Viewports und berücksichtige pro View Culling, Auflösung und Eingabekoordinaten.
4. Lege Screen-/World-UI- sowie AudioListener-Verhalten explizit fest.
5. Prüfe eine Kombination aus 2D, 3D, UI, Posteffects und unterschiedlich großen Viewports.

**Abnahme:**

- [ ] Zwei aktive Kameras erzeugen gleichzeitig die definierten Ansichten.
- [ ] Uniforms und Posteffekte einer Ansicht beeinflussen die andere nicht.
- [ ] Mouse-/Screen-to-World-Abfragen verwenden den richtigen Viewport.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Graphics/Renderer3D.hpp)

<a id="task-gfx-029"></a>

### GFX-029 – Compound-Collider für 2D mit eindeutigem Ownership-Vertrag ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Dokumentiert ist ein Collider pro Entity; Collider auf Kind-Entities bilden separate Bodies. 2D-Compound-Verhalten wie mehrere Shapes eines gemeinsamen Rigidbody fehlt.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Physics/PhysicsSystem2D.cpp`
- `Funke:Funke-Engine/src/Components/Physics/Collider2D.cpp`
- `Funke:Funke-Engine/src/Components/Physics/Rigidbody2DComponent.cpp`

**Anweisungen:**

1. Definiere eine Rigidbody-Owner-Auflösung und eine explizite Regel für Kind-Entities mit eigenem Rigidbody.
2. Fasse Kind-Collidershapes unter einem Body zusammen und berechne lokale Pose, Masse, Sensor-/Materialparameter korrekt.
3. Leite Kontakt-/Raycastresultate auf Owner und konkreten Collider zurück.
4. Behandle Reparenting, Scaleänderungen, Shapeänderungen und Entfernen einzelner Collider.
5. Migriere das Verhalten opt-in oder versioniert, damit bestehende separate Child-Bodies nicht still zusammengelegt werden.

**Abnahme:**

- [ ] Ein zusammengesetzter Körper verhält sich als ein Body mit mehreren Shapes.
- [ ] Kontakte identifizieren den beteiligten Collider eindeutig.
- [ ] Alte Szenen ändern ihr Physikverhalten nicht ohne definierte Migration.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Physics-2D.md#L1-L17) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Physics/PhysicsSystem2D.cpp)

<a id="task-gfx-031"></a>

### GFX-031 – 3D-Character-Controller für einen definierten Basisspieler anbieten

**Komplexität:** K4 · **Status:** offen · **Priorität:** P1 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** GFX-030 (erledigt; siehe [Vertragsdokumentation](Documentation/Editor/Verified-Contracts-2026-11.md))

**Ausgangslage:** Ein dedizierter Character Controller wird nicht exponiert. Rigidbody-, Collider-, Raycast- und Triggerfunktionen existieren bereits.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Physics3D/PhysicsWorld3D.cpp`
- `Funke:Funke-Engine/src/Physics3D/PhysicsSystem3D.cpp`
- `Funke:Funke-ScriptCore/Source/Funke/Scene/ComponentsPhysics3D.cs`

**Anweisungen:**

1. Definiere kapselbasiertes Bewegen, Bodenstatus, MaxSlope, StepHeight und Gravity-Vertrag für den ersten Umfang.
2. Nutze den passenden Jolt-Characterpfad und integriere ihn in FixedUpdate, Transform und Szenen-Lifecycle.
3. Ergänze Move-/Jump-/Grounded-API und einen minimalen Beispielcontroller.
4. Prüfe Treppen, schräge Flächen, niedrige Decken, bewegte Plattformen und Spawn in Überlappung.
5. Lege klar fest, wie dynamische Bodies, Teleports und Netzwerkreplikation später angebunden werden können.

**Abnahme:**

- [ ] Die definierten Bewegungsfälle laufen bei unterschiedlichen Render-FPS stabil auf derselben Physikrate.
- [ ] Kein dauerhaftes Festhängen an üblichen Treppen-/Kantenfixtures.
- [ ] Plattform-/Slope-/Overlap-Grenzen sind dokumentiert statt als universell gelöst beworben.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Physics-3D.md#L98-L101) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Physics3D/PhysicsWorld3D.cpp)

<a id="task-gfx-036"></a>

### GFX-036 – Animations-Layers und Avatar-Masks mit klarer Additivsemantik ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Ausgangslage:** Es gibt eine Controller-Layer. Mehrere Layers und Avatar-/Bone-Masks sind nicht verfügbar.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Animation/AnimatorController.cpp`
- `Funke:Funke-Engine/src/Animation/AnimatorRuntime.cpp`
- `Funke:Funke-Engine/src/Graphics/SkeletalAnimation.cpp`
- `Funke:Funke-Editor/src/Gui/AnimationWindow.cpp`

**Anweisungen:**

1. Definiere zunächst Base plus eine zusätzliche Override-Layer und ein versioniertes Maskasset.
2. Erweitere Posenkomposition, Layergewicht und Maskauflösung auf Bones bzw. animierbare Properties.
3. Füge erst danach einen klar spezifizierten Additive-Modus mit Referenzpose hinzu.
4. Integriere Editorvorschau, Parametersteuerung, Serialisierung und Fehler für nicht vorhandene Bones.
5. Teste laufenden Unterkörper mit unabhängigem Oberkörper sowie Layergewicht 0/1 und Clipwechsel.

**Abnahme:**

- [ ] Maskierte Bones/Properties bleiben von der Zusatzlayer unberührt.
- [ ] Layerausgabe ist unabhängig von zufälliger Registrierungsreihenfolge.
- [ ] Additive-Pose und Referenzpose sind reproduzierbar gespeichert und dokumentiert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Animation.md#L211-L215) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L267)

<a id="task-gfx-037"></a>

### GFX-037 – Root Motion mit kontrollierter Physik-/Scriptübergabe implementieren

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** GFX-034 (erledigt; siehe Vertragsdokumentation)

**Ausgangslage:** Root Motion fehlt. Bewegung kann bisher aus Scripts kommen; Skinning und Clip-Crossfades sind bereits vorhanden.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/SkeletalAnimation.cpp`
- `Funke:Funke-Engine/src/Systems/SkinnedMeshAnimationSystem.cpp`
- `Funke:Funke-Engine/src/Animation/AnimatorRuntime.cpp`

**Anweisungen:**

1. Definiere Root-Bone/-Node, extrahierbare Translations-/Rotationsachsen und In-Place-Fallback.
2. Berechne Root-Deltas über Loopgrenzen, negative Playbackgeschwindigkeit und Crossfades.
3. Stelle die Deltas einem Script/Controller in FixedUpdate zur Verfügung und vermeide doppelte Transformbewegung.
4. Lege Verhalten bei Cull Pose, Cull Completely und Distance Rate fest.
5. Teste Loopübergang, Pause, Clipwechsel, Teleport und vom Controller blockierte Bewegung.

**Abnahme:**

- [ ] Ein Loop erzeugt keinen Positionssprung an seiner Grenze.
- [ ] Physik-/Controllerkonflikte sind explizit geregelt.
- [ ] Culling verursacht keine verlorenen oder nachträglich explodierenden Bewegungsdeltas.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Animation.md#L211-L215) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Systems/SkinnedMeshAnimationSystem.cpp)

<a id="task-gfx-038"></a>

### GFX-038 – glTF-Morph-Targets einschließlich animierter Gewichte importieren und rendern

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [GFX-027 (erledigt)](Documentation/Editor/Performance-Remaining-2026-11.md)

**Ausgangslage:** glTF-Morph-Targets/Blendshapes werden nicht unterstützt. Bestehendes GPU-Skinning verarbeitet bereits Bones; Morphing muss geordnet davor oder integriert erfolgen.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Graphics/MeshAsset.cpp`
- `Funke:Funke-Engine/src/Graphics/SkeletalAnimation.hpp`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`

**Anweisungen:**

1. Erweitere Importdaten um Positions-/Normal-/Tangentendeltas und Defaultgewichte.
2. Lese Animationchannels für Gewichte und definiere die Kombination mit Bone-Skinning.
3. Implementiere zunächst ein festgelegtes begrenztes Morphbudget mit CPU-Fallback oder Computeauswertung.
4. Expose Gewichte im Inspector und in C#; aktualisiere Bounds/Culling für deformierte Geometrie.
5. Prüfe Gesichtsausdrücke, mehrere Targets, negative/über-1-Gewichte nach Vertrag und Bone+Morph-Kombination.

**Abnahme:**

- [ ] Referenz-GLBs zeigen ihre gespeicherten und animierten Morphgewichte.
- [ ] Morphing verliert keine Schatten oder sichtbaren Randbereiche durch falsche Bounds.
- [ ] Budgetüberschreitung wird diagnostiziert und erzeugt keinen stillen Datenverlust.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L61-L65) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L265-L270)

<a id="task-gfx-042"></a>

### GFX-042 – Begrenzte Partikelkollisionen und ereignisbasierte Subemitter einführen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** GFX-030 (erledigt; siehe [Vertragsdokumentation](Documentation/Editor/Verified-Contracts-2026-11.md))

**Ausgangslage:** Partikelkollisionen und Subemitter fehlen. Die vorhandene CPU-Simulation aktualisiert Position/Geschwindigkeit/Lebenszeit direkt.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Components/Graphics/ParticleSystem3DComponent.cpp`
- `Funke:Funke-Engine/src/Systems/Particle3DUpdateSystem.cpp`
- `Funke:Funke-Engine/src/Physics3D/PhysicsWorld3D.cpp`

**Anweisungen:**

1. Definiere zuerst Plane- oder Raycast-Kollisionen mit einem festen Querybudget statt unbeschränkter Physik pro Partikel.
2. Ergänze Bounce, Damping und Kill-Verhalten mit eindeutigen Einheiten.
3. Füge Spawn-/Death-/Collision-Ereignisse und begrenzt verschachtelbare Subemitter hinzu.
4. Verhindere unendliche Emissionsketten und definiere Quality-Scaling.
5. Teste schnelle Partikel, Budgetüberschreitung, Parentzerstörung und wiederholtes Play/Stop.

**Abnahme:**

- [ ] Kollisionen halten den festgelegten Query-/Emissionsrahmen ein.
- [ ] Ein zyklischer Subemittergraph wird abgelehnt oder sicher begrenzt.
- [ ] Events erfolgen nicht doppelt und bleiben bei deaktivierten Systemen kontrolliert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Components/Graphics/ParticleSystem3DComponent.cpp#L90-L117) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L270)

<a id="task-gfx-043"></a>

### GFX-043 – GPU-Partikelsimulation als zusätzlichen begrenzten Backendpfad bauen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-006](Documentation/Editor/Testing.md#verified-desktop-ci-run)

**Ausgangslage:** 3D-Partikel werden auf der CPU simuliert. GPU-Simulation fehlt; das vorhandene GPU-Skinning ist eine andere Funktion und darf nicht als Partikelsimulation gezählt werden.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Components/Graphics/ParticleSystem3DComponent.cpp`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke:Funke-Engine/src/Systems/Particle3DUpdateSystem.cpp`

**Anweisungen:**

1. Definiere einen kompatiblen Kernumfang für Emit, Lebenszeit, Gravity/Drag und Kurven ohne zunächst jedes CPU-Feature zu duplizieren.
2. Implementiere GPU-Pool, Alive-/Dead-Listen und Computeupdate mit begrenzten Buffergrößen.
3. Verbinde Simulation und instanziertes Rendering ohne regelmäßiges vollständiges CPU-Readback.
4. Erhalte CPU-Fallback und kennzeichne nicht unterstützte Module im Inspector.
5. Vergleiche visuelle/statistische Ergebnisse sowie CPU-/GPU-Zeit für feste Lastszenen.

**Abnahme:**

- [ ] Große Partikellasten bewegen sich ohne CPU-Update jedes einzelnen Partikels.
- [ ] Budgetgrenzen und Device-Limits werden sauber behandelt.
- [ ] Die Dokumentation nennt ausdrücklich, welche Module im GPU-Pfad verfügbar sind.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Components/Graphics/ParticleSystem3DComponent.cpp#L90-L117) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/3D.md#L270)

<a id="task-net-006"></a>

### NET-006 – Versionierte Szenensynchronisierung und Late-Join-Bereitschaft ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Voraussetzungen:** Pakettests und lokale Mehrprozessprüfung; reale Internet-/Browserläufe separat.

**Ausgangslage:** Automatische Szenensynchronisierung fehlt. Entwickler müssen derzeit auf allen Peers selbst dieselbe Szene laden.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/NetSession.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/NetworkObjects.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/NetworkManager.cs`

**Anweisungen:**

1. Definiere serverautoritative Scene-IDs, Übergangsepochen, Inhalts-/Protokollversion und Ready-Bestätigungen.
2. Verknüpfe Laden/Abbrechen mit dem vorhandenen Szenenloader und puffere oder verweigere Spawns bis zur passenden Szene.
3. Behandle Late Join, fehlende Szene, Timeout, Disconnect und additive Szenen ausdrücklich.
4. Verwerfe verspätete Nachrichten aus einer alten Szenenepoche und vermeide doppelte Spawns.
5. Erstelle einen Mehrprozess-Test mit langsamem Client und einem Szenenwechsel während des Verbindungsaufbaus.

**Abnahme:**

- [ ] Kein Spawn wird auf die falsche Szene angewendet.
- [ ] Ein nicht kompatibler Client erhält eine klare Fehlermeldung.
- [ ] Host, Dedicated Server und Late Join werden geprüft.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/Source/Internal/NetSession.cs)

<a id="task-net-007"></a>

### NET-007 – Interest Management für große Multiplayerwelten ergänzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** [NET-006](#task-net-006)

**Voraussetzungen:** Pakettests und lokale Mehrprozessprüfung; reale Internet-/Browserläufe separat.

**Ausgangslage:** Die geprüfte Transformversendung verteilt Updates an Clients; eine allgemeine räumliche Relevanzfilterung wurde nicht nachgewiesen.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/NetworkObjects.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/NetSession.cs`

**Anweisungen:**

1. Prüfe vorhandene Observer-/Relevanzmechanismen und definiere bei Fehlen eine serverseitige Observer-Abstraktion.
2. Implementiere zunächst einen räumlichen Gridfilter mit Hysterese und einem expliziten AlwaysRelevant-Pfad.
3. Synchronisiere Sichtbarkeitswechsel mit Spawn/Despawn beziehungsweise Ruhe-/Wiedereintrittssemantik, einschließlich Ownership.
4. Messe Bandbreite, Serverzeit und Speicher gegenüber dem bisherigen Broadcast bei gleicher Welt.
5. Teste schnell bewegte Spieler, Teleports, Szenenwechsel und Objekte an Zellgrenzen.

**Abnahme:**

- [ ] Clients erhalten nur zulässige und relevante Objektdaten nach der definierten Policy.
- [ ] Eintritt/Austritt erzeugt keine verlorenen oder doppelten Objekte.
- [ ] Die gewählte Last zeigt nachvollziehbare Einsparungen oder die Optimierung wird entsprechend eingegrenzt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/Source/Internal/NetworkObjects.cs)

<a id="task-net-008"></a>

### NET-008 – Prediction und Reconciliation für einen begrenzten Bewegungstyp umsetzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** NET-003 (erledigt; siehe Vertragsdokumentation), NET-004 (erledigt; siehe Vertragsdokumentation)

**Voraussetzungen:** Mehrprozess-/Netzemulationsläufe zusätzlich; diese Aufgabe ist eine erste begrenzte Prediction-Implementierung, kein komplettes kompetitives Netcode-System.

**Ausgangslage:** Client Prediction und Rollback fehlen. Owner-authoritative Bewegung ist kein Ersatz für serverautoritative Reconciliation.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Networking/Source/NetworkTransform.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/NetClock.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/ (Prediction-Modul neu)`

**Anweisungen:**

1. Wähle einen klar begrenzten Beispielcontroller mit definiertem Tick und Inputformat; beginne nicht mit allgemeinem deterministischem Physik-Rollback.
2. Führe Sequenznummern, Eingabebestätigung, begrenzte Clienthistorie und serverautoritative Zustände ein.
3. Rekonstruiere beim Client ab bestätigtem Zustand und wiederhole nur noch offene Eingaben; glätte die visuelle Korrektur getrennt von der Simulation.
4. Definiere Verhalten bei Paketverlust, Jitter, Historyüberlauf, Teleport und Tickdrift.
5. Prüfe mehrere Latenz-/Verlustprofile und messe Abweichung sowie Korrekturhäufigkeit.
6. Dokumentiere, welche Physik-/Gameplayfälle nicht deterministisch unterstützt werden.

**Abnahme:**

- [ ] Der Server entscheidet über den bestätigten Bewegungszustand.
- [ ] Bei schlechten Netzbedingungen bleibt die Historie begrenzt und die Bewegung korrigierbar.
- [ ] Es wird kein universelles Rollback für Box2D/Jolt behauptet.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/Source/NetworkTransform.cs)

<a id="task-net-009"></a>

### NET-009 – Lag Compensation als getrennte serverseitige Erweiterung evaluieren

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** [NET-008](#task-net-008)

**Voraussetzungen:** Pakettests und lokale Mehrprozessprüfung; reale Internet-/Browserläufe separat.

**Ausgangslage:** Für schnelle serverautoritative Spiele ist aus dem aktuellen Funktionsstand kein fertiger Rewind-/Lag-Compensation-Pfad nachgewiesen. Dieser Ausbau ist spielzielabhängig.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/NetClock.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/ (Lag-Compensation-Modul neu)`

**Anweisungen:**

1. Definiere zunächst das unterstützte Treffer-/Abfragemodell und den maximal akzeptierten Rückblick.
2. Speichere eine begrenzte serverseitige Transform-/Hitboxhistorie und validiere Clientzeitangaben gegen Serverwissen.
3. Führe Abfragen gegen historische Daten aus, ohne den laufenden Physikzustand unkontrolliert zurückzusetzen.
4. Teste Despawn, wechselnde Hitboxen, Teleports, hohe Latenz und manipulierte Zeitstempel.
5. Dokumentiere Fairness- und Ressourcenabwägungen sowie die Grenzen der Lösung.

**Abnahme:**

- [ ] Historische Abfragen verändern die aktuelle Welt nicht.
- [ ] Clients können keinen beliebigen Zeitpunkt erzwingen.
- [ ] Speicher und Rückblick sind begrenzt und messbar.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/Source/Internal/NetClock.cs)

<a id="task-net-010"></a>

### NET-010 – Sicheren Transportpfad mit ehrlicher TLS-/UDP-Abgrenzung bereitstellen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** NET-004 (erledigt; siehe Vertragsdokumentation), NET-005 (erledigt; siehe Vertragsdokumentation)

**Voraussetzungen:** Eine geeignete Transportbibliothek und Testziele für die tatsächlich unterstützten Protokolle.

**Ausgangslage:** UDP und ws:// sind unverschlüsselt. Die README beschreibt für wss:// bereits einen TLS-Reverse-Proxy; dieser schützt nicht automatisch den parallelen UDP-Pfad.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/UdpTransport.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/WebSocketClientTransport.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/WebSocketServerTransport.cs`

**Anweisungen:**

1. Definiere anhand des vorgesehenen Spiels, welche Daten und Identitäten geschützt werden müssen; erfasse Plattform- und Browsergrenzen.
2. Biete einen getesteten wss://-Pfad mit Zertifikatsprüfung und klarer Serverkonfiguration an; deaktiviere Zertifikatsprüfung nicht zur Fehlerbehebung.
3. Wähle für einen verschlüsselten UDP-Pfad ein etabliertes, kompatibles Protokoll beziehungsweise eine gepflegte Bibliothek statt selbst entworfener Kryptografie.
4. Implementiere Schlüssel-/Session-/Replay-Verhalten entsprechend dem gewählten Protokoll und trenne Verschlüsselung von Spielautorisierung.
5. Teste falsche Zertifikate, abgebrochene Handshakes, Replay/alte Sessions und gemischte Transportverbindungen.
6. Veröffentliche im Task keine echten Zertifikate, Tokens oder einen ungefragten externen Server.

**Abnahme:**

- [ ] Die Sicherheitsanzeige entspricht dem tatsächlich verwendeten Transport.
- [ ] Keine Klartext-Fallbacks ohne ausdrückliche Konfiguration.
- [ ] Ein geprüfter Negativfall wird korrekt abgelehnt.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/Source/Internal/UdpTransport.cs)

<a id="task-net-012"></a>

### NET-012 – NAT-Punchthrough mit definiertem Fallback prüfen und integrieren

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [NET-011](#task-net-011)

**Voraussetzungen:** Tests benötigen unterschiedliche Netze und einen Rendezvousdienst; ohne diese bleiben praktische Internetfälle offen.

**Ausgangslage:** Das Paket dokumentiert fehlendes NAT-Punchthrough, obwohl der eingebundene Transportcode entsprechende Drittanbieterbausteine enthalten kann. Vorhandene Dateien sind kein fertig integrierter Dienst.

**Dateien und Einstiegspunkte:**

- `Funke-PackageRegistry:packages/Funke.Networking/Source/ThirdParty/LiteNetLib/NatPunchModule.cs`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/UdpTransport.cs`

**Anweisungen:**

1. Prüfe die tatsächlich eingebundene Version und welche Rendezvous-/Punchthrough-APIs nutzbar sind.
2. Definiere einen authentisierten Rendezvousablauf mit Zeitlimits und Schutz vor missbräuchlicher Fremdadressverwendung.
3. Integriere kontrollierte Verbindungsversuche und falle bei nicht unterstütztem NAT auf Relay oder eine klare Fehlermeldung zurück.
4. Teste unterschiedliche Netze/NAT-Typen; localhost oder zwei Prozesse im selben LAN reichen als Internetnachweis nicht.
5. Dokumentiere nicht lösbare Fälle wie bestimmte Carrier-/symmetrische NAT-Konfigurationen.

**Abnahme:**

- [ ] Verbindungsversuche enden deterministisch mit Verbindung oder verständlichem Fallback.
- [ ] Erfolgreiche Tests benennen die geprüften Netzbedingungen.
- [ ] Es wird keine universelle Portfreigabe-Freiheit versprochen.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/README.md) · [Beleg 2](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/0fa5a15540073926e3784e1eb3d92eb011ccf6f7/packages/Funke.Networking/Source/ThirdParty/LiteNetLib/NatPunchModule.cs)

<a id="task-qal-015"></a>

### QAL-015 – Android-ABI-Grenzen explizit machen und zusätzliche Architektur gezielt ausbauen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-014](#task-qal-014)

**Voraussetzungen:** Passende Dawn-, SDL-, Mono- und native Paketartefakte sowie Gerät oder Emulator für die neue ABI.

**Ausgangslage:** Known Limitations nennt arm64-v8a als einzige aktuelle Androidarchitektur. Zusätzliche ABIs sind eine Funktionserweiterung, kein bestehender arm64-Bug.

**Dateien und Einstiegspunkte:**

- `Funke:platforms/Funke.Platform.Android/Builder/android_toolchain.py`
- `Funke:platforms/Funke.Platform.Android/Builder/android_libraries.py`
- `Funke:platforms/Funke.Platform.Android/Builder/android_bundle.py`
- `Funke:platforms/Funke.Platform.Android/Native/CMakeLists.txt`
- `Funke:scripts/ci/test_android_builder.py`

**Anweisungen:**

1. Prüfe ABI-Annahmen in Toolchain, Mono-/Dawnartefakten, APK/AAB-Packaging und Paketauflösung.
2. Lege zunächst die unterstützte ABI-Matrix und klare Fehlermeldungen für nicht unterstützte Kombinationen fest.
3. Erweitere für eine konkret benannte nächste ABI, bevorzugt einen tatsächlich verfügbaren Emulator-/Testgerätefall, sämtliche erforderlichen Build- und Bibliothekspfade.
4. Prüfe, dass kein arm64-Binary irrtümlich unter einem anderen ABI-Verzeichnis landet und abhängige native Pakete dieselbe Architektur liefern.
5. Führe die vollständige echte Androidabnahme für jede neu deklarierte ABI durch; dokumentiere verbleibende Architekturen weiter als nicht unterstützt.

**Abnahme:**

- [ ] Aktuelle arm64-Exports bleiben funktionsfähig.
- [ ] Eine zusätzlich deklarierte ABI besitzt echte passende Bibliotheken und einen bestandenen Laufzeitnachweis.
- [ ] Nicht unterstützte ABIs scheitern vor teurem Build mit einer konkreten Meldung.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.Platform.Android/Builder/android_toolchain.py) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.Platform.Android/Builder/android_libraries.py)

<a id="task-qal-020"></a>

### QAL-020 – Web-Jobausführung transparent machen und optionalen Threadmodus entwickeln

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-018](#task-qal-018)

**Voraussetzungen:** Benötigt einen Browser/Server mit den Voraussetzungen für SharedArrayBuffer beziehungsweise den gewählten Threadmechanismus. Isolation-/Headerbereitstellung und Webneubauten zusätzlich; fehlende Browserfähigkeit muss einen getesteten sequentiellen Fallback ergeben.

**Ausgangslage:** Known Limitations dokumentiert, dass Jobs im aktuellen Browserbuild sequentiell laufen. Ein Threadmodus wäre eine neue Fähigkeit; die vorhandene sequentielle Ausführung ist nicht per se falsch.

**Dateien und Einstiegspunkte:**

- `Funke:platforms/Funke.Platform.Web/Builder/build.py`
- `Funke:platforms/Funke.Platform.Web/Builder/preview.py`
- `Funke:platforms/Funke.Platform.Web/Native/WebMain.cpp`
- `Funke:platforms/Funke.Platform.Web/Template/dotnet/main.js`
- `Funke:Github-Docs/Documentation/Editor/Platform-Packages.md`

**Anweisungen:**

1. Erfasse die vorhandenen Job-/Runtimeanforderungen und stelle die tatsächlich verfügbare Parallelität über eine Capability dar.
2. Bewahre einen korrekten sequentiellen Fallback und teste darin Abhängigkeiten/Completion ohne Desktopannahmen.
3. Entwickle einen expliziten optionalen Web-Threadbuild einschließlich erforderlicher nativer/Mono-Konfiguration und Serverheader.
4. Prüfe Start und Fehlerdiagnosen bei vorhandener und fehlender Cross-Origin-Isolation; schalte nicht stillschweigend in einen inkorrekten Zustand.
5. Messe einen identischen realen Jobworkload in beiden Modi, um Nutzen und Overhead zu belegen.

**Abnahme:**

- [ ] Das Spiel kann zur Laufzeit korrekt feststellen, ob Parallelität verfügbar ist.
- [ ] Sequentieller und optional paralleler Modus liefern gleiche fachliche Ergebnisse.
- [ ] Der Threadmodus wird nur für tatsächlich getestete Browser-/Hostingkombinationen als unterstützt dokumentiert.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Platform-Packages.md) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.Platform.Web/Builder/build.py)

<a id="task-qal-022"></a>

### QAL-022 – Optionales Web-Trimming mit nachgewiesener Script-/Reflectionkompatibilität anbieten

**Komplexität:** K4 · **Status:** offen · **Priorität:** P2 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-018](#task-qal-018)

**Voraussetzungen:** Erfordert echte Webexport- und Browsertests; Linker-/Wasmneubauten zusätzlich. Kein Größen-/Startzeitgewinn als erwiesen ausgeben, bevor Original und getrimmtes Artefakt gemessen wurden.

**Ausgangslage:** Die Dokumentation nennt die ungetrimmte Klassenbibliothek des aktuellen Webexports als Grenze. Funke nutzt Script-/Paketentdeckung und Reflection, deren Nutzung ein Trimmer erhalten muss.

**Dateien und Einstiegspunkte:**

- `Funke:platforms/Funke.ScriptRuntime.Mono/Builder/dotnet_wasm.py`
- `Funke:platforms/Funke.Platform.Web/Builder/build.py`
- `Funke:platforms/Funke.Platform.Web/Template/dotnet/main.js`
- `Funke:Funke-ScriptCore/Source/Funke/Interop/PackageScriptRegistry.cs`
- `Funke:Github-Docs/Documentation/Editor/Platform-Packages.md`

**Anweisungen:**

1. Erfasse dynamisch entdeckte Script-/Pakettypen und Reflectionpfade, die beim Trimming erhalten bleiben müssen.
2. Führe ein optionales Releaseprofil mit passenden Deskriptoren/Annotations beziehungsweise generierten Registrierungen ein.
3. Teste die tatsächlich benötigten Scriptlebenszyklen, Paketregistrierung, Serialisierung und Fehlerausgaben im getrimmten Browserexport.
4. Vergleiche Downloadgröße, dekomprimierte Größe, Startzeit und Speicher mit demselben ungetrimmten Projekt.
5. Bewahre einen klar benannten kompatiblen ungetrimmten Fallback und dokumentiere Grenzen statt Compilerwarnungen pauschal auszublenden.

**Abnahme:**

- [ ] Eine repräsentative getrimmte Anwendung erfüllt dieselben Fachassertions wie der ungetrimmte Export.
- [ ] Reflection-/Linkerwarnungen sind bewertet; fehlende Typen/Methoden werden in Regressionstests sichtbar.
- [ ] Der tatsächliche Größengewinn und zusätzliche Voraussetzungen sind angegeben.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/platforms/Funke.ScriptRuntime.Mono/Builder/dotnet_wasm.py) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Platform-Packages.md)

<a id="task-qal-024"></a>

### QAL-024 – Desktop-Crossexport über explizite Zielbuilder statt stiller Hostannahmen ermöglichen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Dokumentierte Grenze · **Belegstatus:** dokumentiert

**Abhängigkeiten:** [QAL-008](#task-qal-008), [QAL-009](#task-qal-009)

**Voraussetzungen:** Umfasst einen ersten klar begrenzten Mehrhost-Buildpfad, keine universelle Crosscompilation. Zielworker, SDKs, Runnerbereitstellung und mehrere vollständige Builds zusätzlich; neue Remote-/CI-Ausführung nur mit vorhandener Autorisierung.

**Ausgangslage:** Desktopspiele werden derzeit ausschließlich für die Plattform des Editors exportiert. Dies ist eine dokumentierte Fähigkeitseinschränkung, kein Beweis fehlerhafter Hostexports.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Project/PlatformPackages.cpp`
- `Funke:Funke-Editor/src/Build/GameBuilder.cpp`
- `Funke:.github/workflows/ci.yml`
- `Funke:Github-Docs/Documentation/Editor/Building-and-Shipping.md`
- `Funke:Github-Docs/Documentation/Known-Limitations.md`

**Anweisungen:**

1. Definiere Ziel-OS/Architektur/Toolchain explizit im Exportauftrag und verhindere früh inkonsistente Host-/Targetkombinationen.
2. Kapsle vorhandene plattformeigene Builder so, dass ein versionierter Projekt-/Paketstand an einen passenden Zielworker übergeben werden kann.
3. Implementiere zuerst einen benannten Mehrhost-Ablauf mit Hash-/Versionsprüfung statt alle nativen Abhängigkeiten blind crosszukompilieren.
4. Erzeuge überprüfbare Buildreports und hole Artefakte zurück, ohne Zugangsdaten ins Projekt einzubetten.
5. Prüfe das Ergebnis tatsächlich auf der Zielplattform und dokumentiere lokale Hostexports sowie den neuen Workerpfad getrennt.

**Abnahme:**

- [ ] Mindestens ein konkret definierter Export auf ein anderes Desktop-OS ist reproduzierbar auslösbar und auf dem Ziel geprüft.
- [ ] Nicht verfügbare Zielworker oder SDKs ergeben klare nicht-unterstützt/blockiert-Meldungen.
- [ ] Das System behauptet keine universelle Crosscompilation oder Bereitstellung zusätzlicher Remoteinfrastruktur.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Known-Limitations.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Project/PlatformPackages.cpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Editor/Building-and-Shipping.md)

<a id="task-qal-026"></a>

### QAL-026 – Beta-Kompatibilität, ABI und Datenmigration als überprüfbare Releaseverträge definieren

**Komplexität:** K4 · **Status:** offen · **Priorität:** P1 · **Art:** Ausbau · **Belegstatus:** prüfen

**Abhängigkeiten:** EDT-011 (erledigt; siehe [Vertragsdokumentation](Documentation/Editor/Verified-Contracts-2026-11.md)), EDT-019 (erledigt; siehe Vertragsdokumentation)

**Voraussetzungen:** Benötigt mindestens einen archivierten älteren Engine-/Paket-/Projektstand für echte Kompatibilitätsfälle; ohne ihn ist nur die neue Vertragsprüfung abnehmbar. Matrixbuilds zusätzlich.

**Ausgangslage:** Versionmetadaten, Native-ABI-Dateien, SemanticVersion und Paketlocks existieren; der Audit hat keine über mehrere öffentliche Releases bewiesene Upgrade-/Kompatibilitätsgarantie festgestellt. Bestehende Mechanismen müssen zuerst geprüft werden.

**Dateien und Einstiegspunkte:**

- `Funke:Version.props`
- `Funke:Funke-Engine/src/Core/EngineAbi.hpp`
- `Funke:scripts/packages/WriteNativeAbi.py`
- `Funke:Tests/Funke-Engine-Tests/src/EngineAbiTests.cpp`
- `Funke:Funke-Engine/src/Packages/SemanticVersion.cpp`
- `Funke:CHANGELOG.md`
- `Funke:Github-Docs/Documentation/Packages/Package-System.md`

**Anweisungen:**

1. Inventarisiere vorhandene Versions-/ABI-/Projektformatprüfungen und dokumentiere deren tatsächliche Grenzen, statt ein zweites konkurrierendes Versionssystem einzuführen.
2. Definiere nachvollziehbare Regeln für kompatible Fixes, API-/ABI-Brüche, Projekt-/Szenenformatmigration und unterstützte Beta-Upgrades.
3. Prüfe native Paketmetadaten gegen Engineversion, Architektur und ABI; lehne nachweislich inkompatible Binärpakete mit klarer Meldung vor dem Laden ab.
4. Übernimm die konkreten Native-Script-Nachweise aus EDT-011 und Scene-/Prefab-Migrationsnachweise aus EDT-019; ergänze echte Altpaketfixtures und eine integrierte Release-Kompatibilitätsmatrix. Baue keine zweite Szenenmigration.
5. Verknüpfe die Verträge mit Release Notes und einer maschinenlesbaren Kompatibilitätsmatrix.
6. Halte spielseitige SaveData-Migration aus EDT-020 als eigenen Vertrag getrennt von Engine-/Projektformaten.

**Abnahme:**

- [ ] Ein absichtlich inkompatibles Paket wird vor nativer Ausführung eindeutig abgewiesen.
- [ ] Mindestens ein tatsächlicher erlaubter Upgrade-/Migrationsfall sowie ein verbotener Fall sind geprüft.
- [ ] Die Dokumentation verspricht nur die getesteten Kompatibilitätsrichtungen; bestehende Versionierungsmechanismen bleiben konsistent.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Version.props) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Core/EngineAbi.hpp) · [Beleg 3](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/scripts/packages/WriteNativeAbi.py) · [Beleg 4](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Tests/Funke-Engine-Tests/src/EngineAbiTests.cpp)

<a id="task-wld-002"></a>

### WLD-002 – Begrenzten Chunk-Streaming-Scheduler mit Abbruch und Speicherbudget entwickeln

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** WLD-001 (erledigt; siehe Vertragsdokumentation), [PRF-003](#task-prf-003)

**Voraussetzungen:** Neue Engine-/Paketfunktionen benötigen zusätzlich passende native Builds und praktische Szenenprüfung.

**Ausgangslage:** Additive Szenen und Assetvorbereitung existieren. Für allgemeines Worldstreaming braucht es zusätzlich Lebenszyklen, Priorisierung und Budgets.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Assets/AssetLoader.cpp`
- `Funke:Funke-Engine/src/Scene/SceneManager.hpp`
- `Funke-PackageRegistry:packages/ (World-Streaming-Paket neu)`

**Anweisungen:**

1. Definiere Chunk-IDs, Zustände, Interessebereiche, maximale gleichzeitig laufende Requests und CPU-/GPU-Speicherbudgets.
2. Trenne Datenlesen/-generierung, GPU-/Physikaktivierung und Freigabe; verwende vorhandene Loader statt parallele Konkurrenzsysteme.
3. Implementiere Cancellation, Generationstokens und Prioritätswechsel bei Teleports und schnellem Richtungswechsel.
4. Entlade erst, wenn Jobs, Renderer, Physik und Spiellogik keine gültigen Referenzen mehr benötigen.
5. Prüfe eine Fahrt über viele Chunkgrenzen mit begrenztem Speicher und absichtlich verspäteten Ergebnissen.

**Abnahme:**

- [ ] Veraltete Requests aktivieren keine falschen Chunks.
- [ ] Speicher-/Requestbudgets werden eingehalten oder nachvollziehbar gemeldet.
- [ ] Keine pauschale Frametimespitzen-Freiheit ohne PRF-003-Nachweis.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Scenes.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Assets/AssetLoader.cpp)

<a id="task-wld-005"></a>

### WLD-005 – Floating Origin konsistent über alle benötigten Subsysteme umsetzen

**Komplexität:** K4 · **Status:** offen · **Priorität:** P3 · **Art:** Ausbau · **Belegstatus:** vorgeschlagen

**Abhängigkeiten:** [WLD-002](#task-wld-002), WLD-004 (erledigt; siehe Vertragsdokumentation)

**Voraussetzungen:** Eine gemeinsame Referenzszene für Physik, Rendering, Streaming, Scripting und Paketkomponenten.

**Ausgangslage:** Dieser große Ausbau ist nur erforderlich, wenn WLD-004 eine entsprechende Reichweitenanforderung und Lösung ergeben hat.

**Dateien und Einstiegspunkte:**

- `Funke:Funke-Engine/src/Scene/Scene.hpp`
- `Funke:Funke-Engine/src/Physics3D/PhysicsWorld3D.cpp`
- `Funke:Funke-Engine/src/Graphics/Renderer3D.cpp`
- `Funke-PackageRegistry:packages/Funke.Networking/Source/Internal/NetworkObjects.cs`

**Anweisungen:**

1. Implementiere den beschlossenen Welt-/Lokalkoordinatenvertrag ohne stille Vermischung der beiden Räume.
2. Führe Originwechsel an einer definierten Framegrenze aus und aktualisiere Render-, Physik-, Audio-, Partikel-, Navigations- und Interpolationszustände.
3. Definiere das Verhalten von Caches, Bounds, Jobs und offenen asynchronen Ergebnissen über einen Shift hinweg.
4. Speichere Weltpositionen ohne Informationsverlust und halte Netzwerksnapshots in einem eindeutig versionierten Koordinatenraum.
5. Teste häufige Shifts während Bewegung, Kollisionen, Szenenstreaming, Save/Load und Multiplayer.
6. Dokumentiere Einschränkungen für Pakete und Drittanbieterkomponenten, die nicht am Shiftvertrag teilnehmen.

**Abnahme:**

- [ ] Ein Shift erzeugt keine künstliche Geschwindigkeit oder sichtbaren Sprung im geprüften Szenario.
- [ ] Save/Load und Netzwerkpartner rekonstruieren dieselbe Weltposition.
- [ ] Die bestätigten Reichweitenfälle aus WLD-004 bestehen.

**Quellen:** [Beleg 1](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Github-Docs/Documentation/Engine-Core/Scenes.md) · [Beleg 2](https://github.com/Ben-Scr/Funke/blob/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9/Funke-Engine/src/Physics3D/PhysicsWorld3D.cpp)
