#pragma once
#include "media.h"

namespace setup {
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
}
