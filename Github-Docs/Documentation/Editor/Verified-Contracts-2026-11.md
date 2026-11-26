# Verifizierte Verträge und Prüfgrenzen

Stand: 13. November 2026. Übernommene Nachweise früherer Arbeitsrunden sind keine erneuten Testläufe. Diese technische Referenz ersetzt erledigte Einträge der Arbeitsbacklog. Plattform- und Veröffentlichungsgrenzen bleiben bestehen.

| Referenz | Verhalten / Vertrag | Prüfbeleg und Grenze |
|---|---|---|
| AIN-001 | [Inventar der Navigation](../Engine-Core/Navigation-and-Worlds.md). Voxel-A*, Gegnersteuerung und Zustandsautomaten in Beispielen konkret belegt. | Core, ScriptCore, Paketquellen und hashgeprüfte Voxtau-/Aetherwild-Archive inventarisiert. Kein allgemeines Navmesh-/Behavior-Tree-System behauptet. |
| EDT-006 | Readonly-Zugriff vermeidet globale Dirty-Markierung; gezielte Setter und konservativer Raw-ref-Fallback bleiben getrennt. | NativeTransformAccessTests; vorhandene Hierarchie-/Bewegungsmessungen, siehe Entity-Movement-Measurements.md. |
| EDT-008 | Tatsächliche ECB-Fähigkeitsmatrix, ausgewählter Pfad und konkrete Fallbackgründe über das bestehende Diagnoseflag; gezielte Lücken bei Modusgrenzen, Returnwerten und Fehlern geschlossen. | 54 native Tests und 21 Managedtests bestanden. Reale Worker-Ausführung und geordnete/inline Fallbacks im Log nachgewiesen. |
| EDT-014 | Die vorhandene Vorabprüfung von ScriptCore-Identität und Feldlayout verhindert einen scheinbar erfolgreichen inkompatiblen Hot Reload. Neu: Export der tatsächlich kompilierten nativen Feldverträge für den direkten C#-Vergleich. | Native Transform2D-/SpriteRenderer-Verträge passen nach dem inkrementellen Neubau zu den C#-Spiegeln. Geänderte Offsets bei unveränderter Größe werden abgelehnt. Kein pauschaler Größenbeweis für alle Komponenten. |
| EDT-015 | Bestehende Undo-/Recovery-Absicherung kartiert; fehlenden Zwischenzustand einer nur halb wiederhergestellten Linked-Undo-Verbindung ergänzt. | Undo bleibt diagnostiziert blockiert, Inhalte/Dirty-Flags/Historien bleiben erhalten; nach Wiederherstellung des Partners funktionieren Undo und Redo. Keine zusätzliche Undo-Implementierung nötig. |
| EDT-016 | Reload, Schließen und Szenenersatz erklären das Ende der jeweiligen Undo-/Redo-Historie. Bestehende Dialoge und Statusleiste übernehmen den Hinweis. | Native Retention-/Reloadtests sowie echter isolierter Editorlauf: Play/Stop erhält die Historie, Szenenersatz verwirft sie. Visuelle Detailabnahme siehe unten. |
| EDT-017 | Blockierte verknüpfte Schritte nennen Partnerszene, vorher erforderliche Schritte und den nächsten Befehlsnamen. Geschlossene oder geleerte Partnerhistorien erhalten eigene Diagnosen. | Native Tests für beide Richtungen, Änderungen auf beiden Seiten, Clear/Close/Reopen und unveränderten Szeneninhalt. Genau-einmal-Ausführung und erfolgreiche Wiederholung bleiben geprüft. Menü-/Tooltipbedienung nicht vollständig automatisiert. |
| EDT-019 | Vorhandene Versionsprüfung und archivierte Fixtures geprüft. Unversionierte/v0/v1-Dokumente bleiben unterstützt; unbekannte Versionen werden vor Laden/Überschreiben abgewiesen. Kein neues Format eingeführt. | 24 bestehende Szenen-/Prefab-Tests mit 368 Assertions bestanden, einschließlich Versionsablehnung, historischer Szene, Identitäten und Prefabreferenzen. Ausgeführt im vorhandenen nativen Testbinary, kein neuer Gesamtbuild. Kompatibilitätsmatrix steht in Scenes.md. |
| EDT-020 | Vorhandene versionierte SaveData-Migration geprüft. Gemeinsame feste `.tmp`-Datei durch private Staging-Dateien ersetzt; Bereinigung bei fehlgeschlagener Veröffentlichung ergänzt. | Migrationen, Legacy-Slots, Zukunftsversionen, beschädigte Daten, Callback-/Validierungsfehler und Originalerhalt bestanden. Zwei neue Fälle prüfen belegte fremde Staging-Dateien und fehlgeschlagenes Rename ohne fremde Dateien zu löschen. |
| EDT-023 | Async-Ownership und Reload-Übergänge geprüft; native Neustartzyklen und tatsächlichen Managed-Assembly-Unload mit verspäteter Completion ergänzt. | Alte IDs/Callbacks können neue Requests nicht abschließen oder abbrechen. Geteilte Vorbereitung und observerbezogener Abbruch bleiben erhalten. Kein zweites Ladesystem. |
| EDT-026 | Mausfokus nutzte bisher eine eigene Registry-Reihenfolge. Er verwendet jetzt denselben Picker wie UI-Events; Tastaturfokus folgt der gemeinsamen Hierarchie und überspringt deaktivierte Eltern. | Sichtbarer GPU-Repro plus Masken-, Canvas-, Fokus- und Touchtests. Capture nach Layoutverschiebung löst am alten Ort keinen Klick aus. Kein physischer Mobilgerätetest. |
| EDT-040 | Gemeinsame Zustände `unknown`, `not_compiled`, `disabled`, `enabled` für Projektvalidierung, Startdiagnose, Paketabhängigkeiten und CLI. Unbekannte IDs werden abgelehnt, bewusst leere Modullisten erhalten. | Native Tests, echte CLI-Projektladeprüfung und vier separat kompilierte 2D-/3D-Kombinationen einschließlich Null-Modul-Build erfolgreich. |
| EDT-042 | Asset-Undo meldet den betroffenen Pfad und unterscheidet neue Inhalte, externe Änderungen, fehlende Dateien und Typwechsel. Der blockierte Schritt bleibt für einen sicheren Wiederholungsversuch erhalten. | Native Rename-/Move-/Delete-/Create-/Rollbacktests schützen Inhalt, Metadaten und GUIDs. UI-Fehlerpfad gebaut und geprüft; keine vollständige Mausabnahme aller Assetabläufe. |
| GFX-001 | AudioMixer in beide Richtungen synchronisiert, stille native UI-Updates verhindern verzögerte/quantisierte Rückkopplung; gleichzeitige Slider-/Toggle-Änderungen bleiben erhalten; Unbind idempotent. | Unitregressionen und echte API-Kompilierung bestanden. Echter Windows-Release-Editor: 17 Checks über 110 Scriptupdates, 0 Scriptfehler, Exit 0; Script/Fade/Snapshot, native UI-Events, Unbind und zerstörte Widgets geprüft. |
| GFX-004 | Ein zu großer Sprite-Pass wird in begrenzte Uploadfenster zerlegt, statt vollständig verworfen. Globale Reihenfolge und Material-/Maskenruns bleiben erhalten. | Sieben alphaüberlagerte Sprites sind bei künstlichen Limits von eins und zwei pixelgleich zum ungeteilten Bild; Instanzzählung bleibt sieben. Echte Speichererschöpfung wurde nicht provoziert. |
| GFX-005 | Reproduzierbarer Material-Benchmark und dokumentierte Wiederverwendungsempfehlungen mit tatsächlichen Draw Calls, CPU-Submit- und GPU-Pass-Zeiten. | 48 Messläufe / 2.880 gepaarte Samples; Sichtbarkeit, Transparenz, Materialänderung und identische Bilder geprüft. |
| GFX-008 | Stabile Licht-/Occluderauswahl bei gleichen Distanzen; begrenzte Budgets über vorhandene Ambient-Komponente; Inspector- und C#-Zähler für den letzten View. | 8 native Tests, reale Runtime-Szenarien, Inspector-Sichtung sowie Play/Stop und Reload bestanden. |
| GFX-033 | Vorhandene Modi `EulerMultiTurn`, `ShortestAngle` und `Quaternion` einschließlich Metadaten, Dateiformat und kompatiblem Euler-Default abgenommen. | Native Animationsfälle für 350→10, −179→179, Mehrfachdrehung, gemischte Achsen und Skeletal-Crossfades bestanden. Editor-Prüfprojekt zeigt alle drei Varianten im Play-Modus. |
| GFX-034 | Bereits vollständige Quaternion-API, Local-/World-Zugriff und Dokumentation geprüft. Kein zweites Quaternionmodell eingeführt. | Managedtests für native GLM-Referenzwerte, Komposition, Inverse, antipodales Slerp, degeneriertes LookRotation, Layout und rotierte Eltern bestanden. Der Hierarchietest verwendet kontrollierte native Callbacks; kein neuer GPU-Lauf. |
| GFX-044 | alle acht zuvor unregistrierten Renderer3D-Scopes registriert und in CPU-Panelgruppe aufgenommen; leere Buffer zeigen „No samples“. | Vollständiger Registrierungsabgleich, Shutdown/Reinit-Regression und Editorbuild bestanden. Panel-Start ohne Fehler; das Hauptfenster-Screenshot erfasst das separate Profilerfenster nicht. |
| GFX-045 | Vorhandene Slot-/Callback-Lebensdauerkorrektur und Diagnose für fehlende GPU-Werte abgenommen. | Vier echte Dawn-Geräteläufe: fehlendes Timestamp-Feature, fehlgeschlagenes Mapping, Shutdown mit ausstehendem Callback, injizierter Device-Loss. Kein physischer Treiberreset; Windows/D3D12-Abnahme. |
| GFX-049 | nur WGSL als Shader auswählbar; Legacyformate bleiben Quelldateien mit „Source only“-Kennzeichnung und Zuweisungsdiagnose. | Registry-Regressionen inkl. Großschreibung, GUID-/Inhaltserhalt bestanden; Editor gebaut und sichtbare Browsermarkierung im echten Fenster geprüft. Drag/drop-Diagnose kompiliert und im Code geprüft, nicht durch Mausbedienung ausgeführt. |
| INP-001 | Frame-Vertrag an `InputAction.WasPressed/WasReleased` dokumentiert. Vorhandenes gepuffertes FixedUpdate-Beispiel war korrekt. | 164 Input-Tests bestanden. Veraltete Lifecycle-Eventnamen im Paket auf aktuelle API migriert. |
| INP-002 | Reine Konfliktabfrage über ausgewählte Maps mit Action-/Binding-/Compositeinformationen und Controllertrennung. Optionaler Rebind-Vorschlag lässt Beibehalten, Tauschen und Ablehnen beim Spiel. | 16 neue Konfliktfälle; bisherige gemeinsame Belegungen bleiben erlaubt. Paket und öffentliche Beispiele getrennt gegen echte ScriptCore-Assembly kompiliert. |
| INP-003 | Stabile bindingId, Schema-2-Profile, explizite historische Schema-1-Migration; keine Zuordnung über heutige Indizes. | 283 Pakettests laut dokumentierter Vorabnahme; unveröffentlichter Quellstand. |
| INP-004 | Optionaler `InputActionRouter` priorisiert Maps und konsumiert deren physische Controls deterministisch. Ohne Router bleiben Maps unabhängig. | Abfragefolge, Prioritätsgleichstand, Menü/Gameplay, globale Shortcuts, Composites, lokale Spieler und Rebinding geprüft. Ein noch gehaltener Menü-Confirm wird erst nach Neutralstellung wieder für Gameplay freigegeben. |
| INP-005 | Tap/Hold mit injizierbarer Zeitquelle und explizitem Pause-/Fokusvertrag. | 245 Pakettests laut Vorabnahme; keine physische Geräteabnahme. |
| INP-006 | Tastatur-/Maus-Chords mit physischen Modifiern, Konfliktprüfung, JSON-Profilen und expliziter Rebinding-Aufnahme ergänzt. | Beide Druckreihenfolgen, Loslassen, mehrere Modifier, alte IDs/Profile, ungültige Kombinationen und gehaltene Starttasten geprüft. Keine beliebigen verschachtelten Chords oder Gamepad-Chords. |
| INP-008 | Runtime InputActionDataAsset mit GUID-/Bundle-Ladeweg; lose JSON-Dateien benötigen weiterhin lose Bereitstellung. | Export- und Ladenachweise aus Runde 7; unveröffentlichter Quellstand. |
| INP-010 | Optionaler begrenzter Ereignispuffer mit Frame-/Zeitstempel, Altersgrenzen, `ConsumeOnce`/`ConsumeUntil`, Pause-/Fokus-/Map-/Rebinding-Behandlung. Polling bleibt unverändert. | 27 neue Pufferfälle; wechselnde Render-/Physikraten, mehrere Ticks je Frame, Überlauf und Lifecycle des echten Beispiels. Physische Controller und OS-Fokuswechsel nicht durch Gerätetest abgenommen. |
| MEM-001 | Legacy-Accessor deprecated, Snapshot-Migration dokumentiert; exportiertes Symbol bleibt. | Keine produktiven Legacy-Aufrufer im Enginecheckout gefunden. Echte `Memory.cpp` unter MSVC ASan mit konkurrierenden Snapshot-/Allocate-/Free-Zugriffen geprüft. |
| MEM-002 | Debug-Threadbindung der globalen Arenas, einschließlich Zugriff über gespeicherte Arena-Referenzen; Release/Dist ohne zusätzliche Felder/Checks. | 9 isolierte Debug-Tests, 48 Assertions bestanden. Worker-Lokalarenen und neuer Lifecycle-Thread geprüft. Debug-Verbraucher müssen wegen des Debug-Klassenlayouts neu gebaut werden. |
| NET-001 | Varint-Endbyte-Überläufe, nichtkanonische Encodings und overflowanfällige Boundschecks korrigiert. | Networking-Suite mit Grenzwerten, ungültigen Längen und echter UDP-Session besteht; gültige Nachricht nach fehlerhaftem Paket bleibt nutzbar. |
| NET-003 | Autorisierung anhand Transportabsender und aktuellem Owner vor Konstruktion/Deserialisierung. | Vier echte Zweiprozessläufe UDP/WebSocket Dedicated/Host laut Vorabnahme; kontrollierte verwaltete Objektdaten. |
| NET-004 | Begrenzte Nachrichtengrößen, Warteschlangen und reproduzierbare Transportlastbudgets. | Dokumentierter Paket-Lastnachweis aus Runde 5; keine Internet-/NAT-Abnahme. |
| NET-005 | Verbindungsfähigkeiten zeigen Transport, echte Unreliable-Unterstützung, lokale Hostfähigkeit und beobachteten Verschlüsselungsstatus. Begrenzte Sendewarteschlangen, aktuelle Transformwerte bevorzugt, zuverlässige Broadcasts vor Annahme geprüft. Objekt-Lifecycle ist gegen Überläufe abgesichert. | 108 Tests, echte UDP-/WebSocket-Sockets mit langsamen Empfängern sowie getrennte Server-/Clientprozesse erfolgreich. 18 Lifecycle-Regressionen verwenden kontrollierte Managed-Entity-Bindings; kein nativer Welt-/Physiklauf behauptet. Browser-/WSS-/Internetabnahme offen. |
| PKG-001 | vier fehlende Katalog-Mindestversionen ergänzt; `Index-PackageRegistry/docs/package-compatibility.md` dokumentiert Matrix und Engine-Prerelease-Normalisierung. | Acht veröffentlichte ZIPs: Name, Version, SHA-256 und Größe korrekt. Acht Quellpakete gegen echte ScriptCore-API gebaut; native Package-/Registry-/SemVer-Tests bestanden. Keine native Paket-Gerätefreigabe. |
| PRF-001 | [versionierter Ergebnisvertrag](../Editor/Benchmark-Results.md), kompatibel zum vorhandenen FunkeBench-CSV; Sidecar bindet Build, Workload und Rohdaten. | FunkeBench-Releasearchiv per SHA-256 geprüft, Export-/Statistikquellen gelesen. Keine neuen Leistungswerte behauptet. |
| QAL-010 | README, Getting Started und Installertexte führen zum tatsächlichen Source-Build und unterscheiden temporäre CI-Artefakte. | Öffentliche Releases-API liefert `[]`; vorhandene Buildskripte und Links geprüft. Windows-Projektneuanlage mit frisch gebauter CLI erfolgreich. Kein neuer kompletter Linux-/macOS-Source-Build. |
| QAL-025 | Clone-/Security-Ziele auf Funke korrigiert. | GitHub-API bestätigt `Ben-Scr/Index` als eigenes älteres Repo, nicht als Funke-Redirect. Beide privaten Reporting-URLs führen zum Login; authentifizierte Meldungsabgabe nicht ausgeführt. 47 relative Linkziele in den betroffenen Einstieg-/neuen Dokumenten geprüft. |
| WLD-001 | [Weltfähigkeitsmatrix](../Engine-Core/Navigation-and-Worlds.md) mit Chunkstreaming, Meshjobs, Terrain-LOD, Persistenz und Präzisionsgrenzen. | Veröffentlichte Voxtau-/Aetherwild-ZIPs hashgeprüft. Spielcode von Engine-API getrennt; allgemeine World-Partition-/Origin-Features und reale Lastgrenzen bleiben unbewiesen. |
| WLD-004 | Float-Reichweiten, gerenderte Relativbewegung und Physikkontakte vermessen; Koordinatenvertrag und atomarer Origin-Shift geplant. | Sechs Physikabstände und zwölf GPU-Bilder. Ab 100.000 m verschwindet ein direkt addierter Millimeter im Float. WLD-005 bleibt die Implementierung. |

