// [CHOSEN, not recovered] Native reader for the owner's verified installer media.
// Format reference: installer-probe/tools/macos/installer/formats.py and
// twogood/unshield's MIT lib/{cabfile.h,file.c,directory.c,file_group.c}.
#include "media.h"
#include <fstream>
#include <mutex>
#include <algorithm>
#include <array>
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
std::string str(const Bytes& b, size_t o) {
    require(o < b.size(), "string outside header");
    auto end = std::find(b.begin() + o, b.end(), 0);
    require(end != b.end(), "unterminated string");
    return std::string(b.begin() + o, end);
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
}
