#!/bin/bash
# build-release.sh must hand cmake and TMPDIR absolute paths even when it is
# invoked with relative OUTPUT.dmg / BUILD_DIR arguments from another directory
# (CMake's try_compile runs elsewhere and fails on a relative TMPDIR). Uses the
# script's SETUP_RELEASE_PRINT_PATHS hook, so nothing is configured or built.
set -euo pipefail
scratch=$1
macos=$2
fixture=$(mktemp -d "$scratch/release-paths.XXXXXX")
trap 'rm -rf "$fixture"' EXIT
fixture=$(cd "$fixture" && pwd -P)
mkdir "$fixture/client" "$fixture/work"
script="$macos/build-release.sh"
check() {
  local label=$1 want_build=$2 want_output=$3; shift 3
  local out
  out=$(cd "$fixture/work" && SETUP_RELEASE_PRINT_PATHS=1 /bin/bash "$script" "$@")
  local build output scratch_dir client
  build=$(printf '%s\n' "$out" | sed -n 's/^build=//p')
  output=$(printf '%s\n' "$out" | sed -n 's/^output=//p')
  scratch_dir=$(printf '%s\n' "$out" | sed -n 's/^scratch=//p')
  client=$(printf '%s\n' "$out" | sed -n 's/^client=//p')
  [[ "$build" == "$want_build" ]] || { echo "$label: build=$build, want $want_build" >&2; exit 1; }
  [[ "$output" == "$want_output" ]] || { echo "$label: output=$output, want $want_output" >&2; exit 1; }
  [[ "$scratch_dir" == "$want_build/scratch" ]] || { echo "$label: scratch=$scratch_dir" >&2; exit 1; }
  [[ "$client" == "$fixture/client" ]] || { echo "$label: client=$client" >&2; exit 1; }
  [[ -d "$build" ]] || { echo "$label: build directory was not created" >&2; exit 1; }
}
# Relative BUILD_DIR (the OpenBF2 CI case) and relative OUTPUT.dmg.
check 'relative build dir' "$fixture/work/rel-build" "$fixture/work/out/OpenBF2.dmg" \
  ../client out/OpenBF2.dmg rel-build
check 'dotted build dir' "$fixture/work/dotted" "$fixture/work/out/OpenBF2.dmg" \
  ../client out/OpenBF2.dmg ./nested/../dotted
# Default BUILD_DIR sits beside the output, absolute.
check 'default build dir' "$fixture/work/out/openbf2-setup-build" "$fixture/work/out/OpenBF2.dmg" \
  ../client out/OpenBF2.dmg
# Absolute arguments are unchanged (after symlink resolution).
check 'absolute' "$fixture/abs-build" "$fixture/abs/OpenBF2.dmg" \
  "$fixture/client" "$fixture/abs/OpenBF2.dmg" "$fixture/abs-build"
# A missing client directory is still an error.
if (cd "$fixture/work" && SETUP_RELEASE_PRINT_PATHS=1 /bin/bash "$script" ../missing out/x.dmg b >/dev/null 2>&1); then
  echo 'accepted a missing client directory' >&2; exit 1
fi
echo 'build-release.sh resolves relative OUTPUT.dmg and BUILD_DIR to absolute paths.'
