# Backlog-Runde 4 — 9. November 2026

Bearbeitung auf dem vorhandenen Branch, ohne Commit, Push oder Veröffentlichung.
Ausgangspunkt: `ed9552a2e0a8269dd6d5322271fae0983c3b5ad9` mit umfangreichen vorhandenen
Änderungen. Die drei bisherigen Berichte dokumentieren zusammen **33 unterschiedliche
Task-IDs**, darunter ausdrücklich nur teilweise erledigte Aufgaben. Sie sind keine
Abnahme sämtlicher 167 Backlogpunkte.

## QAL-007 — Implementierung ergänzt, echte CI-Abnahme offen

Der bisherige Gesamtcheck konnte nur aggregierte GitHub-Jobstatus ausgeben. Er erzeugte
keinen maschinenlesbaren Gesamtbericht und unterschied die Plattformausführungen einer
Matrix nicht. Der neue Check fordert für jede ausgewählte Ausführung einen eigenen
Bericht an; die Vollauswahl umfasst zwölf Ausführungen.

Geänderte Dateien:

- `.github/workflows/ci.yml`: IDs für 24 Pflichtschritte; Berichterstellung und Upload
  je Ausführung mit `always()`; Download, Aggregation und abschließendes JSON-Artefakt.
  Der Sparse-Checkout des Standalonejobs enthält auch die beiden Berichtsmodule.
- `scripts/ci/check_ci_evidence.py`: Capability-Matrix, Status und Gründe, Prüfung von
  Commit/Lauf/Versuch, Ablehnung fehlender, doppelter oder veralteter Berichte. Ein mit
  `continue-on-error` kaschierter Schrittfehler wird anhand von `outcome` erkannt.
- `scripts/ci/test_evidence.py`: Python-/TRX-Auswertung mit Fallidentitäten, tatsächlichen
  Ausführungen, Skipgründen und unbekannten Assertionzahlen als `null`.
- `scripts/ci/run_script_tests.py`: bisherige unittest-Discovery mit strukturiertem
  Ergebnis; auch Subtestfehler und erwartete Fehler werden sichtbar bilanziert.
- `scripts/ci/test_ci_evidence.py`: Regressionen für Matrixgeschwister, Abbruch, Skip,
  leere/ungültige Ergebnisse, falsche Herkunft, doppelte Berichte und CLI-Fehlerausgabe.
- `scripts/ci/test_release_scripts.py`: vorhandene Quellprüfung an die bereits vor
  dieser Runde eingeführte zeilenweise Compilerdiagnostik angepasst. Fehler müssen
  weiterhin als Fehler sichtbar sein; die native Implementierung blieb unverändert.
- `Github-Docs/Documentation/Editor/Testing.md`: Pflichtmatrix, Zählweise und Grenzen.

Die Fixture mit 589 C#-Fällen auf drei Plattformen ergibt **589 eindeutige Fälle,
drei Suite-Ausführungen und 1.767 ausgeführte Fälle**. Dies ist ein Test des Aggregators,
kein behaupteter echter Dreiplattformlauf. Native und Smokeprüfungen liefern weiterhin
Schrittstatus und ihre vorhandenen Detailartefakte; ihre Assertionzahlen werden nicht
aus Jobzahlen abgeleitet. Hardware-GPU und Mobilgeräte bleiben explizit `not_run`.

Prüfungen:

- 20 gezielte Coverage-/Evidence-Tests bestanden, einschließlich kontrollierter
  Fehler-, Abbruch-, Skip- und Fehlberichtsfälle.
- Vollständige Python-Discovery unter Windows: **281 Fälle, 271 bestanden,
  10 begründete Plattform-Skips, keine Fehler**. Alle Skipgründe stehen in
  `artifacts/backlog-round4/python-verified.json`.
- 71 vorhandene Vector-Tests der Release-ScriptCore-Testassembly erfolgreich
  ausgeführt; den dabei erzeugten echten TRX-Bericht fehlerfrei eingelesen. `--no-build`
  war bewusst gewählt: geprüft wurde der neue Parser, nicht die Aktualität aller
  anderweitig geänderten C#-Quellen.
