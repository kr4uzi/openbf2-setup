# Edition criteria for owner review (2026-11-10)

The current criterion is documented here for the owner to confirm or adjust;
no new edition heuristic was adopted in this round. Detection reads the
InstallShield **file/group inventory**, not the ISO filename/label and not the
contents inside ZIP archives. Installation separately checks the full pinned
ISO (or cabinet set), payload MD5/size and final manifest SHA-256 identities.

## Complete owner-local exports

Both read-only images listed in `coord/evidence/installer-inputs.md` were exported
with this repository's rebuilt native CLI, without mounting or executing them:

```sh
bf2_setup extract-disc '/path/to/BF2 Image.iso' /path/to/export/bf2
bf2_setup extract-disc '/path/to/BF2 Deluxe Image.iso' /path/to/export/deluxe
```

| Export folder | Manifest rows | Exported files | Exported bytes | Groups |
| --- | ---: | ---: | ---: | ---: |
| `bf2/` | 1,727 | 1,662 | 2,057,294,927 | 120 |
| `deluxe/` | 3,162 | 3,073 | 3,111,957,840 | 140 |

Each folder contains `manifest.tsv` with one row per cabinet descriptor: export
path, size, cabinet path, exact cabinet group names, storage, MD5 and source.
These are file groups from the cabinet header, not reconstructed InstallScript
component/feature dependencies. `payloads/<descriptor-index>/<cabinet-path>`
retains aliases and identical filenames belonging to different groups/languages.
All valid descriptors are included, beyond the installer's English selection:
installer helpers, other movie/support languages and loose files referenced by
the cabinet are retained too. These are unpatched disc payloads, not runnable
installations. No keys, profiles or reference-installation bytes were copied.

BF2 has 472 direct cabinet payloads, 81 cabinet links and 1,109 checked loose
payloads. Deluxe has 690 direct cabinet payloads, 188 links and 2,195 checked
loose payloads. Every exported file matches its descriptor's MD5 and size.
BF2's 62 and Deluxe's 86 invalid descriptors (flag 8) are metadata-only rows.
Each also has three empty self-reference placeholders (`layout.bin`, `data1.hdr`,
`data1.cab`); their declared zero bytes are not replaced with the nonempty loose
installer files. Those rows have no exported path. Counts retain aliases rather
than deduplicating bytes. Both export roots and all game bytes are outside every
Git repository and remain as the owner's local copy.

## Cabinet differences

Deluxe has 37 group names absent from BF2; BF2 has 17 absent from Deluxe.
The exact group/path comparison has 1,607 Deluxe-only association rows, including
89 new paths in shared groups. Of these, 1,527 associations refer to actual
exported files and 80 to metadata-only descriptors. The
[complete Deluxe-only file/group list](edition-deluxe-only.tsv)
links each association to its descriptor and relative export path. A group rename
counts as a new association, so this is not a count of entirely new game files.
The [shared-group/path identity changes](edition-shared-changed.tsv) separately
record 189 rows whose declared size or MD5 changed. Neither TSV contains payload
bytes. Paths are compared case-insensitively, group names exactly.

| Group name only in Deluxe | Descriptor associations |
| --- | ---: |
| `Menu - BF2` | 7 |
| `Menu - XP1` | 7 |
| `MenuObjects - BF2` | 160 |
| `MenuObjects - XP1` | 1 |
| `Mods - xpack` | 15 |
| `Mods - xpack # client` | 4 |
| `Mods - xpack # objects_client` | 1 |
| `Mods - xpack - AI` | 9 |
| `Mods - xpack - Logs` | 1 |
| `Mods - xpack - Settings` | 52 |
| `Mods - xpack - levels` | 24 |
| `Mods - xpack - levels # client` | 68 |
| `Mods - xpack - localization Chinese` | 3 |
| `Mods - xpack - localization Dutch` | 3 |
| `Mods - xpack - localization English` | 3 |
| `Mods - xpack - localization French` | 3 |
| `Mods - xpack - localization German` | 3 |
| `Mods - xpack - localization Italian` | 3 |
| `Mods - xpack - localization Korean` | 3 |
| `Mods - xpack - localization Polish` | 3 |
| `Mods - xpack - localization Spanish` | 3 |
| `Mods - xpack - localization Swedish` | 3 |
| `Mods - xpack - python` | 5 |
| `Mods - xpack - shaders` | 1 |
| `Movie - Misc - XP1` | 1 |
| `Movies - Chinese - XP1` | 3 |
| `Movies - Dutch - XP1` | 3 |
| `Movies - English - XP1` | 3 |
| `Movies - French - XP1` | 3 |
| `Movies - German - XP1` | 3 |
| `Movies - Italian - XP1` | 3 |
| `Movies - Korean - XP1` | 3 |
| `Movies - Polish - XP1` | 3 |
| `Movies - Spanish - XP1` | 3 |
| `Movies - Swedish - XP1` | 3 |
| `Python - BF2` | 13 |
| `eReg - XP1` | 1,089 |

`Menu - BF2`, `MenuObjects - BF2` and `Python - BF2` are base-game group
renames/updates, not evidence of SF by themselves. `eReg - XP1` primarily adds
SupportXP1 documentation/help/registration assets; it also is not the detector's
criterion. The regular-only group names are:

