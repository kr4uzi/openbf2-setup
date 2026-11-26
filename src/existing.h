// An existing Battlefield 2 1.50 installation which OpenBF2 uses in place (docs/existing-installation.md):
// a retail or Steam folder, also one inside a Wine or Proton prefix. Its game data generation holds no
// copies, only links into the folder: one for every mod below mods/, and the Python script folders. The
// folder keeps working with the game it came with, its mods appear in OpenBF2, and settings saved by
// either game are the same files. No core API here writes into the folder.
#pragma once
#include "edition.h"
#include "platform.h"
#include "progress.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace setup {
namespace fs = std::filesystem;

// What a path of a content manifest means for a linked installation. [CHOSEN, not recovered] the
// import rule: OpenBF2 only reads assets and scripts from the game folder, never a retail binary.
enum class LinkedRole {
    // read by every client (levels, objects, menus, sounds, movies): must match the manifest
    asset,
    // mods/<mod>/localization/: a language which is missing is allowed, a file which is there must match
    localization,
    // mods/<mod>/settings/: the game saves over these, they only have to exist
    settings,
    // python/, admin/, pylib-2.3.4.zip and mods/<mod>/python/: only run in a game the player hosts.
    // A changed or missing script is reported, never refused
    script,
    // everything else (executables, DLLs, PunkBuster, tools, readmes): never read, never linked
    unused
};
LinkedRole linkedRole(const std::string& manifestPath);

// The folder of an installation as the user chose it: a directory with mods/bf2/mod.desc (names in any
// case). Complete when mods/xpack has the Special Forces levels, BF2 otherwise; the files themselves are
// checked by linkExisting. Throws with the reason a folder is not one.
DiscEdition detectExisting(const fs::path& folder);

// What the verification of a linked installation tolerated.
struct LinkedReport {
    // scripts which are missing or differ from 1.50, as the manifest names them
    std::vector<std::string> scripts;
    // localization files of languages which are not installed
    size_t missingLocalization = 0;
};

struct LinkOptions {
    fs::path folder, supportRoot, manifest;
    // the edition to use (installedEdition of the detected one): Special Forces is linked and checked
    // only when it has them
    DiscEdition edition = DiscEdition::BF2;
    Control control;
};

// Makes the linked generation GameData/1.50-<token> below the support root, verifies the folder's files
// through its links and publishes it: Setup/Current.txt names it, its receipt names the folder. A
// failed verification throws and leaves the current installation as it was.
fs::path linkExisting(const LinkOptions& options, Platform& platform, LinkedReport* report = nullptr);

// The full verification of a linked generation, through its links: every asset matches, the
// localization files which are there match, every settings file exists. Throws, naming the files
// which do not, when anything else is found. Scripts and missing languages go to `report`.
void verifyLinked(const fs::path& game, const fs::path& manifest, const Control& control = {}, LinkedReport* report = nullptr);

// The start-up shortcut, as verificationStamp/stampMatches (install.h) for a linked generation: sizes
// and modification times of the files the full verification checked.
std::string linkedStamp(const fs::path& game, const fs::path& manifest);
bool linkedStampMatches(const fs::path& game, const fs::path& manifest, const std::string& stamp);

// The links in mods/ of a linked generation as the folder's mods are now: a mod which was added gets one,
// a link whose mod was removed goes. A name already taken in mods/ is left alone.
void linkFolderMods(const fs::path& game, const fs::path& folder);
// Whether `link` (in mods/ of a generation) is a link to the mod of the same name in the folder.
bool leadsIntoFolder(const fs::path& link, const fs::path& folder);

// Removes a generation without following a single link in it: links are removed, never their targets.
void removeGeneration(const fs::path& game);
}
