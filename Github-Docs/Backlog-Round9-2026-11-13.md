# Backlog-Runde 9 – 13. November 2026

Bearbeitet: **QAL-001, QAL-002, QAL-004, QAL-007, QAL-023**.
Die Auswahl setzt bei den noch offenen Abnahmen der früheren Runden an.
NET-002 liegt im benachbarten, in dieser Sitzung nicht schreibbaren Paketrepository
und wurde nicht geändert. Bestehende Nutzeränderungen wurden erhalten.

**Keiner dieser fünf Gesamtaufträge ist durch diese Runde vollständig abgenommen.**
Die folgenden konkreten Lücken sind im Quellstand korrigiert und lokal geprüft;
die verbleibenden echten Plattformläufe bleiben offen.

| Aufgabe | Änderung / Nachweis | Noch offen |
|---|---|---|
| QAL-001 | Der Standaloneharness parst nun jede vollständige `--json`-stdout-Antwort, auch Play/Stop/Screenshot. Präfixe und Nachlauftext führen zum Fehler; stdout/stderr bleiben getrennt erhalten. Prozessbasierte Regression für verunreinigte Antworten bestanden. | Vollständiger Standalone-/Agentlauf mit zusammenpassenden Engine-/CLI-Binaries und reduziertem PATH. |
| QAL-002 | Bereits die erste Bridge-Abfrage und ihre Wiederholungen verwenden das verbleibende gemeinsame Zeitbudget. Die bisherige zusätzliche Zehn-Sekunden-Zugabe entfällt. Regression mit 250-ms-Gesamtbudget bestanden; bestehende Readiness-Tests bestanden. | Echter langsamer Linux-Editorstart, Crash und Recompile-Negativfall auf dem aktuellen Stand. |
| QAL-004 | Die unabhängige Sanitizer-Lane speichert doctest-XML; der Evidenzcheck lehnt fehlende, leere, unvollständige oder fehlgeschlagene Testnachweise trotz erfolgreichem Schrittstatus ab. Parserregressionen und Workflowverdrahtung geprüft. | Vollständiger Linux-ASan/UBSan/LeakSanitizer-Lauf und kontrollierter vorgelagerter Editorfehler im echten CI. |
| QAL-007 | Sanitizer-Testfälle fließen in den Gesamtbericht ein. Widersprüchliche Ausführungsflags, nichtboolesche Python-Abschlussmarker und nichtganzzahlige Fallzahlen werden abgelehnt. Negative Gate-Tests bestanden. | Echter Gesamtbericht aus einem aktuellen CI-Lauf einschließlich aller erforderlichen Plattform-/GPU-/Coverageberichte. |
| QAL-023 | Optionaler vertrauenswürdiger Buildbeleg wird vor dem Start an erwarteten Commit, Plattform, Exporthash und Toolchain gebunden. Mock-/fremde/veraltete Belege scheitern. Ohne Buildbeleg bleibt ein erfolgreicher Smoke `real_runtime: unverified`. | Automatische Belegerzeugung durch echte Builder, Übernahme in den Plattformworkflow und reale Export-/Geräteabnahmen. |

## Prüfungen und Grenzen

- **62 gezielte Python-Tests bestanden:** 61 im gemeinsamen Lauf, anschließend
  ein zusätzlicher Integrationstest für Buildbelegprüfung und Ergebnisweitergabe.
  Die simulierten Plattformtests sind Unit-Tests, keine Browser-/Geräteabnahme.
- Drei bestehende Tests scheiterten im ersten breiteren Lauf an der Umgebung und
  wurden im erfolgreichen gezielten Lauf ausdrücklich ausgenommen, nicht als
  bestanden gezählt: `test_generated_project_rejects_aliased_absolute_repository_reference`
  (Verzeichnisverknüpfung: Zugriff verweigert),
  `test_nonzero_child_and_timeout_preserve_structured_evidence`
  (`taskkill` beendete den eigenen Testprozess nicht; nachfolgender Timeout),
  `test_browser_server_uses_wasm_mime_and_isolated_local_origin`
  (Loopback-Verbindung: WinError 10013).
- Ein vorhandenes natives Testbinary führte den SemanticVersion-Fall erfolgreich
  aus: 1 Test, 42 Assertions, 982 übersprungene/gefilterte Fälle. Sein echtes XML
  wurde mit dem neuen Leser verarbeitet. Das prüft das Berichtsformat, nicht den
  aktuellen nativen Quellstand oder Sanitizerinstrumentierung.
