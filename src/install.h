// The installation transaction and the checks of its result. No native UI or
// platform-specific APIs.
#pragma once
#include "platform.h"
#include "media.h"
#include "progress.h"

namespace setup {
struct InstallOptions {
    fs::path disc, patch141, patch150, supportRoot, manifest;
    Control control;
    // Deluxe media only: false installs the base game alone (DeluxeBase receipt,
    // no xpack disc content, no XP1 patch records). Ignored for regular discs.
    bool specialForces = true;
};
// Installs an immutable generation and atomically switches Setup/Current.txt.
// Resumes its private Setup/work transaction; never reads a reference tree.
fs::path install(const InstallOptions&, Platform&);
// allowExtraFiles is for a generation the game has run in: regular files outside the
// manifest are tolerated (Client.lock, logs, saved settings), and so are changed
// files below mods/<mod>/settings/, where the client saves settings over shipped
// ones, and symlinks directly in mods/ (the user's own mods; never followed).
// Every manifest file must still exist, and every other one match exactly.
void verifyManifest(const fs::path& game, const fs::path& manifest, const Control& = {}, bool allowExtraFiles = false);
std::string sha256(const View&);
// Diagnostic: export every cabinet entry (all languages/features), preserving
// duplicates by descriptor index. Requires a fresh output directory.
void exportDisc(const fs::path& disc, const fs::path& output, const Control& = {});
struct Options {
    fs::path iso, patch141, patch150, output, reference, acceptance;
};
/// Writes only a fresh private scratch tree. Returns nonzero for any incomplete phase.
int prototype(const Options& options);
/// Enumerates both complete trees and hashes every regular file; never supplies source bytes.
int compare(const fs::path& output, const fs::path& reference, const fs::path& acceptance);
}
