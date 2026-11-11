# Supported content and reference comparison

The regular-disc contract is `core/content-150.tsv`: 1,755 paths, 3,493,000,738 bytes,
with exact sizes and SHA-256. It was generated from the checked media pipeline,
not by copying reference bytes. The 125 independent earlier-patch acceptance
paths remain `tests/acceptance-125.txt`. All 125 match the owner's reference.
Only metadata and implementation code are in the repository. Deluxe uses a
separate 3,030-file contract; see [disc-editions.md](disc-editions.md) for its
source inventory, 108 XP1 record outcomes and validation limits.

## Input identities

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| English retail BF2 ISO | 2,053,931,008 | `2116a6b7107fdf1054e3fd86282f5db02b60c01658d7a4ca13e953d747b95854` |
| 1.0 → 1.41 SFX | 562,044,991 | `7bfd6236b67c0380acb522d237273d51a2764769a8f9c223098f93350bdd5c64` |
| 1.41 → 1.50 SFX | 2,067,515,943 | `c5b150c96916a4c642daff03da3e9e52a7bccec6a7d6c6492494b57733c77977` |

DVD-folder mode accepts the equivalent `data1.hdr`, `data1.cab`, `data2.cab`
identities encoded in Installer.cpp, with MD5/size validation of every selected
expanded file, including loose English support text. ISO parsing never mounts
or modifies the image. SFX parsing reads bounded ranges, never executes EXEs.

## Eleven prototype record errors

The generic fork's checksum rules have not been changed. The selected English
base-game chain handles every one of the 394 top-level BF2 records explicitly:
126 checked outputs, 158 deleted/already absent/policy-absent outcomes, and
110 targets supplied in full by the checked 1.50 cabinet. No unexplained required
record error remains. This does not claim reconstruction of unavailable
historical versions or a general fix in rtptool.

| Prototype failure | Effective-chain handling |
| --- | --- |
| `mods/bf2/levels.md5` | Exact DVD SHA-256 `233e5b3dc1375c17b02c9bec4e0a73c396425893968c07da38919858003109ff` is removed as an obsolete manifest. The terminal operation is DELETE; its omitted historical MODIFY bytes cannot be reconstructed. A different present version still fails. |
| `python/bf2/stats/medal_data.py` | No selected DVD source, no ADD history, absent from reference and final contract. Explicit unselected historical path, accepted only when absent. |
| `LinuxReadmes/readme-linux.txt` | Same explicit absent-source policy. |
| `00000001.TMP` | Same explicit absent-source policy. |
| `ArchiveScripts/Client.lst` | Same explicit absent-source policy. |
| `demo.cmd` | Same explicit absent-source policy. |
| `demo_documentation.txt` | Same explicit absent-source policy. |
| `dice_TextureAtlasTool.exe` | Same explicit absent-source policy. |
| `FragAnalyzer.exe` | Same explicit absent-source policy. |
| `makeMiniMap.con` | Same explicit absent-source policy. |
| `ArchiveScripts/Exclude.lst` | Same explicit absent-source policy. |

These ten absent targets end in MODIFY, so pretending they were successful
DELETEs would misdescribe the evidence. They are reported as
`unselected-historical-absent`. They cannot be synthesized from the supplied
media. The explicit base-game content contract excludes them. If any appears
as a present unknown source, the ordinary strict applier must handle it or fail.

The 110 full replacements are selected from 1.50's feature mapping, not a broad
error-ignore list. Avoiding their obsolete 1.41 histories is safe because each
RTP target is independent, each final replacement passes cabinet size/MD5,
and final full SHA-256 verification is mandatory. No booster or XP1 RTP payload
is applied without its required base media. XP1 is now applied only for the
verified Deluxe edition, as documented separately.

## Six missing reference paths and one size mismatch

`RendDX9x2.dll` is now produced from the original DVD renderer before patches.
Its 2,115,072 bytes match the reference, SHA-256
`1565c0dcab852ebe00a41a4ec016d3d0540e88d7ac049e16295aabd318e8d2f8`.
No reference bytes are used.

The remaining five reference-only files are intentional exclusions:

| Reference-only path | Reason |
| --- | --- |
| `BF2Patch.log` | Run-specific retail installer log, not an input payload. Our own redacted metadata reports replace installer diagnostics. |
| `patch.err` | Run-specific retail installer diagnostic, not an input payload. |
| `dbghelp.backup.dll` | Reference backup equals the 894,464-byte vendor DLL. Unnecessary duplicate; not generated. |
| `dbghelp_original.dll` | Same vendor bytes and intentional backup omission. |
| `mods/bf2/Settings/UserSettings.con` | Terminal DELETE is respected. The used reference has a locally generated/retained settings file; setup does not copy personal settings from it. |

The active `dbghelp.dll` remains the verified 894,464-byte vendor payload,
SHA-256 `c33e7ecbf0903fa279dd7ec6b0395e33568b2581d8d5d2797839d876b6e1ef81`.
The reference instead contains 629,760 bytes with SHA-256
`342d51b6436d97af14352ab91277be0e9b18e29930c747e3c2f174a30416b362`.
Its origin has not been established from these installers. Replacing the vendor
payload merely to match that local file would violate read-only reference use.
Retail PE/DLL files remain inert data; the native launcher never loads them.

Twelve media-sourced extras are retained for completeness pending a smaller
native-runtime manifest decision: seven `adminutils/` files, `BF2ServerLauncher.exe`,
`BF2ServerLauncherBG.bmp`, `msvcp71d.dll`, `msvcr71d.dll`, and `vtuneapi.dll`.
The original thirteenth extra (`mods/bf2/levels.md5`) is now correctly retired.

| Full raw union comparison | Before | Current |
| --- | ---: | ---: |
| Exact size/SHA-256 matches | 1,741 | 1,742 |
| Actual-only files | 13 | 12 |
| Reference-only files | 6 | 5 |
| Size mismatch | 1 | 1 (documented vendor/local DLL) |
| Same-size hash mismatch | 0 | 0 |
| Independent acceptance | 125/125 | 125/125 |
| Required unexplained patch failures | 11 | 0 under explicit base-game policy |

Exact used-reference parity is neither achieved nor claimed. Every remaining
raw difference is named and intentional under this contract. Final manifest
verification is strict: zero missing, zero extra, zero size/hash mismatch
relative to the media-derived contract. The owner should review this policy
before calling the artifact release-ready.
