# Linux installer, launcher and packaging

For Linux users, builders and release maintainers. See [validation](linux-validation.md)
for what has actually run; implementation and packaging scripts are not release
certification.

## Application

Linux uses Qt 6 Widgets for the existing `Setup` coroutine. A 5 ms Qt timer polls
Asio in bounded batches on the UI thread; the existing worker pool still hashes,
extracts, patches and verifies. All seven pages share the macOS installer policy,
including supported media identities, Deluxe/Special Forces selection and local
key validation. Complete Edition support has not changed.

The installation page keeps separate progress blocks for DVD extraction, patch
1.41, patch 1.5 and final verification, as on macOS. Completed stages stay full;
the pre-install media check appears above them. Cancellation keeps its status
visible until the worker stops, and retry/resume resets the displayed stages.

The launcher follows the macOS grouping in a compact window: startup/display
preferences above the mod selector, paired server/port and player/password rows,
and a lock button beside the command line. Window dimensions appear only in
windowed mode. Unlocking the command line disables the generated options until
it is locked again. Voice Setup and Verify Game Data share the footer with Play;
all actions disable while a client or verification operation is running.

`bin/OpenBF2` starts the wizard when no installation exists and otherwise follows
the existing quick-check/direct-launch policy. `bin/OpenBF2 --launcher` opens the
settings page; “Always show this launcher” persists the preference. Run
`bin/OpenBF2 --install-desktop` to add the user-scoped menu entry and its “Launcher
and settings” action. Run it again after moving the application. Flatpak exports
its own entry. AppImage integration records the persistent AppImage path rather
than the temporary mount point. The icon is the existing original macOS icon's
embedded 256-pixel PNG, with no redrawing or game artwork.

The application is immutable during use. Persistent data is
`$XDG_DATA_HOME/OpenBF2`; empty or relative XDG values fall back to
`~/.local/share/OpenBF2`. `Setup/`, `GameData/`, `Profiles/`, `Registry.con`,
`ProtectedData.key` and `CustomMods/` retain their formats and locations across
application replacement. Each selected generation stays writable for client
logs and settings. Application replacement preserves existing user data.

QProcess receives argument vectors, runs in the current generation, and reports
startup failures and abnormal exits asynchronously. `Setup/Install.lock` stays
held through child exit; the engine retains its own `Client.lock`. HOME is the
same home used to resolve support data. Desktop/session/XDG variables are
preserved; foreign Python variables are removed, `DXVK_WSI_DRIVER=SDL3` is set,
and the application's `lib/` is prepended only in the child's library path.
Launcher diagnostics use fresh 0600 files inside 0700 `Setup/Logs`, capped at
16 MiB per launch. No command line or key is added by the launcher. Client output
can contain private data; logs are never added to packages or uploaded.

SDL3 opens a mono float recording stream and polls it on the Qt thread. Linux
does not use macOS's permission prompt. A failed open or capture is visible,
with retry and skip actions; Skip leaves existing voice defaults alone. SDL's
dummy driver is used only in tests. macOS uses the same SDL3 capture and asks for permission through Qt (`QMicrophonePermission`).

## Development build

Prerequisites: CMake 3.24+, Python 3.10+, C++23 compiler, cached Cargo dependencies, zlib,
Boost >=1.86, Qt 6 >=6.4 Widgets/Test, SDL3 >=3.2. Packaging tests also use Python
>=3.10, a C compiler, binutils and patchelf. CMake and Cargo do not download.

```sh
cmake -S . -B out/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build out/linux -j4
ctest --test-dir out/linux --output-on-failure
out/linux/bin/OpenBF2 --smoke-test out/linux/screenshots  # fresh output directory
out/linux/bin/OpenBF2 --launcher
```

The build tree carries manifests and icons. A frontend build without an engine
can install and verify data but disables Play. `-DSETUP_BUILD_APP=OFF` builds
only core/CLI checks on hosts without Qt/SDL. Set `QT_QPA_PLATFORM=offscreen`
for headless screenshots. The CTest Qt cases set it themselves and never use
live profiles, keys, media, microphones or servers.

To include the separately built client, use the same CMake option as macOS:

```sh
cmake -S . -B out/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSETUP_CLIENT_ARTIFACT=/artifacts/linux-engine
cmake --build out/linux --target OpenBF2 -j4
cmake --install out/linux --prefix /path/to/OpenBF2
```

The artifact must satisfy the release input contract below. Python and binutils
(`readelf`, `strings`) validate it before copying; the engine is never built or
downloaded here. Both a normal build and `cmake --install` embed the client.
`--prefix` and `DESTDIR` work. Rebuilding imports changed artifact files, and
reconfiguring with `-DSETUP_CLIENT_ARTIFACT=` removes only files listed in the
previous embedding inventory. Unrelated files remain; modified embedded files
cause an error rather than being silently replaced. Inputs are read-only.

Embedding checks the setup source/build directories for leaked paths. Supply
`-DSETUP_ARTIFACT_BUILD_ROOTS_FILE=/build-inputs/roots.json` to also check the
engine and SDK producer directories. This local JSON file stays outside both
the input artifacts and application directories; it is never copied into them.

For local engine testing only, add `-DSETUP_CLIENT_ALLOW_DEVELOPMENT=ON`.
This permits a newer libc/CPU and embedded debug paths without release metadata.
It still requires the complete engine layout, x86-64 ELFs, relative loader paths,
and dependency closure; raw `bin_linux/` output is not an artifact. Normalize
paths on staging copies, never the engine build. The embedding record marks this
mode, and release assembly/archive/wrappers reject it. This flag is independent
of `CMAKE_BUILD_TYPE`: an optimized Release build can still be a development
package. Normal CMake builds use the builder's Qt; use the release entry point
to bundle Qt and produce a distributable application.

`bin/OpenBF2 --check-client` reports whether the launcher's required client files
are present and executable, without opening user data or starting the game. It
does not replace the ELF audit or a gameplay test. It can run with
`QT_QPA_PLATFORM=offscreen`; the release script runs it on the assembled package.

Use `RelWithDebInfo` for interactive previews: it retains debug symbols while
optimizing the shared hashing, extraction and verification code. An unoptimized
`Debug` build is useful for debugging but makes large patch checks substantially
slower. Release packaging already requests `Release`, as on macOS.

## Release inputs and baseline

Release is offline and requires a separately provisioned Ubuntu 22.04 SDK and
engine artifact. The toolchain is pinned in [toolchain.json](../linux/toolchain.json)
and enforced by [build-release.sh](../linux/build-release.sh). The SDK must build
all linked dependencies with glibc 2.35, generic x86-64, GCC 15.2.0 and the stated
Rust/Boost versions, with source checksums and licenses recorded. Provisioning
that immutable SDK and engine CI is the dependency described in the
[engine integration contract](linux-engine-packaging-addendum.md), not an action performed by
the setup frontend build. The current local development engine cannot be a
release input.

Both input artifacts use `bin/`, `lib/`, `share/`. Ownership is:

| Producer | Files |
| --- | --- |
| Engine artifact | `bin/bf2_client`, `lib/libdice_py.so`, `lib/libaidll.so`, `lib/libdxvk_d3d9.so.0`, `lib/libSDL3.so.0`, `lib/libopenal.so.1`, PulseAudio/ALSA and the complete non-host dependency closure, engine provenance and licenses |
| Setup runtime artifact, produced from the agreed SDK | Qt libraries/plugins and their closure, SDL3 and other shared runtime libraries required by setup, runtime provenance and licenses |
| This repository | `bin/OpenBF2`, `bin/qt.conf`, setup manifests, desktop entry, icon and setup licenses; artifact validation, embedding and final package assembly |

The engine artifact must not supply Qt or replace setup-owned files. The runtime
artifact must not supply the engine executable, Python/AI or DXVK. Shared files
(for example SDL3, libstdc++ and audio libraries) must have identical hashes;
there is no winner chosen between different builds. Both metadata records must
name the same immutable SDK image by OCI digest. The SDK prefix additionally has
headers/CMake packages and is used only to build, not copied into the release.
Qt runtime construction belongs with SDK provisioning; this repository owns
its packaging requirements. The engine build owns provisioning and workflow integration.