- Workflow-YAML geprüft: keine `needs`-Abhängigkeit der Instrumentierung,
  `fail-fast: false`, getrennte Sanitizer-/Coveragevarianten, Leak-/UBSan-Optionen,
  XML-Ausgabe und Berichtserfassung mit `always()` vorhanden.
- `git diff --check` für die geänderten Prüfskripte und betroffenen eigenständig
  geänderten Dokumentationsdateien ohne Whitespacefehler.
- WSL-Abfrage scheitert mit `Wsl/E_ACCESSDENIED`. Kein Linuxlauf behauptet.
- Die vorhandene CLI meldet Headerstand `1e2bfa0aa2d69005`, die geladene Engine-DLL
  `a6b0826d9b977dca`. Der Neubauversuch wurde ohne Abschluss beendet, nachdem keine
  weitere Ausgabe über den ImGui-Schritt hinaus erfolgte. Die Ursache dieses
  Stillstands wurde nicht bestimmt. Kein neuer Engine-/CLI-/Editorbuild behauptet.

Lokale Rohbelege: `artifacts/backlog-ci-targeted.json`,
`artifacts/backlog-doctest-schema.xml`, `artifacts/backlog-engine-build.log`.
Der zusätzliche Buildbeleg-Integrationstest wurde separat mit unittest ausgeführt.

## Dauerhafte Dokumentation

- [Standalone-Prüfung](Documentation/Editor/Standalone-Verification.md): vollständige JSON-Ausgabe und gemeinsames Zeitbudget.
- [Testnachweise](Documentation/Editor/Testing.md): Sanitizer-XML und konsistente Ausführungszählung.
- [Exportprüfung](Documentation/Editor/Platform-Export-Testing.md): Buildbelegformat, Vertrauensgrenze und unbestätigte Smokes.

Keine offenen Gesamtaufträge entfernt, keine Veröffentlichung und kein Commit.


## Fortsetzung am 13. November

QAL-023 weitergeführt:

- `build_export_evidence.py` erzeugt den Beleg um den tatsächlichen, vom CI-Job
  vorgegebenen Exportbefehl. Commit und sauberer Quellstand werden davor und
  danach geprüft; vorhandene Exporte, fehlende Toolversionen, fehlgeschlagene
  Befehle oder fehlende Ausgaben ergeben keinen erfolgreichen Beleg. Buildlog,
  Toolversionen und Exportinventar bleiben erhalten.
- Der Webworkflow lädt optional den Beleg aus demselben erfolgreichen Quelllauf.
  Erwarteter Commit, Repository und Push-/Dispatch-Ereignis werden geprüft;
  PR-/Forkläufe sind keine vertrauenswürdigen Buildbelege. Ungebundene Smokes
  bleiben weiterhin ausdrücklich unbestätigt.
- Das Inventar sortiert relative Pfadstrings jetzt auf allen Betriebssystemen
  identisch. Zuvor konnte Windows bei gemischter Groß-/Kleinschreibung einen
  anderen Hash als Linux erzeugen.
- Neun neue Regressionen prüfen Erzeugung/Verbrauch des Belegs, fehlende oder
  alte Exporte, Compilerfehler, Quelländerungen, falsche Commits, CI-Laufherkunft
  und plattformunabhängige Sortierung. Die verwendeten kleinen Testbefehle sind
  ausdrücklich synthetische Fixtures, keine echten Plattformcompiler.

Der Gesamtauftrag bleibt offen bis zu tatsächlichen CI-/Export-/Geräteläufen.
Die direkte Integration in jeden Editor-Builder ist nicht erfolgt; der neue
Wrapper ist der vorgesehene explizite Aufruf für den vertrauenswürdigen CI-Build.
Keine neuen nativen Builds oder Plattformabnahmen in dieser Fortsetzung.

Abnahme der Fortsetzung: **71 gezielte Tests bestanden**; die drei oben genannten
umgebungsbedingt blockierten Tests waren ausdrücklich ausgenommen. Workflow-YAML
und sämtliche eingebetteten Python-Schritte wurden geparst beziehungsweise
kompiliert. Whitespaceprüfung bestanden. Maschinenlesbarer Nachweis:
`artifacts/backlog-round9-continuation-tests.json`.
