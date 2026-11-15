#!/usr/bin/env bash
# Install or update ps4view (no sudo needed: installs to ~/.local by default).
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

PREFIX="${PREFIX:-$HOME/.local}"

if [ -d .git ]; then
    echo "Checking for updates..."
    before="$(git rev-parse HEAD 2>/dev/null || echo)"
    git pull --ff-only --quiet
    after="$(git rev-parse HEAD 2>/dev/null || echo)"
    if [ -n "$before" ] && [ "$before" = "$after" ] && [ -x "$PREFIX/bin/ps4view" ]; then
        echo "Already up to date ($(./ps4view --version 2>/dev/null || echo unknown))."
        exit 0
    fi
    if [ -n "$before" ] && [ "$before" != "$after" ]; then
        echo "Updated to $(git rev-parse --short HEAD). Rebuilding..."
    fi
fi

make check-deps
make
make install PREFIX="$PREFIX"

echo
echo "Installed ps4view ($(./ps4view --version 2>/dev/null || echo unknown)) to $PREFIX/bin"
echo "Run: ps4view"
