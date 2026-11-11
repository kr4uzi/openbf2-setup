# Validation (2026-11-10)

Host: Intel macOS, Apple clang 17, existing offline Cargo cache. Distribution
CLI, core, Rust archive and AppKit host are universal arm64/x86_64. Execution
results here are Intel results; no Apple-silicon runtime result is inferred.

## Full owner-media runs

| Run | Wall time | Result |
| --- | ---: | --- |
| Portable baseline, prototype extraction + full reference comparison | 151.16 s | Reproduced 11 RTP failures and original 125/125 acceptance; raw reference differences unchanged. |
| First fresh transactional installation | 105.823 s | Exit 0; 1,755-file final manifest; atomic generation publication. |
| Optimized fresh transactional installation | 80.208 s | Exit 0; full input hashing, effective patch chain, strict 1,755-file verification and publication. |
| Final fresh installation after lazy-handle fix | **93.142 s** | Exit 0; all 1,755 files verified and published; peak RSS 1.76 GB. |
| Retry of a stopped patch phase (before hash parallelization) | 57.194 s | Exit 0; rehashed inputs and completed journal outputs; full manifest and publication. |

The fresh optimized run starts with no staging/installed tree. It installs
3,493,000,738 bytes. Its phase starts, relative to process launch, were:

| Phase start | Seconds |
| --- | ---: |
| Input verification | 0.328 |
| DVD extraction | 10.399 |
| Preserve original renderer | 22.419 |
| Effective 1.41 patch chain | 22.442 |
| Full 1.50 overlay | 60.421 |
| Final SHA-256 verification | 73.712 |
| Complete | 80.208 |

These are local wall-clock measurements, not a controlled retail-installer
benchmark. The baseline also hashes the entire reference union, whereas
`install` checks its shipped manifest and has no reference dependency. Do not
claim a direct percentage improvement over the old prototype or retail patcher
from these different workloads. The earlier prototype report's ~114 seconds
used CommonCrypto and a different comparison/build arrangement.

Implemented hot-spot changes: four-way independent cabinet decompression,
1 MiB compressed-input read cache, no decompression of unselected installer
features after full source identity verification, omission of 110 obsolete
histories that have checked full 1.50 replacements, concurrent independent input
hashing, parallel final file verification, and filling a single Rust-owned RTP
buffer through a bounded C++ reader rather than copying the whole payload over
the C ABI. Cabinets/SFX never need on-disk temporary input copies.

Measured peak RSS varies: ~1.03 GB on the early resume, 1.40 GB on the first
fresh run, and 1.76 GB on the optimized fresh run. Removing the redundant RTP
input copy did not establish a lower end-to-end RSS peak; history buffers and
allocator retention remain work. The current diff engine is in-memory, with a
1 GiB RTP-input cap, and cancellation between histories. Do not call it fully
streaming or bounded to a small memory budget.

## Checks

- 59 C++ synthetic checks passed: bounded input/reader callbacks, path/device
  names, SFX/cabinet truncation, MD5/SHA-256 vectors (including a million-byte
  streaming vector), corruption, strict final manifest inventory, cancellation,
  key format, symlink rejection, lock exclusion/release, atomic replacement and
  300 lazily opened input views. All 59 also passed with a 64-descriptor limit.
- The same 59 checks passed under ASan + UBSan on x86_64, with no diagnostic.
  The Rust dependency itself was not sanitizer-instrumented.
- Offline pinned Rust tests: one codec unit test and ten integration tests
  passed. Its optional media harness did not run; C++ exercised the real media.
- The full union reference comparison passed all 125 acceptance paths and was
  repeated on the final output with a 64-descriptor limit. Raw
  counts: 1,742 matches, 12 extras, five missing, one intentional vendor/local
  size difference, no same-size hash difference. See correctness.md.
- Publication fault injection on a real verified scratch generation passed:
  failing just before Current.txt replacement retained the old pointer and old
  generation; retry reverified and recovered the durable publication intent.
- Actual SIGINT during DVD extraction returned 1 after 11.6 seconds, left no
  active receipt and no `.openbf2-part` file, and retained completed journal
  entries for resume. Re-running the same command completed all phases, verified
  all 1,755 files and published successfully in 82.359 seconds (exit 0).
- All 16 vendored rtptool snapshot files matched the owner's pinned Git revision;
  that external checkout remained unchanged.
- Universal `lipo -archs` checks passed for the core/Rust archives, CLI, tests and
  app host. Ad-hoc signature verification passed on the packaging staging copy.
- `hdiutil create` failed with `Device not configured`; no DMG was created.
- AppKit smoke launch aborted in macOS `_RegisterApplication` during
  NSApplication initialization, before the wizard's UI code. No screen snapshot
  or interactive validation was obtained in this managed session.
- Whitespace checks include untracked files (the new repository has no tracked
  implementation files yet); ordinary `git diff --check` alone is insufficient.

Release gates still include real desktop wizard testing, mounted-DVD physical
media, Apple-silicon runtime, Windows/Linux compilation/runtime, client lock and
launch integration, ENOSPC/power-loss and adversarial same-user traversal tests,
DMG creation, Developer ID signing, notarization and fresh Gatekeeper launch.

## Round 3 (2026-11-10)

The universal build and all three CTest suites pass: 90 core checks, 55 session
checks and synthetic packaging. Both actual owner-disc session checks pass
edition detection, complete patch hashing/progress, both wrong-slot diagnoses
and pre-staging zero-space rejection. Fresh production CLI installs passed
1,755/1,755 BF2 and 3,030/3,030 Deluxe identities and published atomically.
The actual assistant-only app also passes build-dmg.sh --check-only architecture
and staged signature verification. GUI, SF runtime/independent reference, DMG
and Apple-silicon runs are not claimed. The new plaintext key rows build; their
interactive smoke check remains pending in a normal desktop session.

See [disc-editions.md](disc-editions.md),
[patch-verification.md](patch-verification.md), and PROGRESS.md for exact evidence
and limits. Only metadata from these content checks is retained. This round did
not repeat the unchanged Rust tests or the earlier sanitizer/reference tests.
