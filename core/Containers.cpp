// [CHOSEN, not recovered] Native reader for the owner's verified installer media.
// Format reference: installer-probe/tools/macos/installer/formats.py and
// twogood/unshield's MIT lib/{cabfile.h,file.c,directory.c,file_group.c}.
#include "Installer.h"
#include "Digest.h"
#include <fstream>
#include <mutex>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>

#include <limits>
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
void insert(Files& out, const std::string& path, const View& v) {
    require(out.emplace(lower(safePath(path)), v).second, "duplicate container pathname: " + path);
}
}

struct View::File {
    fs::path path;
    std::ifstream stream;
    std::mutex mutex;
    explicit File(const fs::path& source) : path(source) {
        require(!fs::is_symlink(fs::symlink_status(path)) && fs::is_regular_file(path), "input is not a regular file");
    }
};
View::View(const fs::path& path) : file_(std::make_shared<File>(path)) {
    size_ = fs::file_size(path);
}
View View::sub(uint64_t offset, uint64_t length) const {
    require(offset <= size_ && length <= size_ - offset, "range outside input");
    View out = *this; out.base_ += offset; out.size_ = length; return out;
}
void View::readInto(uint64_t offset,uint8_t* data,size_t length) const {
    sub(offset,length);
    require(length<=size_t(std::numeric_limits<std::streamsize>::max()),"read exceeds stream address space");
    std::lock_guard<std::mutex> lock(file_->mutex);
    if(!file_->stream.is_open()) {
        require(!fs::is_symlink(fs::symlink_status(file_->path)), "symlink input");
        file_->stream.open(file_->path,std::ios::binary);require(bool(file_->stream),"cannot open input: "+file_->path.string());
    }
    file_->stream.clear();file_->stream.seekg(std::streamoff(base_+offset));
    if(length)file_->stream.read(reinterpret_cast<char*>(data),std::streamsize(length));
    require(bool(file_->stream),"truncated/unreadable input");
}
Bytes View::read(uint64_t offset,size_t length) const {
    sub(offset,length);Bytes out(length);readInto(offset,out.data(),length);return out;
}

Bytes View::all() const {
    require(size_ <= std::numeric_limits<size_t>::max(), "input exceeds address space");
    return read(0, size_t(size_));
}
void View::stream(const Sink& sink) const {
    for (uint64_t pos = 0; pos < size_;) {
        auto b = read(pos, size_t(std::min<uint64_t>(1024 * 1024, size_ - pos)));
        sink(b.data(), b.size()); pos += b.size();
    }
    // Directory providers/comparison inventories may contain thousands of Views.
    // Open lazily and release completed stream handles; random cabinet reads keep
    // their shared handles until the input view is destroyed.
    std::lock_guard<std::mutex> lock(file_->mutex);file_->stream.close();
}
std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}
std::string safePath(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    require(!text.empty() && text.front() != '/' && text.find(':') == std::string::npos, "unsafe pathname: " + text);
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('/', start); if (end == std::string::npos) end = text.size();
        auto part = text.substr(start, end - start);
        require(!part.empty() && part != "." && part != "..", "unsafe path component: " + text);
        require(part.back()!='.' && part.back()!=' ' && part.find_first_of("<>\"|?*")==std::string::npos, "ambiguous Windows pathname");
        auto device=lower(part.substr(0,part.find('.')));
        require(device!="con"&&device!="prn"&&device!="aux"&&device!="nul"&&
            !(device.size()==4&&(device.substr(0,3)=="com"||device.substr(0,3)=="lpt")&&device[3]>='1'&&device[3]<='9'), "reserved Windows device pathname");
        for (unsigned char c : part) require(c >= 32 && c < 127, "unsupported pathname encoding");
        start = end + 1;
    }
    require(text.back() != '/', "trailing slash in file path");
    return text;
}