## Ergänzende Vertragsreferenzen

- [Input und Networking](../Packages/Input-Networking-Contracts.md)
- [ECS und Jobvertrag](../Engine-Core/ECS.md)
- [Bewegungsmessungen](../Engine-Core/Entity-Movement-Measurements.md)
- [Scripting und SaveData](../Scripting/Scripting.md)
- [Szenenformate](../Engine-Core/Scenes.md)
- [Animation](../Engine-Core/Animation.md)
- [UI-Fokus](../Engine-Core/UI-Focus-Order.md)

## Prüfung vom 13. November – Transformzugriff, Jobs und Reload

- Release-Neubau von ScriptCore, ScriptCompiler und Managedtests: keine Fehler oder Warnungen.
- 40 Managedfälle aus `ScriptAssemblyReloadTests`, `JobDispatchTests` und
  `NativeTransformAccessTests` bestanden. Direkter sequenzieller Aufruf der
  xUnit-Methoden einschließlich InlineData, Instanzaufbau und Dispose; kein
  erfolgreicher VSTest-Transportlauf behauptet.
- Neu geprüft: schreibfreier Transform-Lesezugriff und mehrere native Writes,
  explizite ECB-Producer-Abhängigkeit sowie Fehlerweitergabe, abgelehnte native
  Komponentenregistrierung bei Erstladen und Ersatz einschließlich erfolgreichem Retry.
