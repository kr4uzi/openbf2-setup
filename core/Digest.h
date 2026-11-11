#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace setup {
// Streaming portable MD5 (cabinet integrity) and SHA-256 (content identity).
class Digest {
    bool sha_;
    std::array<uint32_t,8> h_;
    std::array<uint8_t,64> block_{};
    uint64_t bytes_ = 0;
    size_t used_ = 0;
    void compress(const uint8_t*);
public:
    explicit Digest(bool sha = true);
    void update(const uint8_t*, size_t);
    std::string finish();
};
}
