#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_host=home
remote_root=/home/a/openkrx-build
mkdir -p "$project_root/dist" "$project_root/research"
# The remote tree is dedicated to this project's compiler output. Analysis stays local.
ssh -o BatchMode=yes -o ConnectTimeout=10 "$build_host" \
  'mkdir -p /home/a/openkrx-build/source /home/a/openkrx-build/build'
rsync -a --exclude='/build/' --exclude='/dist/' --exclude='/research/' \
  --exclude='/test-server/' --exclude='/.git/' \
  "$project_root/" "$build_host:$remote_root/source/"
ssh -o BatchMode=yes "$build_host" \
  'cmake -S /home/a/openkrx-build/source -B /home/a/openkrx-build/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTOOLS=OFF -DSERVER=ON -DDOWNLOAD_GTEST=OFF -DDISCORD=ON -DDISCORD_DYNAMIC=ON && cmake --build /home/a/openkrx-build/build --target DDNet DDNet-Server -j10' \
  2>&1 | tee "$project_root/research/build-remote.log"
rsync -a "$build_host:$remote_root/build/DDNet" "$project_root/dist/OpenKRX"
rsync -a "$build_host:$remote_root/build/DDNet-Server" "$project_root/dist/DDNet-Server"
rsync -a "$build_host:$remote_root/build/discord_game_sdk.so" "$project_root/dist/discord_game_sdk.so"
{
  printf 'source_commit='
  git -C "$project_root" rev-parse HEAD
  printf 'built_at_utc='
  date -u +%FT%TZ
  printf 'build_host=%s\n' "$build_host"
  sha256sum "$project_root/dist/OpenKRX" "$project_root/dist/DDNet-Server"
} > "$project_root/dist/BUILD.txt"
printf 'Artifacts: %s/dist\n' "$project_root"
