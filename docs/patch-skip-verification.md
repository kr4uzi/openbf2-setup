# Patch full-file skip verification (2026-11-10)

The owner's rule was already implemented and remains unchanged: if the selected
1.5 cabinet supplies a destination as a full file, skip that target before the
1.41 RTPatch call. RTPatch processes the remaining targets with its existing
strict source/output checks and explicit historical-source policy.

`install` creates the replacement set from `selection(full, false)` in
`core/Installer.cpp`. That selection contains **274 full-file destinations** from
381 cabinet descriptors, after feature/language selection. The `patch141` loop
checks exact normalized destinations against that set before loading the old
source or calling `obf2_rtp_apply`; it records `superseded-by-150`. The same set
is used for the BF2 histories on both discs and the XP1 histories on Deluxe.
The 1.5 overlay still validates cabinet MD5/size; full final verification is
mandatory before publication. No checksum fallback or blanket ignore was added.

## Fresh native runs

| Disc | BF2 targets skipped | XP1 targets skipped | Final verification |
| --- | ---: | ---: | --- |
| BF2 | 110 / 394 | Not run | 1,755 / 1,755; 3,493,000,738 bytes |
| Deluxe | 110 / 394 | 17 / 108 | 3,030 / 3,030; 4,020,313,462 bytes |

Every skipped path was independently cross-checked against the selected 1.5
full-file inventory. The remaining BF2 dispositions were 126 produced and 158
deleted/absent/explicitly unselected. Deluxe XP1 had 79 produced, nine
deleted/absent and three explicitly unselected absent historical sources.
Zero required unexplained failures occurred. Both generations published their
receipts only after exact final size/SHA-256 verification. Temporary patched
game trees were removed after retaining metadata; owner raw exports remain.

## Measured time saved

Four fresh CLI installs ran sequentially on this Intel Mac (native x86_64),
Release C++/Rust, four extraction/verification workers, identical pinned inputs
and final manifests. BF2 order was skip then no-skip; Deluxe order was reversed.
No reference bytes were used. The production patch logic remained unchanged. A
scratch-only baseline disabled the replacement early skip to measure the
obsolete 1.41 work that would otherwise run. Its only other source edit excluded
full-replacement failures from the aggregate error count, allowing the later
full-file overlay. BF2 had zero baseline errors. Deluxe's BF2 baseline had four
source-history errors, all for exact full replacements: Japanese/Thai
`japanese.utxt`, `thai.utxt`, `japanese_autorun.utxt` and `thai_autorun.utxt`.
Those source languages are absent from the Deluxe disc. The baseline permits
only known full replacements to continue; all other failures remain fatal.
Deluxe XP1 had zero baseline errors. All four runs passed their full final
manifests. The production path skips these four targets before RTPatch and has
zero errors. This experiment measures the cost of attempting obsolete histories;
it is not a supported alternate patch mode, and its binary is not shipped.

RTP time is measured from the first `patch141` progress event to the first
`overlay150` event, including XP1 on Deluxe. Total time covers input identity
hashing, extraction, patching, overlay, final verification and publication.

| Disc / stage | No skip | Production skip | Saved |
| --- | ---: | ---: | ---: |
| BF2: all RTP | 86.35 s | 45.00 s | 41.35 s |
| BF2: total install | 148.37 s | 107.53 s | 40.84 s |
| Deluxe: BF2 RTP | 34.14 s | 18.33 s | 15.80 s |
| Deluxe: XP1 RTP | 10.80 s | 5.66 s | 5.14 s |
| Deluxe: all RTP | 44.93 s | 23.99 s | 20.95 s |
| Deluxe: total install | 117.98 s | 96.44 s | 21.54 s |

BF2's RTP work fell by 47.9%. Deluxe's RTP work fell by 46.6%. These are one
paired measurement per disc, not a benchmark distribution;
filesystem/cache/load differences also affect total install time. The stage
timings isolate the work the rule actually avoids. No claim about an earlier
round's wall time or other hardware is implied.

## Retained evidence

All build/test/install/benchmark evidence is under the authorized
`scratchpad/setup` tree:

- The `{bf2,deluxe}-{skip,attempt}` install logs, the benchmark JSON, the
  benchmark script and its progress log.
- The baseline copy of `Installer.cpp`, baseline source-edit metadata, compile
  command/log and the experimental binary, kept outside the repo/app.
- `reports/{bf2,deluxe}-{skip,attempt}/`: receipts and complete per-target patch
  dispositions; the full-1.50 selection TSV, the skip audit and the savings JSON.
- Universal build/test logs, final export checks and architecture/signature audit.

The production core's patch logic and the two final content manifests were not
changed in that round. The new production functionality is the diagnostic raw
cabinet export command described in [edition criteria](edition-criteria.md).
