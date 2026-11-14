// [CHOSEN, not recovered] Native reader for the owner's verified installer media.
// Format reference: installer-probe/tools/macos/installer/formats.py and
// twogood/unshield's MIT lib/{cabfile.h,file.c,directory.c,file_group.c}.
#include "cabinet.h"
#include "digest.h"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace setup {
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
uint64_t number(const Bytes& b, size_t off, unsigned n) {
    require(off <= b.size() && n <= b.size() - off, "truncated integer");
    uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i) v |= uint64_t(b[off + i]) << (8 * i);
    return v;
}
uint16_t u16(const Bytes& b, size_t o) { return uint16_t(number(b, o, 2)); }
uint32_t u32(const Bytes& b, size_t o) { return uint32_t(number(b, o, 4)); }
uint64_t u64(const Bytes& b, size_t o) { return number(b, o, 8); }
std::string str(const Bytes& b, size_t o) {
    require(o < b.size(), "string outside header");
    auto end = std::find(b.begin() + o, b.end(), 0);
    require(end != b.end(), "unterminated string");
    return std::string(b.begin() + o, end);
}
std::string hex(const uint8_t* bytes, size_t n) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) { out += digits[bytes[i] >> 4]; out += digits[bytes[i] & 15]; }
    return out;
}
}

Cabinet::Cabinet(const Files& c, const std::string& prefix)
    : volumes_{{1, c.at(prefix + "data1.cab")}, {2, c.at(prefix + "data2.cab")}} {
    const auto& header = c.at(prefix + "data1.hdr");
    require(header.size() <= 16 * 1024 * 1024, "oversized cabinet header");
    auto b = header.all();
    require(u32(b, 0) == 0x28635349 && u32(b, 4) == 0x01009500, "unsupported cabinet format (requires verified format 9)");
    size_t base = u32(b, 12), table = base + u32(b, base + 12);
    size_t ndirs = u32(b, base + 28), nfiles = u32(b, base + 40);
    require(ndirs < 100000 && nfiles < 100000 && u32(b, 16) != 0, "invalid cabinet counts");
    std::vector<std::string> dirs;
    for (size_t i = 0; i < ndirs; ++i) dirs.push_back(str(b, table + u32(b, table + i * 4)));
    size_t records = table + u32(b, base + 44);
    for (size_t i = 0; i < nfiles; ++i) {
        size_t p = records + i * 0x57;
        Entry e;
        e.flags = u16(b, p); e.size = u64(b, p + 2); e.compressed = u64(b, p + 10); e.offset = u64(b, p + 18);
        require(p + 0x57 <= b.size(), "truncated cabinet record");
        e.md5 = hex(b.data() + p + 26, 16); e.previous = u32(b, p + 76); e.links = b[p + 84]; e.volume = u16(b, p + 85);
        if (!(e.flags & 8) && u32(b, p + 58)) {
            auto name = str(b, table + u32(b, p + 58));
            auto dir = dirs.at(u16(b, p + 62));
            std::replace(dir.begin(), dir.end(), '\\', '/');
            while (!dir.empty() && dir.front() == '/') dir.erase(0, 1);
            e.path = safePath(dir.empty() ? name : dir + "/" + name);
        }
        files.push_back(std::move(e));
    }
    std::set<size_t> seen;
    for (unsigned i = 0; i < 71; ++i) {
        size_t offset = u32(b, base + 0x3e + i * 4);
        while (offset) {
            require(seen.insert(offset).second, "cyclic cabinet group list");
            size_t desc = base + u32(b, base + offset + 4);
            auto name = str(b, base + u32(b, desc));
            size_t first = u32(b, desc + 22), last = u32(b, desc + 26);
            require(first <= last && last < files.size(), "invalid cabinet group range");
            for (size_t j = first; j <= last; ++j) files[j].groups.push_back(name);
            offset = u32(b, base + offset + 8);
        }
    }
}

void Cabinet::extract(size_t index, const Sink& sink) const {
    const auto& e = files.at(index);
    require(!(e.links & ~3), "unsupported cabinet link flags");
    if (e.links & 1) {
        require(e.previous < index && files[e.previous].size == e.size && files[e.previous].md5 == e.md5, "invalid cabinet link");
        extract(e.previous, sink); return;
    }
    // Link bit 0 follows the previous descriptor; bit 1 marks a chain head.
    // The probe verifies both chain heads and aliases against their own MD5.
    require(!(e.flags & ~4) && e.offset && !(e.links & ~3), "unsupported cabinet flags/storage: " + e.path);
    auto source = volumes_.at(e.volume).sub(e.offset, (e.flags & 4) ? e.compressed : e.size);
    Digest digest(false);
    uint64_t pos = 0, total = 0;
    auto emit = [&](const uint8_t* data, size_t n) {
        require(n <= e.size - total, "cabinet expansion exceeds declared size");
        digest.update(data, n); total += n; sink(data, n);
    };
    Bytes cache;
    uint64_t cacheBase = UINT64_MAX;
    auto read = [&](uint64_t at, size_t n) {
        require(at <= source.size() && n <= source.size() - at, "cabinet block outside source");
        if (cacheBase == UINT64_MAX || at < cacheBase || at + n > cacheBase + cache.size()) {
            cacheBase = at;
            cache = source.read(at, size_t(std::min<uint64_t>(1024*1024, source.size()-at)));
        }
        return Bytes(cache.begin() + size_t(at-cacheBase), cache.begin() + size_t(at-cacheBase) + n);
    };
    while (pos < source.size()) {
        if (!(e.flags & 4)) {
            auto bytes = source.read(pos, size_t(std::min<uint64_t>(65536, source.size() - pos)));
            emit(bytes.data(), bytes.size()); pos += bytes.size(); continue;
        }
        auto length = u16(read(pos, 2), 0); pos += 2;
        require(length != 0, "empty cabinet deflate block");
        auto block = read(pos, length); pos += length; block.push_back(0);
        std::array<uint8_t, 65537> output;
        z_stream z{};
        require(inflateInit2(&z, -15) == Z_OK, "zlib initialization failed");
        z.next_in = block.data(); z.avail_in = unsigned(block.size());
        z.next_out = output.data(); z.avail_out = unsigned(output.size());
        int status = inflate(&z, Z_FINISH);
        size_t n = z.total_out; auto remaining = z.avail_in;
        inflateEnd(&z);
        require(status == Z_STREAM_END && n <= 65536 && remaining <= 1, "invalid/oversized cabinet deflate block");
        emit(output.data(), n);
    }
    require(total == e.size && digest.finish() == e.md5, "cabinet size/MD5 mismatch: " + e.path);
}
}
