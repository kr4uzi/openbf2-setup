#pragma once
#include "cabinet.h"

namespace setup {
// The Base variants describe installations without Special Forces, never media.
enum class DiscEdition { BF2, Deluxe, DeluxeBase, Complete, CompleteBase };
inline bool hasSpecialForces(DiscEdition edition) {
    return edition==DiscEdition::Deluxe||edition==DiscEdition::Complete;
}
inline DiscEdition installedEdition(DiscEdition edition,bool specialForces) {
    return specialForces?edition:edition==DiscEdition::Deluxe?DiscEdition::DeluxeBase:
        edition==DiscEdition::Complete?DiscEdition::CompleteBase:edition;
}
struct DiscContents {
    DiscEdition edition;
    Files base, specialForces;
};
// Component-relative views; the Complete Edition stores separate installers.
DiscContents discContents(const Files&);
View includedPatch141(const Files&);
// Detect content, never the ISO filename. Reject incomplete xpack inventories.
DiscEdition discEdition(const std::vector<Entry>&);
// Special Forces content on the Deluxe disc: destinations under mods/xpack/
// and SupportXP1/. The 1.50 overlay's own mods/xpack files are not disc content.
bool expansionContent(const std::string& destination);
DiscEdition detectDisc(const fs::path&);
std::string discDestination(const std::string& group, const std::string& path);
std::string specialForcesDestination(const std::string& group,const std::string& path);
}

namespace setup::assistant {
// Bundled content manifest resource name for an edition, without extension.
const char* manifestName(DiscEdition);
DiscEdition validateDisc(const fs::path&);
}
