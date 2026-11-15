#!/usr/bin/env bash
# Launcher for ps4view (portable: run from anywhere).
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"
if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${DISPLAY:-}" ] && [ -t 0 ]; then
    export SDL_VIDEODRIVER=kmsdrm
    echo "ps4view-run: using KMS/DRM direct scanout"
else
    unset SDL_VIDEODRIVER
    echo "ps4view-run: using desktop (Wayland/X11) path"
fi
exec ./ps4view "$@"
