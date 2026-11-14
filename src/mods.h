#pragma once

#include "edition.h"

#include <filesystem>
#include <string>
#include <vector>

namespace setup {
	struct Mod
	{
		// the name of its directory below mods/, as it is spelled on disk. this is what +modPath gets
		std::string directory;
		// what the game calls it, from its mod.desc
		std::string title;

		bool operator==(const Mod&) const = default;
	};

	// where the user keeps mods of their own: <support root>/CustomMods/<name>. a reinstallation makes a new
	// generation of the game data, mods inside the old one would stay behind. (not "Mods": the client keeps
	// caches in <support root>/mods, and the usual macOS file system does not tell the two names apart)
	std::filesystem::path mod_library(const std::filesystem::path& support);

	// the game only knows mods as mods/<name> below its working directory, so every mod of the library gets
	// a symlink there. a name which is taken in the game (bf2, xpack, a mod copied there) is left alone, and
	// links to mods which left the library are removed. nothing else in the game data is touched
	void link_mods(const std::filesystem::path& library, const std::filesystem::path& game);

	// the mods of an installed game which can be started: every directory below mods/ with a mod.desc and
	// at least one level. Battlefield 2 itself comes first. a regular installation carries a stub of
	// Special Forces (the 1.50 patch puts some of its files there): it has no levels, and Special Forces is
	// only offered by an edition which installed it anyway. a symlink counts if it leads into the library
	std::vector<Mod> find_mods(const std::filesystem::path& game, DiscEdition edition, const std::filesystem::path& library = {});

	// what a mod.desc has between <tag> and </tag>, e.g. its title
	std::string mod_value(const std::string& description, const std::string& tag);
	// the text of a key in a localization file of the game (.utxt: UTF-16 with a byte order mark,
	// one "KEY  text" per line), as UTF-8. empty if the file does not have the key
	std::string localized(const std::string& utxt, const std::string& key);
}