Required Qt modules/plugins: Widgets, Gui, Core, DBus and their closure; xcb,
offscreen, a Wayland platform plugin, xdg-shell integration, and
`platformthemes/libqxdgdesktopportal.so`. Include the remaining Wayland client
integration/decoration plugins required by the chosen Qt build. Preserve the
same Qt build/version throughout. Include dynamically loaded audio backends,
not just the executable's DT_NEEDED list. GPU drivers, Vulkan/GL loaders, libc
and kernel-facing window-system libraries come from the supported host; the
auditor's explicit allowlist documents that boundary. Bundle libstdc++/libgcc
from the baseline builder. The host needs working Vulkan graphics and an audio
service; exact GPU support still requires gameplay acceptance.

Each artifact has `share/openbf2/engine-artifact.json` or
`share/openbf2/runtime-artifact.json`:

```json
{
  "schema": 1,
  "architecture": "x86_64",
  "glibc": "2.35",
  "revision": "<40 lowercase hex digits: engine or SDK source commit>",
  "sdk": "<registry/image>@sha256:<64 lowercase hex digits: immutable SDK image digest>",
  "toolchain": "<the JSON object from linux/toolchain.json, not this string>",
  "dependencies": [{
    "name": "SDL3",
    "version": "<exact version>",
    "source": "<upstream source URL>",
    "sha256": "<64 lowercase hex digits: source archive digest>",
    "license": "share/licenses/SDL3/LICENSE.txt"
  }],
  "files": {"bin/bf2_client": "<SHA-256>"}
}
```

`files` is the exact inventory of every regular file except that metadata file,
including licenses, nested modules and data. No symlinks or special files are
accepted; stage SONAME aliases as regular files. Record all dependency versions,
the immutable builder's identity and applicable source/license obligations.
Qt remains dynamically linked so the LGPL replacement mechanism is retained;
ship the actual Qt license notices and corresponding source/build information
for the chosen distribution. Do not assume a setup license covers dependencies.

`toolchain` describes the shared SDK, including setup's Rust/Boost prerequisites;
it does not require the C++ engine to use Rust or Boost. The pins remain the
proposed configuration until the engine build provisions and validates that SDK.
No SDK digest is invented or supplied by this repository. Both sides must update
the contract together if they agree different toolchain pins; glibc 2.35 remains
the release compatibility requirement.

The engine build can check its handoff before building setup:

```sh
python3 linux/package.py check-inputs --engine /artifacts/engine --runtime /artifacts/runtime \
  --build-roots-file /build-inputs/roots.json
```

The producer generates `roots.json` as a nonempty JSON array of the concrete
absolute source, build and SDK directories used to compile its inputs, for example:

```json
["/build/engine/source", "/build/engine/output", "/build/sdk/source", "/build/sdk/output"]
```

Use the actual compiler-visible paths, including any path aliases used during
the build. Do not list a general parent such as `/tmp`. The audit rejects each
listed directory and its descendants, while permitting unrelated runtime paths
such as `/tmp/SDL-tray-XXXXXX` and `/tmp/alsa-dmix-%i-%li-%li`. A directory named
`/build/foobar` does not match `/build/foo`. The roots file is local build input,
not published provenance. Missing or invalid producer roots fail release assembly.
Standalone `package.py` commands also accept repeatable `--build-root PATH`;
without roots they perform ELF/ABI/loader checks but cannot detect unknown source
directory literals. Absolute RPATHs and path-valued dependencies remain rejected
regardless of the supplied roots.

```sh
export SETUP_CLIENT_ARTIFACT=/artifacts/engine
export SETUP_RUNTIME_ARTIFACT=/artifacts/runtime
export SETUP_SDK_PREFIX=/opt/openbf2-sdk
export SETUP_BOOST_ROOT=/opt/boost-1.89.0
export SETUP_ARTIFACT_BUILD_ROOTS_FILE=/build-inputs/roots.json
linux/build-release.sh /output/OpenBF2-linux-x86_64.tar.gz /scratch/new-release
```

