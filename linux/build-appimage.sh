#!/bin/bash
# Wrap an already validated portable application. APPIMAGETOOL must be supplied locally.
set -euo pipefail
[[ $# == 3 ]] || { echo 'Usage: build-appimage.sh PORTABLE_DIR OUTPUT.AppImage NEW_WORK_DIR' >&2; exit 2; }
repo=$(cd "$(dirname "$0")/.." && pwd -P)
portable=$(realpath "$1")
output=$(realpath -m "$2")
work=$(realpath -m "$3")
: "${APPIMAGETOOL:?Supply the local appimagetool executable}"
: "${APPIMAGETOOL_SHA256:?Supply its pinned SHA-256}"
: "${APPIMAGE_RUNTIME:?Supply the local x86-64 AppImage runtime}"
: "${APPIMAGE_RUNTIME_SHA256:?Supply its pinned SHA-256}"
[[ ! -e "$output" && ! -e "$work" ]] || { echo 'Output/work path already exists' >&2; exit 1; }
printf '%s  %s\n' "$APPIMAGETOOL_SHA256" "$APPIMAGETOOL" | sha256sum -c -
printf '%s  %s\n' "$APPIMAGE_RUNTIME_SHA256" "$APPIMAGE_RUNTIME" | sha256sum -c -
python3 "$repo/linux/package.py" audit-runtime "$APPIMAGE_RUNTIME"
python3 "$repo/linux/package.py" validate-release "$portable"
mkdir -p "$work/OpenBF2.AppDir" "$(dirname "$output")"
cp -a "$portable/." "$work/OpenBF2.AppDir/"
cp "$repo/linux/AppRun" "$work/OpenBF2.AppDir/AppRun"
chmod 755 "$work/OpenBF2.AppDir/AppRun"
cp "$portable/share/applications/org.openbf2.Setup.desktop" "$work/OpenBF2.AppDir/"
cp "$portable/share/icons/hicolor/256x256/apps/org.openbf2.Setup.png" "$work/OpenBF2.AppDir/"
# No update URL and no network downloads; appimagetool/runtime are preinstalled, pinned inputs.
ARCH=x86_64 "$APPIMAGETOOL" --no-appstream --runtime-file "$APPIMAGE_RUNTIME" "$work/OpenBF2.AppDir" "$output"
sha256sum "$output" > "$output.sha256"
printf 'appimagetool_sha256=%s\nruntime_sha256=%s\n' "$APPIMAGETOOL_SHA256" "$APPIMAGE_RUNTIME_SHA256" > "$output.build.txt"
