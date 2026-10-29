#!/usr/bin/env bash
# Launch the locally built OpenKRX client with its own data directory.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
# Desktop launches have no terminal. Keep the client's diagnostics available
# when investigating a missed rescue; explicit command-line options still win.
mkdir -p "$root/research/runtime/client-logs"
run_log="$root/research/runtime/client-logs/openkrx-$(date +%Y%m%d-%H%M%S)-$$.log"
exec "$root/dist/OpenKRX" "logfile $run_log" "$@"
