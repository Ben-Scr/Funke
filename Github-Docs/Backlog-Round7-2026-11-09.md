# Backlog-Runde 7 — 9. November 2026

**INP-008 ist im unveröffentlichten Paketquellstand abgeschlossen.** Die vorhandenen
Action-Asset-Definitionen und Factorys wurden weiterverwendet. Neu sind die Anbindung
an Funke-DataAssets und ein konkreter Exportnachweis. Bestehende Branches und lokale
Änderungen blieben erhalten; keine Commits, Pushes oder Veröffentlichungen.

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
