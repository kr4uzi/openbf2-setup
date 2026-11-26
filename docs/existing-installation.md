# Using an existing installation

*For:* Maintainers, and the engine side for the game-folder contract. *Covers:* how the setup uses a Battlefield 2 1.50 installation which is on the computer already (Steam, a retail disc, Wine or Proton) in place, which of its files are checked, and what the generation looks like.

## What the user does

The welcome page offers "Use an Existing Installation…" beside the disc. The
user chooses the game's folder (on Steam: right-click the game, Manage, Browse
local files) and enters its CD key (Steam shows it with the game). The setup
checks the files, publishes a generation and continues with the voice setup and
the launcher as after an installation from disc. A Deluxe or Complete
installation asks for the Special Forces key unless "Install Special Forces"
is unchecked.

## Why links, not copies

An existing installation usually carries mods of its own, and its owner wants to
keep using it with the game it came with: the retail game, BF2Hub, a server.
The setup therefore never copies its files. The generation
`GameData/1.50-<id>` holds only links into the folder:

| In the generation | Leads to | Why |
| --- | --- | --- |
| `mods/<name>`, one per directory of the folder's `mods/` | `<folder>/mods/<name>` | the game data and every mod of the folder, added and removed as the folder changes |
| `python`, `Admin` (spelled as in the folder) | the folder's directories | the game's Python search path (`pylib-2.3.4.zip`, `python`, `mods/<mod>/python`, `admin`) |
| `pylib-2.3.4.zip` | the folder's file (symbolic link; Windows: hard link, a copy on another volume) | the same |

Links are symbolic links, on Windows directory junctions, which need neither
administrator rights nor developer mode (`src/links.h`). Nothing else of the
folder is linked: no executable, DLL or PunkBuster file, so a BF2Hub-patched
`BF2.exe` (it imports `bf2hbc.dll` instead of `WS2_32.dll`) never matters.

The client still runs in the generation, as for a disc installation
([client-artifact.md](client-artifact.md)). `Client.lock`, the logs, `Demos/`
and the top-level `settings/` stay in OpenBF2's own data. Settings the game
saves below `mods/<mod>/settings/` go through the link into the folder, where
the original game saves them as well: both share them. The setup itself writes
nothing into the folder. A folder which cannot be written (a read-only drive,
such as an NTFS volume mounted by macOS) is refused, because the game could not
save its settings.

Mods of the setup's own library (`CustomMods/`) are linked as for a disc
installation; a mod of the folder with the same name comes first.

## What is checked

[CHOSEN, not recovered] OpenBF2 depends only on the game's assets and scripts,
never on a retail binary. Every path of the edition's content manifest
(`core/content-*.tsv`) has one of these roles (`linkedRole` in `src/existing.cpp`):

| Role | Paths | Rule |
| --- | --- | --- |
| asset | everything else below `mods/<mod>/` | must exist and match the manifest |
| localization | `mods/<mod>/localization/`, `mods/bf2/locale` | may be missing (another language); a file which is there must match |
| settings | `mods/<mod>/settings/` | must exist; the game saves over these |
| script | `python/`, `admin/`, `pylib-2.3.4.zip`, `mods/<mod>/python/` | a changed or missing script is reported, never refused |
| unused | executables, DLLs, `pb/`, `AdminUtils/`, `Support/`, `Redist/`, … | neither linked nor checked |

Scripts only run when the player hosts a game (single player, co-op, a local
server): the engine starts Python only in `GameServer::startServer`. A changed
script therefore only changes games the player hosts, and the setup reports the
scripts once, after linking. File names are compared in any case: a Windows
installation spells `mods/bf2/Levels/Dalian_plant/...`, the manifests are lower
case. Like the Linux client, a name which exists as written is taken as it is,
any other is looked up in any case, and two names which differ only in case are
refused.

Special Forces: an installation with `mods/xpack/levels/` is offered as
Complete and checked against `content-complete-150.tsv`, otherwise as BF2
against `content-150.tsv`. The assets of Deluxe and Complete are the same;
only the Complete installer's `mods/bf2/locale` differs, which is optional.

The start-up quick check works as for a disc installation, through the links:
`Setup/Verified-<generation>.tsv` records size and modification time of every
file the full verification checked. A changed asset or script, or a language or
script which appeared since, opens the launcher with the note that the data has
not been verified. "Verify Game Data…" updates the mod links and runs the full
check again; on failure it names the folder to repair (on Steam: Verify
integrity of game files).

## Receipt

`Setup/Receipt-<id>.txt` of a linked generation:

```
openbf2-setup-linked-v1
source=<the folder, absolute, UTF-8>
<SHA-256 of the manifest>
edition=complete            (Complete; complete-base without Special Forces; nothing for BF2)
generation=GameData/1.50-<id>
```

`Setup/Current.txt` names generation and receipt as for a disc installation;
replacing it is the commit point. A verification which fails before that
removes the new generation again, link by link, never following one.

## Removing

Removing a generation removes its links, never their targets
(`removeGeneration`). The Windows uninstaller removes every junction and
symbolic link in `%LOCALAPPDATA%\OpenBF2` before it deletes that folder, so the
deletion cannot reach into the folder of the installation. On macOS and Linux,
deleting the data folder removes symbolic links, not their targets.

## Evidence

- Boot Camp's read-only Deluxe 1.50 installation, verified with this code through
  scratch links against `content-complete-150.tsv`: the assets pass, the two
  scripts it changed (`python/bf2/__init__.py`, `python/bf2/stats/rank.py`) are
  reported, 23 localization files of other languages are missing, its own mod
  `bf2all64` is linked, and linking it for real is refused as read-only.
- A BF2 1.50 installation with BF2Hub, compared by its hash list with the same
  rule: every asset and script matches; BF2Hub changes only `BF2.exe` and
  `bf2_w32ded.exe`, which are not used.
- `tests/existing_install.cpp` checks links, roles, stamps and the refusals on a
  small tree spelled as Windows spells it; `tests/setup_test.cpp` the pages.

## Limits

- The 32-bit Windows client loads `binkw32.dll` and PunkBuster from its working
  directory, which a linked generation does not provide; removing that
  dependency is tracked in the engine repository.
