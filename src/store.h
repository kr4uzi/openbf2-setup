#pragma once

#include "edition.h"
#include "key_check.h"
#include "mods.h"
#include "session.h"

#include <filesystem>
#include <string>

namespace setup {
	struct KeyEntry;
	struct Installation;

	// what the setup keeps below the support root (~/Library/Application Support/OpenBF2) apart from the game
	// data: the CD keys in Registry.con and the voice defaults for the first profile
	class Store
	{
		std::filesystem::path m_Support;

	public:
		explicit Store(std::filesystem::path support);

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
		// the mod the user played last (the name of its directory), empty before the first choice
		std::string LoadMod() const;
		void SaveMod(const std::string& mod);

		// a missing or unreadable file gives the defaults
		assistant::VoiceSettings LoadVoice() const;
		void SaveVoice(const assistant::VoiceSettings& settings);
		// existing profiles keep their own voice settings, the saved ones only seed new profiles
		bool ProfilesExist() const;
	};
}
