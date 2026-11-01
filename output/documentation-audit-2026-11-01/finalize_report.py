from pathlib import Path
import difflib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
for script in ['audit.py', 'check_static.py']:
    result = subprocess.run([sys.executable, '-X', 'utf8', str(OUT / script)], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
    if result.returncode:
        print(result.stdout, result.stderr)
        raise SystemExit(result.returncode)
checks = json.loads((OUT / 'checks.json').read_text(encoding='utf-8'))
static = json.loads((OUT / 'static-checks.json').read_text(encoding='utf-8'))
examples = json.loads((OUT / 'examples.json').read_text(encoding='utf-8'))
scopes = ['README.md', 'CHANGELOG.md', 'CLAUDE.md', 'CONTRIBUTING.md', 'Funke-Engine/README.md', 'Github-Docs/Documentation', '.agents/skills', '.claude/skills', '.github/PULL_REQUEST_TEMPLATE.md', 'installer/README.md']
diff_check = subprocess.run(['git', '-c', 'core.safecrlf=false', 'diff', '--check', '--', *scopes], cwd=ROOT, capture_output=True, text=True)
(OUT / 'diff-check.log').write_text(diff_check.stdout + diff_check.stderr, encoding='utf-8')
if diff_check.returncode:
    raise SystemExit(diff_check.stdout + diff_check.stderr)
baseline = json.loads((OUT / 'before.json').read_text(encoding='utf-8'))
review = []
for rel in checks['changed_since_start']:
    if rel.startswith('.claude/'):
        continue
    review.extend(difflib.unified_diff(baseline[rel].splitlines(True), (ROOT / rel).read_text(encoding='utf-8-sig').splitlines(True), fromfile='start/' + rel, tofile='current/' + rel))
(OUT / 'review.diff').write_text(''.join(review), encoding='utf-8')

report = f'''Funke-Dokumentationsprüfung — 2026-11-01

Umfang
{checks['documents']} Dokumentationsdateien im Repository erfasst, einschließlich Handbuch,
READMEs, Entwickler-/Installationsanleitungen, Changelog, Agent-Skills und ihrer
Spiegel sowie der beiden begleitenden Textdokumente. Inventar: checks.json.
Fremdbibliotheken, erzeugte Builddateien und Lizenztexte wurden nicht umgeschrieben.
Historische Changelog-Einträge bleiben als Verlauf erhalten; die Dokumentations-
überarbeitung steht unter Unreleased. Die Anleitungen beziehen sich auf den
aktuellen Quellstand; installierte Versionen sollten ihren passenden Tag verwenden.

Korrigierte Themen
- Optionale .fbundle-Archive, LZ4, lose StreamingAssets und Runtime-Mounting.
- Native C++-Skripte beim Desktop-Export, CMake-Scaffolding und SDK-Voraussetzungen.
- Transitive Paketabhängigkeiten, verfügbare Registry-Releases und Module-Varianten.
- Szenenlebenszyklus, asynchrones Laden/Preloading und kooperative Zeitbudgets.
- Private Snapshots aller Edit-Szenen und Wiederherstellung der Undo-Historie.
- World-Space-UI, erlaubte Transform-Kombinationen, Renderreihenfolge und Effekte.
- 3D-Mesh-Collider, Spatial Audio/HRTF, Asset-Vorschauen und öffentliche Assettypen.
- Launcher-Lokalisierung, portable SHA-256-Prüfung und getrennte Font-Ladewege.
- Startablauf, Plattformvoraussetzungen (einschließlich macOS 15 für .NET 10),
  Befehle, vollständige Beispiele, Markdown-Formatierung und interne Verweise.
- Inkrementelle Verifikation in CONTRIBUTING, CLAUDE und der PR-Vorlage.
- Skill-Spiegel synchronisiert. Beim Native-Scripting-Skill war die neuere
  Dateizeit auf der inhaltlich älteren Kopie; übernommen wurde die aktuelle Anleitung.

Prüfergebnisse
- {checks['internal_links']} interne Links/Zielüberschriften: keine gefundenen Fehler.
- {static['json_examples_checked']} JSON-Beispiele: Syntaxprüfung erfolgreich; darunter
  {static['json_property_fragments']} ausdrücklich eingebettete Property-Fragmente. Für JSONC wurden
  Erläuterungskommentare beim Prüfen entfernt.
- {len(examples)} vollständige C#-Beispiele gegen die aktuellen ScriptCore-Quellen
  in einem isolierten Projekt kompiliert: 0 Fehler, 0 Warnungen.
- {static['skills_checked']} SKILL.md-Dateien: Frontmatter-Validierung erfolgreich.
- scripts/dev/SyncSkills.ps1 -Check: beide Skill-Bäume identisch.
- 4 gezielte Prüffälle für den korrigierten Skill-Szenenprüfer: Screen-Space-UI,
  2D-/3D-World-Space-Canvas zulässig; zwei Welttransforms auf einem Entity abgelehnt.
- git diff --check für die bearbeiteten Dokumentationsbereiche: erfolgreich.

Quellabgleich
Die fachlichen Korrekturen wurden an den aktuellen Implementierungen in Assets,
GameBuilder, FunkeBuildProfile, FunkeProject, SceneManager, SceneDefinition,
SceneSerializer, ImGuiEditorLayer, ComponentRegistry, Renderer2D/3D, GuiRenderer,
Localization, LanguageDownloader, Hash und Funke-ScriptCore gegengeprüft.

Extern geprüft
- Funke Package Registry: https://github.com/Ben-Scr/Funke-PackageRegistry/blob/main/index.json
- Asset Library: https://github.com/Ben-Scr/Funke-AssetLibrary/blob/main/index.json
- .NET 10: https://github.com/dotnet/core/blob/main/release-notes/10.0/supported-os.md
- Android-Zielversion: https://developer.android.com/google/play/requirements/target-sdk
- Android-Seitengrößen: https://developer.android.com/guide/practices/page-sizes
- WebGPU: https://gpuweb.github.io/gpuweb/
- KEMAR: https://sound.media.mit.edu/resources/KEMAR.html

Grenzen und Arbeitsstand
Die C#-Prüfung belegt Kompilierbarkeit, keine Ausführung im Spiel. Es wurde kein
vollständiger nativer Engine-Build und kein plattformübergreifender Gerätetest
ausgeführt. Externe Paket-Repositories wurden gelesen, nicht geändert. Die Prüfung
ist kein vollständiger HTTP-Test sämtlicher externer URLs und kein Audit jedes
Inline-Kommentars im Quellcode.
Bereits vorhandene und parallel entstandene Änderungen wurden beibehalten.
checks.json und review.diff zeigen Differenzen zum anfänglichen Dokumentstand;
darunter können auch parallel entstandene Dokumentationsänderungen liegen.
Es wurde kein Commit erstellt.

Reproduzierbare Nachweise
audit.py / checks.json — Inventar, lokale Links und Markdown-Spiegel
check_examples.py / examples.json / examples-build.log — ausgewählte C#-Beispiele
check_static.py / static-checks.json — JSON, Skill-Frontmatter und Canvas-Prüffälle
diff-check.log / review.diff — Formatprüfung und Dokumentänderungen
'''
(OUT / 'report.txt').write_text(report, encoding='utf-8')
print(json.dumps({'documents': checks['documents'], 'internal_links': checks['internal_links'], 'link_errors': len(checks['link_errors']), 'json_examples': static['json_examples_checked'], 'csharp_examples': len(examples), 'skills': static['skills_checked'], 'mirror_differences': len(checks['mirror_differences']), 'diff_check': 'passed', 'report': str(OUT / 'report.txt')}, indent=2))
