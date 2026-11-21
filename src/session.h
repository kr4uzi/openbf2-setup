#pragma once
#include "edition.h"
#include "launch.h"
#include <optional>
#include "mods.h"
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
// The installed edition, including base-only Deluxe and Complete installations.
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
// profileHome is where Profiles/ lives: the support root on macOS and Linux, Documents\Battlefield 2 on
// Windows (retail's @HOME@); empty means the support root
void saveVoiceSettings(const fs::path& support, const VoiceSettings&, Platform&, const fs::path& profileHome = {});
// True once the client has created a profile directory under Profiles/.
bool profilesExist(const fs::path& support);
bool clientPresent(const ClientLayout& layout);
inline bool clientPresent(const fs::path& contents) { return clientPresent(macClientLayout(contents)); }
struct Launch {
    fs::path executable, directory, support;
    std::vector<std::string> arguments;
};
// A preset's provider file (OpenSpy, BF2Hub) in the options' provider folder, where the game reads it; written at
// every start, so the game reads this version's preset. Nothing for another choice.
void writeProviderPreset(const LaunchOptions& options, Platform&);
// The mod of the options (or of +modPath in an override) must be a directory below mods/ of the
// current generation; an override without +modPath leaves the mod to the client.
#if defined(_WIN32)
// whether the system's ANSI code page shows every character of a UTF-8 path: the client reads its command line in
// it (this application runs with UTF-8), so a path with a character outside it reaches the game changed. a system
// which uses UTF-8 itself takes every path
bool systemAnsiTakes(const std::string& utf8);
#endif
Launch clientLaunch(const ClientLayout& layout, const fs::path& support, const LaunchOptions& options);
inline Launch clientLaunch(const fs::path& contents, const fs::path& support, const LaunchOptions& options) { return clientLaunch(macClientLayout(contents), support, options); }
Launch clientLaunch(const fs::path& contents, const fs::path& support, const std::string& mod = "bf2");
// The launcher's own choices (Setup/Launch.txt, see launch.h); the game's files are never
// written for them. An unreadable or missing file gives the defaults.
// The options carry the services of the chosen online service: the OpenSpy or BF2Hub preset, or the kept provider
// file of the other provider; none when that file is missing or invalid. A preset's file is written to
// Setup/Provider-OpenSpy.json or Setup/Provider-BF2Hub.json by clientLaunch, where the game reads it.
LaunchOptions loadLaunch(const fs::path& support);
// The demos of the game folder (Demos/, where the game records): its .bf2demo and .con files, relative to the
// game folder, the newest first.
std::vector<fs::path> findDemos(const fs::path& game);
void saveLaunch(const fs::path& support, const LaunchOptions& options, Platform&);
// The provider file of "other" (provider_online): checked (parse_provider), then kept as Setup/Provider-Other.json, replacing an
// earlier one. A file which is not valid throws with the reason and leaves the kept one as it was.
ProviderServices importProvider(const fs::path& support, const std::string& online, const fs::path& file, Platform&);
ProviderServices keepProvider(const fs::path& support, const std::string& online, const std::string& json, Platform&);
// The kept provider file's services; nothing when it is missing or no longer valid.
std::optional<ProviderServices> storedProvider(const fs::path& support, const std::string& online);
}
