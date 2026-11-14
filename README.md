# OpenBF2 Setup

Standalone native Battlefield 2 setup: a portable C++17 ISO/DVD, cabinet, SFX
and RTPatch pipeline, a diagnostic CLI, and a minimal macOS AppKit assistant.
No Wine, Python interpreter, Rust runtime, game content or patch payload is
required in the distributed app. Users supply their own supported English DVD
or ISO (regular BF2 or BF2 Deluxe with Special Forces) and the official
**1.0 → 1.41 → 1.50** patch chain.

The app is `OpenBF2.app`, intended for drag-and-drop into Applications. It is
both the installer and the launcher: with no installation it runs the setup
wizard, afterwards it verifies the game data and launches the game.
First launch walks through Welcome → DVD/ISO → patches → CD key → installation
→ voice setup → shader preparation → Play. Deluxe shows an "Install Special Forces" checkbox (on by default) and a
second Special Forces key row; unchecked, only the base game is installed from the Deluxe disc and no expansion key is
asked for or stored (see [disc editions](docs/disc-editions.md)). Both key rows use the independently
reconstructed [retail local CD-key check](docs/cdkey-check.md); Install requires
all required keys to validate. This does not establish online entitlement.
Each patch selector verifies the pinned size/SHA-256 in the background with progress. Data, keys and profiles live under
`~/Library/Application Support/OpenBF2`; the app bundle stays immutable.
After installation a voice-chat step, modelled on retail's BF2VoiceSetup
("BF2 BattleCommo"), asks for microphone access, shows a level meter and saves
capture volume, activation threshold and boost; see [voice setup](docs/voice-setup.md).
Play requires a separately supplied native client artifact.

## Build offline

Prerequisites already available on the builder: CMake 3.24+, C++23 compiler,
zlib development library, Boost headers (1.86 or newer, for the app and its
application checks), Cargo with the locked dependencies cached. macOS also
needs Command Line Tools and preinstalled `aarch64-apple-darwin` and
`x86_64-apple-darwin` Rust targets. Builds never install or download dependencies.

For this owner session all output must stay in the authorized scratch root:

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
linked. C++ and AppKit get the same universal architectures. No Rust compiler
is needed on an end user's machine.

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
triple is detected; Apple frameworks and AppKit are selected only on macOS.
POSIX and Windows adapters implement locking and durable publication in
`src/posix.cpp` and `src/windows.cpp`. Windows/Linux compilation and runtime are not yet validated. The
Windows UI remains future work. See [third-party provenance](third_party/README.md)
for the pinned owner-fork source and alternate checkout option.

## Reading the code

All sources are in `src/`, one flat directory. The assistant is one application
class, `Setup`, whose `Launch` coroutine reads top to bottom as the whole program.
It knows nothing about AppKit:

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
| `window.h`, `window.mm` | what the application needs from a window (pages, events, progress) and its AppKit implementation |
| `installer` | disc and patch checks, installation and verification on the worker threads |
| `store` | CD keys and voice defaults below the support root |
| `client.h`, `client.mm` | the game process: started with NSTask, awaited until it quits |
| `microphone.h`, `microphone.mm` | microphone permission and the level meter (AVFoundation) |
| `main.mm` | puts the pieces together, runs the window, holds the two UI checks |
| `media`, `cabinet`, `edition`, `patch`, `install`, `digest`, `key_check` | the portable setup core |
| `session` | Registry.con, receipts, voice file, launch options and launch arguments as plain functions |
| `mods` | the mods of an installed game and their names, from `mod.desc` and the game's localization |
| `platform.h`, `posix.cpp`, `windows.cpp` | locks, flushes, atomic replace |

The application needs Boost.Asio (header-only, 1.86 or newer; `-DSETUP_BOOST_ROOT=DIR`
for a header tree which is not installed). The core, the CLI and their checks build
without it. A Windows front end would implement `window.h`, `client.h` and
`microphone.h` again and reuse everything else.

`application_checks` links the real `src/setup.cpp` against `tests/fake.cpp`, which
implements `Window`, `Installer`, `Store`, `Client` and `Microphone` on top of a
described machine and a scripted user. It covers the order of the pages, Back,
key gating, the Special Forces checkbox, wrong media, Cancel and resume, failures
and Play. It runs on Linux and macOS. It does not cover AppKit, the media formats
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
every repository. See [edition criteria](docs/edition-criteria.md) for the owner's
two retained exports, full differences and the current detection rule.

`prototype` and `compare` are diagnostics: their report output is confined to
`SETUP_SCRATCH`. `prototype` requires a fresh direct child there. `compare`
uses `tests/acceptance-125.txt` and deliberately returns 1 for any full-tree
reference difference, including documented reference-local differences. Reference
bytes are never replacement sources. Logs contain filenames and integrity
metadata, never a CD key.

The supported input hashes, resolved patch outcomes and intentional differences
are documented in [correctness](docs/correctness.md). The contracts cover the
observed English BF2 and Deluxe discs and the two pinned official patches.
Other editions/locales and exact Windows installer side effects are unvalidated.

## App and DMG

The app is built at `build/macos/OpenBF2.app`. Mounted DVD folders and ISO
files both work through the same cabinet pipeline; the app never mounts an ISO.
Drag-and-drop and Choose are available for the disc and both patches. Each key row has five visible text
fields supporting whole-key paste, auto-advance and backspace within that row. Progress/cancellation
run through the C++ callbacks on a worker thread, with UI updates on the main
thread. Existing content is reverified before Play is enabled.

```sh
macos/build-dmg.sh "$scratch/build/macos/OpenBF2.app" "$scratch/OpenBF2.dmg"
```

The script audits every Mach-O for both architectures, ad-hoc signs a staging
copy by default, verifies it, adds an Applications shortcut and invokes
`hdiutil create`. It never runs notarization. Here hdiutil failed with
`Device not configured`; the app and signature audit built successfully.
See [distribution and client contract](docs/distribution.md) before release.
Use `--check-only` as the third script argument to stage/sign/verify without
creating a disk image. This path is tested with synthetic client code.

A developer UI smoke mode renders the nine screens into PNG files and checks key
paste and the voice controls. It shows no window and does not read an installation,
touch the microphone, launch anything or save anything:

```sh
"$scratch/build/macos/OpenBF2.app/Contents/MacOS/OpenBF2" \
  --smoke-test "$scratch/ui-smoke"
```

The documents under `docs/` record the design, the correctness evidence and the
validation results; `docs/client-artifact.md` is the contract for the embedded
native client.

Current media support and evidence: [disc editions and SF patch requirements](docs/disc-editions.md),
[official patch pins and verification](docs/patch-verification.md), and
[retail CD-key check and provenance](docs/cdkey-check.md). No third-party key tool
was copied or consulted. Interactive wizard and SF client validation
remain desktop checks; this is not a release-ready playable client.
