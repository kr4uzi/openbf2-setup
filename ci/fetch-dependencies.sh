#!/bin/bash
# Fetches the toolchain of ci/versions.env into PREFIX: Boost headers, SDL3 (static, audio only) and,
# on macOS, the official universal Qt. On Windows (Git Bash, inside an MSVC x64 developer environment) also
# zlib (static), the official Qt for MSVC, the DirectX files of DXSETUP and Inno Setup. Every archive is
# checked against its pinned SHA-256 (Qt by aqtinstall against Qt's published checksums). For CI and for
# setting up a developer machine; the build itself never downloads anything.
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

windows=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) windows=1 ;; esac
# the path as Windows programs (CMake, MSVC) take it
native() { if [[ $windows ]]; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

fetch() { # URL FILE SHA256
  [[ -f "$2" ]] || curl -sSfL -o "$2" "$1"
  local actual
  if command -v shasum >/dev/null; then actual=$(shasum -a 256 "$2"); else actual=$(sha256sum "$2"); fi
  [[ "${actual%% *}" == "$3" ]] || { echo "Checksum mismatch: $2" >&2; exit 1; }
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
  elif [[ $windows ]]; then
    options=(-G Ninja "${options[@]}" -DCMAKE_INSTALL_PREFIX="$(native "$prefix/sdl3")")
  else
    # audio only: no X11 or Wayland. the audio backends (ALSA, PulseAudio, PipeWire) come from their -dev packages
    options+=(-DSDL_UNIX_CONSOLE_BUILD=ON)
  fi
  # the build folder's name must not start with the source folder's: SDL's source groups take that for a subfolder
  build="$prefix/build-sdl3"
  rm -rf "$build"
  cmake -S "$(native "$downloads/$sdl")" -B "$(native "$build")" "${options[@]}" >/dev/null
  cmake --build "$(native "$build")" -j"$(getconf _NPROCESSORS_ONLN)" >/dev/null
  cmake --install "$(native "$build")" >/dev/null
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

windows_options=""
if [[ $windows ]]; then
  zlib="zlib-$ZLIB_VERSION"
  # the static library's name differs between zlib's releases
  library=""
  for name in zs.lib libzs.lib zlibstatic.lib; do [[ -f "$prefix/zlib/lib/$name" ]] && library="$prefix/zlib/lib/$name" && break; done
  if [[ -z "$library" ]]; then
    fetch "https://github.com/madler/zlib/releases/download/v$ZLIB_VERSION/$zlib.tar.gz" "$downloads/$zlib.tar.gz" "$ZLIB_SHA256"
    rm -rf "$downloads/$zlib" "$prefix/build-zlib" && tar -xzf "$downloads/$zlib.tar.gz" -C "$downloads"
    cmake -S "$(native "$downloads/$zlib")" -B "$(native "$prefix/build-zlib")" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$(native "$prefix/zlib")" >/dev/null
    cmake --build "$(native "$prefix/build-zlib")" >/dev/null
    cmake --install "$(native "$prefix/build-zlib")" >/dev/null
  fi
  for name in zs.lib libzs.lib zlibstatic.lib; do [[ -f "$prefix/zlib/lib/$name" ]] && library="$prefix/zlib/lib/$name" && break; done
  [[ -n "$library" ]] || { echo "zlib: no static library in $prefix/zlib/lib" >&2; exit 1; }

  qt="$prefix/Qt/$QT_WINDOWS_VERSION/msvc2022_64"
  if [[ ! -f "$qt/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    python=python
    python --version >/dev/null 2>&1 || python="py -3"
    $python -m venv "$downloads/aqt-venv"
    "$downloads/aqt-venv/Scripts/pip" install -q aqtinstall
    (cd "$downloads" && "$downloads/aqt-venv/Scripts/aqt" install-qt windows desktop "$QT_WINDOWS_VERSION" "$QT_WINDOWS_ARCH" --archives qtbase qttools --outputdir "$(native "$prefix/Qt")" >/dev/null)
  fi

  # Microsoft's redist unpacks itself (a signed Microsoft program); only DXSETUP's five files are kept, each by hash
  directx="$prefix/directx"
  if [[ ! -f "$directx/DXSETUP.exe" ]]; then
    fetch "$DIRECTX_REDIST_URL" "$downloads/directx_Jun2010_redist.exe" "$DIRECTX_REDIST_SHA256"
    rm -rf "$downloads/dxredist" "$directx" && mkdir -p "$downloads/dxredist" "$directx"
    # Git Bash would take the /switches for paths and rewrite them
    MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' "$downloads/directx_Jun2010_redist.exe" /Q "/T:$(cygpath -w "$downloads/dxredist")" || true
    for file in DXSETUP.exe DSETUP.dll dsetup32.dll dxupdate.cab Apr2005_d3dx9_25_x64.cab; do
      [[ -f "$downloads/dxredist/$file" ]] || { echo "DirectX redist: $file missing" >&2; exit 1; }
      cp "$downloads/dxredist/$file" "$directx/"
    done
    rm -rf "$downloads/dxredist"
  fi

  inno="$prefix/innosetup"
  if [[ ! -f "$inno/ISCC.exe" ]]; then
    fetch "$INNO_SETUP_URL" "$downloads/innosetup-$INNO_SETUP_VERSION.exe" "$INNO_SETUP_SHA256"
    MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' "$downloads/innosetup-$INNO_SETUP_VERSION.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CURRENTUSER /NOICONS "/DIR=$(cygpath -w "$inno")"
    [[ -f "$inno/ISCC.exe" ]] || { echo "Inno Setup did not install into $inno" >&2; exit 1; }
  fi
  windows_options=" -DZLIB_LIBRARY=$(native "$library") -DZLIB_INCLUDE_DIR=$(native "$prefix/zlib/include") -DSETUP_DIRECTX_REDIST=$(native "$directx") -DSETUP_ISCC=$(native "$inno/ISCC.exe")"
fi

echo "-DSETUP_BOOST_ROOT=$(native "$prefix/$boost") -DSETUP_SDL3_ROOT=$(native "$prefix/sdl3")${qt:+ -DSETUP_QT_ROOT=$(native "$qt")}$windows_options"
