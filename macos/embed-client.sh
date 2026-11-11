#!/bin/bash
# Read-only source artifact. Only these three native files may enter the bundle.
set -euo pipefail
[[ $# == 2 ]] || { echo 'Usage: embed-client.sh SOURCE_DIRECTORY BUNDLE_MACOS_DIRECTORY' >&2; exit 2; }
source_dir=$1
destination=$2
fail() { echo "$*" >&2; exit 1; }
[[ -d "$source_dir" && ! -L "$source_dir" && -d "$destination" && ! -L "$destination" ]] || fail 'Expected real source and destination directories'
[[ "$(cd "$source_dir" && pwd -P)" != "$(cd "$destination" && pwd -P)" ]] || fail 'Source and destination must differ'
# Release imports must be universal. SETUP_CLIENT_ALLOW_THIN=1 is a developer
# override for an engine build of the host architecture only; build-dmg.sh still
# rejects such a bundle, so it can never reach a disk image.
required_archs='arm64 x86_64'
if [[ "${SETUP_CLIENT_ALLOW_THIN:-}" == 1 ]]; then
  required_archs=$(/usr/bin/uname -m)
  echo "WARNING: accepting a $required_archs-only client artifact (developer build, not distributable)" >&2
fi
for item in bf2_client libdice_py.dylib libaidll.dylib; do
  binary="$source_dir/$item"
  [[ -f "$binary" && ! -L "$binary" ]] || fail "Missing regular native artifact: $item"
  kind=$(/usr/bin/file -b "$binary")
  if [[ "$item" == bf2_client ]]; then
    [[ "$kind" == *Mach-O*executable* && -x "$binary" ]] || fail "Expected executable Mach-O: $item"
  else
    [[ "$kind" == *Mach-O*dynamically\ linked\ shared\ library* ]] || fail "Expected native dylib: $item"
  fi
  # shellcheck disable=SC2086
  /usr/bin/lipo "$binary" -verify_arch $required_archs || fail "Expected architectures ($required_archs): $item"
  for arch in $required_archs; do
    while IFS= read -r dependency; do
      case "$dependency" in
        /usr/lib/*|/System/Library/*|@rpath/libdice_py.dylib|@loader_path/libdice_py.dylib|@executable_path/libdice_py.dylib|@rpath/libaidll.dylib|@loader_path/libaidll.dylib|@executable_path/libaidll.dylib) ;;
        *) fail "Unsupported dependency in $item ($arch): $dependency" ;;
      esac
    done < <(/usr/bin/otool -arch "$arch" -L "$binary" | /usr/bin/awk '/^[[:space:]]/ {sub(/^[[:space:]]+/, ""); sub(/ \(compatibility.*/, ""); print}')
    paths=$(/usr/bin/otool -arch "$arch" -l "$binary" | /usr/bin/awk '$1 == "cmd" && $2 == "LC_RPATH" {getline; getline; sub(/^ *path /, ""); sub(/ \(offset.*/, ""); print}')
    while IFS= read -r rpath; do
      [[ -z "$rpath" || "$rpath" == '@loader_path' || "$rpath" == '@executable_path' ]] || fail "Unsupported rpath in $item ($arch): $rpath"
    done <<< "$paths"
    if [[ "$item" == bf2_client ]]; then
      [[ "$paths" == *'@loader_path'* || "$paths" == *'@executable_path'* ]] || fail "Missing sibling-library rpath in $item ($arch)"
    fi
  done
  [[ ! -L "$destination/$item" ]] || fail "Symlink destination: $item"
done
for item in bf2_client libdice_py.dylib libaidll.dylib; do
  /bin/cp "$source_dir/$item" "$destination/$item"
done
echo "Embedded bf2_client, libdice_py.dylib and libaidll.dylib ($required_archs; source unchanged)."
