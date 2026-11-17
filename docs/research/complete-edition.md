# Complete Edition media

The checked Complete Edition uses separate installers for BF2, Special Forces,
Euro Force and Armored Fury beneath `installers/`. Its top-level cabinet is
only the installer bootstrap. Setup reads the nested cabinets directly, without
mounting the image or executing Windows installers.

## Source identities

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Complete Edition ISO | 4,895,942,656 | `cf77875795c4d75f3c802e6cbaa75111c9887b730e743aafd3c63aef706f799d` |
| `installers/sf/data1.hdr` | 179,561 | `966a5bb19fb68a3d01286a27415dfbcf088f39b979224b236f8f157eaa14b6bd` |
| `installers/sf/data1.cab` | 10,081,031 | `3318718c107a1b8699bebb02fe40a1e64e5c0449c394e47ee07c2631834a3803` |
| `installers/sf/data2.cab` | 993,840,383 | `4cc17a91ae7a391262bbb620d81231fc82c396f0cfc367013f5971cafc67bc0f` |

The three `installers/bf2/` cabinet files exactly match the supported regular
BF2 cabinets. Detection checks both component inventories (1,727 BF2 descriptors
and 1,508 SF descriptors), the required BF2/SF assets and the included patch.
Installation additionally verifies the entire ISO, or both cabinet sets for a
mounted DVD, and each selected payload's size and MD5.

Read-only payload checks covered 1,662 BF2 and 1,474 SF descriptors with no
checksum failures. Euro Force's 92 payload descriptors also passed. Armored Fury
had 102 passing descriptors and one mismatching loose `aurora.png` in its
`Fingerprint` group; that installer metadata is not selected for installation.
Invalid descriptors and three empty installer self-references per component
are excluded from these payload counts.

## Included patches and booster packs

`installers/bf2 patch 1.41.exe` is exactly the supported official 1.41 patch:
562,044,991 bytes, SHA-256
`7bfd6236b67c0380acb522d237273d51a2764769a8f9c223098f93350bdd5c64`.
The patch page selects it automatically and shows **Included on disc — Complete
Edition**, with the normal background hash check and verification status.
It is a bounded view into the ISO or a file on the mounted DVD; no temporary
copy of the executable is needed. Changing to regular or Deluxe media clears
this selection and requires an external 1.41 patch again. A failed included
patch keeps Continue disabled. Installation rechecks the same pinned identity.

Patch 1.50 is not on this image and must still be selected. Its verified full
cabinet supplies all 27 Euro Force and 37 Armored Fury game paths, including
their six maps and booster archives. Of those 64 paths, 28 have updated bytes
and 36 match the disc versions. The separate EF/AF installers and the older
patch under the SF installer are therefore not run or staged. The checked
1.41 histories and 1.50 replacements supply the required final game data.

## Special Forces mapping and patch policy

The standalone SF component has different groups from the combined Deluxe
cabinet. Its Python files map to `python_xpack/`; cursor, menu, object and
English movie groups map under `mods/xpack/`; support and registration files
map to `SupportXP1/`, with loose input files read from the component's own
`Support/` directory. The existing `Mods - xpack` mappings still apply.

The SF cabinet has two copies of the menu cursors. For colliding destinations,
the dedicated `Xpack Cursors` group takes precedence over `Xpack Menu`.
For `cursor_busy.ani`, only the dedicated group's MD5
`646d21a172d55b16e9bfba6cd307135e` matches the XP1 history, which deletes it.
The generic Menu copy has different bytes despite the same size.

The original `mods/xpack/Fonts_server.zip` is not a recognized source in the
1.41 history, whose terminal action is DELETE. Complete Edition retires only
the exact 22,125-byte disc archive with SHA-256
`0dedc24a5652c70b76ef0a543566a77a21ea75dd0691fb681ef8ae6b39c6c48b`,
and only in the Complete Edition XP1 phase when that terminal action is DELETE.
Unknown source bytes still fail. The generic RTPatch checksum rules are unchanged.

With these mappings the BF2 phase has 126 produced targets, 158 deleted or
absent targets, and 110 full 1.50 replacements. The XP1 phase has 82 produced,
9 deleted or absent, and 17 full replacements. Both have zero unresolved
errors. The three targets absent from Deluxe's English selection—XP1 Python
constants and Japanese/Thai localization—are present here and pass strict
patching; they are not treated as absent exceptions.

## Installed contract

`core/content-complete-150.tsv` records 3,058 files and 4,021,644,867 bytes,
including 217 files below `mods/xpack/`. It contains only relative paths,
sizes and SHA-256 metadata. Relative pathname casing follows the staged files
so verification records also work on case-sensitive filesystems.

Compared with the Deluxe contract, Complete has 30 extra files, lacks two,
and differs in 43 shared files. The changes come from the regular BF2 baseline,
the standalone SF Python/localization files and support material. All shared
game archives and map payloads match the Deluxe contract. Complete records
`edition=complete`; installing without SF records `edition=complete-base` and
uses the existing 1,755-file regular BF2 manifest. Both choices receive the
1.50 booster packs. SF remains optional and requires its own key when selected.

The shared core, receipt handling, key requirements and both native frontends
use this media policy. The two frontends retain their existing toolkit and
client packaging workflows.

## Validation (2026-11-17)

Fresh production transactions from the read-only ISO successfully verified and
published both the full 3,058-file contract and the base-only 1,755-file contract,
using the included 1.41 patch and the separately verified 1.50 patch. No existing
installation was used as an output or a source of replacement bytes.

Application tests cover automatic selection, failure blocking Continue, the SF
choice, and clearing the included patch when switching to regular media. Real
Qt tests cover its source label, disabled replacement controls, and the Complete
Edition install/cancel/resume/verify/Play workflow with fake media and client
services. The Release build's patch page was also inspected offscreen.

Quick-verification path resolution now uses the same case-insensitive lookup as
staging, fixing lowercased manifest paths on case-sensitive Linux filesystems.
Both published installations passed receipt loading, manifest selection and
quick-verification checks. The final Linux suite passed 12 tests; the optional
real-key test was skipped because its test keys were not supplied.
Native macOS builds, physical DVDs, and an
SF game session remain untested in this environment.
