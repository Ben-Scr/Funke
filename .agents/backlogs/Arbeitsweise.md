# Backlogs pflegen und bearbeiten

Die [Gesamtübersicht](../../Github-Docs/Funke_Agenten_Backlog.md) führt zu jeder maßgeblichen Aufgabenbeschreibung. Themenordner dienen als Einstieg. Fachlich verschiedene Arbeiten am
gleichen System bleiben getrennt, zum Beispiel Messung, Umsetzung und Plattformabnahme.

Vor dem Entfernen erledigter Aufgaben die dauerhafte Dokumentationsabdeckung
unter `Github-Docs/Documentation/` prüfen. Lokale Projektanweisungen und aktuelle
Nutzeraufträge haben Vorrang. Task-IDs werden nicht neu nummeriert.

## 1. Prüfstand und Quellen

| Bestandteil | Geprüfter Stand |
|---|---|
| Engine | [`ed9552a2`](https://github.com/Ben-Scr/Funke/commit/ed9552a2e0a8269dd6d5322271fae0983c3b5ad9) – bei Erstellung erneut als `master`-HEAD bestätigt |
| Paketregistry | [`0fa5a155`](https://github.com/Ben-Scr/Funke-PackageRegistry/tree/0fa5a15540073926e3784e1eb3d92eb011ccf6f7) |
| Asset Library | [`555cc0fd`](https://github.com/Ben-Scr/Funke-AssetLibrary/tree/555cc0fdae1f015fba9517ee197260c11a7bf403) |
| CI-Evidenz des ursprünglichen Audits | [Run 41534529365](https://github.com/Ben-Scr/Funke/actions/runs/41534529365) |

Grundlage sind ausgewählte Implementierungen, Tests, Dokumentation und bereits vorhandene CI-Logs/Diagnoseartefakte. Im Audit wurde kein eigener vollständiger Enginebuild und kein eigener FPS-Vergleich durchgeführt. Konkrete Quellen stehen zusätzlich bei jedem Task. Die Quellenlinks sind auf den Auditstand fixiert; arbeite im späteren aktuellen Checkout und übertrage die Absicht auf inzwischen verschobene Dateien.

**Bereits vorhanden und wiederzuverwenden:** C++- und C#-Scripting, native Rebuild-/Reloadpfade, GPU-Skinning mit Fallback, Undo/Recovery, asynchrone Assetvorbereitung, Agenten-Bridge, Tilemap-, Input-, Networking-, AudioMixer- und weitere Pakete. Ein fehlender Unteraspekt rechtfertigt keinen pauschalen Neubau des gesamten Systems.

## 2. Komplexität, Priorität und Ausführung

Die [Komplexitätsstufen K1 bis K4](../../Github-Docs/Funke_Agenten_Backlog.md) bewerten Restarbeit, Schnittstellen und Prüfumfang. Aufgaben aufsteigend nach Stufe und anschließend nach ID einordnen. Neue Nachweise können die Einstufung ändern. Für die Ausführung gelten die sachlichen Abhängigkeiten.

| Kennzeichnung | Bedeutung |
|---|---|
| **P0** | Aktuell belegte Blocker im CI-/Auslieferungsablauf zuerst beheben. |
| **P1** | Hoher Nutzen für Zuverlässigkeit, Nutzbarkeit oder einen belastbaren Release. |
| **P2** | Gezielte Verbesserung beziehungsweise wichtige Ergänzung je nach Spiel-/Produktziel. |
| **P3** | Größerer oder spezialisierter Ausbau; bewusst auswählen. |
| **Fehler** | Konkrete Abweichung mit Beleg. Reproduktion erhalten, Ursache beheben. |
| **Dokumentierte Grenze** | Bestehende eingeschränkte Funktion; kein automatisch fehlerhafter Code. |
| **Prüfauftrag** | Auffälligkeit oder fehlender Nachweis. Erst prüfen; nur einen nachgewiesenen Defekt reparieren. |
| **Ausbau** | Vorgeschlagene zusätzliche Fähigkeit, deren Bedarf vom Zielprojekt abhängt. |
| **Pflege/Refactoring** | Verständlichkeit, Verträge oder Wartbarkeit ohne unbeabsichtigte Funktionsänderung verbessern. |

<a id="arbeitsregeln"></a>

### Arbeitsregeln für den ausführenden Agenten

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

### Kopiervorlage für einen einzelnen Auftrag

```text
Arbeite Task <ID> aus der über den Themenordner verlinkten Fachbeschreibung ab.
Prüfe zuerst aktuellen Stand und direkte Abhängigkeiten. Berücksichtige bestehende
Implementierungen. Setze die nummerierten Anweisungen um und prüfe die Abnahme.
Bearbeite angrenzende Tasks nur, soweit sie unvermeidbare Voraussetzungen sind;
weise größere zusätzliche Arbeiten gesondert aus. Zeige am Ende konkrete Änderungen,
Testnachweise, offene Blocker und den Status an. Behaupte keinen Abschluss ohne Abnahme.
```

<a id="abschlussprotokoll"></a>

## 3. Abschlussprotokoll für die schrittweise Bearbeitung

Dokumentiere den Abschluss mit diesem Block in einem Ergebnisbericht. Entferne danach die vollständig abgenommene Aufgabe aus der maßgeblichen Aufgabenbeschreibung und aktualisiere Gesamtübersicht, Themeneinstieg und Abhängigkeiten. Bei Teilfortschritt bleibt nur die konkrete Restarbeit offen; fehlende Nachweise dürfen nicht als bestanden markiert werden.

```text
Task-ID:
Status: erledigt | bereits erledigt | teilweise erledigt | blockiert | bewusst zurückgestellt
Geprüfter Ausgangscommit / Ergebniscommit:
Geänderte Dateien:
Konkrete Verhaltensänderung oder bestätigtes Prüfergebnis:
Ausgeführte Tests / Plattform / Buildprofil / Ergebnis:
Nicht ausgeführte Prüfungen und genauer Grund:
Erfüllte und offene Abnahmekriterien:
Benötigte Paketversion / Migrationshinweis:
Nächster sinnvoller Task:
```
