// Portable setup core. No native UI or platform-specific APIs.
#pragma once
#include "Platform.h"
#include "KeyCheck.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace setup {
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using Sink = std::function<void(const uint8_t*, size_t)>;

/// A bounded read-only range; subranges retain the input file's lifetime.
class View {
    struct File;
    std::shared_ptr<File> file_;
    uint64_t base_ = 0, size_ = 0;
public:
    explicit View(const fs::path& path);
    uint64_t size() const { return size_; }
    View sub(uint64_t offset, uint64_t length) const;
    Bytes read(uint64_t offset, size_t length) const;
    void readInto(uint64_t offset, uint8_t* data, size_t length) const;
    Bytes all() const;
    void stream(const Sink& sink) const;
};

using Files = std::map<std::string, View>;
std::string lower(std::string text);
std::string safePath(std::string text);
Files readIso(const View& image);
Files readDisc(const fs::path& imageOrDirectory);
Files readSfx(const View& image);

struct Entry {
    std::string path, md5;
    uint16_t flags = 0, volume = 0;
    uint64_t size = 0, compressed = 0, offset = 0;
    uint32_t previous = 0;
    uint8_t links = 0;
    std::vector<std::string> groups;
};

/// Limited InstallShield format-9 reader; rejects encodings absent from the probe.
class Cabinet {
    std::map<unsigned, View> volumes_;
public:
    std::vector<Entry> files;
    explicit Cabinet(const Files& container, const std::string& prefix);
    void extract(size_t index, const Sink& sink) const;
};

struct Progress {
    std::string phase, path;
    uint64_t completedBytes = 0, totalBytes = 0;
    size_t completedFiles = 0, totalFiles = 0;
    // Progress of the item named by path, when several items are processed at
    // once (input checks). Zero itemTotalBytes means only the totals above apply.
    uint64_t itemCompletedBytes = 0, itemTotalBytes = 0;
};
struct Control {
    std::function<void(const Progress&)> progress;
    std::atomic<bool>* cancelled = nullptr;
    unsigned workers = 4;
};
// DeluxeBase is an installed edition only: Deluxe media installed without
// Special Forces. discEdition/detectDisc never return it; receipts carry it.
enum class DiscEdition { BF2, Deluxe, DeluxeBase };
// Detect content, never the ISO filename. Reject incomplete xpack inventories.
DiscEdition discEdition(const std::vector<Entry>&);
// Special Forces content on the Deluxe disc: destinations under mods/xpack/
// and SupportXP1/. The 1.50 overlay's own mods/xpack files are not disc content.
bool expansionContent(const std::string& destination);
DiscEdition detectDisc(const fs::path&);
std::string discDestination(const std::string& group, const std::string& path);
// Diagnostic: export every cabinet entry (all languages/features), preserving
// duplicates by descriptor index. Requires a fresh output directory.
void exportDisc(const fs::path& disc, const fs::path& output, const Control& = {});
enum class PatchSlot { Patch141, Patch150 };
struct PatchIdentity { uint64_t size; std::string hash; };
const PatchIdentity& expectedPatch(PatchSlot);
void requirePatchIdentity(PatchSlot, const PatchIdentity&);
// Streaming/cancellable; UI callers run this on their worker thread.
PatchIdentity verifyPatch(const View&, PatchSlot, const Control& = {});
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
// allowExtraFiles tolerates regular files outside the manifest (runtime output of
// the game such as settings or logs); manifest files must still match exactly.
void verifyManifest(const fs::path& game, const fs::path& manifest, const Control& = {}, bool allowExtraFiles = false);
std::string sha256(const View&);
bool keyFormat(const std::string& key);
struct Options {
    fs::path iso, patch141, patch150, output, reference, acceptance;
};
/// Writes only a fresh private scratch tree. Returns nonzero for any incomplete phase.
int prototype(const Options& options);
/// Enumerates both complete trees and hashes every regular file; never supplies source bytes.
int compare(const fs::path& output, const fs::path& reference, const fs::path& acceptance);
}
