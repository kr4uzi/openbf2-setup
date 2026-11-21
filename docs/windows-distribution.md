# Windows build and layout

*For:* Maintainers building or packaging the Windows application. *Covers:* the build, the application
folder, the installer, the DirectX runtime of the 64-bit client, where an installation keeps its data, and what
is not done yet.

## Application

Windows uses the same Qt 6 Widgets front end as macOS and Linux (`src/window_qt.cpp`, `client_qt.cpp`,
`microphone_sdl.cpp`, `main_qt.cpp`). `src/platform_windows.cpp` holds the Windows differences:

- holding **Shift** while OpenBF2 starts opens the launcher page instead of starting the game;
- the microphone settings button opens the Windows privacy settings (`ms-settings:privacy-microphone`);
  Windows asks no per-application permission for a desktop program;
- before the game starts, OpenBF2 allows it to take the foreground (`AllowSetForegroundWindow`).

The launcher page offers a **Client** choice on Windows: **64-bit** (the default) and **32-bit, for
PunkBuster servers**. PunkBuster exists only as 32-bit DLLs, so the 64-bit client cannot join servers which
require it. The choice is stored as `client=x64` or `client=x86` in `Setup/Launch.txt`. The 32-bit choice
falls back to the 64-bit client when the application folder has no `x86\` folder.

## Build

Prerequisites on the builder: Visual Studio 2022 (MSVC x64, its CMake and Ninja), Rust with the
`x86_64-pc-windows-msvc` target and the locked dependencies fetched, Boost headers 1.86 or newer, a static
zlib, Qt 6.5 or newer for `msvc2022_64`, and SDL3 3.2 or newer; for the installer also DXSETUP's files from
Microsoft's DirectX June 2010 redist and Inno Setup 6. Builds never download anything. `ci/fetch-dependencies.sh`
(Git Bash, inside the developer environment) fetches all of them at the versions and hashes of `ci/versions.env`
and prints the CMake options.

From a Visual Studio x64 developer prompt:

```bat
cmake -S . -B C:\build\openbf2 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSETUP_SCRATCH=C:/build/scratch ^
  -DSETUP_BOOST_ROOT=C:/deps/boost -DSETUP_QT_ROOT=C:/deps/Qt/6.10.3/msvc2022_64 -DSETUP_SDL3_ROOT=C:/deps/sdl3 ^
  -DZLIB_LIBRARY=C:/deps/zlib/lib/libzs.lib -DZLIB_INCLUDE_DIR=C:/deps/zlib/include ^
  -DSETUP_CLIENT_X64=C:/clients/openbf2-windows-x64-client -DSETUP_CLIENT_X86=C:/clients/openbf2-windows-x86-client
