#!/bin/bash
# Builds a downloadable bundle without installing it or changing the user's Flatpak remotes.
set -euo pipefail
[[ $# == 3 ]] || { echo 'Usage: build-flatpak.sh PORTABLE_DIR OUTPUT.flatpak NEW_WORK_DIR' >&2; exit 2; }
repo=$(cd "$(dirname "$0")/.." && pwd -P)
portable=$(realpath "$1")
output=$(realpath -m "$2")
work=$(realpath -m "$3")
[[ ! -e "$output" && ! -e "$work" ]] || { echo 'Output/work path already exists' >&2; exit 1; }
python3 "$repo/linux/package.py" validate-release "$portable"
# A runtime is intentionally not included in the .flatpak file. Builders need both
# SDK and Platform installed in their user installation before this offline command.
flatpak info --user org.freedesktop.Platform/x86_64/25.08 >/dev/null
flatpak info --user org.freedesktop.Sdk/x86_64/25.08 >/dev/null
mkdir -p "$work" "$(dirname "$output")"
cp "$repo/linux/flatpak/org.openbf2.Setup.json" "$work/manifest.json"
cp -a "$portable" "$work/portable"
flatpak-builder --user --arch=x86_64 --disable-download --repo="$work/repo" "$work/build" "$work/manifest.json"
flatpak build-bundle --arch=x86_64 "$work/repo" "$output" org.openbf2.Setup master
sha256sum "$output" > "$output.sha256"
flatpak info --user --show-commit org.freedesktop.Platform/x86_64/25.08 > "$output.runtime-commit.txt"
