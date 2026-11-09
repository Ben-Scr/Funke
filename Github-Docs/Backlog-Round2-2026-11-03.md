# Zweite Backlogrunde vom 3. November 2026

Acht weitere Punkte aus `Funke_Agenten_Backlog.md`, aufgeteilt auf vier parallel arbeitende Chats. Ausgangspunkt sind die uncommitteten Änderungen der [ersten Runde](Backlog-Quickfixes-2026-11-03.md). Engine und Paketrepository bleiben auf den vorhandenen Branches `master` und `main`; keine Commits oder Veröffentlichungen. Die ursprüngliche Backlogdatei bleibt unverändert.

Die Chats besitzen getrennte Bereiche: Undo (EDT-016/017/042), Plattform und Module (EDT-040/QAL-003), Input (INP-002/010) und Networking (NET-005). Native Builds und die abschließende Integration wurden zentral durchgeführt. Der Inputchat prüfte zusätzlich unabhängig die Netzwerk-Warteschlange.

## Änderungen und Abnahme

| ID | Ergebnis | Beleg und Grenze |
|---|---|---|
| EDT-016 | Reload, Schließen und Szenenersatz erklären das Ende der jeweiligen Undo-/Redo-Historie. Bestehende Dialoge und Statusleiste übernehmen den Hinweis. | Native Retention-/Reloadtests sowie echter isolierter Editorlauf: Play/Stop erhält die Historie, Szenenersatz verwirft sie. Visuelle Detailabnahme siehe unten. |
| EDT-017 | Blockierte verknüpfte Schritte nennen Partnerszene, vorher erforderliche Schritte und den nächsten Befehlsnamen. Geschlossene oder geleerte Partnerhistorien erhalten eigene Diagnosen. | Native Tests für beide Richtungen, Änderungen auf beiden Seiten, Clear/Close/Reopen und unveränderten Szeneninhalt. Genau-einmal-Ausführung und erfolgreiche Wiederholung bleiben geprüft. Menü-/Tooltipbedienung nicht vollständig automatisiert. |
| EDT-042 | Asset-Undo meldet den betroffenen Pfad und unterscheidet neue Inhalte, externe Änderungen, fehlende Dateien und Typwechsel. Der blockierte Schritt bleibt für einen sicheren Wiederholungsversuch erhalten. | Native Rename-/Move-/Delete-/Create-/Rollbacktests schützen Inhalt, Metadaten und GUIDs. UI-Fehlerpfad gebaut und geprüft; keine vollständige Mausabnahme aller Assetabläufe. |
| EDT-040 | Gemeinsame Zustände `unknown`, `not_compiled`, `disabled`, `enabled` für Projektvalidierung, Startdiagnose, Paketabhängigkeiten und CLI. Unbekannte IDs werden abgelehnt, bewusst leere Modullisten erhalten. | Native Tests, echte CLI-Projektladeprüfung und vier separat kompilierte 2D-/3D-Kombinationen einschließlich Null-Modul-Build erfolgreich. |
| QAL-003 | Originalfehler im Smoke-Preflight reproduziert: Bundle war korrekt, Helfer suchte nach Auflösung des Executable-Alias in `Contents/MacOS` statt `Contents/Resources`. Enge Bundleerkennung korrigiert; tatsächlicher nativer Ressourcenpfad wird geprüft. | Reales Bundle-Staging mit Fixtures, Unicode-/Verschiebepfade und Negativfälle geprüft. Windows-Launcher und Editor aus fremdem Unicode-cwd erfolgreich. **Echte macOS-DMG-Abnahme weiterhin offen.** |
| INP-002 | Reine Konfliktabfrage über ausgewählte Maps mit Action-/Binding-/Compositeinformationen und Controllertrennung. Optionaler Rebind-Vorschlag lässt Beibehalten, Tauschen und Ablehnen beim Spiel. | 16 neue Konfliktfälle; bisherige gemeinsame Belegungen bleiben erlaubt. Paket und öffentliche Beispiele getrennt gegen echte ScriptCore-Assembly kompiliert. |
| INP-010 | Optionaler begrenzter Ereignispuffer mit Frame-/Zeitstempel, Altersgrenzen, `ConsumeOnce`/`ConsumeUntil`, Pause-/Fokus-/Map-/Rebinding-Behandlung. Polling bleibt unverändert. | 27 neue Pufferfälle; wechselnde Render-/Physikraten, mehrere Ticks je Frame, Überlauf und Lifecycle des echten Beispiels. Physische Controller und OS-Fokuswechsel nicht durch Gerätetest abgenommen. |
| NET-005 | Verbindungsfähigkeiten zeigen Transport, echte Unreliable-Unterstützung, lokale Hostfähigkeit und beobachteten Verschlüsselungsstatus. Begrenzte Sendewarteschlangen, aktuelle Transformwerte bevorzugt, zuverlässige Broadcasts vor Annahme geprüft. Objekt-Lifecycle ist gegen Überläufe abgesichert. | 108 Tests, echte UDP-/WebSocket-Sockets mit langsamen Empfängern sowie getrennte Server-/Clientprozesse erfolgreich. 18 Lifecycle-Regressionen verwenden kontrollierte Managed-Entity-Bindings; kein nativer Welt-/Physiklauf behauptet. Browser-/WSS-/Internetabnahme offen. |

## Zentrale Prüfung