- Vorhandenes natives Testbinary: 50 Fälle / 926 Assertions zum Filter
  `Physics layers:*,*transform*` bestanden. Es enthält die neuen Raymask- und
  Generationsregressionen dieser Runde nicht.
- `AnimationBinding.cpp`, `PhysicsWorld3D.cpp`, `AnimationTests.cpp` und
  `Physics3DTests.cpp` mit MSVC und Projektdefines separat übersetzt. Die vorhandenen
  int/float-Konvertierungswarnungen in AnimationTests bleiben; kein neuer Engine-Link.
- Vollständiger Neubau blockiert bei `generate_build_metadata.py --identity-scope engine`:
  Git kann `.git/modules/External/ImGuizmo` nicht betreten (Permission denied).
  Das Integritätsgate wurde nicht umgangen. Neue native Tests bleiben bis zum
  passenden Link und tatsächlicher Ausführung offen.
- Relative Links und Taskanker der bereinigten Backlog geprüft. Keine Verweise
  auf entfernte Taskanker; 125 eindeutige offene Audit-IDs. Die zusätzliche
  Build-/Asset-Validierungsbacklog wird separat geführt.

Die aktuellen Regeln stehen in [ECS](../Engine-Core/ECS.md),
[Managed reload](../Scripting/Scripting.md),
[3D physics](../Engine-Core/Physics-3D.md) und
[Animation binding lifetime](../Engine-Core/Animation.md).
Die frühere Paket-Kompatibilitätsmatrix ist unter
[Compatibility audit](../Packages/Compatibility-Audit-2026-11.md) erhalten.


