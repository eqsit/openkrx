#!/usr/bin/env bash
# Launch the locally built OpenKRX client with its own data directory.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
# Prefer the native Wayland session when its socket is available. Explicit
# SDL driver selections take precedence, including test sessions on Xvfb.
if [[ -z "${SDL_VIDEODRIVER:-}" && -z "${SDL_VIDEO_DRIVER:-}" && -n "${WAYLAND_DISPLAY:-}" && -n "${XDG_RUNTIME_DIR:-}" ]]; then
    task_wayland_socket="$WAYLAND_DISPLAY"
    if [[ "$task_wayland_socket" != /* ]]; then
        task_wayland_socket="$XDG_RUNTIME_DIR/$task_wayland_socket"
    fi
    if [[ -S "$task_wayland_socket" ]]; then
        export SDL_VIDEODRIVER=wayland
    fi
fi
# Diagnostics are opt-in through the existing OpenKRX setting. An explicit
# logfile option can still be supplied when investigating a problem.
exec "$root/dist/OpenKRX" "$@"
