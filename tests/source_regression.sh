#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if grep -Eq 'logs->|QTextEdit \*logs' src/main.cpp; then
  echo 'FAIL: old null log widget reference remains' >&2
  exit 1
fi
if ! grep -q 'eventLog.write' src/main.cpp; then
  echo 'FAIL: persistent event logging missing' >&2
  exit 1
fi
echo 'PASS: no dangling log widget references'