## Prüfung vom 13. November - zehn weitere ausgewählte Aufgaben

Acht Aufgaben sind abgeschlossen; zwei behalten ihre ausstehende Abnahme.

| Aufgabe | Dauerhafter Vertrag und Änderung | Nachweis |
|---|---|---|
| EDT-011 | [Native ABI](../Scripting/Scripting.md): inkompatiblen Ersatz vor Nutzung ablehnen, laufende DLL behalten, erwartete/gelieferte Version und Rebuildhinweis ausgeben. Fixture-CMake verwendet eingebettete MSVC-Debuginformationen. | Sieben echte neu gebaute Windows-DLLs; ein Hosttest mit 55 Assertions. Alte/zukünftige/fehlende ABI, fehlende Exports, Retry und Shadow-Cleanup bestanden. Bestehendes Hostbinary; keine neue andere Plattform behauptet. |
| EDT-038 | [Dry-run und Recovery](Editor-Bridge.md): Revisionen prüfen, fehlgeschlagenes Restore separat sperren, Snapshot/Diagnose für explizites `history.recover` behalten. | Frisch übersetzte Implementierung mit Setter- und Deserialize-Fehlerinjektion, unverändertem Dry-run, Konflikt, Wiederholung und verknüpfter Historiensperre ausgeführt. |
| EDT-039 | [Bindingvertrag](../Scripting/Binding-Contracts.md) für 16 Physics3D-Slots, Signaturen, Positionen, Modulzuordnung und Layoutversion. | Fünf Python-Prüfungen bestanden, einschließlich gleich großer vertauschter Tabellen und einseitiger Änderungen. Bestehende Runtime-Handshaketests ebenfalls ausgeführt. |
| GFX-030 | [3D-Abfragemasken](../Engine-Core/Physics-3D.md): C#-Raycast und CheckSphere, 64-Bit-Masken, explizite Trigger, Compound-Owner-Filter. | Neue native Queryimplementierung frisch übersetzt und ausgeführt: ausgeschlossener naher Körper, Membershipwechsel, Trigger und Compoundfälle. Managed Callbacktests prüfen Bit 63 und Ergebnisdekodierung. |
| GFX-039 | [IComponent-Animation](../Engine-Core/Animation.md): gemeinsamer Runtime-Bytecodec, Feldregistrierung, echte ECS-Writes und vorhandene Trackauswahl. Aufnahmedeskriptoren bei Schemareload verwerfen. | Frisch übersetzte AnimationPreview und AnimatorRuntime: Auswahl, verschachtelter Feldname, Pose, Rückschreiben authored Werte, Component-/Entity-Remove und Registry-Unload während Aufnahme bestanden. Keine visuelle UI-Abnahme behauptet. |
| Asset-Validierung: Fehleranzeige | [Exportdiagnose](Build-Asset-Validation.md): Projektpfad und konkrete Serializerursache in gemeinsamem Buildresultat für Editor/CLI. | Echter Serializer und gemeinsame Konvertierungsfunktion ausgeführt; GameBuilder und seine Ergebnisweitergabe kompiliert/geprüft. Sichtbare Darstellung gehört zur ausstehenden Exportabnahme. |
| Asset-Validierung: Auswahl | [Pruningvertrag](Build-Asset-Validation.md): nur nachweislich unbenutzte Assets ausschließen; konservativen Grund erhalten. | Referenziertes/unbenutztes leeres Asset und konservative Übernahme wegen nativer Quelle getestet. |
| Asset-Validierung: Dokumentation | [Dauerhafter Exportvertrag](Build-Asset-Validation.md) statt erledigter Backloghistorie. | Regeln, Diagnose, Implementierungsorte und verbleibende Abnahme dokumentiert. |

