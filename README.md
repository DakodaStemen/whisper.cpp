# whisper.cpp — dictation fork

This is a fork of [ggml-org/whisper.cpp](https://github.com/ggml-org/whisper.cpp) with a Linux desktop dictation system built on top of it. Press a hotkey to start recording, press it again to transcribe and type the result into whatever window is focused.

---

## What this fork adds

### Hotkey dictation (`whisper-toggle.sh`)

A toggle script that handles the full record-transcribe-type loop:

- First press: starts `arecord` capturing 16 kHz / 16-bit mono audio
- Second press: stops recording, runs `whisper-cli` on the capture, and types the transcript into the focused window via `wtype` (Wayland-native) with fallback to `wl-clipboard` + `xdotool`
- Transcripts are always saved to `$XDG_RUNTIME_DIR/whisper_last_transcript.txt` so nothing is lost if paste fails
- Press ESC on the HUD while recording to cancel without transcribing
- Set `WHISPER_MIC` env var to override the capture device (defaults to system default / PipeWire)
- A lock file prevents race conditions when the hotkey is pressed rapidly

### Floating HUD overlay (`whisper-dictation-hud`)

A minimal GTK3 window that sits at the bottom center of the screen and shows a reactive waveform:

- Green bars while recording — amplitude driven from the live audio file
- Blue animated bars while transcribing
- Runs under XWayland so window positioning works correctly on GNOME Wayland / GNOME 48
- Transparent glass-style background, no window chrome
- Closes cleanly on SIGTERM or ESC (ESC cancels the recording)

### One-shot setup (`scripts/setup-whisper-dictation.sh`)

Installs dependencies, builds `whisper-cli` and `whisper-dictation-hud`, downloads the `small.en` model, and creates a `.desktop` entry so the toggle can be bound to a keyboard shortcut.

---

## Quick setup

```bash
git clone https://github.com/DakodaStemen/whisper.cpp.git
cd whisper.cpp
bash scripts/setup-whisper-dictation.sh
```

The script handles everything. When it finishes, assign `whisper-toggle.sh` to a keyboard shortcut:

- **GNOME**: Settings -> Keyboard -> Custom Shortcuts -> Add
  Command: `/path/to/whisper-toggle.sh`
- **KDE**: System Settings -> Keyboard -> Custom Shortcuts -> Command/URL

Then focus any text field, press the shortcut to start recording, speak, and press it again to transcribe.

---

## Dependencies

The setup script installs these automatically on Debian/Ubuntu:

| Package | Purpose |
|---|---|
| `cmake`, `build-essential`, `pkg-config` | Build toolchain |
| `alsa-utils` | `arecord` for audio capture |
| `libgtkmm-3.0-dev` | HUD overlay (GTK3/C++) |
| `wl-clipboard` | Clipboard paste fallback on Wayland |
| `xdotool` | Keystroke simulation fallback |
| `wtype` (optional) | Preferred Wayland-native typing |

Install `wtype` separately if your distro packages it:
```bash
sudo apt install wtype   # or build from https://github.com/atx/wtype
```

---

## Manual build

If you want to build without the setup script:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DWHISPER_BUILD_DICTATION_HUD=ON
cmake --build build -j --config Release --target whisper-cli whisper-dictation-hud

# download the model manually
bash models/download-ggml-model.sh small.en
```

The `small.en` model gives the best balance of speed and accuracy for English dictation. Swap for `base.en` on slower machines or `medium.en` for better accuracy.

---

## Configuration

| Variable | Default | Description |
|---|---|---|
| `WHISPER_MIC` | *(system default)* | ALSA device string, e.g. `hw:1,0` |

Set it in your shell profile or prefix the hotkey command:

```bash
WHISPER_MIC=hw:1,0 /path/to/whisper-toggle.sh
```

---

## Upstream whisper.cpp

Everything below is from the original project. The upstream `whisper-cli` tool, all bindings, quantization, GPU support, and the full model API are unchanged.

---

## whisper.cpp

High-performance inference of [OpenAI's Whisper](https://github.com/openai/whisper) automatic speech recognition (ASR) model:

- Plain C/C++ implementation without dependencies
- Apple Silicon first-class citizen — optimized via ARM NEON, Accelerate framework, Metal and Core ML
- AVX intrinsics support for x86 architectures
- Mixed F16 / F32 precision
- Integer quantization support
- Zero memory allocations at runtime
- Vulkan GPU support
- NVIDIA GPU support via CUDA
- OpenVINO support
- C-style API

Supported platforms: macOS, iOS, Android, Linux, FreeBSD, WebAssembly, Windows, Raspberry Pi, Docker.

The entire high-level implementation is in [whisper.h](include/whisper.h) and [whisper.cpp](src/whisper.cpp). The rest is part of the [ggml](https://github.com/ggml-org/ggml) machine learning library.

### Basic usage (upstream)

```bash
# build
cmake -B build
cmake --build build -j --config Release

# download a model
bash models/download-ggml-model.sh base.en

# transcribe
./build/bin/whisper-cli -f samples/jfk.wav
```

Input must be 16-bit WAV at 16 kHz mono. Convert with ffmpeg:

```bash
ffmpeg -i input.mp3 -ar 16000 -ac 1 -c:a pcm_s16le output.wav
```

For full upstream documentation, examples, bindings, and GPU setup see the [original repo](https://github.com/ggml-org/whisper.cpp).