Files readIso(const View& image) {
    Bytes selected;
    bool joliet = false;
    for (unsigned sector = 16; sector < 64; ++sector) {
        auto d = image.read(sector * 2048, 2048);
        if (std::memcmp(d.data() + 1, "CD001", 5) != 0 || (d[0] != 1 && d[0] != 2)) continue;
        bool j = d[0] == 2 && d[88] == '%' && d[89] == '/' && (d[90] == '@' || d[90] == 'C' || d[90] == 'E');
        if (d[0] == 1 && selected.empty()) selected = d;
        if (j) { selected = d; joliet = true; }
    }
    require(!selected.empty() && u16(selected, 128) == 2048, "unsupported ISO volume/block size");
    Files files;
    std::set<uint64_t> visited;
    std::function<void(uint64_t, uint32_t, std::string, unsigned)> visit;
    visit = [&](uint64_t extent, uint32_t length, std::string parent, unsigned depth) {
        require(depth < 64 && visited.insert(extent).second && length <= 16 * 1024 * 1024, "cyclic/oversized ISO directory");
        auto data = image.read(extent, length);
        for (size_t pos = 0; pos < data.size();) {
            auto len = data[pos];
            if (!len) { pos = (pos / 2048 + 1) * 2048; continue; }
            require(len >= 34 && len <= data.size() - pos && pos % 2048 + len <= 2048, "invalid ISO record");
            Bytes r(data.begin() + pos, data.begin() + pos + len); pos += len;
            auto count = r[32]; require(33u + count <= r.size(), "invalid ISO name");
            if (count == 1 && r[33] <= 1) continue;
            require(!(r[25] & 0x80) && !r[26] && !r[27], "unsupported ISO extent/interleave");
            auto be32 = [&](size_t o) { return (uint32_t(r[o]) << 24) | (uint32_t(r[o+1]) << 16) | (uint32_t(r[o+2]) << 8) | r[o+3]; };
            require(u32(r, 2) == be32(6) && u32(r, 10) == be32(14), "ISO dual-endian mismatch");
            std::string name;
            if (joliet) {
                require(count % 2 == 0, "odd Joliet name");
                for (unsigned i = 0; i < count; i += 2) {
                    require(r[33+i] == 0 && r[34+i] < 127, "unsupported Joliet name encoding"); name += char(r[34+i]);
                }
            } else name.assign(r.begin() + 33, r.begin() + 33 + count);
            name = name.substr(0, name.find(';'));
            auto path = safePath(parent + name);
            uint64_t offset = uint64_t(u32(r, 2)) * 2048;
            auto view = image.sub(offset, u32(r, 10));
            if (r[25] & 2) visit(offset, u32(r, 10), path + "/", depth + 1);
            else insert(files, path, view);
        }
    };
    visit(uint64_t(u32(selected, 158)) * 2048, u32(selected, 166), "", 0);
    require(files.count("data1.hdr") && files.count("data1.cab") && files.count("data2.cab"), "wrong disc: BF2 cabinet set absent");
    return files;
}

Files readDisc(const fs::path& source) {
    if (!fs::is_directory(source)) return readIso(View(source));
    require(!fs::is_symlink(fs::symlink_status(source)), "symlink disc directory");
    Files files;
    for (const auto& e : fs::recursive_directory_iterator(source)) {
        require(!fs::is_symlink(e.symlink_status()), "symlink on disc");
        if (e.is_directory()) continue;
        require(e.is_regular_file(), "nonregular disc file");
        insert(files, e.path().lexically_relative(source).generic_string(), View(e.path()));
    }
    require(files.count("data1.hdr") && files.count("data1.cab") && files.count("data2.cab"), "DVD cabinet inventory missing");
    return files;
}

Files readSfx(const View& image) {
    auto d = image.read(0, size_t(std::min<uint64_t>(image.size(), 65536)));
    require(d.size() >= 64 && d[0] == 'M' && d[1] == 'Z', "patch is not PE");
    size_t pe = u32(d, 60);
    require(pe <= d.size() && 24 <= d.size() - pe && u32(d, pe) == 0x4550 && u16(d, pe + 4) == 0x14c, "patch is not x86 PE");
    size_t optional = pe + 24;
    require(u16(d, optional) == 0x10b, "patch is not PE32");
    size_t sections = optional + u16(d, pe + 20);
    auto n = u16(d, pe + 6); require(n > 0 && n < 128, "invalid PE section count");
    uint64_t pos = 0;
    for (unsigned i = 0; i < n; ++i) {
        size_t s = sections + i * 40;
        pos = std::max(pos, uint64_t(u32(d, s + 20)) + u32(d, s + 16));
    }
    require(pos < image.size(), "missing SFX overlay");
    Files files;
    while (pos < image.size()) {
        auto fields = image.read(pos, size_t(std::min<uint64_t>(2048, image.size() - pos)));
        size_t cursor = 0;
        std::array<std::string, 4> values;
        for (auto& value : values) { value = str(fields, cursor); cursor += value.size() + 1; }
        require(!values[0].empty() && !values[1].empty() && !values[3].empty(), "invalid SFX header");
        require(fs::path(safePath(values[1])).filename() == values[0], "SFX name/path mismatch");
        uint64_t length = 0;
        for (char c : values[3]) {
            require(c >= '0' && c <= '9' && length <= (UINT64_MAX - 9) / 10, "invalid SFX size");
            length = length * 10 + unsigned(c - '0');
        }
        pos += cursor;
        insert(files, values[1], image.sub(pos, length)); pos += length;
        require(files.size() < 10000, "too many SFX records");
    }
    require(pos == image.size(), "unparsed SFX trailer");
    return files;
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