- Workflow mit PyYAML gelesen; alle 24 Capability-IDs, die `always()`-Berichtswege,
  Standalone-Sparse-Checkout und die Auswahlen all/posix/linux/macos/core abgeglichen.
- Whitespaceprüfung der bearbeiteten Dateien bestanden.
- Tatsächliche lokale Python- und TRX-Ergebnisse gemeinsam aggregiert:
  `artifacts/backlog-round4/local-aggregate.json`. Die beiden Windows-Suites werden
  korrekt gezählt; das Gesamtgate bleibt wegen fehlender anderer Plattform- und
  Instrumentierungsberichte erwartungsgemäß **nicht bestanden**.

Grenzen: Kein Remote-CI-Lauf dieses uncommitteten Arbeitsstands; keine neue Linux-/macOS-
Abnahme. Der verlangte Gesamtabschluss von QAL-007 bleibt deshalb offen. Für einen
neuen CI-Versuch müssen alle Jobs neu ausgeführt werden: ältere Versuchsartefakte werden
bewusst nicht als aktueller Nachweis akzeptiert. Ein vollständiger Workflowabbruch kann
auch `always()`-Schritte verhindern; das Fehlen der Berichte ergibt keinen Erfolg.

## QAL-001 / QAL-009 — Standaloneblocker konkret reproduziert

Das vorhandene Launcher-Verzeichnis ist ein **Entwickler-SDK**; der ursprüngliche
Aufruf wurde korrekt wegen `EngineSDK/bin` abgelehnt. Für den zweiten Versuch wurden
die vorhandenen Hostdateien in einen separaten Prüfbereich kopiert und das reguläre
`CopyEngineSdk.py --binary-only` verwendet. Kein Quellverzeichnis und keine ursprüngliche
Buildausgabe wurde dafür verändert.

Die echte Standaloneprüfung kopierte dieses Binärlayout nach außerhalb des Repositorys,
verwendete ein fremdes Arbeitsverzeichnis sowie einen reduzierten PATH mit Toolguards.
.NET 10.0.403, Guard-Selbsttest und Launcher-Smoke bestanden. Die Projekterzeugung meldete:

```text
funke.exe headers: ac13aaa439654a2d
loaded Funke-Engine.dll headers: 96a2bd8f5043a5ee
Project package generation failed: invalid option 'funke-project'
```

Der Harness brach korrekt bereits vor Editor-Recompile ab. Der lokal zusammengestellte
Prüfstand ist somit **kein bestandener Standalonenachweis** und kein veröffentlichtes
Releasepaket. Während der Untersuchung liefen andere native Compilerprozesse im selben
Checkout; die CLI-/DLL-Ausgaben änderten sich. Kein konkurrierender Neuaufbau wurde
gestartet und keine Header- oder Toolchainprüfung entfernt.

Nächste Schritte: zusammengehörige CLI-/Engine-/Runtime-Ausgaben nach Abschluss der
laufenden Builds stagen; den regulären Unicode-Premake-Vorbereitungsschritt
`PrepareBundledPremake.ps1` auf der gestagten Kopie anwenden; danach den unveränderten
Standaloneharness erneut ausführen. Erst wenn dieser bis Status/Recompile gelangt,
kann die ursprüngliche JSON-Abnahme abgeschlossen werden.

Belege: `artifacts/backlog-round4/standalone-binary.json` enthält Binärhashes, Manifest,
reduzierten PATH, Befehle und Exitcodes; `standalone-logs/` enthält die Laufprotokolle.
Die Abnahmecheckboxen für QAL-001 und QAL-009 bleiben offen.

## Abgrenzung

Keine nativen Produktionsdateien in dieser Runde verändert; deshalb kein eigener
Enginebuild. Bereits vorhandene Änderungen bleiben erhalten. Ausgangskopien der von
dieser Runde bearbeiteten Dateien liegen unter `artifacts/backlog-round4/baseline/`.
Der Gesamtbacklog bleibt in Bearbeitung; insbesondere unabhängige Sanitizer-/Coverage-
Läufe, reale Plattformabnahmen, größere Ausbauaufträge und Veröffentlichung sind nicht
durch diese Runde erledigt.
