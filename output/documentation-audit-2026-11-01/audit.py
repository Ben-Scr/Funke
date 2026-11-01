from pathlib import Path
from urllib.parse import unquote, urlsplit
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
paths = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '*.md', '*.mdx', '*.rst', '*.adoc'], cwd=ROOT, text=True).splitlines()
paths.extend(['Github-Docs/Preview/INFO.txt', 'installer/macos/Read Me.txt'])
docs = sorted({p for p in paths if not p.startswith(('External/', 'vendor/', 'output/', 'bin/', 'bin-int/'))})
snapshot = OUT / 'before.json'
if not snapshot.exists():
    snapshot.write_text(json.dumps({p: (ROOT / p).read_text(encoding='utf-8-sig') for p in docs}, ensure_ascii=False, indent=2), encoding='utf-8')

def headings(path):
    counts = {}
    result = set()
    for title in re.findall(r'^#{1,6}\s+(.+?)\s*#*$', path.read_text(encoding='utf-8-sig'), re.M):
        title = re.sub(r'<[^>]+>', '', title).strip().lower()
        slug = re.sub(r'[^\w\- ]', '', title).replace(' ', '-')
        count = counts.get(slug, 0)
        counts[slug] = count + 1
        result.add(slug + (f'-{count}' if count else ''))
    return result

errors = []
links = 0
for rel in docs:
    path = ROOT / rel
    body = path.read_text(encoding='utf-8-sig')
    body = re.sub(r'^(`{3,}|~{3,}).*?^\1\s*$', '', body, flags=re.M | re.S)
    targets = re.findall(r'!?\[[^\]\n]*\]\(([^\s)]+)(?:\s+"[^"]*")?\)', body)
    targets += re.findall(r'(?:src|href)="([^"]+)"', body)
    for target in targets:
        if re.match(r'[a-zA-Z][a-zA-Z0-9+.-]*:', target) or target.startswith('//'):
            continue
        links += 1
        parts = urlsplit(target.strip('<>'))
        dest = path.parent / unquote(parts.path) if parts.path else path
        if not dest.exists():
            errors.append({'file': rel, 'target': target, 'error': 'missing file'})
        elif parts.fragment and dest.suffix == '.md' and unquote(parts.fragment) not in headings(dest):
            errors.append({'file': rel, 'target': target, 'error': 'missing heading'})

mirrors = []
for rel in docs:
    if rel.startswith('.agents/skills/'):
        other = ROOT / rel.replace('.agents/', '.claude/', 1)
        if not other.exists() or other.read_bytes() != (ROOT / rel).read_bytes():
            mirrors.append(rel)
baseline = json.loads(snapshot.read_text(encoding='utf-8'))
changed = [p for p in docs if p in baseline and (ROOT / p).read_text(encoding='utf-8-sig') != baseline[p]]
added = [p for p in docs if p not in baseline]
report = {'documents': len(docs), 'internal_links': links, 'link_errors': errors, 'mirror_differences': mirrors, 'changed_since_start': changed, 'newly_covered': added, 'inventory': docs}
(OUT / 'checks.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k != 'inventory'}, ensure_ascii=True, indent=2))