Weitere Arbeit an EDT-012: Fehler in realen generierten C#-OnStart-/OnDestroy-
Callbacks über unmanaged Einstiegspunkte injiziert; Retry, Freigabe und spätere
Assemblyersetzung bestehen. Die vollständige native Drain-/Teardown-/Recreate-
Sequenz bleibt ungeprüft und deshalb offen. Ebenso bleibt ein echter Desktop-
und Web-Export einschließlich HTTP-Start offen.

Ausführung dieser Runde:

- 76 native Testfälle / 1.602 Assertions bestanden. Separater CMake-Testrunner
  mit frisch kompilierten geänderten Engine-/Editor-Quelldateien und vorhandener
  Engine-DLL für übrige Abhängigkeiten. Kein vollständiger Editor-Neubau.
  EditorSceneEdit-Tests verwenden ihre bestehenden leeren Script-Inspector-
  Adapter; die Recoveryabnahme betrifft die geprüften Built-in-Komponenten.
- 31 Managedfälle aus ScriptAssemblyReloadTests, PhysicsQueryMaskTests und
  InteropHandshakeTests direkt sequenziell ausgeführt: bestanden. Release-Build
  der Managedprojekte ohne Fehler/Warnungen; kein VSTest-Transportlauf.
- Fünf deklarative Bindingtests bestanden; zusätzlich echter ABI-DLL-Test mit
  55 Assertions. Nicht mit dem nativen Vollsuite-Ergebnis zusammengezählt.
