// The installation transaction and the checks of its result. No native UI or
// platform-specific APIs.
#pragma once
#include "platform.h"
#include "media.h"
#include "progress.h"

namespace setup {
struct InstallOptions {
    // Complete Edition always uses its included 1.41 patch; patch141 may be empty.
    fs::path disc, patch141, patch150, supportRoot, manifest;
    Control control;
    // Expansion media: false installs the base game alone (a Base receipt,
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
// The start-up shortcut of the verification. The stamp records size and modification time of
// every manifest file right after a full verification; stampMatches compares the files against
// it (regular files, same size and time; the client's own settings files only have to exist),
// in a fraction of a second. Anything else, a missing or torn stamp included, means the full
// verification runs again. The stamp is an integrity cache, not a protection against an
// adversary who can write the game data anyway.
// One row of a content manifest (core/content-*.tsv): the path as the manifest spells it (lower case),
// its size and SHA-256. Throws on a malformed or empty manifest.
struct ManifestEntry {std::string path,hash;uint64_t size=0;};
std::vector<ManifestEntry> readContentManifest(const fs::path& manifest);
std::string verificationStamp(const fs::path& game, const fs::path& manifest);
bool stampMatches(const fs::path& game, const fs::path& manifest, const std::string& stamp);
// A small text file, flushed with its directory.
void writeTextFile(const fs::path& path, const std::string& text, Platform&);
std::string sha256(const View&);
// Diagnostic: export every cabinet entry (all languages/features), preserving
// duplicates by descriptor index. Requires a fresh output directory.
void exportDisc(const fs::path& disc, const fs::path& output, const Control& = {});
struct Options {
    fs::path iso, patch141, patch150, output, reference, acceptance;
};
/// Diagnostics receive the CLI's configured scratch root; the core embeds no developer paths.
/// Writes only a fresh private scratch tree. Returns nonzero for any incomplete phase.
int prototype(const Options& options, const fs::path& scratchRoot);
/// Enumerates both complete trees and hashes every regular file; never supplies source bytes.
int compare(const fs::path& output, const fs::path& reference, const fs::path& acceptance, const fs::path& scratchRoot);
}
