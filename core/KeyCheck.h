#pragma once
#include <string_view>

namespace setup {
// Products are independent of disc editions: Deluxe requires both products.
enum class KeyEdition { BF2, SpecialForces };
// Local retail checksum only; no activation or ownership verdict. Accepts ASCII
// case-insensitive compact keys or five groups separated by single hyphens.
// No trimming, logging, I/O, mutable global state, or key reconstruction.
bool validateKey(KeyEdition edition, std::string_view key);
}
