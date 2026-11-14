#!/bin/bash
# Developer tool: lets the Finder lay out the disk image window once (app left,
# Applications right, background picture) and saves the resulting .DS_Store as
# macos/dmg/DS_Store, which build-dmg.sh copies into every image. Run it again
# only when the background, the icon positions or the volume name change. The
# volume name must be the one build-dmg.sh uses, and no volume of that name may
# be mounted. Needs a graphical session and Finder automation (Apple Events).
#
#   macos/dmg/make-layout.sh "/path/OpenBF2.app"
set -euo pipefail
[[ $# == 1 && -d "$1" ]] || { echo 'Usage: make-layout.sh "/path/OpenBF2.app"' >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd -P)
volume='OpenBF2'
[[ ! -d "/Volumes/$volume" ]] || { echo "A volume named '$volume' is mounted; eject it first." >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/openbf2-layout.XXXXXX")
device=''
cleanup() {
  if [[ -n "$device" ]]; then /usr/bin/hdiutil detach "$device" -quiet -force || true; fi
  rm -rf "$work"
}
trap cleanup EXIT
mkdir "$work/stage" "$work/stage/.background"
/usr/bin/ditto "$1" "$work/stage/OpenBF2.app"
ln -s /Applications "$work/stage/Applications"
cp "$here/background.png" "$work/stage/.background/background.png"
/usr/bin/hdiutil create -quiet -volname "$volume" -srcfolder "$work/stage" -format UDRW -ov "$work/layout.dmg"
device=$(/usr/bin/hdiutil attach -readwrite -noverify -noautoopen "$work/layout.dmg" | /usr/bin/awk '/\/Volumes\// {print $1; exit}')
[[ -n "$device" && -d "/Volumes/$volume" ]] || { echo 'The writable image did not mount.' >&2; exit 1; }
/usr/bin/osascript <<EOF
tell application "Finder"
  tell disk "$volume"
    open
    set current view of container window to icon view
    set toolbar visible of container window to false
    set statusbar visible of container window to false
    set the bounds of container window to {400, 200, 960, 560}
    set options to the icon view options of container window
    set arrangement of options to not arranged
    set icon size of options to 128
    set text size of options to 14
    set background picture of options to file ".background:background.png"
    set position of item "OpenBF2.app" of container window to {140, 170}
    set position of item "Applications" of container window to {420, 170}
    close
    open
    update without registering applications
    delay 1
    close
  end tell
end tell
EOF
/bin/sync
sleep 1
[[ -f "/Volumes/$volume/.DS_Store" ]] || { echo 'The Finder wrote no .DS_Store.' >&2; exit 1; }
cp "/Volumes/$volume/.DS_Store" "$here/DS_Store"
/usr/bin/hdiutil detach "$device" -quiet
device=''
echo "Saved $here/DS_Store ($(/usr/bin/stat -f %z "$here/DS_Store") bytes)."
