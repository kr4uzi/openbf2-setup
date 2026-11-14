// Read-only input ranges and the containers found on the media: ISO 9660/Joliet
// images, mounted DVD directories and the patches' PE self-extractor records.
#pragma once
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
}
