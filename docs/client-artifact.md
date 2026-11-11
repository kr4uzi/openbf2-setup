# Native client artifact contract

The setup app ("Battlefield 2.app") embeds the OpenBF2 macOS client at build
time and starts it from the Play page. The engine build supplies the client as
a directory; the setup build copies exactly three files out of it and never
modifies, re-links or re-signs the source. This page is the contract between
the two build systems.

## Directory layout

```text
<client-dir>/
  bf2_client          Mach-O executable, universal (arm64 + x86_64)
  libdice_py.dylib    Mach-O dynamic library, universal
  libaidll.dylib      Mach-O dynamic library, universal
```

Other files in the directory (maps, licences, README, the dedicated-server
`bf2`) are ignored. Symlinks are rejected. Nothing else is read.

## Requirements checked by `macos/embed-client.sh`

For each of the three files, for **both** architectures:

| Check | Rule |
| --- | --- |
| Architectures | `lipo -verify_arch arm64 x86_64` succeeds (a thin file is rejected) |
| File type | `bf2_client` is an executable Mach-O with the execute bit; the two libraries are `Mach-O ... dynamically linked shared library` |
| Load commands | every dependency is under `/usr/lib/` or `/System/Library/`, or is one of the two sibling libraries referenced as `@rpath/`, `@loader_path/` or `@executable_path/` + name |
| LC_RPATH | only `@loader_path` or `@executable_path`; `bf2_client` must have at least one of them (the libraries are loaded from the same `Contents/MacOS` directory) |
| Links | no symlink for any of the three files, and no symlink at the destination |

The current OpenBF2 macOS build already satisfies the per-architecture rules:
`bf2_client` links `@rpath/libdice_py.dylib`, `libaidll.dylib` has install name
`@rpath/libaidll.dylib`, and the rpath is `@loader_path`. No absolute
build-machine path may remain (for example a Homebrew or `/opt` library, or an
rpath into the engine's build tree): such a client would run on the build
machine only.

Release builds require universal files. `-DSETUP_CLIENT_ALLOW_THIN=ON` exists
only for developer builds on a machine whose engine build is single-arch;
`build-dmg.sh` rejects such bundles, so they can never become a disk image.

## Producing the directory from the OpenBF2 release packages

OpenBF2's `Code/Build/package_release.py --platform macos-arm64` and
`--platform macos-x86_64` each produce `openbf2-macos-<arch>-client.zip`
containing `openbf2-macos-<arch>-client/{bf2_client,libdice_py.dylib,libaidll.dylib,...}`.
The release job merges the two legs into one directory:

```sh
mkdir client-universal
for item in bf2_client libdice_py.dylib libaidll.dylib; do
  lipo -create "arm64/openbf2-macos-arm64-client/$item" \
               "x86_64/openbf2-macos-x86_64-client/$item" \
       -output "client-universal/$item"
done
chmod +x client-universal/bf2_client
```

Both legs must come from the same OpenBF2 revision. The setup build records
nothing about the engine revision; the OpenBF2 release notes should state it.

To validate a directory without building the app:

```sh
mkdir /tmp/embed-check
bash macos/embed-client.sh client-universal /tmp/embed-check && rm -r /tmp/embed-check
```

## Runtime contract (what the setup app does with the client)

- Launches `Contents/MacOS/bf2_client` with the arguments `+modPath mods/bf2`.
- Working directory: the verified game-data generation,
  `~/Library/Application Support/OpenBF2/GameData/1.50-<id>` (a complete 1.50
  tree: `mods/bf2/...`, `python/`, `pylib-2.3.4.zip`, DLL stand-ins, etc.).
- Environment: the user's environment plus `OPENBF2_DATA_ROOT=<generation>` and
  `OPENBF2_SUPPORT_ROOT=~/Library/Application Support/OpenBF2`. The client
  currently derives everything from `HOME`; the two variables are informational.
- Per-user state the client reads and writes under
  `~/Library/Application Support/OpenBF2`: `Registry.con` (the CD key line the
  setup app wrote, mode 0600), `Profiles/`, `Client.lock`, `FxNative/`
  (shader cache) and `ProtectedData.key`. Nothing is written into the app bundle.
- Runtime writes into the game-data generation observed with the 2026-11-11
  client: `RendDX9Rebuilt.log` and `mods/bf2/Settings/usersettings.con`. The
  setup app's start-up verification therefore tolerates extra regular files in
  the generation, but every manifest file must still be byte-identical, and a
  fresh installation is verified strictly. Keep client writes to new files;
  modifying installed content makes the installation unverifiable.
- The setup app holds `Setup/Install.lock` while the client runs, refuses a
  second Play and refuses to quit until the client exits, and shows a dialog
  if the client exits with a non-zero status. A clean quit must therefore exit
  with status 0.

## CI entry point

```sh
macos/build-release.sh client-universal /path/to/Battlefield-2.dmg [build-dir]
```

Configures a universal Release build with `SETUP_CLIENT_ARTIFACT` and the thin
override off, builds, runs `ctest` with the opt-in owner/media tests cleared
from the environment, runs `build-dmg.sh` (ad-hoc signing unless
`SETUP_SIGN_IDENTITY` is set), verifies the image and writes `<dmg>.sha256`.
Build prerequisites on the runner: Xcode Command Line Tools, CMake 3.24+, and
a Rust toolchain with the `aarch64-apple-darwin` and `x86_64-apple-darwin`
targets whose dependencies are already vendored/cached (the Rust build runs
`--offline --locked`). Notarisation is not part of the script; see
[distribution.md](distribution.md).