`<Support>Japanese Files` (1), `<Support>Japanese OS Independent Files` (3), `<Support>Japanese String Tables` (1), `<Support>Thai Files` (1), `<Support>Thai OS Independent Files` (2), `<Support>Thai String Tables` (1), `Cursors` (8), `Menu` (7), `MenuObjects` (111), `Mods - bf2 - localization Japanese` (2), `Mods - bf2 - localization Thai` (2), `Movie - BFMC` (1), `Movies - Japanese` (3), `Movies - Thai` (3), `Python` (13), `Support - Japanese` (1), `Support - Thai` (1).

The shared-group additions include Wake Island 2007 client/server data and
menu metadata, per-map archive MD5s, coop menu maps and `gpm_coop.py`, additional
base-game localization (`*_Mappacks.utxt`, `*_Patch.utxt`, `XP*.utxt`), settings,
PunkBuster updates, a dedicated-server AI DLL and installer helpers. These later
BF2 disc revisions do not identify the expansion on their own.

## Expansion paths actually present

Deluxe has **210 `Mods - xpack*` descriptors (207 payloads and three invalid entries)**; regular BF2 has none. The paths
below are English install destinations derived by `discDestination`; the local
manifest preserves original cabinet names/case/groups for every descriptor.

| Source groups | Paths / content | Descriptors / payloads |
| --- | --- | ---: |
| `Mods - xpack`, `# client`, `# objects_client` | `mods/xpack/{Objects_client.zip,Objects_server.zip,Common_client.zip,Common_server.zip,Fonts_client.zip,Shaders_client.zip,Menu_client.zip,Menu_server.zip,Init.con,...}` | 20 / 20 |
| `Mods - xpack - levels`, `# client` | `mods/xpack/Levels/<map>/{client.zip,server.zip,Info/...,archive.md5}` | 92 / 92 |
| `Mods - xpack - AI` | `mods/xpack/AI/...` | 9 / 9 |
| `Mods - xpack - Settings` | `mods/xpack/Settings/...` | 52 / 51 |
| `Mods - xpack - localization <language>` | `mods/xpack/Localization/<language>/{xpack*.utxt,...}` (ten languages) | 30 / 30 |
| `Mods - xpack - python` | `mods/xpack/python/game/...` | 5 / 5 |
| `Mods - xpack - shaders` | Invalid descriptor; no payload | 1 / 0 |
| `Mods - xpack - Logs` | Invalid descriptor; no payload | 1 / 0 |

The eight map directories are `Devils_Perch`, `Ghost_Town`, `Iron_Gator`, `Leviathan`, `Mass_Destruction`, `Night_Flight`, `Surge`, `Warlord`.

Additional expansion payloads map to `mods/xpack/Menu/External` (six),
English `mods/xpack/Movies` (four) and `SupportXP1` (1,089). `Menu - XP1` also
has one invalid descriptor; the sole `MenuObjects - XP1` descriptor is invalid
and exports no file. Other-language XP1 movies are retained in the raw export
but not the English installation. See the linked TSV for every individual path.

## Exact rule currently implemented

`src/edition.cpp:discEdition` performs these inventory checks:

1. Require an entry named `objects_client.zip` (case-insensitive) in a group
   whose name starts exactly with `Mods - bf2`. Otherwise reject unsupported media.
2. Collect destinations of entries in groups starting exactly with `Mods - xpack`.
   With no such groups, return regular BF2.
3. If any xpack group is present, require all five case-insensitive destinations:
   `mods/xpack/objects_client.zip`, `mods/xpack/objects_server.zip`,
   `mods/xpack/init.con`, `mods/xpack/levels/warlord/client.zip` and
   `mods/xpack/levels/warlord/server.zip`.
4. If all five exist, return Deluxe; otherwise reject incomplete SF media.

`detectDisc` and `install` additionally require exactly 1,727 descriptors for BF2
or 3,162 for Deluxe. The five sentinels are presence checks in metadata: this rule
does not independently open those archives, require all eight maps, or determine
an edition from payload hashes. Full supported-media identities and extraction
checks are enforced during installation. The policy supports these two observed
English discs; it does not infer arbitrary regional/repack/standalone SF editions.

The assistant sets `self.deluxe` from that accepted disc result. Its SF label and
five plain-text fields are added only when that flag is true. No xpack inventory
means one BF2 key row; incomplete xpack inventory cannot advance. On restart, the
verified generation's Deluxe receipt/manifest selects the second row/key
requirement. A few xpack full files supplied by patch 1.5 on a regular install do
not identify SF or expose its key row. The SF key gate therefore uses expansion
media evidence, not just any post-patch `mods/xpack` filename.

Both key rows already subclass `NSTextField`; no `NSSecureTextField` is used.
Both keep independent whole-key paste, auto-advance and backspace handling.
Both rows now use the independently reconstructed retail local CD-key check,
with distinct products; the 2026-11-10 owner decision supersedes format-only
handling. See [validation provenance](cdkey-check.md).
Desktop rendering/paste checks remain desktop GUI checks.

Owner review: confirm these five sentinels and strict inventory sizes, or specify
a different content criterion. This document records the current behavior without
assuming that review is complete.
