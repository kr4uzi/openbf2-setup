#pragma once

#include "installer.h"
#include "task.h"
#include "window.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>

namespace setup {
	class Client;
	class Microphone;
	class Store;

	class Setup
	{
		// a patch well: what was chosen and what the check of it said. a newer choice replaces a running check
		struct Patch
		{
			std::filesystem::path path;
			bool verified = false;
			std::shared_ptr<std::atomic<bool>> cancelled;
		};

		Window& m_Window;
		Installer& m_Installer;
		Store& m_Store;
		Client& m_Client;
		Microphone& m_Microphone;

		Media m_Media;
		std::array<Patch, 2> m_Patches;

	public:
		Setup(Window& window, Installer& installer, Store& store, Client& client, Microphone& microphone);

		// the whole application, returns the exit code
		task<int> Launch();

	private:
		task<std::optional<Installation>> FindInstallation();
		task<Installation> InstallGame();
		task<void> Ready(const Installation& installation);

		// the pages of the assistant. false: the user went back
		task<void> Welcome();
		task<bool> ChooseDisc();
		task<bool> ChoosePatches();
		task<bool> EnterKeys(bool deluxe, bool back);
		// for an installation whose keys are gone: asks until they are saved
		task<void> EnterMissingKeys(bool deluxe);
		// nothing if the installation was cancelled or failed: it can be resumed from the key page
		task<std::optional<Installation>> Install();
		task<void> VoiceSetup();
		task<void> Play(const Installation& installation, std::string mod);

		task<void> CheckPatch(PatchSlot slot, std::filesystem::path path);
		bool ShowKeyStatus(bool deluxe);
		void ShowMicrophone();

		// like Window::WaitForEvent, but closing the window ends the application
		task<Event> NextEvent();
		task<void> CancelOnClose();
		task<void> RefuseClose();
	};
}
