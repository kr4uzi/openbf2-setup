#pragma once

#include "edition.h"
#include "key_check.h"
#include "launch.h"
#include "mods.h"
#include "session.h"

#include <filesystem>
#include <map>
#include <string>

namespace setup {
	struct KeyEntry;
	struct Installation;

	// what the setup keeps below the support root (~/Library/Application Support/OpenBF2) apart from the game
	// data: the CD keys in Registry.con, the start-up options and the voice defaults for the first profile
	class Store
	{
		std::filesystem::path m_Support;
		// where the game keeps Profiles/: the support root on macOS and Linux, Documents\Battlefield 2 on Windows
		std::filesystem::path m_ProfileHome;

	public:
		explicit Store(std::filesystem::path support, std::filesystem::path profileHome = {});

		// Setup/Current.txt names a generation. whether it is intact is for Installer::Verify to say
		bool HasInstallation() const;

		// the local check of the retail key format, no statement about ownership
		bool ValidKey(KeyEdition edition, const std::string& key) const;
		// a Deluxe installation needs the key of Special Forces as well
		bool HasKeys(DiscEdition edition) const;
		void SaveKeys(const KeyEntry& keys);

		// where the user puts mods of their own. it is made on the first call
		std::filesystem::path ModLibrary();
		// what Play can start, Battlefield 2 first. the mods of the library are linked into the installation
		// on the way, which is how the game finds them
		std::vector<Mod> Mods(const Installation& installation);
		// the recorded demos of the game folder (Demos/), relative to it, the newest first; the folder itself, made
		// on the first call
		std::vector<std::filesystem::path> Demos(const Installation& installation);
		std::filesystem::path DemosFolder(const Installation& installation);
		// the start-up options (Setup/Launch.txt), the defaults before the first save
		LaunchOptions LoadLaunch() const;
		void SaveLaunch(const LaunchOptions& options);
		// checks the other provider's file the user chose, or its downloaded text, and keeps it (Setup/Provider-Other.json); throws
		// with the reason when it is not valid
		ProviderServices ImportProvider(const std::string& online, const std::filesystem::path& file);
		ProviderServices KeepProvider(const std::string& online, const std::string& json);
		// the kept provider file of "other" when it is valid
		std::map<std::string, ProviderServices> KeptProviders() const;

		// a missing or unreadable file gives the defaults
		assistant::VoiceSettings LoadVoice() const;
		void SaveVoice(const assistant::VoiceSettings& settings);
		// existing profiles keep their own voice settings, the saved ones only seed new profiles
		bool ProfilesExist() const;
	};
}
