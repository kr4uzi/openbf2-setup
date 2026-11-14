#!/bin/bash
# Signs the nested client, its libraries and then the bundle, and verifies the
# result. IDENTITY defaults to ad-hoc ("-"). Developer builds are signed this way
# after the client is embedded, so macOS files permission grants (microphone,
# local network) against a stable code identity instead of an unsigned bundle.
# Release identity/entitlements are chosen by build-release.sh.
set -euo pipefail
[[ $# == 1 || $# == 2 ]] || { echo 'Usage: sign-bundle.sh "/path/OpenBF2.app" [IDENTITY]' >&2; exit 2; }
app=$1
identity=${2:--}
[[ -d "$app" && ! -L "$app" ]] || { echo "Not a bundle directory: $app" >&2; exit 1; }
while IFS= read -r -d '' dylib; do /usr/bin/codesign --force --sign "$identity" "$dylib"; done < <(/usr/bin/find "$app/Contents" -type f -name '*.dylib' -print0)
if [[ -f "$app/Contents/MacOS/bf2_client" ]]; then
  /usr/bin/codesign --force --sign "$identity" "$app/Contents/MacOS/bf2_client"
fi
/usr/bin/codesign --force --sign "$identity" "$app"
/usr/bin/codesign --verify --strict "$app"
