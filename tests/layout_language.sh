#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
grep -q 'summaryLayout->addWidget(ipRefresh)' src/main.cpp
grep -q 'base->addWidget(journalButton)' src/main.cpp
grep -q 'language=="en" ? "Status: 🟢 Connected"' src/main.cpp
grep -q 'language=="en" ? "Status: 🔴 Disconnected"' src/main.cpp
printf 'PASS: Full-width journal and locale-aware dynamic VPN status\n'
