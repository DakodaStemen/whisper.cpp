# whisper.cpp — dictation fork

A fork of [ggml-org/whisper.cpp](https://github.com/ggml-org/whisper.cpp) with a Linux desktop hotkey dictation system built on top. Press a keyboard shortcut to start recording, speak, press it again — the transcript is typed into whatever window is focused.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

---

## Table of Contents

- [What This Fork Adds](#what-this-fork-adds)
- [Architecture](#architecture)
- [Quick Setup](#quick-setup)
- [Manual Build](#manual-build)
- [Usage](#usage)
- [Configuration](#configuration)
- [Dictation HUD Details](#dictation-hud-details)
- [Transcript Storage](#transcript-storage)
- [Wayland Compatibility Notes](#wayland-compatibility-notes)
- [Dependencies](#dependencies)
- [Upstream](#upstream)
- [License](#license)

---

## What This Fork Adds

Three new components on top of the upstream whisper.cpp codebase:

| Component | Path | Description |
|-----------|------|-------------|
| Toggle script | `whisper-toggle.sh` | Hotkey-driven record → transcribe → type loop |
| Dictation HUD | `examples/dictation-hud/` | GTK3 floating overlay showing recording state and live waveform |
| Setup script | `scripts/setup-whisper-dictation.sh` | One-shot dependency install, build, model download, desktop entry creation |

Everything else in this repo is unmodified upstream whisper.cpp.

---

## Architecture

```
hotkey press
    │
    ▼
whisper-toggle.sh
    ├─ first press → arecord (16kHz/16-bit mono WAV) + launch HUD (green)
    └─ second press → stop recording → whisper-cli → HUD (blue, transcribing)
                                              │
                                              ▼
                               wtype (Wayland) / wl-clipboard + xdotool (fallback)
                                              │
                                              ▼
                               transcript typed into focused window
                               + saved to $XDG_RUNTIME_DIR/whisper_last_transcript.txt
```

### whisper-toggle.sh

The toggle script manages the full lifecycle with a lock file at `$XDG_RUNTIME_DIR/whisper.pid`:

- **First press (no lock file):** Writes the PID lock file, launches `arecord` capturing audio at 16 kHz / 16-bit mono to a temp WAV file, and starts `whisper-dictation-hud listen <audio-file>` in the background.
- **Second press (lock file exists):** Sends SIGTERM to `arecord` to stop recording cleanly, sends SIGTERM to the HUD (which transitions to busy/blue state), runs `whisper-cli` on the captured WAV, then types the resulting transcript into the focused window.
- **ESC while recording:** The HUD catches the ESC keypress, sends SIGTERM to the toggle script, and cancels the recording without transcribing.

The lock file prevents race conditions if the hotkey is pressed multiple times in rapid succession.

### Audio capture

`arecord` captures from the system default audio device (PipeWire presents itself as an ALSA device on modern systems). The capture format is fixed at 16 kHz / 16-bit mono because that is what whisper.cpp expects. The `WHISPER_MIC` environment variable overrides the ALSA device string if the default is not the right device.

### Transcript paste

After transcription, the transcript is typed into the focused window. Two methods are tried in order:

1. **`wtype` (preferred):** Wayland-native keystroke injection. Types the transcript character by character directly into the Wayland compositor's input stream without relying on clipboard state.
2. **`wl-clipboard` + `xdotool` (fallback):** Copies the transcript to the clipboard, then sends `Ctrl+Shift+V` via `xdotool` to paste. Less reliable on pure Wayland but works in most applications.

---

## Quick Setup

```bash
git clone https://github.com/DakodaStemen/whisper.cpp.git
cd whisper.cpp
bash scripts/setup-whisper-dictation.sh
```

The setup script:
1. Installs missing packages (cmake, build-essential, alsa-utils, libgtkmm-3.0-dev, wl-clipboard, xdotool)
2. Runs `cmake` and builds `whisper-cli` and `whisper-dictation-hud`
3. Downloads the `small.en` model (~466 MB)
4. Creates a `.desktop` entry in `~/.local/share/applications/`

When it finishes, assign `whisper-toggle.sh` to a keyboard shortcut in your desktop environment:

**GNOME:**
Settings → Keyboard → Custom Shortcuts → Add
- Name: `Whisper Dictation`
- Command: `/full/path/to/whisper-toggle.sh`
- Shortcut: your preferred key

**KDE:**
System Settings → Shortcuts → Custom Shortcuts → New → Command/URL
- Command: `/full/path/to/whisper-toggle.sh`

---

## Manual Build

If you want to build without the setup script:

```bash
# Build whisper-cli and the dictation HUD
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DWHISPER_BUILD_DICTATION_HUD=ON

cmake --build build -j \
  --config Release \
  --target whisper-cli whisper-dictation-hud

# Download the model
bash models/download-ggml-model.sh small.en
```

`WHISPER_BUILD_DICTATION_HUD=ON` enables the HUD build. The HUD requires `libgtkmm-3.0-dev`. If the library is not found, CMake skips the target with a status message rather than failing the whole build.

---

## Usage

Once the setup is complete and a keyboard shortcut is configured:

1. Focus the text field you want to type into (a browser address bar, terminal, document editor, etc.)
2. Press the shortcut — the HUD appears at the bottom of the screen with a green waveform, indicating recording is active
3. Speak
4. Press the shortcut again — the HUD turns blue (transcribing), then closes; the transcript is typed into the focused window

**Cancel:** Press ESC while the HUD is visible to cancel the recording without transcribing.

### Model selection

The setup script downloads `small.en` by default. To use a different model, download it and update the model path in `whisper-toggle.sh`:

| Model | Size | Notes |
|-------|------|-------|
| `tiny.en` | ~75 MB | Fastest; accuracy is noticeably lower |
| `base.en` | ~142 MB | Good for slower machines |
| `small.en` | ~466 MB | Best balance of speed and accuracy (default) |
| `medium.en` | ~1.5 GB | Higher accuracy, slower (~2–5s on CPU) |

---

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `WHISPER_MIC` | *(system default)* | ALSA device string, e.g. `hw:1,0`, `plughw:0,0` |

Set in your shell profile or prefix the hotkey command:

```bash
WHISPER_MIC=hw:1,0 /path/to/whisper-toggle.sh
```

To find the right device string:
```bash
arecord -l   # list capture devices
```

---

## Dictation HUD Details

The HUD is a small GTK3 window (`examples/dictation-hud/dictation-hud.cpp`) that displays recording state visually.

**Visual design:**
- Semi-transparent dark glass background (45% opacity, RGBA visual)
- No window title bar or frame
- Positioned at the bottom center of the primary monitor
- The window is a `WINDOW_POPUP` type, which bypasses the window manager entirely (required for GNOME 48, which suppresses `WINDOW_TYPE_HINT_NOTIFICATION` windows)
- Runs under XWayland (`GDK_BACKEND=x11`) so that `window.move()` for bottom-center positioning works reliably on GNOME Wayland

**Waveform:**
- Listen mode (green bars): Reads the last ~50ms of audio samples from the live WAV file being written by `arecord`. Computes amplitude over bins and renders vertical bars. The waveform is updated on a 60ms timer.
- Transcribing mode (blue animated bars): Static animation indicating processing in progress.

**Exit codes:**
- `0` — normal close (SIGTERM received when recording stopped)
- `2` — user cancelled (ESC keypress)

The toggle script checks the HUD exit code to determine whether to proceed with transcription.

---

## Transcript Storage

Every transcription is saved to:

```
$XDG_RUNTIME_DIR/whisper_last_transcript.txt
```

This ensures transcripts are never lost even if the paste step fails. `$XDG_RUNTIME_DIR` is typically `/run/user/$(id -u)` on systemd-based systems and is cleaned up on logout.

---

## Wayland Compatibility Notes

| Scenario | Status |
|----------|--------|
| GNOME Wayland (GNOME < 48) | Works via XWayland for HUD positioning; `wtype` for paste |
| GNOME Wayland (GNOME 48+) | Works; WINDOW_POPUP bypasses notification suppression |
| KDE Plasma Wayland | Works; `wtype` handles paste |
| X11 desktop | Works; `xdotool` handles paste directly |

The HUD runs under XWayland even on Wayland desktops. This is intentional: native Wayland windows cannot reposition themselves freely (security model forbids it), so `window.move()` does not work for bottom-center placement without XWayland.

---

## Dependencies

The setup script installs these automatically on Debian/Ubuntu systems:

| Package | Purpose |
|---------|---------|
| `cmake`, `build-essential`, `pkg-config` | C/C++ build toolchain |
| `alsa-utils` | `arecord` audio capture |
| `libgtkmm-3.0-dev` | GTK3/C++ HUD overlay |
| `wl-clipboard` | Clipboard paste fallback on Wayland |
| `xdotool` | Keystroke simulation fallback |
| `wtype` | Preferred Wayland-native typing (install separately if not packaged) |

Install `wtype` from source or via your distro if available:

```bash
# Debian/Ubuntu (may not be in older releases)
sudo apt install wtype

# Build from source
git clone https://github.com/atx/wtype
cd wtype && cmake -B build && cmake --build build && sudo install build/wtype /usr/local/bin/
```

---

## Upstream

This fork tracks [ggml-org/whisper.cpp](https://github.com/ggml-org/whisper.cpp). All whisper.cpp core functionality (inference, model loading, server, CLI, bindings) is unmodified. Only the following files were added by this fork:

```
whisper-toggle.sh                         hotkey toggle script
scripts/setup-whisper-dictation.sh        one-shot setup
examples/dictation-hud/
├── dictation-hud.cpp                     HUD implementation
└── CMakeLists.txt                        HUD build rules
```

---

## License

MIT License — see [LICENSE](LICENSE).

whisper.cpp and all upstream code: Copyright (c) 2022 Georgi Gerganov and contributors.
Dictation fork additions: Copyright (c) 2026 Dakoda Stemen.
