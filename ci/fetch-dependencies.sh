#!/bin/bash
# Fetches the toolchain of ci/versions.env into PREFIX: Boost headers, SDL3 (static, audio only) and,
# on macOS, the official universal Qt. Every archive is checked against its pinned SHA-256 (Qt by
# aqtinstall against Qt's published checksums). For CI and for setting up a developer machine;
# the build itself never downloads anything.
#
#   ci/fetch-dependencies.sh PREFIX
#
# Prints the -D options for CMake (or put them into the environment for build-release.sh).
set -euo pipefail
[[ $# == 1 ]] || { echo 'Usage: fetch-dependencies.sh PREFIX' >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck disable=SC1091
source "$here/versions.env"
mkdir -p "$1"
prefix=$(cd "$1" && pwd -P)
downloads="$prefix/downloads"
mkdir -p "$downloads"

fetch() { # URL FILE SHA256
  [[ -f "$2" ]] || curl -sSfL -o "$2" "$1"
  echo "$3  $2" | shasum -a 256 -c - >/dev/null || { echo "Checksum mismatch: $2" >&2; exit 1; }
}

boost="boost_${BOOST_VERSION//./_}"
if [[ ! -f "$prefix/$boost/boost/asio/awaitable.hpp" ]]; then
  fetch "https://archives.boost.io/release/$BOOST_VERSION/source/$boost.tar.gz" "$downloads/$boost.tar.gz" "$BOOST_SHA256"
  tar -xzf "$downloads/$boost.tar.gz" -C "$prefix" "$boost/boost" "$boost/LICENSE_1_0.txt"
fi

sdl="SDL3-$SDL3_VERSION"
if [[ ! -f "$prefix/sdl3/lib/cmake/SDL3/SDL3Config.cmake" ]]; then
  fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/$sdl.tar.gz" "$downloads/$sdl.tar.gz" "$SDL3_SHA256"
  rm -rf "$downloads/$sdl" && tar -xzf "$downloads/$sdl.tar.gz" -C "$downloads"
  options=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix/sdl3" -DSDL_SHARED=OFF -DSDL_STATIC=ON
    -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_VIDEO=OFF -DSDL_RENDER=OFF -DSDL_GPU=OFF
    -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF -DSDL_POWER=OFF
    -DSDL_DIALOG=OFF -DSDL_TRAY=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
  if [[ "$(uname -s)" == Darwin ]]; then
    options+=(-DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3)
  else
    # audio only: no X11 or Wayland. the audio backends (ALSA, PulseAudio, PipeWire) come from their -dev packages
    options+=(-DSDL_UNIX_CONSOLE_BUILD=ON)
  fi
  # the build folder's name must not start with the source folder's: SDL's source groups take that for a subfolder
  build="$prefix/build-sdl3"
  rm -rf "$build"
  cmake -S "$downloads/$sdl" -B "$build" "${options[@]}" >/dev/null
  cmake --build "$build" -j"$(getconf _NPROCESSORS_ONLN)" >/dev/null
  cmake --install "$build" >/dev/null
fi

qt=""
if [[ "$(uname -s)" == Darwin ]]; then
  qt="$prefix/Qt/$QT_VERSION/macos"
  if [[ ! -f "$qt/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    python3 -m venv "$downloads/aqt-venv"
    "$downloads/aqt-venv/bin/pip" install -q aqtinstall
    (cd "$downloads" && "$downloads/aqt-venv/bin/aqt" install-qt mac desktop "$QT_VERSION" clang_64 --archives qtbase qttools --outputdir "$prefix/Qt" >/dev/null)
  fi
fi

echo "-DSETUP_BOOST_ROOT=$prefix/$boost -DSETUP_SDL3_ROOT=$prefix/sdl3${qt:+ -DSETUP_QT_ROOT=$qt}"
