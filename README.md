# OpenBF2 Setup

Standalone native Battlefield 2 setup: a portable C++17 ISO/DVD, cabinet, SFX
and RTPatch pipeline, a diagnostic CLI, and one Qt 6 Widgets assistant for macOS, Linux and Windows.
No Wine, Python interpreter, Rust runtime, game content or patch payload is
required in the distributed app. Users supply their own supported English DVD
or ISO (regular BF2, Deluxe or Complete Edition) and the official
**1.0 → 1.41 → 1.50** patch chain. Complete Edition supplies 1.41 on its disc:
setup selects and verifies it automatically, showing **Included on disc**.
Only 1.50 needs to be selected separately for that edition; it includes Euro
Force and Armored Fury.

On Linux, run `bin/OpenBF2`; `--launcher` opens settings and `--install-desktop`
adds a user menu entry. Data lives in `$XDG_DATA_HOME/OpenBF2`, falling back to
`~/.local/share/OpenBF2`. See [Linux builds and distribution](docs/linux-distribution.md)
and the [validation record](docs/linux-validation.md). The Linux release scripts
require a separately built glibc 2.35 engine/SDK; a certified release package is
not yet available.

On macOS the app is `OpenBF2.app`, intended for drag-and-drop into Applications. It is
both the installer and the launcher: with no installation it runs the setup
wizard, afterwards it starts the game right away, with no window.
Hold the Option (⌥) key while opening the app to get its launcher page instead,
with the start-up options, Play, the voice setup and "Verify Game Data…"; the
page has "Open launcher on startup" to show it every time. Every start compares sizes and
modification times of the installed files against the record of the last full
verification, which takes a moment; when that comparison fails or no record
exists, the launcher page opens with a note instead, and the full check of every
file runs only from its button.
First launch walks through Welcome → DVD/ISO → patches → CD key → installation
→ voice setup → Play. Deluxe and Complete Edition show an "Install Special Forces" checkbox (on by default) and a
second Special Forces key row; unchecked, only the base game is installed from the Deluxe disc and no expansion key is
asked for or stored (see [disc editions](docs/disc-editions.md)). Both key rows use the independently
reconstructed [retail local CD-key check](docs/cdkey-check.md); Install requires
all required keys to validate. This does not establish online entitlement.
Instead of the disc, the welcome page can use a Battlefield 2 1.5 installation which is on the
computer already (Steam, a retail disc, Wine or Proton) in place, with its mods: the game data
then links into that folder, its game files are checked, and nothing there is copied or changed
(see [existing installation](docs/existing-installation.md)).
Each patch selector verifies the pinned size/SHA-256 in the background with progress. Data, keys and profiles live under
`~/Library/Application Support/OpenBF2`; the app bundle stays immutable.
After installation a voice-chat step, modelled on retail's BF2VoiceSetup
("BF2 BattleCommo"), asks for microphone access, shows a level meter and saves
capture volume, activation threshold and boost; see [voice setup](docs/voice-setup.md).
The start-up options (intro movies, fullscreen or a window size, the mod, a server
to join with its password and a player name, or a free-form command line behind a
lock) only ever go
on the client's command line, as retail's shortcut switches did; the app writes no
file of the game for them. Play requires a separately supplied native client artifact.

## What gets installed

Everything lives in `~/Library/Application Support/OpenBF2`; the app bundle is
never written to.

| Path | Content |
| --- | --- |
| `GameData/1.50-<id>/` | one complete, patched installation, a "generation": `mods/bf2`, `python`, the DLL stand-ins and so on. The client runs with it as its working directory and writes its logs and saved settings there. A new installation makes a new generation and switches to it; old ones are kept. |
| `Setup/Current.txt`, `Setup/Receipt-<id>.txt` | which generation is current and which edition it is |
| `Setup/Verified-<id>.tsv` | size and time of every installed file after the last full verification; the quick check at every start compares against it |
| `Setup/Launch.txt` | the start-up options of the launcher page |
| `Setup/work/` | an installation in progress; it resumes after a cancel and is gone once published |
| `Registry.con` | the CD keys in retail's registry line format, mode 0600; the client reads it |
| `Profiles/` | the game's own profiles, plus `BattleCommoDefaultUserSettings.con` from the voice setup |
| `CustomMods/` | your own mods, linked into the generation's `mods/` at every start |
| `mods/`, `FxNative/`, `logocache/`, … | the client's caches |

