# Official patch identity checks

These pins were independently recomputed on 2026-11-10 from the owner's read-only
files listed in the external installer-inputs.md manifest. They identify these
specific supplied executables; filenames are not trusted. No executable bytes
are stored in the repository or downloaded by setup.

| Slot | Bytes | SHA-256 |
| --- | ---: | --- |
| BF2 1.0 → 1.41 | 562,044,991 | `7bfd6236b67c0380acb522d237273d51a2764769a8f9c223098f93350bdd5c64` |
| BF2 1.41 → 1.5 | 2,067,515,943 | `c5b150c96916a4c642daff03da3e9e52a7bccec6a7d6c6492494b57733c77977` |

`core/Media.cpp` is the common pin source. A matching size and complete SHA-256
are both required for the chosen slot. The other known patch gives a targeted
message, for example: “This is the 1.5 patch; please select the 1.41 patch here.”
An unknown, truncated or modified file gives an expected-slot size/SHA-256 error.
An impossible size fails immediately. A file with either known size is hashed,
so a renamed other-slot file can be identified precisely, and a same-size
modified executable cannot pass by matching a header or name.

Each AppKit patch selector starts its own worker when a file is chosen or dropped.
SHA-256 streams in 1 MiB chunks and displays a progress bar and percentage in the
selector, including for the 2 GB 1.5 executable. The main thread only updates UI.
Choosing another file cancels the old read between chunks; a revision token
prevents stale success, progress or errors from authorizing the new selection.
Continue is enabled only after both selected files pass their own slot check.
Installation rehashes inputs before any payload staging and enforces the same
pins, protecting against file changes after selection. EXEs are never run.

Synthetic unit checks cover accepted pins, both wrong-slot diagnoses, unknown
sizes, wrong sizes, same-size wrong hashes, real streaming rejection of a sparse
fake of the expected length, streaming progress and cancellation. Optional
owner-media session checks exercise both selectors' real hash/progress and
wrong-slot messages, disc edition detection and pre-staging low-space rejection.
Independent Python hashlib results and native test evidence stay in
the scratch tree's patch-identity JSON and media logs.