- GameBuilder.cpp, AnimationPreview.cpp und ScriptComponentInspector.cpp sowie
  geänderte native Binding-Übersetzungseinheiten separat mit Projektdefines
  kompiliert. Vorhandene Warnungen bleiben.
- Vollständiger Engine-Neubau erneut am Integritätsgate blockiert:
  `generate_build_metadata.py --identity-scope engine` erhält bei
  `.git/modules/External/ImGuizmo` weiterhin `Permission denied`.
  Gate und ABI-Prüfungen wurden nicht abgeschaltet. Bestehende ausführbare
  Editor-/CLI-Dateien enthalten die neuen Funktionen deshalb noch nicht.

Nach Bereinigung: 120 offene Audit-IDs und eine zusätzliche Exportabnahme,
insgesamt 121 offene Aufgaben. Erledigte Taskdetails sind durch die obigen
Dokumentationsverweise abgedeckt.


## Prüfung vom 13. November - fünf weitere Aufgaben

| Aufgabe | Dauerhafter Vertrag | Nachweis |
|---|---|---|
| EDT-002 | [ScriptHost-Aufteilung](../Scripting/ScriptHost-Lifecycle.md): Lifecycle, Instanzen, Metadaten, Felder und Events mit unverändertem gemeinsamen Besitzer. | 207 Memberkörper samt Interop-Attributen unverändert übernommen; Reload-/WeakReference-/Metadaten-/Inspector-/Coroutine-Prüfungen bestanden. |
| EDT-003 | [UI-Verarbeitungsreihenfolge](../Engine-Core/UI-Focus-Order.md#widget-implementation-boundary): eigenständige Textfeldbearbeitung, gemeinsamer Pointerzustand bleibt im UIEventSystem. | 28 Funktionen extrahiert; Hit-/Masken-/Fokus-/Touch-/Scroll-/Canvas-Tests und gemischtes UI-Fixture bestanden. |
| EDT-004 | [Borrow-Grenze](../Scripting/Component-Access-Lifetimes.md#queryref-development-guard): Debug/Development blockieren Strukturänderung vor Speicheränderung. | Native verschachtelte Borrows, Create/Destroy/Add/Remove/Bulk/Clear, dynamische Speicher und ECB sowie Managed early break, Exception und Dispose-Kopien bestanden. |
| EDT-005 | [Geprüfte Wert-Handles](../Scripting/Component-Access-Lifetimes.md): Szeneninstanz, Entity-Generation, Struktur- und Schemarevision; keine herausgegebene Rohreferenz. | Remove/Readd, Destroy/Recreate, Szenenwechsel/-zerstörung, Schemarevision und Kopier-/Schreibverhalten bestanden. |
| EDT-028 | [LocalizedUI](../Engine-Core/Localization.md#widget-labels-options-and-placeholders): optionale Bindungen, Literale und Eingaben bleiben erhalten. | Wiederholter Sprachwechsel, leere/fehlende Schlüssel, Auswahl/Eingabe und serialisierte Bindungen inklusive remappter Label-Entity bestanden. |

Ausführung: 50 native Fälle mit 560 Assertions im gezielten CMake-Runner bestanden.
Geänderte Implementierungen sind frisch kompiliert, weitere Abhängigkeiten stammen
aus der vorhandenen Engine-DLL; Physics2D ist in diesem UI-/ECS-Runner deaktiviert.
88 Managedfälle direkt sequenziell bestanden (kein VSTest-Transport); Managed
Release-Build ohne Fehler oder Warnungen. Fünf bestehende deklarative Bindingtests
bestanden. Produktionsdateien ScriptBindings, ScriptBindingsEcb, SceneManager,
GuiRenderer und UI-Registrierung mit den Projektdefines separat kompiliert.
Die vier neuen Bindings sind ausschließlich angehängt; Layoutversion 57.

Die zwei bereits offenen Gesamtabnahmen bleiben offen: EDT-012 (nativer kompletter
Reload) und der echte Desktop-/Web-Export. Das Build-Metadatengate scheitert erneut
am Git-Zugriff auf `.git/modules/External/ImGuizmo` mit `Permission denied`.
Kein Gate wurde umgangen, kein vollständiger Editor-Neubau oder visueller Lauf
mit den neuen Funktionen behauptet. Änderungen stehen im Quellcode; die neuen
Interop-Bindings benötigen gemeinsam gebaute native und verwaltete Binärdateien.

Nach Bereinigung: **115 offene Audit-IDs plus eine Exportabnahme = 116 Aufgaben**.


## Prüfung vom 13. November – Job-Lebensdauer, 3D-ECB, History und Varianten

| Aufgabe | Stand und dauerhafter Vertrag | Nachweis / offene Abnahme |
|---|---|---|
| EDT-007 | Abgeschlossen: [Query-Job-Leases](../Scripting/Component-Access-Lifetimes.md#asynchronous-query-jobs) schützen die Szene bis einschließlich Cleanup; Leser/Schreiber-Konflikte werden vor Dispatch abgewiesen. | Worker-Barrieren, echte geladene Szene beim Unload, Strukturänderung/ECB sowie Managed Fehler/Abbruch/leer/fehlgeschlagene View geprüft. |
| EDT-009 | Abgeschlossen: begrenzter [nativer Transform3D-Parallelpfad](../Engine-Core/ECS.md#native-transform3d-opt-in-batch); Auto unverändert. | Defaults, Byte-Payload-Gleichheit, Remove, Hierarchie/Hook/Duplikat-Ablehnung und öffentlicher Mixed-Fallback; komplette Playback-Messung dokumentiert. |
| GFX-046 | Abgeschlossen: [geplanter 3D-Camera-Rig](../Engine-Core/Camera-Follow.md#scheduled-3d-inspector-rig) mit Inspector, Follow/Orbit, Ziel- und Parent-Behandlung. | Bewegung/Zielverlust/Orbit/Parent, Dämpfung sowie tatsächliche Jolt-Simulation und Präsentation bei 30/50/120 Physikticks geprüft. Kein visueller Jittervergleich behauptet. |
| EDT-001 | Implementiert: [EditorHistorySession](Editor-State-Ownership.md) besitzt History-Zustand mit schmalen Übergaben. | Windows-Editor geprüft: Szenenwechsel mit identischer Entity-ID, Editieren, Undo/Redo und Play/Stop. |
| EDT-018 | Implementiert: [Prefabvarianten](Prefab-Variants.md) anlegen, bearbeiten, rücksetzen und propagieren, ohne die Vererbung abzuflachen. | Referenzen, Basisumbenennung, Komponentenentfernung, Anlage-Undo/Redo, mehrstufige geladene Varianten und externe Änderungen geprüft. [Sichtbare Windows-Abnahme vom 26. November](Prefab-Variants.md#windows-editor-acceptance) bestanden. |

Gezielte Ausführung: **61 native Fälle, 28.154 Assertions, keine Fehler**. Darin
enthalten sind die bestehenden UndoTests und die neuen Job-/ECB-/Kamera-/History-/
Variantenfälle. Der opt-in Benchmark wurde separat ausgeführt; seine 13 nicht
selektierten Fälle sind keine zusätzlich bestandene Suite. Der aktuelle Runner
kompiliert die geänderten nativen Implementierungen und Editor-Authoringdateien
frisch; weitere Abhängigkeiten einschließlich Jolt/PhysicsWorld3D stammen aus der
vorhandenen Engine-DLL. Physics2D ist im Runner deaktiviert. Ein kurzer temporärer
Pfad vermeidet die MAX_PATH-Grenze des tiefen Sandbox-Temp-Verzeichnisses beim
Verzeichnis-Undo-Test; daraus folgt keine allgemeine Long-Path-Abnahme.

**48 Managedfälle** direkt sequenziell bestanden, Build ohne Fehler/Warnungen;
kein VSTest-Transport. **Fünf Binding-Vertragstests** bestanden. Native und Editor-
Produktionseinheiten mit Projektdefines separat kompiliert; vorhandene Warnungen
bleiben. Die zwei Job-Lease-Bindings sind angehängt, aktuelle Layoutversion **58**.
Neue Editor-Testquellen und ihre Implementierungen sind in der regulären
Editor-Testprojektdefinition eingebunden.

Vollständiger Engine-/Editor-Build weiterhin blockiert: Git kann
`.git/modules/External/ImGuizmo` nicht lesen (`Permission denied`). WSL meldet
`Wsl/E_ACCESSDENIED`. Integritäts- und ABI-Gates bleiben aktiv. Die vorhandene
Editor-EXE wurde nicht als Test der neuen Funktionen verwendet oder ersetzt;
Engine und ScriptCore müssen für diese ABI gemeinsam gebaut werden.

Der damalige Bereinigungsstand umfasste 112 offene Audit-IDs und eine zusätzliche
Exportabnahme. Die [Gesamtübersicht](../../Funke_Agenten_Backlog.md) führt den
aktuellen Bestand.


## Prüfung vom 14. November – Laden, Stringtabellen und Font-Fallbacks

| Aufgabe | Implementierung / dauerhafter Vertrag | Verbleibende Abnahme |
|---|---|---|
| PRF-003 | [Loading capture](../Engine-Core/Cooperative-Loading.md): rohe Frameintervalle, getrennte Decode-/Transfer-/Aktivierungs-/Freigabephasen, begrenzte P95/P99-Erfassung und CSV. | Fünf getrennte Lastfälle auf echter GPU mit nachvollziehbarem P95/P99/Maximum und Ursachenbericht. |
| EDT-022 | [Prefab-Batches](../Engine-Core/Cooperative-Loading.md#scene-and-prefab-construction): einzelne Entities/Parentlinks, späterer Referenzfixup, echte Fortschrittszahlen und unveröffentlichte Szene. | Windows-Editor geprüft: stufenweiser Aufbau, Abbruch in drei Phasen, verspäteter Fehler, additive Wiederholung und Eingabe in der aktiven Szene. |
| EDT-021 | [Textur-Publikation](../Engine-Core/Cooperative-Loading.md#texture-publication): private Allokation, begrenzte Transfers, Mips und abschließende Cachefreigabe. | Reale Treiber-/GPU-Restblockzeiten, Gerätefehler und Projektwechsel während Teiltransfer. |
| EDT-027 | [String Tables](String-Tables.md): vorhandenes Format, Suche, fehlende Werte, Duplikatdiagnose, Dateihistorie und externe Konflikte. | Im lokalen Novemberlauf im neu gebauten Windows-Editor geprüft, einschließlich Docking nach Neustart. |
| EDT-030 | [Font-Fallbackfamilie](../Assets/Font-Fallbacks.md): geordnete GUID-Liste, Glyphen-Atlas/Metriken und Layoutrevision. | Windows mit D3D12 und Vulkan, GUI-Reihenfolge, Caret, Hot-Reload und lokaler Windows-Export geprüft. Weitere Plattformen offen. |

**22 native Fälle / 3467 Assertions bestanden**; zusätzlich **3 Dawn-Null-Fälle /
80 Assertions bestanden**. Der fokussierte Runner kompiliert die betroffenen
Implementierungen frisch und nutzt für übrige Abhängigkeiten die vorhandene
Engine-DLL; Physics2D ist dort deaktiviert. Die dauerhafte Probe liegt unter
`Tests/Loading-Resource-Probe`. Produktionsquellen wurden mit Engine-/Editor-
Projektdefines compilergeprüft; vorhandene Warnungen bleiben.

Der normale Vollbuild bleibt am Metadatengate für
`.git/modules/External/ImGuizmo` (`Permission denied`) blockiert. D3D12 findet den
Adapter (Treiber 32.0.32015.7029), kann aber `d3dcompiler_47.dll` nicht laden
(Windows Error 87); Vulkan scheitert an `vulkan-1.dll` mit demselben Fehler.
Null führt keine GPU-Arbeit aus. Kein echter Performancegewinn, vollständiger
Editor-Neubau oder sichtbarer Rendernachweis wird daraus abgeleitet.

Alle fünf Einträge behalten ihre oben genannten Restprüfungen. Daher unverändert
**112 offene Audit-IDs plus eine zusätzliche Exportabnahme = 113 Aufgaben**.
Die Backlog enthält für diese fünf Punkte nur noch die Restarbeit und verweist
für die implementierten Verträge auf die dauerhafte Dokumentation.
