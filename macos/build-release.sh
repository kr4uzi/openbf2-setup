#!/bin/bash
# Non-interactive release build for CI: universal setup app with the universal
# native client embedded, tests, and the DMG. No downloads, no installs.
#
#   macos/build-release.sh CLIENT_DIR OUTPUT.dmg [BUILD_DIR]
#
# CLIENT_DIR must satisfy docs/client-artifact.md (bf2_client, libdice_py.dylib,
# libaidll.dylib; each arm64 + x86_64). OUTPUT.dmg must not exist. BUILD_DIR
# defaults to openbf2-setup-build beside OUTPUT.dmg and may be reused between
# runs; relative OUTPUT.dmg and BUILD_DIR are resolved against the current
# directory. SETUP_SIGN_IDENTITY selects a codesign identity; unset means ad-hoc.
# Opt-in owner/media tests stay off: their environment variables are cleared.
set -euo pipefail
if [[ $# != 2 && $# != 3 ]]; then
  echo 'Usage: build-release.sh CLIENT_DIR OUTPUT.dmg [BUILD_DIR]' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd -P)
fail() { echo "build-release: $*" >&2; exit 1; }
client=$(cd "$1" && pwd -P) || fail "client directory not found: $1"
output=$2
mkdir -p "$(dirname "$output")"
output="$(cd "$(dirname "$output")" && pwd -P)/$(basename "$output")"
# Every path handed to cmake and TMPDIR must be absolute: CMake's try_compile
# and the compiler run in other directories, so a relative BUILD_DIR (relative
# to this shell's cwd) would leave them with a TMPDIR that does not exist there.
build=${3:-$(dirname "$output")/openbf2-setup-build}
mkdir -p "$build"
build=$(cd "$build" && pwd -P)
scratch="$build/scratch"
if [[ -n "${SETUP_RELEASE_PRINT_PATHS:-}" ]]; then
  # Test hook: show the resolved paths and stop before any validation or build.
  printf 'client=%s\noutput=%s\nbuild=%s\nscratch=%s\n' "$client" "$output" "$build" "$scratch"
  exit 0
fi
[[ ! -e "$output" ]] || fail "output already exists: $output"
for item in bf2_client libdice_py.dylib libaidll.dylib; do
  [[ -f "$client/$item" ]] || fail "missing client artifact: $client/$item"
  /usr/bin/lipo "$client/$item" -verify_arch arm64 x86_64 || fail "client artifact is not universal: $item"
done
command -v cmake >/dev/null || fail 'cmake not found'
command -v cargo >/dev/null || fail 'cargo not found (both Apple Rust targets must be installed)'
jobs=$(/usr/sbin/sysctl -n hw.ncpu)
mkdir -p "$scratch"
export TMPDIR="$scratch"
echo "== configure ($build)"
cmake -S "$repo" -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' -DSETUP_SCRATCH="$scratch" \
  -DSETUP_CLIENT_ARTIFACT="$client" -DSETUP_CLIENT_ALLOW_THIN=OFF
echo "== build"
cmake --build "$build" -j"$jobs"
app="$build/macos/OpenBF2.app"
for item in bf2_client libdice_py.dylib libaidll.dylib; do
  /usr/bin/cmp -s "$client/$item" "$app/Contents/MacOS/$item" || fail "embedded $item differs from the artifact"
done
echo "== test"
# -u clears the opt-in media/key tests so CI never needs proprietary inputs.
env -u SETUP_TEST_ISO -u BF2_TEST_KEY -u BF2SF_TEST_KEY \
  ctest --test-dir "$build" --output-on-failure -j"$jobs"
echo "== dmg"
SETUP_SIGN_IDENTITY="${SETUP_SIGN_IDENTITY:--}" /bin/bash "$repo/macos/build-dmg.sh" "$app" "$output"
/usr/bin/hdiutil verify -quiet "$output"
/usr/bin/shasum -a 256 "$output" | /usr/bin/tee "$output.sha256"
echo "build-release: done ($output)"