Five installed variants exist. A regular disc installs 1,755 files. A Deluxe disc with
"Install Special Forces" checked installs 3,030 files, with Special Forces
playable as the `xpack` mod. A Deluxe disc with the box unchecked installs the
base game alone, 1,749 files, and asks for no expansion key. Complete Edition
installs 3,058 files with Special Forces, or the regular 1,755-file contract
without it. Patch 1.50 supplies a 19-file `mods/xpack` subset even without SF;
that subset has no levels and is not offered as a mod. Details and evidence:
[disc editions](docs/disc-editions.md).

## Build offline

Prerequisites already available on the builder: CMake 3.24+, a C++23 compiler
(the sources use the C++20 library; verified with Apple clang 17
and Boost 1.92), zlib development library, Boost headers (1.86 or newer, for the app and its
application checks), Cargo with the locked dependencies cached, and for the
application Qt 6 Widgets and SDL3 3.2 or newer. macOS also needs Command Line Tools,
preinstalled `aarch64-apple-darwin` and `x86_64-apple-darwin` Rust targets, a
**universal** Qt 6.5 or newer (the official binaries, e.g. installed with
`aqtinstall` into `~/Qt/<version>/macos`; Homebrew's Qt is single-architecture, and
Qt 6.8.3 no longer links against the current macOS SDK) and a universal SDL3 (a
static, audio-only build from the release source is enough). Pass their locations
with `-DSETUP_QT_ROOT=DIR` and `-DSETUP_SDL3_ROOT=DIR` when they are not installed
system-wide. Builds never install or download dependencies.

`ci/fetch-dependencies.sh PREFIX` fetches the pinned versions of `ci/versions.env`
(Boost headers, SDL3, and on macOS the universal Qt), each checked against its hash,
and prints the matching `-DSETUP_..._ROOT` options; `CMakePresets.json` has `macos`
and `linux` presets that read the same roots from the environment. The workflow in
`.github/workflows/ci.yml` builds and tests both platforms with them.

Use a separate directory for build and test output:

```sh
scratch=/path/outside/the/repo/openbf2-setup-build   # all build and test output stays here
cmake -S . -B "$scratch/build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' -DSETUP_SCRATCH="$scratch"
cmake --build "$scratch/build" -j4
ctest --test-dir "$scratch/build" --output-on-failure
```

Targets: `bf2_setup_core`, `bf2_setup_platform`, `bf2_setup`,
`bf2_setup_checks`, `bf2_setup_rtp`, and macOS `OpenBF2`. Both Rust slices
are built with `--release --offline --locked`, merged with `lipo` and statically
linked. C++ and the Qt front end get the same universal architectures; the Qt
frameworks and plugins are deployed into the bundle with `macdeployqt` and signed with
it. No Rust compiler is needed on an end user's machine.

For a release build driven by CI, `macos/build-release.sh CLIENT_DIR OUT.dmg`
configures, builds, tests and packages in one step; the client directory
contract is in [docs/client-artifact.md](docs/client-artifact.md).

Optional `-DSETUP_CLIENT_ARTIFACT=/path/to/mac-metal/bin_macos` imports only the
existing universal `bf2_client`, `libdice_py.dylib` and `libaidll.dylib`, all into
Contents/MacOS for sibling-library loading. Add `-DSETUP_CLIENT_ALLOW_THIN=ON` only
for a developer build whose engine artifact exists for the host architecture
alone; the DMG script still rejects such a bundle. Build all targets or
`bf2_setup_client` to validate/import them. No client is fetched or built here.

