#pragma once
#include "media.h"
#include "progress.h"

namespace setup {
enum class PatchSlot { Patch141, Patch150 };
struct PatchIdentity { uint64_t size; std::string hash; };
const PatchIdentity& expectedPatch(PatchSlot);
void requirePatchIdentity(PatchSlot, const PatchIdentity&);
// Streaming/cancellable; UI callers run this on their worker thread.
PatchIdentity verifyPatch(const View&, PatchSlot, const Control& = {});
}

namespace setup::assistant {
void validatePatches(const fs::path& patch141, const fs::path& patch150, const Control& = {});
}
