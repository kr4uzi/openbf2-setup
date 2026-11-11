# mac-setup progress and validation

Historical prototype snapshot. Current build, decisions and results are in
[README](../README.md), [setup-design.md](setup-design.md) and
the project notes. Original repository links below are historical.

`[CHOSEN, not recovered]` Owner-requested native macOS installation work, based on
the decisions supplied on 2026-11-10. The owner's request substitutes for an issue
under [macos-port.md](macos-port.md). Work began with a clean worktree on
`agent/mac-setup` from `fc2f5cfe`. Changes remained uncommitted for review.
No engine behavior was recovered or changed, and no IDA/Ghidra recovery claim is
made.

## Delivered

- [macos-setup-design.md](macos-setup-design.md): the in-app wizard screens/states,
  extraction/patch pipeline, native modules, vendoring/builds, bundle layout,
  signing/notarization, transactional failure handling and both data locations.
  Recommendation: fixed per-user Application Support game data, leaving the app
  immutable. The owner still decides the data location.
- [tools/macos/setup](../tools/macos/setup/README.md): C++ ISO/Joliet, SFX and
  InstallShield format-9 readers; bounded read-only input views; raw DEFLATE and
  stored size/MD5 checks; fresh private staging; selected English feature mapping;
  native rtptool C ABI; CLI and full-tree SHA-256 comparison.
- A pinned, unmodified MIT rtptool source subset from the owner's local
  `bf2-0x0320` branch at `101433150a37001c74823659f56391723378e0b0`. A separate Rust
  crate exposes opaque handles, path enumeration, checked history application
  and allocated-buffer release. No checksum-bypass option.
- CMake targets `bf2_setup_core`, `bf2_setup`, `bf2_setup_rtp` and
  `bf2_setup_checks`. All are independent of the engine source manifests and
  excluded from the default build. The CMake integration is macOS-only because
  this prototype uses macOS CommonCrypto and the SDK zlib. No shared engine
  platform conditionals were introduced.
- [Code/Tests/macos_setup.cpp](../Code/Tests/macos_setup.cpp): synthetic tests for
  unsafe names, bounded reads, symlinks, malformed/truncated SFX, cabinet
  size/digest/compression failures and C ABI errors.

Unshield sources were not available locally. Following the owner's fallback,
`Containers.cpp` implements the needed cabinet reading from the verified
`agent/installer-probe` format notes. It is our MIT code, not a new general-purpose
unshield distribution. Third-party attribution and the locked Rust dependency
notices are retained. The native setup has no Wine or Python runtime dependency.

Two details discovered in the actual run are handled explicitly: cabinet link
heads use link bit 1, and the loose English readme is sourced from
`Support/English/readme.txt` while installing to `Support/readme.txt`. Its cabinet
size/MD5 is checked; no reference file is used as replacement data.

## Actual owner-media run

Inputs were the owner's own media (ISO and the two patch executables). The
reference was the owner's existing installation at
`~/Applications/Battlefield 2.app/Contents/SharedSupport/prefix/drive_c/Battlefield 2`.
All inputs and reference files were opened read-only. The final input SHA-256
values agree with the installer probe:

| Input | SHA-256 |
| --- | --- |
| BF2 Image.iso | `2116a6b7107fdf1054e3fd86282f5db02b60c01658d7a4ca13e953d747b95854` |
| 1.00 → 1.41 patch | `7bfd6236b67c0380acb522d237273d51a2764769a8f9c223098f93350bdd5c64` |
| 1.41 → 1.50 patch | `c5b150c96916a4c642daff03da3e9e52a7bccec6a7d6c6492494b57733c77977` |

The C++ CLI traversed 1,148 ISO files, parsed 17 and nine SFX entries exactly to
EOF, extracted 1,481 selected disc paths, attempted all 394 BF2 1.41 top-level
RTP records, and overlaid 274 full 1.50 destination paths. Cabinet verification
included selected and nonselected stored descriptors (553 disc and 345 1.50
extraction/check operations, including empty files and links; these are not
unique nonempty payload counts). The 1.41 SFX's BF2 RTP range was passed directly
to Rust; the small 1.41 installer-support cabinets were not needed or extracted.
The complete final chain and comparison took approximately 114 seconds on the
Intel host. No reference bytes supplied missing patch sources.

**Acceptance: 125 matched, zero mismatched, zero missing.**

This uses the independent path-only set from
`installer-probe/out/installer-probe/live-141-gap.json`, preserved as
`tools/macos/setup/acceptance-125.txt`. Every path is compared by size and
SHA-256 after the full pipeline. All reference and actual files, including files
outside the acceptance set, are hashed for the full union comparison:

| Full-tree result | Count |
| --- | ---: |
| Byte-identical by size/SHA-256 | 1,741 |
| Actual-only files | 13 |
| Reference-only files | 6 |
| Size mismatch | 1 |
| Same-size hash mismatch | 0 |
| Actual files / reference files / union | 1,755 / 1,748 / 1,761 |

The size mismatch is `dbghelp.dll` (894,464 extracted bytes versus 629,760 in the
reference). As the earlier probe observed, this reference has dbghelp backup
copies and is not a pristine vendor oracle. The six reference-only paths are:

```text
BF2Patch.log
dbghelp.backup.dll
dbghelp_original.dll
mods/bf2/Settings/UserSettings.con
patch.err
RendDX9x2.dll
```

The 13 actual-only paths are:

```text
adminutils/_socket.pyd
adminutils/_ssl.pyd
adminutils/library.zip
adminutils/python24.dll
adminutils/remoteconsole.exe
adminutils/remoteconsole.py
adminutils/setup.py
BF2ServerLauncher.exe
BF2ServerLauncherBG.bmp
mods/bf2/levels.md5
msvcp71d.dll
msvcr71d.dll
vtuneapi.dll
```

