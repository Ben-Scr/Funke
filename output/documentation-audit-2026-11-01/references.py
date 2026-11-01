from pathlib import Path
import json
import re
ROOT = Path(__file__).resolve().parents[2]
docs = json.loads((Path(__file__).parent / 'checks.json').read_text(encoding='utf-8'))['inventory']
prefixes = ('Funke-', 'scripts/', 'premake/', 'cmake/', 'Tests/', 'Github-Docs/', 'templates/', 'platforms/', 'installer/')
errors = []
for rel in docs:
    if rel.startswith('.claude/') or rel == 'CHANGELOG.md':
        continue
    text = (ROOT / rel).read_text(encoding='utf-8-sig')
    for token in re.findall(r'`([^`\n]+)`', text):
        token = token.replace('\\', '/').rstrip('.,;:')
        if not token.startswith(prefixes) or '/' not in token:
            continue
        if any(c in token for c in (' ', '*', '<', '>', '{', '}', '|', '(', ')', '=', '→', '…')):
            continue
        if not (ROOT / token).exists():
            errors.append({'file': rel, 'path': token})
print(json.dumps(errors, indent=2, ensure_ascii=True))
