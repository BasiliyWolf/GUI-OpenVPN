#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 - <<'PY2'
import json,re,base64,pathlib
s=pathlib.Path('src/main.cpp').read_text()
assert 'uiLanguageTimer' not in s, 'Periodic UI translations must be removed'
assert 'QEvent::Show' in s and 'translateVisible' in s
m=re.search(r'QByteArray::fromBase64\("([^"]+)"\)',s)
assert m
source=json.loads(base64.b64decode(m.group(1)))
external=json.loads(pathlib.Path('translations/ru_EN.json').read_text())
assert source==external
assert len(source)>=100, f'Only {len(source)} translations'
print(f'PASS: {len(source)} translation entries and event-based UI localization')
PY2
