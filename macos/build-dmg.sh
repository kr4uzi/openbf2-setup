#!/bin/bash
# Builds only from supplied local artifacts. No download or notarization.
#
#   build-dmg.sh "/path/OpenBF2.app" /path/OpenBF2.dmg [--check-only]
#
# The image opens as a drag-and-drop installer: the app on the left, the
# Applications folder on the right, an arrow between them on a background
# picture. The window layout is macos/dmg/DS_Store, made once by
# macos/dmg/make-layout.sh with the Finder; here it is only copied, so no
# Finder automation runs in CI. SETUP_SIGN_IDENTITY selects the codesign
# identity ("-", ad-hoc, by default); a real identity also signs the image,
# and nothing may touch the image after that. SETUP_DMG_ALLOW_THIN=1 is a
# developer override which accepts a non-universal client in the image for
# local tests; CI never sets it.
set -euo pipefail
if [[ $# != 2 && !( $# == 3 && "$3" == --check-only ) ]]; then
  echo 'Usage: build-dmg.sh "/path/OpenBF2.app" /path/OpenBF2.dmg [--check-only]' >&2
  exit 2
fi
app=$1
output=$2
here=$(cd "$(dirname "$0")" && pwd -P)
volume='OpenBF2'
[[ -d "$app" && ! -e "$output" ]] || { echo 'App must exist; output must be new.' >&2; exit 1; }
[[ "$(basename "$app")" == 'OpenBF2.app' ]] || { echo 'Expected OpenBF2.app' >&2; exit 1; }
[[ -f "$here/dmg/DS_Store" && -f "$here/dmg/background.png" ]] || { echo 'macos/dmg/DS_Store and background.png are missing; run macos/dmg/make-layout.sh' >&2; exit 1; }
mkdir -p "$(dirname "$output")"
# Keep temporary packaging content beside the requested artifact.
stage=$(mktemp -d "$(dirname "$output")/.openbf2-dmg.XXXXXX")
trap 'rm -rf "$stage"' EXIT
/usr/bin/ditto "$app" "$stage/OpenBF2.app"
# Audit every embedded Mach-O, including any owner-supplied client/dylibs.
while IFS= read -r -d '' binary; do
  if /usr/bin/file -b "$binary" | /usr/bin/grep -q 'Mach-O'; then
    archs=$(/usr/bin/lipo -archs "$binary")
    if [[ " $archs " != *' arm64 '* || " $archs " != *' x86_64 '* ]]; then
      [[ "${SETUP_DMG_ALLOW_THIN:-0}" == 1 ]] || { echo "Non-universal binary: $binary" >&2; exit 1; }
      echo "WARNING: non-universal binary accepted for a local test image: $binary" >&2
    fi
  fi
done < <(/usr/bin/find "$stage/OpenBF2.app" -type f -print0)
identity=${SETUP_SIGN_IDENTITY:--}
/bin/bash "$here/sign-bundle.sh" "$stage/OpenBF2.app" "$identity"
if [[ "${3:-}" == --check-only ]]; then
  echo 'Architecture audit and staged signature verification passed; no disk image created.'
  exit 0
fi
ln -s /Applications "$stage/Applications"
mkdir "$stage/.background"
cp "$here/dmg/background.png" "$stage/.background/background.png"
cp "$here/dmg/DS_Store" "$stage/.DS_Store"
/usr/bin/hdiutil create -quiet -volname "$volume" -srcfolder "$stage" -format UDZO "$output"
if [[ "$identity" != "-" ]]; then
  # The image carries the same identity as the app; notarization and stapling happen afterwards, elsewhere.
  /usr/bin/codesign --force --sign "$identity" --timestamp "$output"
  /usr/bin/codesign --verify --strict --verbose=2 "$output"
fi
echo "Created $output (identity: $identity). Notarization was not run."
