# Native client artifact contract

The setup app (`OpenBF2.app`, which is also the launcher) embeds the OpenBF2 macOS client at build
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

The client's side of this contract is OpenBF2's `docs/macos-write-paths.md`: the
directories it uses, every file it writes and what a launcher must and must not
do. The setup app follows it as described here.

- Launches `Contents/MacOS/bf2_client` with the arguments `+modPath mods/<mod>`.
  `<mod>` is the directory the user chose on the Ready page, `bf2` by default.
  Offered is every directory below `mods/` of the generation which has a
  `mod.desc` and at least one level (`levels/<name>/info/<name>.desc`, or a
  non-empty `<importlevels>`); `xpack` only for an edition which installed
  Special Forces. The name shown is `<title>` of `mod.desc`, resolved through the
  English localization files if it is a key. The choice is kept in
  `Setup/Launch.txt` (`mod=<directory>`); no file of the game is written for it.
- Mods of the user live in `~/Library/Application Support/OpenBF2/CustomMods/<name>`,
  outside the generation, so a reinstallation keeps them. (Not `Mods`: the client's
  caches are in `mods/` of the support root, and a case-insensitive volume does
  not tell the two apart.) Whenever the Ready page is shown, the setup app makes a
  symlink `mods/<name>` in the generation for every directory of the library
  with a `mod.desc`, unless the name is taken there, and removes links of mods
  which left the library. The start-up verification tolerates symlinks directly
  in `mods/` and does not follow them. Checked on the engine side with client
  build mac-metal b39a681e: server and client ran `+modPath mods/<name>` through
  such a link (absolute target) and loaded a map. The client's compiled-effect
  cache of every mod except `xpack` is `mods/bf2/cache` in the support root, as
  in retail; a custom mod gets no cache directory of its own.
- Working directory: the verified game-data generation,
  `~/Library/Application Support/OpenBF2/GameData/1.50-<id>` (a complete 1.50
  tree: `mods/bf2/...`, `python/`, `pylib-2.3.4.zip`, DLL stand-ins, etc.). The
  generation is created by the user and stays writable by the user.
- Environment: the user's environment with `HOME` set to the home directory the
  setup app derived the support root from, so both sides look at the same files
  (a test home moves everything), and with `PYTHONPATH` and `PYTHONCASEOK`
  removed. No other variable is set.
- Support root `~/Library/Application Support/OpenBF2`: the setup app writes
  `Registry.con` (CD key lines, mode 0600; the client only reads it),
  `Profiles/BattleCommoDefaultUserSettings.con` and its own `Setup/` and
  `GameData/` directories. Everything else there belongs to the client
  (`Profiles/`, `FxNative/`, `mods/<mod>/cache/`, `screenshots/`, `logocache/`,
  `ProtectedData.key`); the setup app never deletes or replaces any of it, and
  nothing is written into the app bundle.
- The client writes into the generation: `Client.lock`, `RendDX9Rebuilt.log`,
  `settings/banlist.con` and saved settings below `mods/<mod>/settings/`. Some of
  the latter are shipped files (`maplist.con`, `serversettings.con`: the save
  targets while no profile has redirected them). The start-up verification of a
  generation therefore tolerates extra regular files anywhere and changed files
  below `mods/<mod>/settings/`. The client deletes no shipped file, so every
  manifest file must still exist, every other one must be byte-identical, and a
  fresh installation is verified strictly before the client ever runs in it.
- The setup app never deletes or replaces a generation: an installation creates a
  new one and switches `Setup/Current.txt`. Old generations stay on disk.
- Voice chat: the setup app writes
  `Profiles/BattleCommoDefaultUserSettings.con` (four plain lines; see
  [voice-setup.md](voice-setup.md)) and requests microphone access itself. The
  client is a child process of the app bundle, so macOS attributes the
  microphone permission to "OpenBF2" and the game does not prompt
  mid-match. The bundle's Info.plist carries `NSMicrophoneUsageDescription`.
- Shaders: the client builds a map's shaders into its `FxNative` cache at the
  map's first load; the setup app does not pre-build them.
- The setup app holds `Setup/Install.lock` while the client runs, refuses a
  second Play and refuses to quit until the client exits, and shows a dialog
  if the client exits with a non-zero status. A clean quit must therefore exit
  with status 0.

## CI entry point

```sh
macos/build-release.sh client-universal /path/to/OpenBF2.dmg [build-dir]
```

Relative `OUTPUT.dmg` / `build-dir` arguments are resolved against the current
directory before anything is configured (CMake and `TMPDIR` only ever see
absolute paths). Configures a universal Release build with
`SETUP_CLIENT_ARTIFACT` and the thin override off, builds, runs `ctest` with the opt-in owner/media tests cleared
from the environment, runs `build-dmg.sh` (ad-hoc signing unless
`SETUP_SIGN_IDENTITY` is set), verifies the image and writes `<dmg>.sha256`.
Build prerequisites on the runner: Xcode Command Line Tools, CMake 3.24+, the
Boost headers (1.86 or newer, found by CMake or given as `SETUP_BOOST_ROOT` in
the environment of the script), and a Rust toolchain with the `aarch64-apple-darwin` and `x86_64-apple-darwin`
targets whose dependencies are already vendored/cached (the Rust build runs
`--offline --locked`). Notarisation is not part of the script; see
[distribution.md](distribution.md).
