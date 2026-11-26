# Input and networking source contracts

Stand: 13. November 2026. Die folgenden Verträge und früheren Prüfungen beziehen sich auf den Paketquellstand. Eine Veröffentlichung wird dadurch nicht behauptet.


## Touch-Gesten aus Funke.Input

Der lokale Paketstand stellt `Funke.InputActions.TouchGestureAdapter` bereit. Das Spiel ruft `Poll(focused)` einmal pro Eingabeschritt auf und liest die zurückgegebenen Ereignisse vor dem nächsten Aufruf. `IsClaimedByUI` sperrt einen Finger bis zum Loslassen. Tap verwendet unskalierte Zeit, Drag misst Distanzen relativ zur kürzeren Viewportseite und Pinch liefert die stabile Fingerpaarung sowie das Distanzverhältnis zum letzten Sample. Fehlende Finger, Fokusverlust und Größenänderungen brechen laufende Gesten ab. `Sample` verarbeitet aufgezeichnete Frames, darunter das Loslassen und erneute Berühren mit derselben Finger-ID. Die Geräteabnahme ist unter [INP-011](../../Funke_Agenten_Backlog.md#task-inp-011) erfasst.

## INP-005 — erledigt

`InputInteraction` ergänzt optionale Tap-/Hold-Zustandsautomaten für Button-Actions:
`Waiting`, `Started`, `Performed`, `Canceled` sowie framegebundene Übergangsflags.
Tap akzeptiert Loslassen bis einschließlich seiner Zeitgrenze. Hold löst beim ersten
noch gehaltenen Sample ab seiner Mindestdauer einmal aus; auf einem später beobachteten
Loslassen wird kein Hold nachgeholt. Beide verwenden die vorhandene injizierbare
unskalierte Zeitquelle. Mehrere Abfragen oder eine Map-Invalidierung schalten die
Interaktion im selben Frame nicht erneut weiter.

Deaktivierung, Rebinding, Override, Reset, Fokus-/Pausebehandlung im Beispiel und
Controllerverlust brechen offene Interaktionen ab. Eine gehaltene Taste darf nach
Reaktivierung nicht versehentlich starten. Ungültige oder rückwärts laufende Zeit sowie
Frame-Rücksprünge brechen ebenfalls ab. Bestehendes Polling und Ereignispuffer bleiben
unverändert; Interaktionen sind zusätzliche, explizit angelegte Laufzeitobjekte.

Dateien im Paketrepository:

- `packages/Funke.Input/Source/InputInteraction.cs` — neue API und Zustandsautomat.
- `packages/Funke.Input/Source/InputAction.cs` — Integration in Sampling und Lifecycle.
- `packages/Funke.Input/Examples/TapHoldInputExample.cs` — Fokus, Anwendungspause,
  Spielpause und Wiederaufnahme; injizierbare Quelle für die Beispielprüfung.
- `tests/Funke.Input.Tests/InteractionTests.cs` — 38 neue Fälle mit FakeInputSource.
- `packages/Funke.Input/README.md` — Zeit-, Lifecycle- und Nutzungskontrakt.

Abnahme: **245/245 Input-Tests bestanden**, keine Skips. Darin sind 38 neue Interaktions-
und Beispieltests enthalten: genaue Zeitgrenzen, Wiederholungsabfragen, ungültige Zeit,
Pause/Fokus, Rebinding, Controllerverlust, Disposen und erneute Aktivierung. Paketquellen
und Beispiele werden im Testprojekt gegen die echte Release-ScriptCore-Assembly gebaut.
Der Test benötigt keine Wartezeiten und keine nativen Input-Bindings.

Nicht ausgeführt: physische Controller-/OS-Fokusabnahme und ein nativer Editorlauf.
Die Aufgabe verlangt den deterministischen Quellen-/Zeitvertrag; reale Geräteerkennung
bleibt Aufgabe der vorhandenen Inputquelle. Fokus und Pause werden bewusst durch das
Spiel beziehungsweise das Beispiel gemeldet. Dauer und Interaktionsart sind nicht Teil
des Action-Asset- oder Rebinding-Profilschemas. Sourceversion **Funke.Input 0.2.0** bleibt
unveröffentlicht; bestehende Registry-Downloads erhalten diese Änderung noch nicht.

## NET-003 — vorhandene Umsetzung bestätigt, Mehrprozessnachweis ergänzt

Der bei Beginn bereits vorhandene Paketstand bietet `OwnerOnly`, `ServerOnly`,
`CustomAuthorization` und `RegisterPublicServerHandler`. Die Entscheidung verwendet
Transportabsender und aktuellen serverseitigen Owner vor Konstruktion und Deserialisierung
des Payloads. Im bisherigen Backlog war die vorhandene Abnahme aus
`packages/Funke.Networking/Tests/LoadBudgets.md` noch nicht verknüpft.

Neu ergänzt im Paketrepository:

- `tests/Funke.Networking.LoadTests/AuthorizationProbe.cs` — Client und Server in
  getrennten Prozessen, echte UDP-/WebSocket-Verbindungen, Produktions-Objektdispatch.
- `tests/Funke.Networking.LoadTests/Program.cs` — zusätzlicher `authorization`-Aufruf.
- `packages/Funke.Networking/Tests/LoadBudgets.md` — Reproduktion und aktuelle Ergebnisse.

**Vier erfolgreiche Zweiprozessläufe:** UDP/WebSocket jeweils als Dedicated Server und
Host. Fremde/fehlende Objekt-IDs, gefälschte Ownerangaben, Besitzerwechsel, eigene,
öffentliche, benutzerdefiniert autorisierte und serverexklusive Aktionen sowie Host-
Loopback geprüft. Dedicated Server: genau drei erlaubte Konstruktionen/Deserialisierungen;
Host: genau fünf. Unberechtigte Nachrichten lösen keine davon aus.

Die Probe verwendet kontrollierte verwaltete Objektdatensätze; sie behauptet keinen
nativen Entity-/Physiklauf. Die bestehenden Pakettests prüfen zusätzlich öffentliche
Ownerwechsel, Disconnect vor Dispatch und fehlschlagende Autorisierungspredikate.
Die API verspricht keine vollständige Cheat-Erkennung. Sourceversion
**Funke.Networking 1.0.1** ist weiterhin unveröffentlicht.

## NET-004 — vorhandene Umsetzung unter Last erneut abgenommen

Produktionscode und Budgets waren bereits vorhanden; kein weiterer Limitmechanismus
wurde eingeführt. **126/126 Networking-Tests bestanden**, einschließlich Rechteprüfung,
Handshake-/Queuegrenzen, fairer Verarbeitung, Disconnectpriorität, langsamer Empfänger
und großer zuverlässiger Nachrichten. Kein Skip.

Der vorhandene Lasttest wurde für UDP und WebSocket mit jeweils fünf getrennten
Workerprozessen erneut ausgeführt: regulärer Client, langsamer Empfänger, Floodingclient,
Server sowie 40 unvollständige Verbindungsversuche. Beide Läufe bestanden.

| Messung | UDP | WebSocket |
|---|---:|---:|
| Maximal verarbeitete Nachrichten pro Poll | 32 | 32 |
| Maximal gepufferte Nachrichten pro Peer | 128 | 128 |
| Maximal gepufferte Empfangsbytes pro Peer | 1.048.738 | 1.048.687 |
| Backpressure-Ablehnungen | 198 | 171 |
| Antworten beim regulären Client | 199 | 200 |
| Nahezu 1 MiB große zuverlässige Nachricht | empfangen | empfangen |
| Netzwerk-Arbeitszeit p95 / Maximum | 0,971 / 10,724 ms | 0,264 / 4,903 ms |

Überlastende Peers wurden mit `Receive queue or message limit exceeded` getrennt;
die regulären Clients blieben bereit. Speicher- und Zeitmessungen einschließlich
per-sekündlicher Heap-/Working-Set-Werte liegen als Rohdaten vor. Ein etwa elfsekündiger
Lauf belegt die geprüften Queuegrenzen, nicht generelle Leakfreiheit. Die Zeiten gelten
für den Netzwerkworker auf diesem Rechner, nicht als allgemeine Engine-Framerate.
Browser, TLS, Internetbedingungen und native Spielwelt waren nicht Teil dieser Abnahme.

## Belege und Buildumfang

Unter `artifacts/backlog-round5/` im Enginecheckout:

- `input-final/*.trx`, `network-tests/*.trx` — endgültige Pakettestergebnisse.
- `auth-{udp,ws}-{host,server}/server.json` und `client.json` — vier Rechteprüfungen.
- `network-load-{udp,ws}/*.json` — Server, regulärer/langsamer/Floodingclient und Handshakes.
- `baseline/` — Ausgangskopien der in dieser Runde weiterbearbeiteten Dateien.
- `verification.json` — Zusammenfassung und Hashes der verwendeten Dateien/Binaries.

Inkrementell gebaut: Input-Testprojekt samt Paketquellen/Beispielen,
Networking-Testprojekt und Networking-Lasttest, jeweils Release unter .NET 10.
Keine nativen Engineänderungen; kein eigener Enginebuild, Clean oder Premake-Neuaufbau.
Die erste Kompilierung der neuen Mehrprozessprobe fand eine falsche Callback-Signatur;
nach Korrektur bestanden Build und alle vier Läufe. Whitespace- und Berichtslinkprüfung
für das Delta dieser Runde bestanden. Der übrige Gesamtbacklog bleibt offen.


## Ursache und neues Verhalten

Schema-1-Profile adressierten einen Override über Map, Action und Listenindex. Nach
Umordnung konnte derselbe Index einen anderen Standard bezeichnen. Schema 2 speichert
stattdessen `bindingId`. Laufzeit-Overrides und Tastenerfassung ändern die Identität
eines Slots nicht. Der Loader fällt niemals auf dessen heutige Position zurück.

Code kann mit `Bind("keyboard", InputBinding.Key(KeyCode.Space))` stabile, innerhalb
der Action eindeutige IDs vergeben. Assets speichern dieselbe optionale `id`-Eigenschaft.
Der vorhandene Editor erzeugt IDs für neue Bindings und Vorlagen; Verschieben,
Controländerungen, Undo/Redo und Wiederherstellung bewahren sie. Die Konfliktsuche
vergleicht weiterhin physische Belegungen unabhängig von ihren IDs.

Alte Code-Definitionen und Assets ohne IDs bleiben lesbar. Sie verwenden einen
versionierten SHA-256-Fingerabdruck ihrer unveränderten Standardfelder. Dadurch bleiben
eindeutige Standards auch nach Umordnung zuordenbar. Geänderte Standards erhalten eine
andere Identität; identische anonyme Standards werden beim Speichern beziehungsweise
Adressieren eines Overrides als mehrdeutig abgelehnt. Für dauerhaft änderbare Standards
sind explizite IDs vorgesehen. Der Fingerabdruck ist durch einen festen Testwert gegen
unbeabsichtigte Formatänderungen abgesichert und kulturunabhängig.

## Kontrollierter Altprofilimport

Nichtleere Schema-1-Profile benötigen eine ausdrückliche Tabelle von historischen
`LegacyBindingSlot(Action, Index)`-Werten auf aktuelle IDs. Ein `null`-Ziel verwirft einen
entfernten Slot ausdrücklich. Die ursprüngliche Reihenfolge muss aus dem ausgelieferten
Spielstand bekannt sein; bei mehreren früheren Layouts wählt das Spiel die passende
Tabelle über seine eigene Speicher-/Releaseversion. Aus aktuellen Indizes wird keine
historische Zuordnung geraten.

Fehlende Zuordnungen, unbekannte oder mehrdeutige Ziele und mehrere alte Slots mit
demselben Ziel führen zu einem Fehler ohne Änderung aktiver Belegungen oder einer
laufenden Tastenerfassung. Leere Altprofile bleiben ohne Tabelle als Reset importierbar.
Erneutes Speichern schreibt Schema 2. Die neue PlayerPrefs-Überladung bietet denselben
Import; sie schreibt den Speicher nicht automatisch um.

Vollständige Validierung der Hülle, Identitäten und anwendbaren Bindings findet vor
der ersten Mutation statt. Schema-2-Einträge für entfernte Actions/IDs sowie ausdrücklich
verworfene Alt-Slots dürfen weiterhin obsolete Payloads enthalten. Sie werden nicht
angewendet. Grenzen bleiben 1 MiB JSON-Zeichen, 4096 Einträge und Tiefe 16.

Map-/Actionnamen bleiben Teil der Identität und erfordern bei Umbenennung eine eigene
Migration. Die README erklärt außerdem: Reset ändert die Live-Belegung; erst Speichern
ersetzt gespeicherte Overrides. Löschen eines gespeicherten Profils setzt die Live-Belegung
nicht zurück. Ältere Paketversionen können Schema 2 nicht lesen.

## Geänderte Dateien im Paketrepository

- `packages/Funke.Input/Source/InputBindingIdentity.cs` — ID-Prüfung und stabiler Fallback.
- `packages/Funke.Input/Source/InputBinding.cs` — optionale persistierte ID.
- `packages/Funke.Input/Source/InputAction.cs` — IDs, Eindeutigkeit und Erhalt bei Overrides.
- `packages/Funke.Input/Source/InputActionMap.Persistence.cs` — Schema 2 und Altprofilimport.
- `packages/Funke.Input/Source/InputActionAsset.cs` — Prüfung doppelter/ungültiger IDs.
- `packages/Funke.Input/Source/InputBindingConflicts.cs` — physischer Konfliktvergleich.
- `packages/Funke.Input/Source/Editor/InputActionEditorDocument.cs` — IDs für Vorlagen.
- `packages/Funke.Input/Source/Editor/InputActionEditorWindow.cs` — IDs für neue Bindings,
  aktualisierte Hinweise zu Umordnung und alten Standards.
- `packages/Funke.Input/README.md` — Identitätsvertrag und Migrationsbeispiel.
- `tests/Funke.Input.Tests/PersistenceTests.cs` — bestehende Altprofilprüfungen mit
  ausdrücklicher historischer Tabelle; Schema-2-Ausgabe.
- `tests/Funke.Input.Tests/BindingIdentityTests.cs` — 38 neue Fälle.

## Prüfung und Grenzen

**283/283 Tests bestanden, keine Skips.** Die 38 neuen Fälle prüfen Code-/Assetumordnung,
entfernte und unbekannte Ziele, doppelte IDs, anonyme Mehrdeutigkeit, Kulturunabhängigkeit,
geänderte Standards, historischen Import, ungültige spätere Einträge, Rebinding sowie
Editor-Undo/Redo und Wiederherstellung. Die vorhandenen Prüfungen für ungültige
Binding-Payloads und Größenlimits bleiben aktiv. Alle Paketquellen einschließlich
Editorcode und Beispiele wurden gegen die echte Release-ScriptCore-Assembly kompiliert,
mit Warnungen als Fehlern. Gezielte Whitespace- und Berichtslinkprüfung bestanden.

Reproduktion im Paketrepository:

```powershell
dotnet test tests/Funke.Input.Tests/Funke.Input.Tests.csproj -c Release --no-restore --logger 'trx;LogFileName=input-final.trx' --results-directory ../Index/artifacts/backlog-round6/input-final
```

Lokale Belege im Enginecheckout: `artifacts/backlog-round6/input-final/input-final.trx`,
`verification.json`, `task.patch` und `baseline/`. Die Patchdatei zeigt ausschließlich
diese Runde gegenüber dem zuvor bereits veränderten Arbeitsstand; SHA-256-Werte beziehen
sich auf die geprüften Quellen und Assemblies. `verify.py` prüft diese Bilanz erneut.

Kein nativer Enginebuild, kein manueller Editor-/PlayerPrefs-Lauf und kein Geräte- oder
Exporttest: geändert wurden verwaltete Identitäts-, Persistenz- und Editor-Datenpfade,
deren Zustandsübergänge die Pakettests direkt ausführen. Daraus wird keine neue native
Plattformabnahme abgeleitet. **Funke.Input 0.2.0 bleibt unveröffentlicht**; bestehende
Registry-ZIPs und Katalogeinträge wurden nicht verändert. INP-008/INP-009 und der übrige
Backlog bleiben separat zu bearbeiten.


## Gefundene Lücke und Änderung

`InputActionAsset.Load` las bisher ausschließlich über `System.IO`. Eine vom Build
aufgenommene `.inputactions.json` war dadurch aus einem reinen Asset-Bundle nicht
lesbar. Außerdem bot diese direkte Dateireferenz keine stabile Referenz bei Umbenennung.

`InputActionDataAsset : DataAsset` speichert die versionierte Definition im bestehenden
String-Feldcodec der Engine. `ReadDefinition` liefert eine unabhängige, validierte
Definition. Jede Factory erzeugt eigene, zunächst deaktivierte Maps. Das gemeinsame
DataAsset erhält weder aktive Maps noch Spieler-Overrides. `SetDefinition` validiert
vor dem Austausch. Nach erneutem Einlesen geänderte Defaults gelten für neue Instanzen;
laufende Spielerinstanzen behalten ihren Zustand.

`SaveDataAsset` exportiert eine normale `.dataasset`-Hülle mit Typ und Feldwerten atomar.
Vorhandene `.meta`-Dateien bleiben erhalten. Der Input-Editor bietet dafür **Export
runtime DataAsset** einschließlich seiner bestehenden Ersetzen-Abfrage. Der Export
setzt den Bearbeitungsstand der separaten JSON-Quelldatei nicht auf gespeichert.
Das kopierte Nutzungsbeispiel lädt das Runtime-DataAsset. `AssetInputExample` verwendet
eine typisierte `Controls`-Referenz, die der echte ScriptCore als GUID serialisiert.

`Load("Input/Player.dataasset")` und `LoadByGUID` verwenden den vorhandenen AssetManager.
Damit gelten dessen GUID- und Bundle-Ladewege. Fehlende/unpassende Assets liefern eine
verständliche Ausnahme; ungültige Definitionen werden vor der Map-Erzeugung abgelehnt.
Die Code-API und lose JSON-Dateien bleiben nutzbar. Direkter JSON-Dateizugriff benötigt
weiterhin lose Dateien, etwa in `StreamingAssets`; diese Einschränkung steht jetzt
ausdrücklich in der README. Map-/Action-Identität und Binding-IDs folgen INP-003.

## Geänderte Dateien im Paketrepository

- `packages/Funke.Input/Source/InputActionDataAsset.cs` — gemeinsamer DataAsset-Träger
  und validierte, unabhängige Factorys.
- `packages/Funke.Input/Source/InputActionAsset.cs` — DataAsset-/GUID-Laden und Export.
- `packages/Funke.Input/Source/Editor/InputActionEditorWindow.cs` — Runtime-Export und
  angepasste kopierte Nutzung.
- `packages/Funke.Input/Examples/AssetInputExample.cs` — typisierte DataAsset-Referenz.
- `packages/Funke.Input/README.md` — Authoring-, Export-, Referenz- und Migrationsvertrag.
- `tests/Funke.Input.Tests/DataAssetTests.cs`, `EditorDocumentTests.cs` und
  `Funke.Input.Tests.csproj` — 17 zusätzliche Fälle; echter ScriptCore-Feldcodec im Test.
- `packages/Funke.Input/Tests/AssetPipelineProbe.cpp` und `RunAssetPipeline.ps1` —
  gezielte native Abnahme gegen Produktions-Pruningcode und die echte Engine-DLL.

## Nachweise

**300/300 Pakettests bestanden, keine Skips.** Die 17 neuen Fälle prüfen unabhängige
Instanzen, ungültige spätere Definitionen/Controls/Composites, Austausch von Defaults,
Trennung von Spielerprofilen, atomaren Export, Metadaten-Erhalt, Fehlerdiagnosen und
die Ersetzen-/Dirty-Abläufe des Editor-Datenmodells. Der echte ScriptCore-Serializer
bestätigt `DefinitionJson` als String und `Controls` als DataAsset-GUID-Referenz.
Paketquellen, Editorcode und Beispiele wurden gegen die Release-ScriptCore-Assembly
kompiliert, mit Warnungen als Fehlern.

**Acht native Asset-Pipeline-Fälle bestanden:** JSON und Binärformat jeweils lose,
als unkomprimiertes Bundle, mit LZ4 und mit Zstd. Die Probe verwendet die von den
C#-Tests tatsächlich exportierte Datei. Sie kompiliert den aktuellen
`BuildAssetPruning.cpp` und verwendet `AssetRegistry`, `AssetBundle` und
`SceneSerializerStorage` aus beziehungsweise mit der echten Release-Engine:

- Eine serialisierte GUID-Referenz behält Runtime-Asset und `.meta` beim Pruning.
- Unreferenzierte Runtime-Definitionen und die Authoring-JSON werden entfernt.
- Umbenennung mit `.meta` erhält die GUID und bleibt im Build auffindbar.
- Ein literaler C#-Ladepfad hält die Datei ebenfalls im Build.
- Bundles werden in einem separaten Playerverzeichnis ohne lose Runtime-Datei gelesen.
  Typ und Definitionsinhalt bleiben exakt erhalten.
- Fehlende Dateien/GUIDs scheitern kontrolliert; zugehörige Warnungen im Log sind
  beabsichtigte Negativtests.

Es wurden nur die Probe und der betroffene Pruning-Übersetzungsbaustein kompiliert,
kein kompletter Enginebuild und keine Projektregeneration. Die vorhandene Warnung
C4834 in `AssetRegistry.hpp:914` trat dabei auf; kein Fehler im abschließenden Lauf.
Der Hash der geladenen Engine-DLL blieb während der Abnahme unverändert.

Reproduktion im Paketrepository, mit einem neuen Ausgabeordner für die native Probe:

```powershell
$env:FUNKE_INPUT_ASSET_EVIDENCE = 'C:/path/to/evidence/Controls.dataasset'
dotnet test tests/Funke.Input.Tests/Funke.Input.Tests.csproj -c Release --no-restore --logger trx
packages/Funke.Input/Tests/RunAssetPipeline.ps1 -EngineRoot ../Index -Fixture $env:FUNKE_INPUT_ASSET_EVIDENCE -OutputRoot C:/path/to/evidence/native
```

Lokale Belege im Enginecheckout: `artifacts/backlog-round7/input-final/input-final.trx`,
`Controls.dataasset`, `asset-pipeline-v3/pipeline.log`, `asset-pipeline-v3/pipeline.json`,
Compileraufruf/-log, acht Fixtureverzeichnisse, `verification.json`, `task.patch` und
`baseline/`. Fehlgeschlagene Probevorläufe bleiben getrennt erhalten; für die Abnahme
gilt ausschließlich `asset-pipeline-v3`. `verify.py` prüft Bilanz, Links und Hashes.

## Grenzen

Die native Probe belegt die genannten Produktions-Datenpfade, keinen vollständigen
GameBuilder-/SDK-Export oder plattformübergreifenden Spielstart. Der verwaltete Feldcodec
und die native Speicherpipeline wurden gezielt getrennt geprüft; kein vollständiger
CoreCLR-Hoststart mit dem Paket wird behauptet. Der bereits bekannte CLI-/Engine-
Headerstand-Mismatch war beim CLI-Aufruf weiterhin sichtbar; er wurde nicht umgangen
oder abgeschaltet. Die Probe benötigt die CLI nicht.

Keine manuelle Sicht-/Bedienabnahme des Editors und keine Controllerprüfung in dieser
Runde. Die vollständige Editorabnahme bleibt **INP-009**; deren Checkboxen wurden nicht
geändert. **Funke.Input 0.2.0 bleibt unveröffentlicht.** Bestehende Registry-ZIPs erhalten
die Änderungen erst durch einen separaten Release.


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

