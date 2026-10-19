#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="BasiliyWolf/GUI-OpenVPN"
command -v git >/dev/null || { echo "git is required" >&2; exit 1; }
command -v gh >/dev/null || { echo "GitHub CLI (gh) is required" >&2; exit 1; }
gh auth status >/dev/null || { echo "Run gh auth login first" >&2; exit 1; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
gh repo clone "$REPO" "$TMP/repo"
mkdir -p "$TMP/repo"
tar -C "$ROOT" --exclude='./.git' --exclude='./*.deb' -cf - . | tar -C "$TMP/repo" -xf -
cd "$TMP/repo"
git add -A
if ! git diff --cached --quiet; then
    git commit -m "Release v1.0.1: source, GPLv3, docs and screenshots"
    git push origin HEAD:main
fi
DEB="$ROOT/gui-openvpn-basiliywolf-1.0.1-Linux.deb"
if [[ ! -f "$DEB" ]]; then
    echo "DEB missing: $DEB" >&2
    exit 1
fi
if gh release view v1.0.1 --repo "$REPO" >/dev/null 2>&1; then
    gh release upload v1.0.1 "$DEB" --repo "$REPO" --clobber
else
    gh release create v1.0.1 "$DEB" --repo "$REPO" --target main --title "GUI OpenVPN v1.0.1" --notes-file "$ROOT/docs/publication/GITHUB_RELEASE.en.md"
fi
echo "Published: https://github.com/$REPO"
echo "Release: https://github.com/$REPO/releases/tag/v1.0.1"
