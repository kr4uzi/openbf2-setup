# BF2 and Deluxe media (2026-11-10)

The subsequent [edition criteria review](edition-criteria.md) includes complete
owner-local cabinet exports, group/path differences and the exact current rule.

The assistant detects cabinet content, not the filename or volume label. The
owner's regular ISO has 1,727 cabinet entries and BF2 assets; Deluxe has 3,162
entries in one combined cabinet, including BF2 and Special Forces (`mods/xpack`).
The xpack archives, Init.con and both Warlord ZIPs must be present for Deluxe
detection. Incomplete expansion inventories are rejected. Installation also
checks full input SHA-256, so the inventory check alone cannot authorize bytes.

Deluxe displays two independent five-group key rows, Battlefield 2 and Special
Forces. Both use the same letters/numbers format check; neither checks validity,
ownership or activation. The owner-requested fields display their text normally. Both keys are saved by one private atomic Registry.con
replacement under Install.lock, using their separate EA Games product branches.
A bad second key leaves the existing registry untouched. A Deluxe receipt selects
the Deluxe manifest on restart and requires both stored keys to pass their retail
local product check before Play; see [CD-key provenance](cdkey-check.md).
Older BF2 receipts remain supported. A base-game install preserves any previously
stored expansion key. Keys are never included in reports or receipts.

The disc feature mapping includes xpack client/server/common/font archives,
eight SF maps and their metadata, AI, settings, Python, localization, English
movies and menu files, plus the separate SupportXP1 material. The disc calls its
loose support directory `Support_XP1`; the installed destination is `SupportXP1`,
matching the 1.50 cabinet. English is the selected movie/support language; cabinet
localization files remain included as in the base-game policy. All selected
cabinet files pass MD5/size checks; loose files are checked against cabinet
metadata too. No Windows helper is executed.

## Patches checked against the Deluxe disc

The Deluxe disc is a later BF2 baseline than the regular disc. Both still need
the pinned patch chain. Its source histories are recognized by the unchanged
strict RTP applier. The BF2 payload accounts for all 394 records as before:
126 produced, 158 deleted/already absent/explicitly unselected and 110 replaced
in full by 1.50. The early 1.41 cabinet's selected payloads are also supplied by
1.50; its installer helpers do not supply the missing SF game assets.

For Deluxe, the installer additionally applies `disk1/xp1/patch.rtp` from 1.41
against the extracted xpack files, then the 1.50 full-file overlay. For regular
BF2, it does not run XP1 RTP histories. The 1.50 xpack full files already present
in the base-game policy remain installed; their presence alone is not SF.

| XP1 1.41 disposition | Records | Evidence / treatment |
| --- | ---: | --- |
| Checked output | 79 | Disc source histories match, or the RTP history supplies an ADD. Includes the eight maps' client/server ZIPs and archive metadata, localization updates and BotNames.ai. |
| Deleted / already absent | 9 | Strict RTP terminal DELETE handling; includes the six old menu cursor files, absent ClientArchives.con.bak, ServerArchives.con.bak and Fonts_server.zip. |
| Full 1.50 replacement | 17 | GameLogicInit.con, Menu_client.zip, Menu_server.zip, Mod.desc, Objects_client.zip, Objects_server.zip, Shaders_client.zip, bst/std archive manifests (four) and six weapon icons. Cabinet MD5/size plus final SHA-256 verify all replacements. |
| Explicitly unselected, absent historical source | 3 | See below. These are not reported as successfully applied MODIFY records. |
| Unexplained required failure | 0 | Unknown/mismatching present sources still fail; no generic checksum rule changed. |

The three exact absent-source targets are
`python_xpack/bf2/stats/constants.py`,
`mods/xpack/Localization/japanese/xpackJapanese.utxt` and
`mods/xpack/Localization/thai/xpackThai.utxt`. None is supplied by the combined
Deluxe cabinet or by the 1.50 full replacements. Their histories end in MODIFY,
without a usable ADD for absent sources. The shared BF2 constants.py also does
not match the first record's required source. These targets are explicitly
unselected only when absent in the XP1 phase; a present unknown file goes to the
strict applier and fails. No missing source bytes are synthesized. This is an
English-disc installation policy, not Japanese/Thai setup feature parity or a
reconstruction of the original InstallScript side effects.

The 1.50 patch alone cannot complete SF: it lacks the eight SF map client/server
ZIPs, Common_client.zip, Common_server.zip, Fonts_client.zip, AI and most other
disc assets; its 17 overlapping XP1 targets do not replace the other histories.
The 1.50 cabinet also directly overlays scoringCommon.py and Controls.con,
which are not separate XP1 RTP targets. Those retained/updated files are verified in `core/content-deluxe-150.tsv`.
`docs/deluxe-xp1-records.tsv` records the full 108-target disposition and whether
each target exists in the Deluxe cabinet selection.

The owner-confirmed patch policy skips a 1.41 target **before calling RTPatch**
whenever the selected 1.50 cabinet supplies that exact destination as a full
file. This uses the actual selected cabinet inventory, never a filename pattern
or an RTP error fallback: 110 BF2 targets on either disc, plus 17 XP1 targets on
Deluxe. Targets without full replacements retain their required 1.41 histories.
See [patch skip verification](patch-skip-verification.md) for fresh strict
verification and measured time saved in the following round.

## Exact supported identities and final contract

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Deluxe ISO | 3,032,449,024 | `7b462933353f89f5a7d7282987be61062a92d248fec125050039ba440765e99e` |
| Deluxe data1.hdr | 337,711 | `ae4800c392fa1bf730cbc90823b40ae772fe6b82ba28f15b48333c3cd002a350` |
| Deluxe data1.cab | 10,830,712 | `27a1a23e868b434517d39c9e68531442ed7674d15e802a0122ffb064bffa78e1` |
| Deluxe data2.cab | 2,913,382,865 | `7bd10eb4a3740973171d38a952cc2c51bee8818ce923085d466f1cf9295a75b4` |

Mounted-directory mode checks the same three cabinet identities and each
selected loose file. ISO mode reads without mounting. Official patch pins and
selector behavior are in [patch-verification.md](patch-verification.md).

The Deluxe manifest has **3,030 files, 4,020,313,462 bytes**, including 213
`mods/xpack` files totaling 908,325,784 bytes. It was derived by extracting this
read-only disc and applying the checked patches; only pathname/size/hash metadata
is stored in the repository. All 108 XP1 record outcomes were audited separately
against the disc inventory and the 1.50 replacement selection. The fresh
production transactional install matched the entire manifest and published its
edition receipt. No independent installed SF reference or SF client run is
claimed. The regular manifest stays at 1,755 files; its fresh regression install
is recorded in the project notes.

The Deluxe renderer preserved as RendDX9x2.dll is its own 2,544,640-byte disc
renderer, SHA-256 `56705ae93e5eb552a80f08dbbeb5488a78bd29156a175aece82c632d8d5f0c3d`.
It differs from the older regular-disc renderer intentionally. Shared final game
archives otherwise agree between the two manifests; their remaining shared
identity differences are inert SafeDisc language DLLs and support dictionaries.
Desktop key-row/paste/error/progress checks, mounted physical media, native SF
runtime behavior and Apple-silicon execution remain desktop checks.