cmake --build C:\build\openbf2
ctest --test-dir C:\build\openbf2 --output-on-failure
```

`SETUP_CLIENT_X64` and `SETUP_CLIENT_X86` are the unpacked `openbf2-windows-x64-client` and
`openbf2-windows-x86-client` packages of the engine's build. Each is optional; configuration fails when a
folder lacks one of the files the launcher requires (`BF2.exe`, `dice_py.dll`, `RendDX9.dll`,
`SwiffPlayer.dll`, `BF2Audio.dll`).

The tests on Windows: `key_checks`, `setup_checks` (the media readers, verification, locks), the
`windows_session_checks` (the protected key file, the voice defaults in Documents, junctions for mods, the
client choice), `application_checks` and the two Qt workflow checks. With the owner's own key in
`BF2_TEST_KEY`, `windows_session_checks` also saves the key and reads it back.

The developer smoke mode renders every page: `app\OpenBF2.exe --smoke-test NEW_DIRECTORY`.

With `-DSETUP_DIRECTX_REDIST=<folder>` the build copies DXSETUP's five files into `app\directx\`, each only with
its known SHA-256. With `-DSETUP_ISCC=<ISCC.exe>` the `installer` target builds
`installer\openbf2-windows-setup.exe` from `windows/OpenBF2.iss`.

The release build, as OpenBF2's workflow runs it, in the developer environment with the variables that
`ci/fetch-dependencies.sh` prints (`SETUP_BOOST_ROOT`, `SETUP_QT_ROOT`, `SETUP_SDL3_ROOT`, `ZLIB_LIBRARY`,
`ZLIB_INCLUDE_DIR`, `SETUP_DIRECTX_REDIST`, `SETUP_ISCC`):

```bat
powershell -File windows\build-release.ps1 CLIENT_X64_DIR CLIENT_X86_DIR OUTPUT\openbf2-windows-setup.exe [BUILD_DIR]
```

It configures with both client packages, builds, runs the tests (without the owner's media and key), checks the
application folder, builds the installer and writes `openbf2-windows-setup.exe.sha256` beside it.

## The application folder

The build tree's `app\` folder is the application as it is shipped, and can be zipped as it is:

| Path | Contents |
| --- | --- |
| `OpenBF2.exe` | the setup assistant and launcher |
| `Qt6*.dll`, `platforms\`, `styles\`, `imageformats\`, `generic\`, `tls\` | Qt, deployed by `windeployqt` (`tls\` for provider files downloaded over https) |
| `vcruntime140*.dll`, `msvcp140*.dll`, `concrt140.dll` | the Visual C++ runtime of `OpenBF2.exe`, app-local from the compiler's redistributable folder |
| `directx\` | Microsoft's DXSETUP with the cab of the 64-bit `d3dx9_25.dll` (see below) |
| `setup\` | the content manifests (`content-*.tsv`) |
| `licences\` | the licences of the setup, Qt, SDL3 and the Rust dependencies |
| `x64\`, `x86\` | the client packages as the engine's build supplies them |

Each client package carries its own Visual C++ runtime. The 32-bit client uses retail's `d3dx9_25.dll` from the
game folder.

## The installer

`openbf2-windows-setup.exe` (Inno Setup, `windows/OpenBF2.iss`) installs the application folder for the current
user into `%LOCALAPPDATA%\Programs\OpenBF2`, without administrator rights, adds a Start menu entry (a desktop
shortcut on request) and then starts OpenBF2, which installs the game from the user's disc. The uninstaller removes
the application folder; it asks (default no) whether to remove `%LOCALAPPDATA%\OpenBF2` (the installed game, its
settings and the CD keys) and never touches `Documents\Battlefield 2`. The installer is not code-signed yet, so
SmartScreen asks before it runs.

## The DirectX runtime of the 64-bit client

The 64-bit client needs Microsoft's 64-bit `d3dx9_25.dll` (April 2005); without it, it shows a message and quits.
`src/directx.h`:

1. After an installation, OpenBF2 looks on the user's disc for `directx\Apr2005_d3dx9_25_x64.cab` (the regular and
   the Deluxe disc carry it; any file of that name with the known SHA-256 counts), unpacks `d3dx9_25.dll` from it
   with Windows' own cabinet API, checks its SHA-256 and puts it into `x64\`. Nothing from the disc is run.
2. When the 64-bit client is started and finds neither its own copy nor the system's (System32), OpenBF2 asks
   whether to run Microsoft's DXSETUP from `directx\` (`/silent`); Windows asks for administrator rights.
   Declined or failed, the start stops with a message which names the 32-bit client as the way out.

## Where an installation keeps its data

The current-user mode needs no administrator rights and writes nothing to the registry:

| Path | Contents |
| --- | --- |
| `%LOCALAPPDATA%\OpenBF2\GameData\` | the installed game, one generation per installation |
| `%LOCALAPPDATA%\OpenBF2\Setup\` | receipts, `Launch.txt`, the verification record, the provider file the game reads (`Provider-OpenSpy.json`, `Provider-BF2Hub.json` or `Provider-Other.json`) |
| `%LOCALAPPDATA%\OpenBF2\Registry.dat` | the CD keys for the client |
| `%LOCALAPPDATA%\OpenBF2\CustomMods\` | the user's mods; each is linked into the game folder |
| `Documents\Battlefield 2\` | the game's profiles and the voice defaults, where retail keeps them |

`Registry.dat` holds the same text as `Registry.con` on macOS, protected with `CryptProtectData` for the
current user (no entropy, `CRYPTPROTECT_UI_FORBIDDEN`); the file is the raw blob. The client reads it as
described in the engine's `docs/registry-file.md`. Only the current Windows account can decrypt it.

The game finds mods only below its own `mods\` folder, so each mod of the library is linked there. Windows
creates symbolic links only with administrator rights or developer mode; directory junctions need neither,
so OpenBF2 uses junctions (`src/links.cpp`).

## Not done yet

- the all-users mode (administrator rights, the CD keys and the install path in the registry for older
  tools, a shared game folder);
- code signing of the installer and the executables (`signtool`, once a certificate exists);
- a Windows job in this repository's CI (OpenBF2's workflow builds and tests the release).
