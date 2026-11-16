#!/bin/bash
# Offline setup assembly. Engine CI supplies both artifacts; this never builds engine code.
set -euo pipefail
if [[ $# != 2 ]]; then
  echo 'Usage: SETUP_CLIENT_ARTIFACT=DIR SETUP_RUNTIME_ARTIFACT=DIR linux/build-release.sh OUTPUT.tar.gz NEW_BUILD_DIR' >&2
  exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd -P)
: "${SETUP_CLIENT_ARTIFACT:?Set the Linux engine artifact directory}"
: "${SETUP_RUNTIME_ARTIFACT:?Set the pinned Linux SDK runtime artifact directory}"
: "${SETUP_SDK_PREFIX:?Set the matching development SDK prefix (headers and CMake packages)}"
: "${SETUP_BOOST_ROOT:?Set Boost 1.89.0 headers}"
: "${SETUP_ARTIFACT_BUILD_ROOTS_FILE:?Set the local JSON list of engine/SDK source and build directories}"
output=$(realpath -m "$1")
build=$(realpath -m "$2")
[[ ! -e "$output" && ! -e "$build" ]] || { echo 'Output and build directory must be new' >&2; exit 1; }
[[ $(uname -m) == x86_64 && $(getconf GNU_LIBC_VERSION) == 'glibc 2.35' ]] || { echo 'Release requires the glibc 2.35 x86-64 builder' >&2; exit 1; }
[[ $(g++-15 -dumpfullversion) == 15.2.0 ]] || { echo 'Release requires GCC 15.2.0' >&2; exit 1; }
[[ $(rustc --version) == 'rustc 1.90.0 '* ]] || { echo 'Release requires Rust 1.90.0' >&2; exit 1; }
[[ -z ${CFLAGS:-}${CXXFLAGS:-}${LDFLAGS:-}${RUSTFLAGS:-} ]] || { echo 'Release refuses inherited compiler/linker flags' >&2; exit 1; }
for tool in cmake ninja cargo rustc python3 readelf strings patchelf rg; do command -v "$tool" >/dev/null; done
export RUSTFLAGS="-C target-cpu=x86-64 --remap-path-prefix=$repo=. --remap-path-prefix=$build=./build"
mkdir -p "$build" "$(dirname "$output")"
client=$(realpath "$SETUP_CLIENT_ARTIFACT")
runtime=$(realpath "$SETUP_RUNTIME_ARTIFACT")
roots_file=$(realpath "$SETUP_ARTIFACT_BUILD_ROOTS_FILE")
audit_paths=(--build-root "$repo" --build-root "$build" --build-root "$SETUP_SDK_PREFIX" --build-root "$SETUP_BOOST_ROOT" --build-roots-file "$roots_file")
# Fail before compiling if the engine/runtime contract or SDK identity differs.
python3 "$repo/linux/package.py" check-inputs --engine "$client" --runtime "$runtime" "${audit_paths[@]}"
cmake -S "$repo" -B "$build/compile" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$repo/linux/baseline.cmake" -DCMAKE_PREFIX_PATH="$SETUP_SDK_PREFIX" \
  -DCMAKE_CXX_FLAGS="-march=x86-64 -mtune=generic -ffile-prefix-map=$repo=. -ffile-prefix-map=$build=./build -ffile-prefix-map=$SETUP_SDK_PREFIX=./sdk -ffile-prefix-map=$SETUP_BOOST_ROOT=./boost" \
  -DSETUP_BOOST_ROOT="${SETUP_BOOST_ROOT:?Set Boost 1.89.0 headers}" \
  -DSETUP_CLIENT_ARTIFACT="$client" -DSETUP_CLIENT_ALLOW_DEVELOPMENT=OFF \
  -DSETUP_ARTIFACT_BUILD_ROOTS_FILE="$roots_file" \
  -DSETUP_SCRATCH="$build/scratch"
rg -q '#define BOOST_LIB_VERSION "1_89"' "$SETUP_BOOST_ROOT/boost/version.hpp"
cmake --build "$build/compile" -j"${SETUP_JOBS:-4}"
env -u BF2_TEST_KEY -u BF2SF_TEST_KEY -u SETUP_TEST_ISO ctest --test-dir "$build/compile" --output-on-failure
cmake --install "$build/compile" --prefix "$build/app"
revision=$(git -C "$repo" rev-parse HEAD)
# Refuse falsely attributing a dirty source tree to a clean revision.
[[ -z $(git -C "$repo" status --porcelain --untracked-files=normal) ]] || { echo 'Release requires a clean, committed setup checkout' >&2; exit 1; }
python3 "$repo/linux/package.py" assemble --app "$build/app" --engine "$client" --runtime "$runtime" --output "$build/OpenBF2" --setup-revision "$revision" "${audit_paths[@]}"
QT_QPA_PLATFORM=offscreen "$build/OpenBF2/bin/OpenBF2" --check-client
QT_QPA_PLATFORM=offscreen "$build/OpenBF2/bin/OpenBF2" --smoke-test "$build/packaged-ui"
python3 "$repo/linux/package.py" archive "$build/OpenBF2" "$output" "${audit_paths[@]}"
