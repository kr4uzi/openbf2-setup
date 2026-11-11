# Retail CD-key validation (setup-cdkey, 2026-11-10)

The owner's 2026-11-10 decision replaces the earlier format-only policy.
`setup::validateKey(KeyEdition, std::string_view)` independently implements the
retail local check for Battlefield 2 and Special Forces as separate products.
Deluxe is a media edition requiring both product keys.

## Provenance

Only retail binaries were used as validation evidence. The read-only Deluxe
ISO supplied `Support/Battlefield 2_code.exe` and
`Support_XP1/Battlefield 2 Special Forces_code.exe`. Each is 323,584 bytes:

| Product | SHA-256 |
| --- | --- |
| BF2 | `da59e9ac9e135282e80f19ba9ddc6d7b7535cc94b3c6dc19a94d34a9d2866d0e` |
| Special Forces | `5671e943aa681b0c98d3121023b7a299d2b1bff69c9d325ce1fdc786352922c2` |

The BF2 helper also matches the regular-disc copy recorded in
[the initial inspection](cdkey-helper-analysis.md). LLVM objdump inspected fresh
scratch copies. Ghidra 12.0.1 headless decompiled the BF2 copy in a fresh scratch
project with `-readOnly`; its import was discarded. Neither Windows binary was
executed, emulated, patched or loaded for execution. No installer or game
executable was needed. No third-party key tool source, including bf2keyman,
was read, copied or ported. The C++ implementation was written independently
from the observed check and data flow.

Addresses below are PE virtual addresses with image base `0x00400000`:

| Evidence | BF2 | Special Forces |
| --- | --- | --- |
| Dialog's validation call and result branch | `0x00402A80` | `0x00402A79` |
| Boolean wrapper | `0x00405D10` | `0x00405E90` |
| Check routine | `0x00405C50` | `0x00405DD0` |
| Input layout handling | `0x00405A30` | `0x00405BB0` |
| Checksum calculation used by check | `0x00405AE0` | `0x00405C60` |
| Arithmetic helpers | `0x004057E0`, `0x00405830` | `0x00405960`, `0x004059B0` |
| Checksum representations | `0x004058B0`, `0x00405970` | `0x00405A30`, `0x00405AF0` |
| Product configuration data | `0x0042B330`, `0x0042B3B0` | same addresses, distinct values |
| Product context initialization | `0x004024C5` | `0x004024B1` |
| Representation/layout data | `0x0042DC98`, `0x0042DCB8`, `0x0042DCD8` | same addresses and bytes |

The check and arithmetic code ranges `0x004057E0..0x00405D29` (BF2) and
`0x00405960..0x00405EA9` (SF) are byte-identical after relocation. This was
checked directly against the PE sections; product configuration remains distinct.
The dialog's failure path reports an invalid code, while its success path
passes the entered value to local storage. The verdict used here is the check's
boolean result, not the dialog's eventual exit status after repeated errors.

## Behavior and limits

The function checks an embedded checksum, its representation consistency and
the product context. It reads the supplied checksum and compares scalar values;
it cannot return a repaired or generated key. There is no generation, inversion,
search, replacement-key API or list of accepted examples.

The native input boundary accepts exactly 20 ASCII letters/numbers, compact or
in five groups separated by single hyphens. ASCII lowercase is normalized;
whitespace, Unicode, embedded NULs, punctuation and misplaced separators are
rejected. The paste adapter retains its existing outer-whitespace trimming.
These presentation rules are native UI choices; the retail routine receives
the dialog's concatenated groups. No global restricted-alphabet rule is imposed
on the payload: the restricted checksum representation is checked separately.

Each visible wizard row updates its valid/invalid/incomplete status on editing
or paste. Install requires a valid BF2 key, plus a valid SF key for Deluxe.
The action handler rechecks both products. Saving and stored-key detection also
validate, so legacy format-only registry entries return to the key page and
cannot enable Play. Serialization and format normalization alone are not
validation APIs. Keys never enter validation logs or error messages.

Passing proves agreement with this local retail checksum policy. It does not
prove purchase, ownership, uniqueness, entitlement, an unused/unbanned key,
online activation or multiplayer authentication. Some random strings can pass
a finite checksum; this is not a stronger ownership test.

## Verification

`key_checks` covers malformed input, unknown products, format-correct invalid
placeholders, damaged checksum symbols and inconsistent redundancy. The
rejection verdicts for the placeholders were followed statically through the
retail checksum arithmetic; symbols outside its checksum representation and
inconsistent redundancy also cannot match its comparison. No Windows code is
executed by tests.

`key_checks_owner` is opt-in through `BF2_TEST_KEY` and/or `BF2SF_TEST_KEY`.
It skips (CTest code 77) when neither is supplied. It checks acceptance,
presentation equivalence, wrong-product rejection and single-character
corruption at every position, using only in-memory strings. It never prints
or writes keys. Supply environment variables privately; do not put their values
in shell commands, logs, fixtures or screenshots. Run it with:

```sh
ctest --test-dir "$build" -R '^key_checks_owner$' --output-on-failure
```

No real keys were provided in this session, so real-key acceptance and its
corruption tests were skipped. Universal build and rejection/session/packaging
test results are recorded in the project notes. Interactive AppKit verification
remains a desktop check if the sandbox cannot create an application session.
Static reports, decompilation and analysis projects are scratch-only artifacts;
no executable, game content or owner key belongs in this repository.
