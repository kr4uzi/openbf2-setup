#!/bin/bash
# Signs the nested client, its libraries and then the bundle, and verifies the
# result. IDENTITY defaults to ad-hoc ("-"). Developer builds are signed this way
# after the client is embedded, so macOS files permission grants (microphone,
# local network) against a stable code identity instead of an unsigned bundle.
#
# A real identity (Developer ID) gets the hardened runtime and a secure
# timestamp on every signature, and entitlements:
#   SETUP_ENTITLEMENTS_CLIENT  entitlements of bf2_client (required with an identity)
#   SETUP_ENTITLEMENTS_APP     entitlements of the app, default macos/OpenBF2.entitlements
# Ad-hoc signing must stay without the runtime option: an ad-hoc signature has no
# Team ID, so library validation would refuse the client's own dylibs.
set -euo pipefail
[[ $# == 1 || $# == 2 ]] || { echo 'Usage: sign-bundle.sh "/path/OpenBF2.app" [IDENTITY]' >&2; exit 2; }
app=$1
identity=${2:--}
here=$(cd "$(dirname "$0")" && pwd -P)
[[ -d "$app" && ! -L "$app" ]] || { echo "Not a bundle directory: $app" >&2; exit 1; }

options=()
client_entitlements=()
app_entitlements=()
if [[ "$identity" != "-" ]]; then
  options=(--options runtime --timestamp)
  [[ -n "${SETUP_ENTITLEMENTS_CLIENT:-}" && -f "$SETUP_ENTITLEMENTS_CLIENT" ]] || { echo 'SETUP_ENTITLEMENTS_CLIENT must name the entitlements file of bf2_client' >&2; exit 1; }
  app_file=${SETUP_ENTITLEMENTS_APP:-$here/OpenBF2.entitlements}
  [[ -f "$app_file" ]] || { echo "App entitlements not found: $app_file" >&2; exit 1; }
  client_entitlements=(--entitlements "$SETUP_ENTITLEMENTS_CLIENT")
  app_entitlements=(--entitlements "$app_file")
fi

while IFS= read -r -d '' dylib; do
  /usr/bin/codesign --force --sign "$identity" ${options[@]+"${options[@]}"} "$dylib"
done < <(/usr/bin/find "$app/Contents" -type f -name '*.dylib' -print0)
if [[ -f "$app/Contents/MacOS/bf2_client" ]]; then
  /usr/bin/codesign --force --sign "$identity" ${options[@]+"${options[@]}"} ${client_entitlements[@]+"${client_entitlements[@]}"} "$app/Contents/MacOS/bf2_client"
fi
/usr/bin/codesign --force --sign "$identity" ${options[@]+"${options[@]}"} ${app_entitlements[@]+"${app_entitlements[@]}"} "$app"
/usr/bin/codesign --verify --strict --deep --verbose=2 "$app"
if [[ "$identity" != "-" ]]; then
  # Gatekeeper's verdict is informational here: it passes only once the image is notarized.
  /usr/bin/spctl --assess --type execute --verbose=2 "$app" || echo 'sign-bundle: spctl does not accept the app yet (expected before notarization)'
fi
