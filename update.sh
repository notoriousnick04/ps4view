#!/usr/bin/env bash
# Update ps4view: pull latest changes (if in a git repo), rebuild, and reinstall.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

PREFIX="${PREFIX:-$HOME/.local}"

if [ -d .git ]; then
    echo "Checking for updates..."
    before="$(git rev-parse HEAD 2>/dev/null || echo)"
    git pull --ff-only --quiet
    after="$(git rev-parse HEAD 2>/dev/null || echo)"
    if [ -n "$before" ] && [ "$before" = "$after" ]; then
        echo "Already up to date ($(./ps4view --version 2>/dev/null || echo unknown))."
        exit 0
    fi
    echo "Updated to $(git rev-parse --short HEAD). Rebuilding..."
fi

make check-deps
make
make install PREFIX="$PREFIX"

echo
echo "Updated ps4view ($(./ps4view --version 2>/dev/null || echo unknown))"
