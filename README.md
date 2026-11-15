# ps4view

**Low-latency PS4 capture-card viewer for Linux.**

`ps4view` captures from a V4L2 device and renders with SDL2, prioritizing minimal latency: small buffer queues, frame dropping to stay current, zero-conversion YUYV, and optional KMS/DRM direct scanout on a TTY.

---

## Features

- **Auto-detect capture device** — scans `/dev/video*` and selects the best capture source automatically.
- **Low-latency pipeline** — 2-buffer mmap queue that always displays the newest frame and drops stale ones.
- **Efficient rendering** — YUYV path uses an SDL `YUY2` texture with no CPU conversion; MJPG is decoded via turbojpeg.
- **Direct scanout support** — uses KMS/DRM on a TTY for compositor-bypass rendering, with automatic fallback in desktop sessions.
- **On-screen UI** — stats overlay, keybind glossary, and toast notifications.
- **Audio support** — optional capture/playback with configurable software gain (off by default).
- **Persistent settings** — preferences are saved to `~/.config/ps4view/config` and restored on launch.
- **No sudo required** — runs as a regular user (when device permissions and session allow).

## Requirements

- SDL2
- FreeType2
- libturbojpeg
- V4L2 kernel headers

Check dependencies:

```sh
make check-deps
```

Install packages:

| Distro | Command |
| --- | --- |
| Arch Linux | `sudo pacman -S sdl2 freetype2 libjpeg-turbo linux-headers` |
| Debian / Ubuntu | `sudo apt install libsdl2-dev libfreetype-dev libturbojpeg0-dev linux-libc-dev` |
| Fedora | `sudo dnf install SDL2-devel freetype-devel turbojpeg-devel kernel-headers` |

## Quick start

```sh
git clone https://github.com/notoriousnick04/ps4view.git
cd ps4view
./install.sh
ps4view
```

`./install.sh` checks dependencies, builds, and installs to `~/.local/bin` by default. Make sure `~/.local/bin` is on your `PATH`.

To update an existing clone, run `./install.sh` again — it pulls the latest changes (if any), rebuilds, and reinstalls.

## Usage

```sh
ps4view [options]
```

| Option | Description |
| --- | --- |
| `--device PATH` | V4L2 device (default: auto-scan) |
| `--list` | List capture devices and exit |
| `--res WxH` | Resolution (default: `1920x1080`) |
| `--fps N` | Framerate (default: `60`) |
| `--format FMT` | `yuyv` (default) or `mjpg` |
| `--vsync` | Enable vsync (on by default) |
| `--windowed` | Windowed mode (default: fullscreen) |
| `--stats` | Print fps/drop stats to stderr |
| `--overlay` | Show on-screen stats overlay (toggle: Shift+D) |
| `--glossary` | Show keybind glossary (toggle: Shift+S) |
| `--glossary-timeout-ms N` | Auto-hide glossary after N ms (default: `0`, never) |
| `--no-glossary-flash` | Don't flash glossary on startup |
| `--toast-ms N` | Toast duration in ms (default: `3000`) |
| `--no-toast` | Disable toast notifications |
| `--cursor-hide-ms N` | Hide cursor after N ms idle (default: `3000`) |
| `--no-cursor-hide` | Never hide the cursor |
| `--audio` | Enable audio capture/playback at startup |
| `--audio-gain FLOAT` | Software gain for audio (default: `4.0`) |
| `--no-audio` | Disable audio capture/playback |
| `--no-config` | Disable loading/saving settings |
| `--version` | Print version |
| `--help` | Show help |

## Keybinds

| Key | Action |
| --- | --- |
| `Shift+S` | Toggle glossary |
| `Shift+D` | Toggle stats overlay |
| `Shift+V` | Toggle vsync |
| `Shift+A` | Toggle audio |
| `Shift+R` | Reset settings (press twice within 3s to confirm) |
| `q` / `Esc` | Quit |
| Mouse move | Show cursor (auto-hides after inactivity) |

## Settings

Settings are stored in `~/.config/ps4view/config` and saved immediately when toggled.

**Defaults (first run / after reset):**
- Vsync: on
- Audio: off
- Stats overlay: off
- Glossary: off
- Cursor auto-hide: on (3s)
- Toast notifications: on (3s)
- Audio gain: 4.0

Use `--no-config` to run without loading or saving settings.

## Building & installation

```sh
make             # build
make check-deps  # verify dependencies
make install PREFIX=$HOME/.local
make uninstall PREFIX=$HOME/.local
```

Helper scripts:

- `./install.sh` — install or update (builds and installs to `~/.local` by default)
- `./uninstall.sh` — remove installed binaries and configuration
- `./uninstall.sh --purge` — also delete the source directory (with confirmation)
- `./run.sh` — launch with automatic TTY (KMS/DRM) / desktop selection

## Troubleshooting

- **Black screen**: Disable HDCP on the PS4 (Settings → System → uncheck *Enable HDCP*) and set resolution to 1080p.
- **`kmsdrm not available`**: Run from a virtual TTY (e.g. Ctrl+Alt+F3), not inside a desktop session.
- **Device or resource busy**: Another process is using the capture device. Check with `fuser /dev/video2` and stop it, or choose a different device with `--device`.
- **No audio**: Start with `--audio` or press `Shift+A`. Ensure the capture card exposes an audio device.
- **Missing overlay text**: Install a monospace font (e.g. DejaVu Sans Mono or Adwaita Mono).

## License

[MIT](LICENSE)