- Inkrementelle Release-Builds `EngineTests,EditorTests,Editor,CLI` erfolgreich; keine Premake-Regenerierung und kein Clean/Rebuild. Nach Korrektur der Ressourcenfixture nur `EngineTests` erneut gebaut (5,2 Sekunden).
- Engine: **54 Tests, 811 Assertions bestanden**. Editor: **37 Tests, 608 Assertions bestanden**. Filter decken Modul-/Projekt-/Paket-/Pfad-/Smoke- und Undo-/Asset-/Transferfälle ab; übrige native Tests wurden bewusst nicht erneut ausgeführt.
- Der erste Engine-Lauf fand eine alte CI-Smoke-Testfixture ohne Ressourcen. Der Positivfall legt jetzt ein isoliertes Ressourcenverzeichnis an; ein eigener Negativfall verlangt Exit 1 und reguläres `OnQuit` bei fehlenden Ressourcen. Die Produktionsprüfung wurde beibehalten.
- Modulprobe: echte `EngineModules.cpp`, `PackageResolver.cpp`, `PackageManifest.cpp` und `SemanticVersion.cpp`, vier Builds `2D/3D = 00, 10, 01, 11`. Aktivierung, Diagnosen und transitive Paketblockade geprüft. Kein vollständiger Runtime-Variantenbuild behauptet.
- Echte CLI: Tippfehler wird am Projektladeeintritt abgewiesen; leere und gezielt aktivierte Modullisten bleiben unverändert; JSON meldet `compiled`, `status` und `message`.
- Launcher und Editor starten aus `Fremdes Arbeitsverzeichnis Ä`, melden den erwarteten tatsächlichen Ressourcenpfad und beenden sich regulär mit Exit 0. Der Editor lädt dabei ein isoliertes Fixtureprojekt; eigene Benutzerdaten verhindern Änderungen an Nutzerpräferenzen.
- Echter Editor-/CLI-Smoke: Undo/Redo über Play/Stop erhalten; beim Szenenersatz wechselt die Instanz und alte Schritte sind nicht mehr ausführbar. Vollständige Originalaufnahmen vor/nach dem Ersatz visuell geprüft: `Undo/redo history ended` erscheint in der Statusleiste. Belege unter `undo-ui-run-e7796fc6348849b19bf6664a1152583a/`. Diese Prüfung deckt weder Detailpopup, Tooltip, Palette noch manuelle Asset-/Linked-Bedienung vollständig ab.
- Python: **40 bestanden, 1 POSIX-spezifischer Skip** (Ressourcenpfade, macOS-Installerfixtures, SDK-Manifest).
- Input: **207 Tests bestanden**, keine Skips; Paket- und Beispielassembly mit **0 Warnungen/Fehlern**.
- Networking: **108 Tests bestanden**, keine Skips, einschließlich **18 Lifecycle-Regressionen**. Getrennte Server-/Clientprozesse tauschen über UDP und WebSocket jeweils 500 geordnete Ereignisse je Richtung aus; Überlauf und Wiederholung tatsächlich erreicht. Öffentliche Spawn-/Despawn-/SetOwner-Aufrufe werden mit echten Transporten und kontrollierten Managed-Entity-Bindings geprüft.
- Das Abschlussreview fand eine Integrationslücke durch die neuen Sendefehler: lokale Spawn-/Despawn-Änderungen konnten vor Ablehnung der Nachricht eintreten. Spawn prüft jetzt vorab und rollt bei spätem Überlauf zurück; Despawn und Besitzerwechsel werden erst nach erfolgreicher Annahme verändert. Bereits eingetretene Destroy-/Disconnect-Änderungen erreichen verbleibende Clients oder trennen überlastete Peers ausdrücklich. Transport-Close und Besitzerbereinigung sind an eine sichere Poll-Grenze verschoben, damit Close-Callbacks keine gerade iterierten Listen verändern.

Damit sind **91 native Tests mit 1.419 Assertions, 315 Pakettests und 40 Pythonprüfungen** erfolgreich (zusätzlich ein plattformbedingter Python-Skip). Inkrementelle Builds, vier Modulvarianten, CLI-, Ressourcen-, Editor- und Mehrprozessprüfungen sind oben separat ausgewiesen.

## Belege und offene Plattformprüfungen

Die lokalen Rohbelege liegen unter `artifacts/backlog-round2/`: Baseline-Diffs/Hashes, `delta.json`, vier Status-/Berichtsdateien, native Testlogs, Pythonlog, TRX-Dateien, Modulvarianten und CLI-/UI-/Mehrprozesshelfer. Sie enthalten die genaue Trennung zwischen echten Prozessen, Fixtures und noch fehlenden Plattformläufen.

QAL-003 braucht weiterhin ein auf macOS neu gebautes DMG mit Mount/Kopie/Verschiebung, fremdem cwd und Unicodepfad sowie Signatur-, .NET-/SDK-, Runtime-, Export- und Agentprüfungen auf demselben Stand. Windows- und Fixtureerfolge ersetzen dies nicht. Networking hat keine Browser/WASM-, vertrauenswürdige WSS-/Proxy-, Internet-/NAT- oder Mobilefreigabe. Input hat keine physische Geräteabnahme.

Vorhandene Änderungen an Grafikquellen, External-Submodulen, Input-Asset-/Editorflächen, Manifesten und Registryindex wurden erhalten. `delta.json` grenzt diese Runde von der vorherigen Arbeit ab; der Inputchat hat die geschützten Dateien zusätzlich per Hashvergleich geprüft.

Chat-IDs für die Fortsetzung:

- Undo: `01a1a26a-3951-77c2-aa57-e501afb68b6f`
- Plattform/Module: `01a1a26a-3d39-7831-a6c0-29b9f105de52`
- Input: `01a1a26a-40f0-7891-9df6-1494449dc844`
- Networking: `01a1a26a-44dc-7800-aa6c-4f37a9f24f6a`
