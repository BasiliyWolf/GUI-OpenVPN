#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
printf 'Run ./build/gui-openvpn-basiliywolf\n'
