#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Use XDG_RUNTIME_DIR (per-user, permission-restricted) instead of /tmp
TMPDIR="${XDG_RUNTIME_DIR:-/tmp}"
PID_FILE="$TMPDIR/whisper_rec.pid"
AUDIO_FILE="$TMPDIR/whisper_dictation.wav"
OVERLAY_LISTEN_PID="$TMPDIR/whisper_overlay_listen.pid"
OVERLAY_BUSY_PID="$TMPDIR/whisper_overlay_busy.pid"
LOCK_FILE="$TMPDIR/whisper_toggle.lock"

OVERLAY_BIN="$SCRIPT_DIR/build/bin/whisper-dictation-hud"
WHISPER_CLI="$SCRIPT_DIR/build/bin/whisper-cli"
MODEL="$SCRIPT_DIR/models/ggml-small.en.bin"

die() { echo "whisper-toggle: $*" >&2; exit 1; }

# Acquire an exclusive lock so rapid hotkey presses don't race.
# Release it before backgrounding anything so the next invocation can acquire it.
exec 9>"$LOCK_FILE"
flock -n 9 || exit 0

release_lock() { exec 9>&-; }

# Kill a process whose PID is stored in a file, with validation
kill_pid_file() {
    local pidfile="$1"
    [ -f "$pidfile" ] || return 0
    local pid
    pid="$(cat "$pidfile" 2>/dev/null || true)"
    rm -f "$pidfile"
    [ -n "${pid:-}" ] || return 0
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
    fi
}

overlay_kill_listen() { kill_pid_file "$OVERLAY_LISTEN_PID"; }
overlay_kill_busy()   { kill_pid_file "$OVERLAY_BUSY_PID"; }

overlay_listen_start() {
    overlay_kill_listen
    [ -x "$OVERLAY_BIN" ] || return 0
    "$OVERLAY_BIN" listen "$AUDIO_FILE" 9>&- &
    echo $! > "$OVERLAY_LISTEN_PID"
}

overlay_busy_start() {
    overlay_kill_busy
    [ -x "$OVERLAY_BIN" ] || return 0
    "$OVERLAY_BIN" busy 9>&- &
    echo $! > "$OVERLAY_BUSY_PID"
}

# Paste text into the focused window via clipboard + Ctrl+Shift+V.
type_into_focused_window() {
    local text="$1"
    [ -n "$text" ] || return 0
    sleep 0.5  # wait for focus to return after overlay closes
    if command -v wl-copy >/dev/null 2>&1; then
        printf '%s' "$text" | wl-copy
        printf '%s' "$text" | wl-copy --primary
        sleep 0.15  # let clipboard register before keystroke
        xdotool key --clearmodifiers ctrl+shift+v
    elif command -v xdotool >/dev/null 2>&1; then
        xdotool type --clearmodifiers --delay 8 "$text"
    else
        die "install wl-clipboard + xdotool"
    fi
}

[ -x "$WHISPER_CLI" ] || die "missing $WHISPER_CLI — run scripts/setup-whisper-dictation.sh"
[ -f "$MODEL" ] || die "missing $MODEL — run scripts/setup-whisper-dictation.sh"

if [ -f "$PID_FILE" ]; then
    pid="$(cat "$PID_FILE" 2>/dev/null || true)"
    rm -f "$PID_FILE"
    if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
        kill -INT "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi

    overlay_kill_listen
    release_lock
    overlay_busy_start

    # Transcribe (-nt strips timestamps, 2>/dev/null hides system info logs)
    set +e
    PROMPT="The following is a clearly spoken dictation with proper punctuation and grammar."
    TRANSCRIPT=$("$WHISPER_CLI" -m "$MODEL" -f "$AUDIO_FILE" -nt \
        --prompt "$PROMPT" \
        --beam-size 8 \
        -t 4 \
        2>/dev/null)
    set -e

    rm -f "$AUDIO_FILE"

    # Strip the prompt if whisper echoed it back (happens on silence/short clips)
    TRANSCRIPT="${TRANSCRIPT//$PROMPT/}"

    # Trim leading/trailing whitespace
    TRANSCRIPT="${TRANSCRIPT#"${TRANSCRIPT%%[![:space:]]*}"}"
    TRANSCRIPT="${TRANSCRIPT%"${TRANSCRIPT##*[![:space:]]}"}"

    overlay_kill_busy
    type_into_focused_window "$TRANSCRIPT"
else
    command -v arecord >/dev/null 2>&1 || die "arecord not installed (install package alsa-utils)"
    overlay_kill_busy
    # Start recording (16kHz, 16-bit, mono — required by whisper)
    arecord -f S16_LE -c 1 -r 16000 "$AUDIO_FILE" -q 9>&- &
    REC_PID=$!
    echo $REC_PID > "$PID_FILE"
    release_lock

    # Launch overlay and monitor it — if ESC is pressed (exit code 2), cancel recording
    if [ -x "$OVERLAY_BIN" ]; then
        "$OVERLAY_BIN" listen "$AUDIO_FILE" 9>&- &
        OVERLAY_PID=$!
        echo $OVERLAY_PID > "$OVERLAY_LISTEN_PID"
        set +e
        wait $OVERLAY_PID
        OVERLAY_EXIT=$?
        set -e
        rm -f "$OVERLAY_LISTEN_PID"
        if [ "$OVERLAY_EXIT" -eq 2 ]; then
            kill -INT "$REC_PID" 2>/dev/null || true
            wait "$REC_PID" 2>/dev/null || true
            rm -f "$PID_FILE" "$AUDIO_FILE"
        fi
    fi
fi
