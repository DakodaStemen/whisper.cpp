#!/usr/bin/env bash
# One-shot setup: deps, whisper-cli build, small.en model, desktop entry for hotkeys.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOGGLE="$REPO_ROOT/whisper-toggle.sh"
DESKTOP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
DESKTOP_FILE="$DESKTOP_DIR/whisper-dictation.desktop"

die() { echo "error: $*" >&2; exit 1; }

need_cmd() { command -v "$1" >/dev/null 2>&1; }

echo "Repo: $REPO_ROOT"

chmod +x "$TOGGLE" 2>/dev/null || true

# Collect all missing packages in one pass, then install once
PKGS=()

if ! need_cmd cmake || ! need_cmd g++ || ! need_cmd pkg-config; then
  PKGS+=(cmake build-essential pkg-config)
fi

if ! need_cmd arecord; then
  PKGS+=(alsa-utils)
fi

# xdotool for keystroke simulation, wl-clipboard for Wayland clipboard access
if ! need_cmd xdotool; then
  PKGS+=(xdotool)
fi
if ! need_cmd wl-copy; then
  PKGS+=(wl-clipboard)
fi

# Dictation HUD (C++ / gtkmm3), same stack family as GNOME Gtk3.
if ! pkg-config --exists gtkmm-3.0 2>/dev/null; then
  PKGS+=(libgtkmm-3.0-dev)
fi

if [ ${#PKGS[@]} -gt 0 ]; then
  echo "Installing missing packages: ${PKGS[*]}"
  sudo apt-get update -qq
  sudo apt-get install -y "${PKGS[@]}"
fi

WHISPER_CLI="$REPO_ROOT/build/bin/whisper-cli"
HUD="$REPO_ROOT/build/bin/whisper-dictation-hud"
if [ ! -x "$WHISPER_CLI" ] || [ ! -x "$HUD" ]; then
  echo "Configuring and building whisper-cli + whisper-dictation-hud..."
  cmake -B "$REPO_ROOT/build" -DCMAKE_BUILD_TYPE=Release -DWHISPER_BUILD_DICTATION_HUD=ON
  cmake --build "$REPO_ROOT/build" -j --config Release --target whisper-cli whisper-dictation-hud
fi

MODEL="$REPO_ROOT/models/ggml-small.en.bin"
if [ ! -f "$MODEL" ]; then
  echo "Downloading ggml-small.en model..."
  (cd "$REPO_ROOT" && bash ./models/download-ggml-model.sh small.en)
fi

mkdir -p "$DESKTOP_DIR"
cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Whisper dictation
Comment=Toggle recording; second run transcribes and types text (whisper.cpp)
Exec=$TOGGLE
Icon=audio-input-microphone
Terminal=false
Categories=AudioVideo;Audio;
EOF

if need_cmd update-desktop-database; then
  update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
fi

echo ""
echo "Setup finished."
echo "  Toggle script: $TOGGLE"
echo "  Desktop entry: $DESKTOP_FILE"
echo ""
echo "Assign a keyboard shortcut:"
echo "  GNOME: Settings → Keyboard → Keyboard Shortcuts → Custom Shortcuts → Add"
echo "         Command: $TOGGLE"
echo "  KDE:   System Settings → Keyboard → Custom Shortcuts → Add → Command/URL"
echo ""
echo "Usage: focus the text field, press shortcut to start speaking, press again to transcribe."
