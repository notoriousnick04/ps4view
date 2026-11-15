#!/usr/bin/env bash
# Remove ps4view and all of its traces.
# Usage: ./uninstall.sh [--purge]
#   --purge  also delete the source directory (asks for confirmation)
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"

for dir in "$PREFIX/bin" /usr/local/bin /usr/bin; do
    rm -f "$dir/ps4view" "$dir/ps4view-run" 2>/dev/null || true
done

rm -rf "$HOME/.config/ps4view"

hash -r 2>/dev/null || true

echo "Removed installed binaries and configuration."

if [ "${1:-}" = "--purge" ]; then
    src="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    printf 'Delete source directory %s? [y/N] ' "$src"
    read -r reply || reply=""
    case "$reply" in
        y|Y|yes|YES)
            cd -- "$src/.."
            rm -rf -- "$src"
            echo "Source directory removed."
            ;;
        *) echo "Source directory kept." ;;
    esac
fi
