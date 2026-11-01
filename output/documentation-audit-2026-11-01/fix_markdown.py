from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / 'Github-Docs/Documentation/Scripting/Scripting.md'
text = path.read_bytes().decode('utf-8')
for before, after in [('\\---', '---'), ('\\*', '*'), ('\\[', '['), ('\\&', '&')]:
    text = text.replace(before, after)
path.write_bytes(text.encode('utf-8'))

path = root / 'installer/README.md'
text = path.read_bytes().decode('utf-8')
lines = text.splitlines(keepends=True)
seen = set()
result = []
for line in lines:
    if line.startswith(('The default installer contains binaries, assets, templates, package tools', '`scripts/BuildWindows.ps1 -PublishBundle` and `scripts/PackagePosix.py`')):
        if line in seen:
            continue
        seen.add(line)
    result.append(line)
text = ''.join(result)
while '\r\n\r\n\r\n' in text:
    text = text.replace('\r\n\r\n\r\n', '\r\n\r\n')
path.write_bytes(text.encode('utf-8'))
