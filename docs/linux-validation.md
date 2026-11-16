# Linux validation

This records development validation of the Linux installer and launcher.
It does not certify a portable release or gameplay compatibility.
See [distribution](linux-distribution.md) and
[engine integration](linux-engine-packaging-addendum.md).

## Build environment

Validation used x86-64 Linux with glibc 2.43, GCC 16.2.1, Boost 1.92,
Rust 1.99.1, Qt 6.12.0 and SDL 3.4.20. These development versions differ
from the required glibc 2.35 release SDK.

Tests use temporary HOME/XDG roots, synthetic profiles and generated fixtures.
They do not need proprietary media, valid CD keys, a live microphone or a game
server. Builds use Release optimization for interactive verification performance.

## Automated results

The complete Linux CTest suite passed **10 tests**, with the optional
`key_checks_owner` test skipped because no keys were provided. The suite covers:

- Core installation services, session state and the shared application workflow.
- Qt widgets, combined ISO/DVD selection, key entry, patch replacement,
  cancellation/resumption, verification and launcher controls.
- SDL dummy capture, microphone retry/skip, process arguments and environment,
  asynchronous exit handling, installation locking and diagnostic permissions.
- Desktop-entry quoting, read-only application directories, relocation and
  preservation of synthetic user data.
- Twelve packaging cases covering ELF dependencies, runtime paths, libc
  requirements, inventories, shared-library conflicts, SDK identity and
  artifact ownership.
- Four client embedding cases covering the actual CMake build/install hooks,
  `--prefix`, `DESTDIR`, artifact replacement/removal, read-only inputs,
  invalid replacements and rejection of development release inputs.

Packaging tests were rerun successfully after adding read-only input assembly.
The Qt checks also passed an earlier AddressSanitizer run; the full final suite
has not been rerun under that instrumentation. Shell syntax, Python compilation
and diff whitespace checks passed.

Both frontends use the same SHA-256 implementation. A synthetic 64 MiB benchmark
confirmed that optimized builds substantially improve hashing throughput over
Debug builds. This measures hash computation, not full patch verification or
storage performance.

## Real client artifact checks

A native Linux development client and its dependencies were staged as a complete
engine artifact. Loader paths were normalized on staging copies; original
source-file checksums remained unchanged. CMake embedded and installed the
artifact with `SETUP_CLIENT_ALLOW_DEVELOPMENT=ON`.

`--check-client` detected the client in the build and installed trees. The
installed application was relocated to a path containing spaces and made
read-only. Client detection, ELF loader dependency resolution and synthetic
profile preservation passed. The development bundle was rejected by release
validation as intended.

These checks resolve real binary dependencies but do not transfer control to
the game. They establish **client embedding and loading prerequisites**, not
game launch, map loading or gameplay.

## Remaining acceptance

| Check | Status |
| --- | --- |
| glibc 2.35 setup and dependency build | Awaiting the pinned baseline SDK. |
| Production engine/runtime artifacts | Development artifacts checked; release inputs still required. |
| Portable archive, AppImage and Flatpak release | Packaging scripts implemented; production assembly and validation outstanding. |
| Engine workflow integration | Separate engine-repository work; see the integration contract. |
| Supported-media installation, cancel/resume and valid-key checks | Requires supported media and valid keys in an isolated test installation. |
| Game launch, map load/spawn/play, clean exit and relaunch | Not tested with the assembled application. |
| Physical microphone and game audio | Dummy capture tested; hardware acceptance outstanding. |
| Steam Deck gameplay and GPU coverage | Frontend rendering checked; gameplay acceptance outstanding. |
| Ubuntu 22.04, newer Ubuntu and Fedora | Release-package acceptance outstanding. |
| Alpine Flatpak and real portal-selected media | Outstanding; widget routing tests do not establish portal permissions. |
| Alpine portable build with gcompat | Experimental; requires separate installation and gameplay validation. |
| macOS AppKit build/runtime | Not available on the Linux builder; native macOS validation remains necessary. |

For future acceptance, record the package checksum, source revisions, OS/runtime,
display protocol, GPU/driver and each installation/gameplay result. Use isolated
test data and an unused test port. Never publish keys, profiles or client logs.
