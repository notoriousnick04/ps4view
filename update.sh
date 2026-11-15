#!/usr/bin/env bash
# Update ps4view: pull latest changes (if in a git repo), rebuild, and reinstall.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

PREFIX="${PREFIX:-$HOME/.local}"

if [ -d .git ]; then
    echo "Fetching latest changes..."
    git pull --ff-only
fi

make check-deps
make
make install PREFIX="$PREFIX"

echo
echo "Updated ps4view ($(./ps4view --version 2>/dev/null || echo unknown))"
