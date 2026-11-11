#!/bin/bash
# Builds only from supplied local artifacts. No download or notarization.
set -euo pipefail
if [[ $# != 2 && !( $# == 3 && "$3" == --check-only ) ]]; then
  echo 'Usage: build-dmg.sh "/path/Battlefield 2.app" /path/Battlefield-2.dmg [--check-only]' >&2
  exit 2
fi
app=$1
output=$2
[[ -d "$app" && ! -e "$output" ]] || { echo 'App must exist; output must be new.' >&2; exit 1; }
[[ "$(basename "$app")" == 'Battlefield 2.app' ]] || { echo 'Expected Battlefield 2.app' >&2; exit 1; }
mkdir -p "$(dirname "$output")"
# Keep temporary packaging content beside the requested artifact.
stage=$(mktemp -d "$(dirname "$output")/.openbf2-dmg.XXXXXX")
trap 'rm -rf "$stage"' EXIT
/usr/bin/ditto "$app" "$stage/Battlefield 2.app"
# Audit every embedded Mach-O, including any owner-supplied client/dylibs.
while IFS= read -r -d '' binary; do
  if /usr/bin/file -b "$binary" | /usr/bin/grep -q 'Mach-O'; then
    archs=$(/usr/bin/lipo -archs "$binary")
    [[ " $archs " == *' arm64 '* && " $archs " == *' x86_64 '* ]] || { echo "Non-universal binary: $binary" >&2; exit 1; }
  fi
done < <(/usr/bin/find "$stage/Battlefield 2.app" -type f -print0)
identity=${SETUP_SIGN_IDENTITY:--}
# Placeholder: ad-hoc identity by default. Release identity/entitlements require review.
while IFS= read -r -d '' dylib; do /usr/bin/codesign --force --sign "$identity" "$dylib"; done < <(/usr/bin/find "$stage/Battlefield 2.app/Contents" -type f -name '*.dylib' -print0)
if [[ -f "$stage/Battlefield 2.app/Contents/MacOS/bf2_client" ]]; then
  /usr/bin/codesign --force --sign "$identity" "$stage/Battlefield 2.app/Contents/MacOS/bf2_client"
fi
/usr/bin/codesign --force --sign "$identity" "$stage/Battlefield 2.app"
/usr/bin/codesign --verify --strict "$stage/Battlefield 2.app"
if [[ "${3:-}" == --check-only ]]; then
  echo 'Architecture audit and staged signature verification passed; no disk image created.'
  exit 0
fi
ln -s /Applications "$stage/Applications"
/usr/bin/hdiutil create -volname 'Battlefield 2' -srcfolder "$stage" -format UDZO "$output"
echo "Created $output (identity: $identity). Notarization was not run."