These lists identify differences; they are not a release exclusion policy. A
reviewed final manifest must distinguish required native-game data, historical
installer/server tools and reference-local changes before installation can be
accepted.

The BF2 RTP phase produced **236 files, 147 deleted/absent records and 11 errors**:

```text
mods/bf2/levels.md5
python/bf2/stats/medal_data.py
LinuxReadmes/readme-linux.txt
00000001.TMP
ArchiveScripts/Client.lst
demo.cmd
demo_documentation.txt
dice_TextureAtlasTool.exe
FragAnalyzer.exe
makeMiniMap.con
ArchiveScripts/Exclude.lst
```

Ten have no matching supplied source version. `levels.md5` fails the terminal
DELETE source checksum; it is retained in staging rather than silently removed.
These are outside the 125-path set and agree with the fork's documented limits.
The prototype logs every failure and returns **exit 1** after the later overlay
and comparison. It does not publish an installation or claim full-record success.

All extracted game content, including two preliminary incomplete attempts, was
deleted afterwards. `out/mac-setup-scratch/` is empty. Only logs and metadata
remain under the ignored `out/build/mac-setup/`, including:

- `extraction-3.log`, `comparison-repeat.log` and `input-sha256.txt`;
- `results/comparison.tsv`: all 1,761 union paths, sizes, SHA-256s and statuses;
- `results/patch141.tsv`: all 394 record outcomes.

No game content, media, key, profile or extracted script is in Git. Neither the
original game folder, `openbf2-ref`, `bf2server`, the rtptool clone nor any other
worktree was modified. No software was installed or downloaded, no game/server
was run, and no commit, push or pull request was created.

## Build and checks

Environment: macOS 15.7.7 Intel x86_64, Apple clang 17, Command Line Tools,
Unix Makefiles, system zlib/CommonCrypto, existing offline Cargo cache.

| Check | Result |
| --- | --- |
| `cmake --preset macos-x64 -B out/build/mac-setup` | Pass. |
| `cmake --build out/build/mac-setup -j4` | Pass, including the dedicated `bf2` strict link and existing vtable gates. No server runtime test. |
| `cmake --build out/build/mac-setup --target bf2_setup bf2_setup_checks -j4` | Pass; native x86_64 CLI and test executable strictly link against the Rust static library. |
| Native setup synthetic checks | 30 passed. |
| Same C++ checks with ASan + UBSan | 30 passed; no diagnostic. Rust dependency itself was not sanitizer-instrumented. |
| Vendored `cargo test --offline --locked --no-default-features` | One library unit test plus ten synthetic integration tests passed. Optional owner-media Rust harness did not run; the actual media pipeline was exercised by C++. |
| Actual ISO → disc cabinets → checked BF2 RTPatch → 1.50 cabinets → full SHA-256 comparison | Completed; 125/125 acceptance matches, full differences and eleven RTP failures reported above; expected exit 1. |
| Repeat full comparison after output-report confinement change | Same full/125-path results; expected exit 1. |
| Forbidden output, swapped patch inputs, forbidden comparison-report location | All rejected with exit 1 before any transaction/output was created. |
| arm64 C++ compilation | `Containers.cpp`, `Installer.cpp`, `main.cpp` and the check TU cross-compile to Mach-O arm64 objects. |
| Universal preset / native setup target | Configuration passes; link cannot be built: offline Cargo reports E0463, missing `core` for `aarch64-apple-darwin`. Only the Intel Rust target is installed. No installation attempted. |
| Vendored source integrity | Every vendored file matches the pinned Git revision. |
| `git diff --check` | Pass. |

The setup target and tests are built explicitly because they are excluded from
the default build. The Rust build uses `--offline --locked`, `default-features =
false` and build-local target directories. See the design and setup README for
commands. Windows MSVC and Linux gates were not run on this Mac; engine source,
headers and image manifests were not changed.

## Remaining work and decisions

- Owner decision: confirm managed data in Application Support or choose the
  bundle option with its seal, re-signing, notarization, protection and update
  consequences documented in the design.
- arm64 Rust standard library must already be available on a build machine to
  produce the universal setup binary. The CMake path supports both slices;
  universal linkage and Intel/M2 app runtime checks remain undone.
- Implement the native wizard, callbacks, cancellation, mounted-DVD provider,
  input hash catalog, disk-space estimator, locks, durable journal, publication,
  receipts, key/profile persistence and native client launch handoff. The
  prototype deliberately only permits scratch extraction.
- Resolve/classify all eleven BF2 record errors and obtain a reviewed final
  content manifest before exposing Done/Play. The used reference does not
  establish pristine installation parity or which differing files are needed.
- Harden output traversal/exclusive file creation against concurrent same-user
  mutation, bound maximum memory use through RTP histories, and test power-loss,
  ENOSPC, permission and cancellation recovery. The prototype's file-level
  temporary rename is not a durable multi-phase installation transaction.
- Build/sign the actual universal bundle and DMG, test Gatekeeper/notarization,
  and validate signed-app self-write/App Management behavior only if the owner
  chooses bundled game data. No signing/notarization test ran here.
- Deluxe ISO, other media/locales, booster historical source variants and Special
  Forces base installation remain unvalidated. No client, server, loading,
  networking, join or playable-client path ran. No port was bound.

No production engine frontier was added, retired or bypassed. The boundaries
relevant to this change are installer research/design gaps listed above; the
prototype does not contain fake callable engine implementations.
