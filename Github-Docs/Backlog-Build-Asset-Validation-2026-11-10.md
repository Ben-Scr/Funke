# Backlog: Ungültige Assets beim Build

Stand: 10. November 2026. Alle Aufgaben offen; dieses Dokument beschreibt das
Zielverhalten, keine bereits umgesetzte Änderung.

## Anlass und belegter Istzustand

Der Web-Export des Projekts „Neues Spiel41“ bricht bei der Vorbereitung der Assets
ab. `Assets/File.dataasset` ist leer (0 Byte). Das Editor-Log meldet am
10. November um 19:13:29 `Cannot convert serialized asset … File.dataasset: File is empty`.
Die C#-Kompilierung war erfolgreich; der eigentliche Web-Builder wird noch nicht erreicht.

Die gemeinsame Build-Pipeline konvertiert `.scene`, `.prefab` und `.dataasset`
im Exportverzeichnis ins Binärformat. Ein Konvertierungsfehler beendet den Build.
Die Oberfläche zeigt dabei nur `Scene asset optimization failed`; Dateipfad und
Ursache sind dort nicht erkennbar. Die Meldung ist auch deshalb ungenau, weil sie
bei einem Data Asset auftritt.

In diesem Build ist das Entfernen unbenutzter Assets wegen nativer Quellen oder
Paketabhängigkeiten deaktiviert. Deshalb wird die leere Datei mit exportiert.
Dass sie tatsächlich unbenutzt ist, wurde nicht nachgewiesen. Der beobachtete
Fehler liegt in der gemeinsamen Asset-Vorbereitung und ist nicht Web-spezifisch.
Weitere mögliche Web-Build-Fehler nach dieser Stufe sind noch nicht geprüft.

## Gewünschter Vertrag

| Asset-Nutzung | Verhalten bei leerem oder ungültigem serialisiertem Asset |
|---|---|
| Referenziert oder ausdrücklich für den Export vorgesehen | Build mit konkreter Dateiangabe und Ursache abbrechen. |
| Nachweislich unbenutzt und durch die Exportregeln ausschließbar | Vor der Konvertierung weglassen; höchstens eine Warnung, kein Build-Abbruch. |
| Unklar, etwa bei konservativer Übernahme wegen nativer Quellen, Paketen oder dynamischen Zugriffen | Asset beibehalten und bei ungültigem Inhalt abbrechen; Unsicherheit verständlich erklären. |

Eine fehlende statische Referenz allein beweist keine Nichtverwendung. Leere
Dateien dürfen nicht pauschal übersprungen oder automatisch durch leere gültige
Assets ersetzt werden. Quelldateien und Metadaten bleiben unverändert.

## Offene Aufgaben

- [ ] **Fehler vollständig bis zur Build-Anzeige weiterreichen.** Projektbezogenen
  Pfad, fehlgeschlagenen Schritt und eigentliche Lese-/Konvertierungsursache
  erhalten. Beispiel: `Asset-Export fehlgeschlagen: Assets/File.dataasset ist leer.`
  Die kurze Meldung im Build-Panel und das Ergebnis des Kommandozeilen-Builds
  müssen dieselbe konkrete Ursache erkennen lassen. Temporäre Staging-Pfade
  können zusätzlich im Detail-Log stehen.

- [ ] **Exportentscheidung und Validierung aufeinander abstimmen.** Prüfen, ob
  nachweislich unbenutzte ungültige Dateien bereits zuverlässig vor der
  Konvertierung ausgeschlossen werden; verbleibende Lücken schließen. Benötigte
  oder vorsichtshalber mitgenommene ungültige Assets müssen weiterhin blockieren.
  Bei konservativer Übernahme den Grund im Build-Bericht sichtbar machen, statt
  eine fehlende Referenz als Erlaubnis zum Überspringen zu behandeln.

- [ ] **Regressionen und echte Exporte prüfen.** Die folgenden Fälle für die
  gemeinsame Pipeline absichern und die Fehlerdarstellung in Editor und CLI
  kontrollieren. Mindestens einen Web- und einen Desktop-Export prüfen; für den
  Web-Nachweis nach Beseitigung des Asset-Blockers bis zum fertigen Export und
  einem Start über HTTP weiterprüfen.

- [ ] **Dauerhaften Vertrag dokumentieren.** Nach der Umsetzung die Regeln für
  Build-Abbruch, Asset-Ausschluss und konservative Übernahme sowie die Diagnose
  leerer Data Assets in die passende Dokumentation unter `Documentation/`
  übernehmen. Erst nach dokumentierter Abdeckung erledigte Punkte entfernen.

## Abnahmekriterien

| Prüffall | Erwartetes Ergebnis |
|---|---|
| Referenzierte leere `.dataasset` | Build scheitert mit Projektpfad und Ursache „Datei ist leer“. |
| Referenzierte beschädigte `.scene`, `.prefab` oder `.dataasset` | Build scheitert mit konkreter Lesefehlerursache; kein stilles Überspringen. |
| Nachweislich unbenutztes ungültiges Asset bei zuverlässiger Asset-Auswahl | Datei wird ausgeschlossen; Build gelingt bei sonst gültigem Projekt. |
| Dasselbe Asset bei konservativer Übernahme | Build scheitert; Grund für die Übernahme ist nachvollziehbar. |
| Gültige serialisierte Assets | Bisherige Konvertierung und Export funktionieren weiter. |
| Fehlgeschlagener Export | Quelldateien und `.meta` bleiben unverändert; ein vorhandener erfolgreicher Export wird nicht durch einen unvollständigen ersetzt. |

## Einstiegspunkte im Code

- `Funke-Editor/src/Build/GameBuilder.cpp`: `ConvertSerializedAssets`, Fehlerweitergabe
  nach der Konvertierung und `failBuild`.
- `Funke-Editor/src/Editor/BuildAssetPruning.cpp`: Erreichbarkeit und konservative
  Asset-Übernahme.
- `Funke-Engine/src/Serialization/SceneSerializer.cpp`: `ConvertFileFormat` und
  Weitergabe der ursprünglichen Leseursache.
