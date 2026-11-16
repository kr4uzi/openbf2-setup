#pragma once

#include "launch.h"
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
		// home is the home directory the support root was derived from.
		Client(ClientLayout layout, std::filesystem::path support, std::filesystem::path home);
		~Client();

		bool Present() const;
		// starts the game on the given installation with the command line of the options and returns its
		// exit status once it has quit. the installation lock is held meanwhile, so nothing replaces the
		// game data below a running game
		task<int> Run(const Installation& installation, LaunchOptions options);
	};
}
