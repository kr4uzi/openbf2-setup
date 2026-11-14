# App icon

`make-icon.swift` draws the OpenBF2 icon with CoreGraphics: a dark-red angular
"2" on a radar screen. It is an original design under this repository's MIT
licence; no game artwork is used. To regenerate `macos/OpenBF2.icns`:

```sh
swift make-icon.swift            # writes OpenBF2-1024.png
mkdir OpenBF2.iconset
for s in 16 32 128 256 512; do
  sips -z $s $s OpenBF2-1024.png --out OpenBF2.iconset/icon_${s}x${s}.png
  sips -z $((s*2)) $((s*2)) OpenBF2-1024.png --out OpenBF2.iconset/icon_${s}x${s}@2x.png
done
iconutil -c icns OpenBF2.iconset -o ../OpenBF2.icns
```
