#pragma once
#include "Installer.h"

namespace setup::assistant {
// These helpers are shared by AppKit and headless checks. Keys never enter errors.
std::string normalizedKey(const std::string&);
// Serialization/format normalization only; saveKeys and registryHasKey enforce validity.
std::string registryWithKey(const std::string& registry, const std::string& key, bool specialForces = false);
bool registryHasKey(const std::string& registry, bool specialForces = false);
void saveKeys(const fs::path& support, const std::string& key, const std::string& specialForcesKey, Platform&);
void saveKey(const fs::path& support, const std::string& key, Platform&);
fs::path currentGeneration(const fs::path& support);
bool storedKey(const fs::path& support, bool specialForces = false);
DiscEdition currentEdition(const fs::path& support);
DiscEdition validateDisc(const fs::path&);
void validatePatches(const fs::path& patch141, const fs::path& patch150, const Control& = {});
bool clientPresent(const fs::path& contents);
struct Launch {
    fs::path executable, directory, support;
    std::vector<std::string> arguments;
};
Launch clientLaunch(const fs::path& contents, const fs::path& support);
}
