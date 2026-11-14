#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace setup {
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
}
