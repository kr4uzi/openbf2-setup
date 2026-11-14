# Research and provenance

These documents record how the content manifests, the patch policy and the key
check were derived from the owner's media in the first rounds (November 2026).
They are evidence, not instructions: open one when a number in a manifest or a
policy decision is in doubt. The current behaviour is described in the documents
one level up.

| File | What it proves | Open it when |
| --- | --- | --- |
| `correctness.md` | the supported input hashes, every resolved patch record and the intentional differences from a Windows reference installation | a verification fails on a file you believe is correct |
| `validation.md` | the validation runs of 2026-11-10: host, commands, results | you need to know what was tested and how |
| `edition-criteria.md`, `edition-deluxe-only.tsv`, `edition-shared-changed.tsv` | how a Deluxe disc is told from a regular one: cabinet groups, destinations and the exact rule | the edition detection misjudges a disc |
| `deluxe-xp1-records.tsv` | the disposition of all 108 Special Forces 1.41 patch records | a Deluxe installation reports a patch record failure |
| `patch-skip-verification.md` | why 1.41 records whose target the 1.50 patch replaces in full are skipped, and the strict verification of that rule | the patch policy is questioned |
| `cdkey-helper-analysis.md` | the historical static look at the disc's key helper, superseded by the reconstructed check in `../cdkey-check.md` | the provenance of the key check is reviewed |
| `prototype-design.md`, `prototype-report.md`, `prototype-usage.md` | the first prototype's design, results and usage, written while it lived inside the OpenBF2 repository; documents it names from there are given as plain paths | archaeology |
