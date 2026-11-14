#pragma once
#include <filesystem>
#include <cstdint>
#include <memory>
namespace setup {
// All OS-specific locking, durable publication and private-file policy live here.
// Implementations must reject symlink lock files and flush file/directory metadata.
struct Lock { virtual ~Lock() = default; };
struct Platform {
    virtual ~Platform() = default;
    virtual std::unique_ptr<Lock> lock(const std::filesystem::path&) = 0;
    virtual void syncFile(const std::filesystem::path&) = 0;
    virtual void syncDirectory(const std::filesystem::path&) = 0;
    virtual void replace(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
    // Injectable for deterministic ENOSPC preflight tests without filling a disk.
    virtual uint64_t availableSpace(const std::filesystem::path& p) { return std::filesystem::space(p).available; }
};
std::unique_ptr<Platform> nativePlatform();
}
