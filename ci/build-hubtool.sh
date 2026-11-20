#!/bin/bash
# Builds openbf2-hubtool.exe (Windows, 32-bit, static, mingw-w64) from the commit pinned in ci/versions.env, for
# the Windows setup (-DSETUP_HUBTOOL=OUTPUT_DIR/openbf2-hubtool.exe). The pinned commit is exported from REPOSITORY
# into a fresh folder, so local changes in that checkout never reach the build.
#
#   ci/build-hubtool.sh REPOSITORY OUTPUT_DIR
#
# REPOSITORY is a clone of HUBTOOL_REPOSITORY which has HUBTOOL_REVISION. Nothing is downloaded here. Needs
# i686-w64-mingw32-g++ and -windres (override with WINCXX / WINDRES as the hubtool's Makefile allows).
set -euo pipefail
[[ $# == 2 ]] || { echo 'Usage: build-hubtool.sh REPOSITORY OUTPUT_DIR' >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck disable=SC1091
source "$here/versions.env"
repository=$1
git -C "$repository" cat-file -e "$HUBTOOL_REVISION^{commit}" 2>/dev/null \
	|| { echo "build-hubtool: $repository does not have $HUBTOOL_REVISION" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
git -C "$repository" archive "$HUBTOOL_REVISION" | tar -x -C "$work"
make -C "$work" all >&2
mkdir -p "$2"
cp "$work/build/openbf2-hubtool.exe" "$2/openbf2-hubtool.exe"
cp "$work/LICENSE" "$2/openbf2-hubtool-LICENSE.txt"
# the vendored LZMA SDK decoder (public domain) and its notice
cp "$work/third_party/lzma/README" "$2/LZMA-SDK-NOTICE.txt"
echo "build-hubtool: $2/openbf2-hubtool.exe from $HUBTOOL_REVISION"
