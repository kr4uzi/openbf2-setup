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
			bool included = false;
		};

		Window& m_Window;
		Installer& m_Installer;
		Store& m_Store;
		Client& m_Client;
		Microphone& m_Microphone;

		Media m_Media;
		// an existing installation the user chose, and what it holds (existing.h)
		struct Existing
		{
			std::filesystem::path folder;
			DiscEdition edition = DiscEdition::BF2;
		} m_Existing;
		std::array<Patch, 2> m_Patches;
		// the window was closed while an installation or a verification ran: once that has stopped, Launch ends
		bool m_Closing = false;
		// the options as the launcher page last loaded or saved them: only a change is written
		LaunchOptions m_Saved;
		bool m_Unverified = false;
		// the last download of an expired provider file failed: the launcher says so
		bool m_RefreshFailed = false;
		unsigned m_PendingPatchChecks = 0;

	public:
		Setup(Window& window, Installer& installer, Store& store, Client& client, Microphone& microphone);

		// the whole application, returns the exit code
		task<int> Launch();

	private:
		// the installation as the quick check (sizes and times against the record of the last full
		// verification) finds it. m_Unverified says when that check did not pass: the full verification is
		// the user's to ask for
		task<std::optional<Installation>> FindInstallation();
		task<Installation> InstallGame();
		// the page of an installed game: options, Play, voice setup, verification. until the window is
		// closed, or true when the game data failed its verification and is to be installed again
		task<bool> Launcher(const Installation& installation);
		// the full verification, from the button. false: it failed and the user was told
		task<bool> VerifyGameData();

		// the pages of the assistant. false: the user went back
		// true: install from disc; false: use an existing installation
		task<bool> Welcome();
		task<bool> ChooseDisc();
		task<bool> ChooseExisting();
		// nothing if the folder did not pass or linking was cancelled: the key page is shown again
		task<std::optional<Installation>> LinkExisting();
		task<bool> ChoosePatches();
		task<bool> EnterKeys(bool deluxe, bool back);
		// for an installation whose keys are gone: asks until they are saved
		task<void> EnterMissingKeys(bool deluxe);
		// nothing if the installation was cancelled or failed: it can be resumed from the key page
		task<std::optional<Installation>> Install();
		task<void> VoiceSetup();
		// true once the game has run and quit cleanly
		task<bool> Play(const Installation& installation, const LaunchOptions& options);
		// a provider's file from the source the user chose in Online services; kept and in use when it is valid
		task<void> ConfigureProvider(const Installation& installation, const Event& event);
		// a downloaded provider file which no longer holds (its valid_to has passed) is downloaded again
		task<void> RefreshProvider();
		// the saved options with a mod which is installed; links the mods of the library on the way
		LaunchOptions LoadOptions(const Installation& installation);
		void ShowLauncher(const Installation& installation, const LaunchOptions& options);
		// the page with the saved options and the mods of the moment
		void ReloadLauncher(const Installation& installation);
		// false, and the user told, if changed options could not be written to Setup/Launch.txt
		bool SaveOptions();

		task<void> CheckPatch(PatchSlot slot, std::filesystem::path path, bool included = false);
		bool ShowKeyStatus(bool deluxe);
		void ShowMicrophone();

		// like Window::WaitForEvent, but closing the window ends the application
		task<Event> NextEvent();
		task<void> CancelOnClose();
		task<void> RefuseClose();
	};
}
