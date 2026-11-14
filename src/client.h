#pragma once

#include "task.h"

#include <filesystem>
#include <memory>
#include <string>

namespace setup {
	struct Installation;

	// the game itself: the native client which is part of the application
	class Client
	{
		struct Impl;
		std::unique_ptr<Impl> m_Impl;

	public:
		// contents is the Contents directory of the application bundle. home is the home directory the support
		// root was derived from: the client derives its own from it, so both look at the same files
		Client(std::filesystem::path contents, std::filesystem::path support, std::filesystem::path home);
		~Client();

		bool Present() const;
		// starts the game on the given installation with one of its mods (the name of its directory below
		// mods/) and returns its exit status once it has quit. the installation lock is held meanwhile, so
		// nothing replaces the game data below a running game
		task<int> Run(const Installation& installation, std::string mod);
	};
}