On other hosts, configure/build the same core and CLI with CMake. Rust's host
triple is detected; Apple frameworks are selected only on macOS.
POSIX and Windows adapters implement locking and durable publication in
`src/posix.cpp` and `src/windows.cpp`. Linux also builds the Qt frontend when
Qt 6 Widgets and SDL3 are installed; use `-DSETUP_BUILD_APP=OFF` for core/CLI only.
Linux supports the same `-DSETUP_CLIENT_ARTIFACT=DIR` CMake option, using the
`bin/`, `lib/`, `share/` artifact layout. Builds and installs embed that artifact;
release packaging also supplies the matching Qt runtime. See the
[Linux artifact/build contract](docs/linux-distribution.md#development-build)
for prerequisites, the explicit development override and release commands.
Linux checks and remaining runtime acceptance are recorded separately. Windows
builds with MSVC 2022, Qt 6 (msvc2022_64) and SDL3 into an application folder; see
[Windows build and layout](docs/windows-distribution.md). See [third-party provenance](third_party/README.md)
for the pinned owner-fork source and alternate checkout option.

## Reading the code

All sources are in `src/`, one flat directory. The assistant is one application
class, `Setup`, whose `Launch` coroutine reads top to bottom as the whole program.
It knows nothing about Qt or any other toolkit:

```cpp
task<int> Setup::Launch()
{
	try {
		auto installation = co_await FindInstallation();
		if (!installation)
			installation = co_await InstallGame();
		else if (!m_Store.HasKeys(installation->edition))
			co_await EnterMissingKeys(installation->edition == DiscEdition::Deluxe);

		co_await Ready(*installation);
	}
	catch (const WindowClosed&) {
	}

	m_Microphone.Stop();
	co_return 0;
}
```

| File | Content |
|---|---|
| `setup` | the application: pages, their order, what happens on Back, Cancel and failures |
| `window.h`, `window_qt.cpp` | what the application needs from a window (pages, events, progress) and its Qt Widgets implementation, written in plain C++ (no Designer files, no QML) |
| `installer` | disc and patch checks, installation and verification on the worker threads |
| `store` | CD keys, start-up options and voice defaults below the support root |
| `launch` | the start-up options, their file and the command line they become: pure functions |
| `client.h`, `client_qt.cpp` | the game process: started with QProcess, awaited until it quits |
| `microphone.h`, `microphone_sdl.cpp` | the level meter (SDL3) and, on macOS, the microphone permission (Qt) |
| `main_qt.cpp` | puts the pieces together per platform (data folder, bundle or install layout), runs the window, holds the smoke test |
| `platform_qt.h`, `platform_mac.mm`, `platform_linux.cpp`, `platform_windows.cpp` | the few platform differences of the front end: launcher key or switch, hints, handing the foreground to the game |
| `media`, `cabinet`, `edition`, `patch`, `install`, `digest`, `key_check` | the portable setup core |
| `session` | Registry.con (Registry.dat, protected for the user, on Windows), receipts, voice file, launch options and launch arguments as plain functions |
| `mods` | the mods of an installed game and their names, from `mod.desc` and the game's localization |
| `platform.h`, `posix.cpp`, `windows.cpp` | locks, flushes, atomic replace |
| `links` | links into the mod library: symbolic links, directory junctions on Windows |

The application needs Boost.Asio (header-only, 1.86 or newer; `-DSETUP_BOOST_ROOT=DIR`
for a header tree which is not installed). The core, the CLI and their checks build
without it.

`application_checks` links the real `src/setup.cpp` against `tests/fake.cpp`, which
implements `Window`, `Installer`, `Store`, `Client` and `Microphone` on top of a
described machine and a scripted user. It covers the order of the pages, Back,
key gating, the Special Forces checkbox, wrong media, Cancel and resume, failures,
the start-up options, the plain start into the game and Play. It runs on Linux,
macOS and Windows. It does not cover the Qt widgets themselves (`qt_workflow_checks` drives the real Qt window through the same flow), the media formats
or a real installation; those keep their own checks.

## CLI

```text
bf2_setup install DISC_OR_ISO PATCH141 PATCH150 SUPPORT_ROOT MANIFEST
bf2_setup extract-disc DISC_OR_ISO NEW_OUTPUT
bf2_setup verify GAME_ROOT MANIFEST
bf2_setup iso IMAGE
bf2_setup sfx PATCH
bf2_setup records PATCH141
bf2_setup hash FILE
bf2_setup prototype ISO PATCH141 PATCH150 NEW_SCRATCH REFERENCE ACCEPTANCE
bf2_setup compare GAME_ROOT REFERENCE ACCEPTANCE
```

Use `core/content-150.tsv` for regular BF2, `core/content-deluxe-150.tsv` for
Deluxe, or `core/content-deluxe-base-150.tsv` with `--no-special-forces` for
Deluxe media without the expansion, as `MANIFEST`. The app selects the edition
automatically. These contain only paths,
sizes and SHA-256 metadata. `install` needs no installed reference. For developer
media runs use a scratch support root, never the live owner support directory.
`install` checks full input identities, extracts and patches with progress,
checks every final file (1,755 regular; 3,030 Deluxe), then switches a generation receipt atomically.
Ctrl-C cancels. Re-run the same command to resume its `Setup/work` transaction.
Changed inputs are rejected; deleting only `Setup/work` starts a fresh attempt.
Prior published generations are retained. Do not delete or modify inputs.

`extract-disc` exports every valid cabinet descriptor, including unselected
languages and installer features, to a fresh directory. `manifest.tsv` records
the exported path, declared size, cabinet path/group, MD5 and storage source;
descriptor-index directories preserve duplicate names. Loose payloads are
resolved from the disc and checked against cabinet metadata. Invalid descriptors
and empty installer self-references get manifest rows without invented payloads.
This diagnostic never patches or executes content. Keep its game bytes outside
every repository. See [edition criteria](docs/research/edition-criteria.md) for the owner's
two retained exports, full differences and the current detection rule.

`prototype` and `compare` are diagnostics: their report output is confined to
`SETUP_SCRATCH`. `prototype` requires a fresh direct child there. `compare`
uses `tests/acceptance-125.txt` and deliberately returns 1 for any full-tree
reference difference, including documented reference-local differences. Reference
bytes are never replacement sources. Logs contain filenames and integrity
metadata, never a CD key.

The supported input hashes, resolved patch outcomes and intentional differences
are documented in [correctness](docs/research/correctness.md). The contracts cover the
observed English BF2, Deluxe and Complete Edition discs and the two pinned official patches.
Other editions/locales and exact Windows installer side effects are unvalidated.

## App and DMG

The app is built at `build/macos/OpenBF2.app`. Mounted DVD folders and ISO
files both work through the same cabinet pipeline; the app never mounts an ISO.
Drag-and-drop and Choose are available for the disc and external patches;
Complete Edition's included 1.41 is automatic. Each key row has five visible text
fields supporting whole-key paste, auto-advance and backspace within that row. Progress/cancellation
run through the C++ callbacks on a worker thread, with UI updates on the main
thread.

```sh
macos/build-dmg.sh "$scratch/build/macos/OpenBF2.app" "$scratch/OpenBF2.dmg"
```

The script audits every Mach-O for both architectures, ad-hoc signs a staging
copy by default, verifies it, and builds a drag-and-drop image: the app on the
left, the Applications folder on the right, an arrow between them. The window
layout is a committed file, so no Finder automation runs. It never runs
notarization. See [distribution and client contract](docs/distribution.md)
before release. Use `--check-only` as the third script argument to
stage/sign/verify without creating a disk image. This path is tested with
synthetic client code.

A developer UI smoke mode renders every screen into PNG files and checks key
paste, the launcher options and the voice controls. It shows no window and does not read an installation,
touch the microphone, launch anything or save anything:

```sh
"$scratch/build/macos/OpenBF2.app/Contents/MacOS/OpenBF2" \
  --smoke-test "$scratch/ui-smoke"
```

## Documents

`docs/` holds the maintainer documents, each with a line saying who it is for:
[setup design](docs/setup-design.md), [client contract](docs/client-artifact.md),
[distribution and signing](docs/distribution.md), [disc editions](docs/disc-editions.md),
[Windows build and layout](docs/windows-distribution.md), [official patch pins](docs/patch-verification.md), [CD-key check](docs/cdkey-check.md)
and [voice setup](docs/voice-setup.md). `docs/research/` keeps the evidence from
which the manifests and the policies were derived, with [an index](docs/research/README.md)
of when to open which. No third-party key tool was copied or consulted.
