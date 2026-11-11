# Standalone setup design

This document supersedes the prototype's proposed architecture. Current edition
and selector additions are described in [disc-editions.md](disc-editions.md)
and [patch-verification.md](patch-verification.md). The historical
[prototype design](prototype-design.md) and [prototype results](prototype-report.md)
are preserved; their original repository links are historical paths.

## Boundaries

- `core/`: C++17 range readers, ISO/Joliet and mounted-directory providers,
  PE SFX ranges, format-9 cabinet verification, English feature mapping,
  checked RTP application, final manifest, progress/cancellation and transactions.
- `core/rtp-bridge/`: Rust static C ABI for the pinned owner's rtptool fork.
- `platform/`: POSIX/Windows locks, file and directory flushes, atomic replace.
  No macOS APIs, POSIX system calls or Apple digests live in the portable core.
- `cli/`: testing/diagnostic frontend, Ctrl-C and progress text.
- `macos/`: native AppKit wizard, five-field key rows (BF2 plus SF for Deluxe), Registry.con persistence,
  Application Support resolution and the future native client launch hook.
- `third_party/`: unmodified rtptool snapshot with MIT notice and locked Rust
  dependency notices. zlib comes from the build platform.

Cabinets stream compressed ranges directly from the input. A bounded 1 MiB
read cache avoids repeated seek/read calls for block framing. Up to four files
are decompressed concurrently (configurable through `Control`, clamped 1–8).
Destination paths/directories are resolved before workers start, so workers
never race to choose case aliases. Per-file output uses a temporary sibling and
rename after MD5 and length checks. There are no on-disk patch/SFX copies.
RTP histories still use in-memory source/program/destination buffers; measured
peak memory ranged from about 1.0 to 1.8 GB. They are not a fully streaming diff engine.

Input identities are SHA-256 checked against the supported corpus before payload
writes. DVD-directory mode checks the equivalent cabinet hashes; loose selected
files retain cabinet size/MD5 checks. This is an observed-media identity catalog,
not a claim to independently authenticated publisher signatures.

## Installation transaction

```text
OpenBF2/
  Registry.con                    private plain-text key, existing registry syntax
  Profiles/                       frontend/client-managed profiles
  GameData/1.50-<generation>/      verified immutable data generation
  Setup/Install.lock              OS lock shared by setup/client contract
  Setup/Current.txt               atomic pointer: generation + receipt basename
  Setup/Receipt-<generation>.txt   version, input/manifest hashes, generation
  Setup/work/Inputs.txt           exact resume input/manifest identities
  Setup/work/<phase>.journal      per-file output hash or absence
  Setup/work/<phase>.done         durable phase completion checkpoint
  Setup/work/Publish.txt          durable publication intent
  Setup/work/game/                private work, never an active installation
```

Stages: hash inputs → extract selected DVD features → preserve DVD renderer →
apply effective 1.41 records → overlay full 1.50 replacements → verify every
manifest path/size/SHA-256 and reject extras → flush → publish generation →
atomically replace Current.txt. The old generation remains available throughout.
The app's key save preserves unrelated Registry.con lines and uses mode 0600
plus flush/rename; it does not enter receipts or diagnostics. Data and key are
separate commits: startup requires verified data and a formatted stored key.

On cancellation, active `.openbf2-part` files are removed and completed journal
entries remain. Re-running identical inputs rehashes recorded outputs for the
incomplete phase and skips valid work. Complete earlier phases are not replayed
onto files that later phases already changed. Final full manifest verification
always runs. Wrong/corrupt journaled outputs cannot be accepted as final data.
If an external process has damaged completed earlier-stage source data, retry
may require removing only Setup/work and starting again.

Every recorded output is flushed with its parent before journaling. Checkpoints
are flushed before their directory. A durable publication intent allows retry
when interrupted after generation rename but before the Current.txt switch.
Unreferenced published generations are retained, not deleted speculatively.
Clients must participate in the setup lock before using game data; the future
client integration owns that lifecycle (see distribution.md).

Cancellation is checked between input/decompression chunks, files, RTP records
and final verification chunks. A single Rust history application cannot yet be
interrupted internally. No partial generation becomes active. Progress totals
are per phase; RTP shows record progress rather than a fabricated expanded-byte
total. Free-space preflight sums selected disc and overlay sizes plus a 2 GiB
working reserve. Write/close/fsync failures abort; the estimate is conservative,
not a prediction of APFS physical allocation.

## Current limits

Only the supplied English retail base-game corpus is accepted. Unexpected
required patch failures abort. Historical unselected paths, superseded records,
retired checksummed manifests and reference-local differences are explicit policy
in [correctness.md](correctness.md), not checksum bypasses in rtptool.

Symlinks, traversal, ambiguous case aliases, reserved Windows device names and
nonregular files are rejected. Private staging and atomic file replacement are
implemented. Descriptor-relative traversal against a malicious concurrent
same-user process remains hardening work; portable std::filesystem/ifstream
checks do not close every path-check race. Power-loss and ENOSPC fault injection,
Windows filesystem behavior, Apple-silicon runtime, and interactive user testing
remain release gates; no such coverage is inferred from universal linkage.
