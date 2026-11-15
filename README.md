# ps4view - low-latency PS4 capture-card viewer

Captures from a V4L2 device (auto-scans by default) and renders with SDL2.
YUYV is rendered with zero CPU conversion (SDL YUY2 texture). MJPG is
decoded with turbojpeg. A 2-buffer queue plus frame-dropping keeps the
display current.

## Dependencies

- SDL2
- FreeType2
- libturbojpeg
- V4L2 kernel headers (linux-headers / linux-libc-dev)

Check with `make check-deps`. Install:

- Arch: `sudo pacman -S sdl2 freetype2 libjpeg-turbo linux-headers`
- Debian/Ubuntu: `sudo apt install libsdl2-dev libfreetype-dev libturbojpeg0-dev linux-libc-dev`
- Fedora: `sudo dnf install SDL2-devel freetype-devel turbojpeg-devel kernel-headers`

## Build


    cd ~/ps4view
    make

## Install (optional)

    make install PREFIX=$HOME/.local   # no sudo needed
    # then run from anywhere:
    ps4view

    make uninstall PREFIX=$HOME/.local

## Run

Inside your desktop session (Wayland) - compositor adds ~1 frame:

    ./ps4view --stats

On a TTY (Ctrl+Alt+F3) for direct scanout (lowest latency):

    SDL_VIDEODRIVER=kmsdrm ./ps4view --stats

Or use the helper:

    make run   # builds + runs under kmsdrm

## Options

    --device PATH     V4L2 device (default auto-scan)
    --list            list capture devices
    --res WxH         resolution (default 1920x1080)
    --fps N           framerate (default 60)
    --format FMT      yuyv (default) or mjpg
    --vsync           enable vsync (default off)
    --windowed        windowed mode (default fullscreen)
    --stats           print fps/drop stats
    --overlay         show on-screen stats (toggle: Shift+D)

Keys: q / Esc quit.

## Latency tuning checklist

1. Disable PS4 HDCP: Settings -> System -> uncheck Enable HDCP. Set resolution 1080p.
2. sudo cpupower frequency-set -g performance
3. echo -1 | sudo tee /sys/bus/usb/devices/2-1/power/autosuspend_delay_ms
4. Run on a TTY with SDL_VIDEODRIVER=kmsdrm (bypasses compositor).
5. Cursor auto-hides after 3s of inactivity (configurable: --cursor-hide-ms N).
5. Prefer YUYV (default) over MJPG.

## On-screen stats

Run with `--overlay` to show FPS/drops/resolution in the top-left corner, or
toggle at any time with **Shift+D**. `--glossary` (toggle **Shift+S**) shows
the keybind list. Requires a system font (DejaVu/Adwaita mono) for rendering.

## Audio

Audio is off by default. Enable with `--audio` or toggle with Shift+A.
Requires an SDL2-supported audio output.

## Troubleshooting

- **Black screen**: disable HDCP on the PS4 (Settings -> System -> uncheck Enable HDCP) and set resolution to 1080p.
- **kmsdrm not available**: run on a virtual TTY (Ctrl+Alt+F3), not inside the desktop session.
- **Device or resource busy**: another process holds the card. Check with `fuser /dev/video2` and stop it.
- **Missing font**: install a mono font (DejaVu Sans Mono or Adwaita Mono) for the overlays.
- **No audio**: pass `--audio` or press Shift+A; ensure the capture card exposes an audio device.

## Reality check

USB capture adds ~1-2 frames (16-33 ms) of inherent latency. App-side is <1
frame. Best achievable is typically 40-70 ms total - not identical to a
monitor, but close.
