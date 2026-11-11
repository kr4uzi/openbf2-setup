#!/bin/bash
# Synthetic native fixtures only. No game/client code is run.
set -euo pipefail
scratch=$1
macos=$2
fixture=$(mktemp -d "$scratch/client-packaging.XXXXXX")
trap 'rm -rf "$fixture"' EXIT
export TMPDIR="$fixture"
mkdir "$fixture/source" "$fixture/destination"
printf '%s\n' 'int fixture_symbol(void) { return 0; }' > "$fixture/library.c"
printf '%s\n' 'extern int fixture_symbol(void); int main(void) { return fixture_symbol(); }' > "$fixture/main.c"
for item in libdice_py.dylib libaidll.dylib; do
  /usr/bin/clang -arch arm64 -arch x86_64 -dynamiclib "$fixture/library.c" -Wl,-install_name,"@rpath/$item" -Wl,-rpath,@loader_path -o "$fixture/source/$item"
done
/usr/bin/clang -arch arm64 -arch x86_64 "$fixture/main.c" "$fixture/source/libdice_py.dylib" -Wl,-rpath,@loader_path -o "$fixture/source/bf2_client"
printf '%s\n' 'must not overwrite host' > "$fixture/source/Info.plist"
before=$(/usr/bin/shasum -a 256 "$fixture/source/"*)
/bin/bash "$macos/embed-client.sh" "$fixture/source" "$fixture/destination"
[[ "$before" == "$(/usr/bin/shasum -a 256 "$fixture/source/"*)" ]]
[[ ! -e "$fixture/destination/Info.plist" ]]
for item in bf2_client libdice_py.dylib libaidll.dylib; do cmp "$fixture/source/$item" "$fixture/destination/$item"; done
reject() {
  if /bin/bash "$macos/embed-client.sh" "$fixture/source" "$fixture/destination" > "$fixture/rejection.log" 2>&1; then
    echo "Accepted invalid client artifact: $1" >&2; exit 1
  fi
}
mv "$fixture/source/libaidll.dylib" "$fixture/aidll"
reject 'missing library'
ln -s "$fixture/aidll" "$fixture/source/libaidll.dylib"
reject 'symlink library'
rm "$fixture/source/libaidll.dylib"
mv "$fixture/aidll" "$fixture/source/libaidll.dylib"
cp "$fixture/source/bf2_client" "$fixture/client"
/usr/bin/lipo "$fixture/client" -thin x86_64 -output "$fixture/source/bf2_client"
reject 'single architecture'
# The developer override accepts only the host architecture, never the other one.
host=$(/usr/bin/uname -m); other=arm64; [[ "$host" == arm64 ]] && other=x86_64
/usr/bin/lipo "$fixture/client" -thin "$host" -output "$fixture/source/bf2_client"
SETUP_CLIENT_ALLOW_THIN=1 /bin/bash "$macos/embed-client.sh" "$fixture/source" "$fixture/destination" > "$fixture/thin.log" 2>&1
grep -q 'WARNING: accepting' "$fixture/thin.log"
/usr/bin/lipo "$fixture/client" -thin "$other" -output "$fixture/source/bf2_client"
if SETUP_CLIENT_ALLOW_THIN=1 /bin/bash "$macos/embed-client.sh" "$fixture/source" "$fixture/destination" > "$fixture/rejection.log" 2>&1; then
  echo 'Accepted a foreign-architecture client artifact under the developer override' >&2; exit 1
fi
cp "$fixture/client" "$fixture/source/bf2_client"
/bin/bash "$macos/embed-client.sh" "$fixture/source" "$fixture/destination" > /dev/null
cp "$fixture/client" "$fixture/source/bf2_client"
/usr/bin/install_name_tool -add_rpath /private/build-only "$fixture/source/bf2_client"
reject 'absolute build rpath'
cp "$fixture/client" "$fixture/source/bf2_client"
/usr/bin/install_name_tool -change @rpath/libdice_py.dylib /private/build-only/libdice_py.dylib "$fixture/source/bf2_client"
reject 'absolute build dependency'
cp "$fixture/client" "$fixture/source/bf2_client"
# Exercise staging and signing with all three fixtures, never hdiutil.
app="$fixture/Battlefield 2.app"
mkdir -p "$app/Contents/MacOS"
cp "$fixture/source/"{bf2_client,libdice_py.dylib,libaidll.dylib} "$app/Contents/MacOS/"
cp "$fixture/source/bf2_client" "$app/Contents/MacOS/Battlefield 2"
cp "$macos/Info.plist" "$app/Contents/Info.plist"
SETUP_SIGN_IDENTITY=- /bin/bash "$macos/build-dmg.sh" "$app" "$fixture/test.dmg" --check-only
[[ ! -e "$fixture/test.dmg" ]]
echo 'Client import: universal copy, read-only source, restricted files, missing/symlink/single-arch/rpath/dependency rejection, host-only developer override; nested signing passed.'
