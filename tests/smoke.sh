#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash -n helper/bw-netns install-deps.sh build.sh tests/source_regression.sh
./tests/source_regression.sh
./tests/localization.sh
./tests/layout_language.sh
command -v cmake >/dev/null
if command -v shellcheck >/dev/null; then shellcheck -S warning helper/bw-netns || true; fi
if pkg-config --exists Qt6Widgets Qt6Network Qt6Svg; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --parallel 2
  echo 'PASS: C++ Qt build'
else
  echo 'SKIP: Qt6 development modules missing (install libqt6svg6-dev qt6-base-dev)' >&2
fi
printf 'PASS: Shell syntax and packaging checks\n'
