#pragma once
#include "cabinet.h"

namespace setup {
// DeluxeBase is an installed edition only: Deluxe media installed without
// Special Forces. discEdition/detectDisc never return it; receipts carry it.
enum class DiscEdition { BF2, Deluxe, DeluxeBase };
// Detect content, never the ISO filename. Reject incomplete xpack inventories.
DiscEdition discEdition(const std::vector<Entry>&);
// Special Forces content on the Deluxe disc: destinations under mods/xpack/
// and SupportXP1/. The 1.50 overlay's own mods/xpack files are not disc content.
bool expansionContent(const std::string& destination);
DiscEdition detectDisc(const fs::path&);
std::string discDestination(const std::string& group, const std::string& path);
}

namespace setup::assistant {
// Bundled content manifest resource name for an edition, without extension.
const char* manifestName(DiscEdition);
DiscEdition validateDisc(const fs::path&);
}
