# FunkeBench

Eine kompakte Desktop-Anwendung zum interaktiven Messen von Funke-ECS-Operationen und 2D-/3D-Rendering. Das Werkzeug selbst besteht aus einer Funke-Szene und nativen Funke-UI-Komponenten mit C#-Skripten.

## Arbeitsablauf

Szenario wählen, Entity-Anzahl und Messparameter einstellen, Einzelmessung oder wiederholten Lauf starten, Ergebnisse vergleichen und als CSV exportieren. Laufende Renderlast bleibt sichtbar. Reset entfernt ausschließlich die vom Benchmark erzeugten Entities.

## Messregeln

- ECS-Erstellung, Bewegung und Zerstörung werden synchron per hochauflösender Uhr gemessen. Setup und Aufräumen zählen nicht als gemessene Operation, außer sie gehören ausdrücklich zum benannten Test.
- Rendering wird nach einer Aufwärmphase über reale Frameintervalle gemessen. Framezeiten enthalten Anwendung, UI, Rendering und Präsentation; sie sind keine isolierten GPU-Zeiten.
- Jobs schaltet die Verarbeitung des Benchmark-Workloads zwischen serieller Ausführung und Funkes Job-System um. Das Betriebssystem und interne Engine-Threads bleiben aktiv.
- Gleiche Entity-Anzahl, Auflösung, VSync-Einstellung und Animationsoption sind Voraussetzung für Vergleiche. Automatisierte Screenshots dienen der Funktionsprüfung, nicht als belastbare Performance-Ergebnisse.
- Jede Messung hält ihre Einstellungen fest. Abbruch darf keine teilweise abgeschlossenen Läufe als fertiges Ergebnis ausgeben.

## Inhalt und Gestaltung

Die statische Oberfläche, Kameras und Beleuchtung sind in `Assets/Scenes/Bench.scene` editierbar. `Tools/build_ui.py` beziehungsweise der UI-Generator in `Tools/` erzeugt diese Szene reproduzierbar; für dauerhafte Änderungen den Generator bearbeiten.

Benchmark-Entities werden absichtlich zur Laufzeit erzeugt: Diese Operation selbst ist der Messgegenstand. Geometrische Sprites und primitive Meshes sind definierte Testdaten. Keine Spielmechanik, externen Bilder oder Pixel-Art-Assets sind erforderlich.

Die Oberfläche folgt Funke: kühle dunkle Flächen, neutrale Controls, Blau für Aktionen und Zustände, klare serifenlose Schrift, dezente Trennlinien. Palette in `Design/palette.json`.
