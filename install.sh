#!/usr/bin/env bash
# Install ps4view (no sudo needed: installs to ~/.local by default).
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

PREFIX="${PREFIX:-$HOME/.local}"

make check-deps
make
make install PREFIX="$PREFIX"

echo
echo "Installed:"
echo "  $PREFIX/bin/ps4view"
echo "  $PREFIX/bin/ps4view-run"
echo
echo "Make sure $PREFIX/bin is on your PATH, then run: ps4view"