Use a clean committed setup checkout and fresh build/output paths. The entry
point validates the inputs before compiling, builds/tests setup with client
embedding enabled and the development override off, installs it, combines artifacts,
sets relative runtime paths on staging copies, audits every ELF, performs an
offscreen packaged UI smoke and creates the archive/checksum. Absolute source
RPATHs are rejected before normalization. Symbol-version checks inspect required
GLIBC versions and bundled providers, including GLIBCXX/CXXABI. The audit does
not execute untrusted binaries (`ldd` is not used). It checks ELF CPU properties;
build provenance and fixed compiler flags remain necessary because not every
binary declares every instruction extension in its ELF notes.

The release entry point adds setup's source/build/SDK directories to the supplied
producer roots and passes them through input validation, embedding, assembly and
archive validation. The diagnostic CLI retains its configured scratch restriction;
the graphical application does not contain that scratch directory literal.
AppImage and Flatpak wrappers accept the same environment variable to repeat the
producer-root checks when wrapping an already validated portable tree.

The installed `bin/qt.conf` selects `../lib/qt6/plugins` through a relative prefix.
`share/openbf2/release.json` records engine/setup revisions, toolchain, SDK identity and input
metadata digests; `files.sha256` records final staged bytes. Inputs are read-only.
The scripts never publish or submit PRs.

## AppImage

After portable acceptance, supply a local appimagetool and x86-64 runtime, each
with a pinned SHA-256. No download or update URL is implicit:

```sh
export APPIMAGETOOL=/tools/appimagetool
export APPIMAGETOOL_SHA256=<sha256>
export APPIMAGE_RUNTIME=/tools/runtime-x86_64
export APPIMAGE_RUNTIME_SHA256=<sha256>
linux/build-appimage.sh /scratch/new-release/OpenBF2 /output/OpenBF2.AppImage /scratch/new-appimage
```

The wrapper copies the validated tree and adds AppRun, the desktop entry and
icon. Retain the tar archive when FUSE/AppImage mounting is unavailable; extract
it and run `OpenBF2/bin/OpenBF2`. The wrapper does not change compatibility.

## Alpine through Flatpak

The manifest uses Freedesktop Platform 25.08, a glibc runtime supported through
September 2027 according to the [upstream lifecycle](https://freedesktop-sdk.gitlab.io/documentation/wiki/Releases.html).
Recheck support before publication. No native musl build is claimed.

On the builder, install Platform and SDK 25.08 for x86-64 plus flatpak-builder,
then run the offline wrapper:

```sh
linux/build-flatpak.sh /scratch/new-release/OpenBF2 /output/OpenBF2.flatpak /scratch/new-flatpak
```

It creates a local repository and downloadable `.flatpak` asset with checksum,
and records the builder's runtime commit. It does not install the application
or change remotes. On Alpine, install Flatpak and a working desktop portal, add
the Flathub remote, and install `org.freedesktop.Platform//25.08` before installing
the downloaded bundle with `flatpak install --user ./OpenBF2.flatpak`.
[Single-file bundles do not include runtime dependencies](https://docs.flatpak.org/en/latest/single-file-bundles.html).

Run `flatpak run org.openbf2.Setup` or add `--launcher`. Graphics (`dri`, X11 and
Wayland), shared IPC, PulseAudio and network are granted; there is no blanket
home/host filesystem access. On the desktop, **Choose DVD / ISO…** opens one
picker that accepts an ISO file or a DVD folder; double-click folders to browse,
or select one and click **Choose** to use it. In Flatpak, the same button offers
**ISO file…** and **DVD folder…** before opening the native portal dialog, since
the portal requires a file or directory request. Patches also use the portal.
Select the whole DVD directory, not an
individual cabinet. Dropped files must already be accessible in the sandbox.
Verify the installed portal supports selecting directories on the target desktop.

Inside Flatpak the normal XDG rule puts data at
`~/.var/app/org.openbf2.Setup/data/OpenBF2` on the host; caches live in the
corresponding app cache directory. App replacement preserves that data. This is
separate from a portable installation's default data directory; no implicit
migration or broad filesystem override is added.

The portable-build-plus-`gcompat` experiment is separate. It requires the full
installation and gameplay sequence before compatibility can be claimed. Neither
installing gcompat nor launching the first window establishes it.
