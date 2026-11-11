# Native setup prototype

Historical prototype snapshot. Current build, decisions and results are in
[README](../README.md), [setup-design.md](setup-design.md) and
the project notes. Original repository links below are historical.

`[CHOSEN, not recovered]` Owner-requested macOS port work, 2026-11-10.
See [design](../../../docs/macos-setup-design.md) and
[measured results](../../../docs/macos-mac-setup.md). This is a diagnostic
extraction prototype, not a distributable installer. It never stores a CD key,
publishes installed data, launches the game or runs the patch EXEs.

Build from the repository root with the existing toolchain/cache:

```sh
cmake --preset macos-x64 -B out/build/mac-setup
cmake --build out/build/mac-setup --target bf2_setup bf2_setup_checks -j4
out/build/mac-setup/tools/macos/setup/bf2_setup_checks out/mac-setup-fixtures
```

The CLI accepts these commands:

```text
bf2_setup iso IMAGE
bf2_setup sfx PATCH
bf2_setup prototype ISO PATCH141 PATCH150 SCRATCH REFERENCE ACCEPTANCE
bf2_setup compare SCRATCH/game REFERENCE ACCEPTANCE
```

Create `out/mac-setup-scratch` first. `SCRATCH` must be a new direct child of
that directory in the configured source worktree. Pass the path-only
`tools/macos/setup/acceptance-125.txt` as `ACCEPTANCE`. The owner-only input
paths are supplied by the owner; no
source game bytes are checked in here.

`prototype` extracts the selected English disc features, attempts all 394 BF2
1.41 records, overlays all 274 selected full 1.50 paths and compares the full
union of files with the reference. Unsupported cabinet variants, malformed
ranges, invalid paths and content checksums fail visibly. Individual 1.41
application errors are retained while later phases run, so acceptance and the
full comparison can be reported independently. **Any such error or full-tree
difference returns exit 1.** The current owner corpus therefore returns 1 even
though all 125 acceptance paths match. There is no checksum bypass.

`SCRATCH/patch141.tsv` and `SCRATCH/comparison.tsv` contain metadata. Preserve
those reports separately if needed, then delete the entire transaction scratch
tree, including extracted game bytes. Source files and the reference are only
opened read-only. `compare` also restricts its output report to a transaction
under this worktree's scratch directory.

The C++ reader is our MIT implementation of the verified format notes from
`agent/installer-probe:tools/macos/installer/formats.py`; unshield was absent
locally. zlib and CommonCrypto come from the macOS SDK. No Wine, Python,
unshield executable, package manager or system Rust installation is required
at runtime.

`vendor/rtptool` is an unmodified source subset of the local owner's fork at
`101433150a37001c74823659f56391723378e0b0` (`bf2-0x0320`), exported from
the owner's fork with `git archive`. Included: source, tests,
Cargo manifests/lockfile, README, MIT license and `SPEC-0x0320.md`. The original
upstream is [bwrsandman/rtptool](https://github.com/bwrsandman/rtptool).
`rtp-bridge` supplies the separate C ABI; the vendor remains unchanged.

The bridge lockfile resolves only rtptool, thiserror and its proc-macro build
dependencies. The CMake build uses `--offline --locked` and a build-local
target directory. Required crates must already be cached. Notices for the
locked crates are in `THIRD-PARTY-NOTICES.txt`. The vendored unit/synthetic
checks can run without proprietary media:

```sh
cargo test --offline --locked --no-default-features \
  --manifest-path tools/macos/setup/vendor/rtptool/Cargo.toml \
  --target-dir out/build/mac-setup/cargo-tests
```

The universal preset builds each Rust target, then uses `lipo` for the static
archive; CMake compiles both C++ slices. Both Rust standard libraries must be
present already. The arm64 Rust target is absent on this machine; the universal
link fails explicitly and no automatic install/download is attempted.
