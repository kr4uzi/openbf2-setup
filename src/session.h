#pragma once
#include "edition.h"
#include "platform.h"

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
// BF2, Deluxe, or DeluxeBase (Deluxe media installed without Special Forces).
DiscEdition currentEdition(const fs::path& support);
// Voice chat defaults, the one file retail's BF2VoiceSetup writes
// (Profiles/BattleCommoDefaultUserSettings.con). The client reads it only
// when it creates its first profile; existing profiles keep their Audio.con.
struct VoiceSettings {
    double playbackVolume = 1.0, captureVolume = 1.0, captureThreshold = 0.1;
    bool boost = false;
};
// Four plain lines in std::ostream default formatting, e.g. "1\n1\n0.1\n0\n".
std::string voiceSettingsText(const VoiceSettings&);
// Lenient like the client: a missing or partial file keeps the defaults.
VoiceSettings parseVoiceSettings(const std::string&);
VoiceSettings loadVoiceSettings(const fs::path& support);
void saveVoiceSettings(const fs::path& support, const VoiceSettings&, Platform&);
// True once the client has created a profile directory under Profiles/.
bool profilesExist(const fs::path& support);
bool clientPresent(const fs::path& contents);
struct Launch {
    fs::path executable, directory, support;
    std::vector<std::string> arguments;
};
Launch clientLaunch(const fs::path& contents, const fs::path& support);
}
