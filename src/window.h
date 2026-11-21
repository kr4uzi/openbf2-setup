#pragma once

#include "edition.h"
#include "key_check.h"
#include "launch.h"
#include "mods.h"
#include "patch.h"
#include "session.h"
#include "task.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <boost/asio/io_context.hpp>

namespace setup {
	enum class Page { welcome, disc, patches, keys, install, voice, launcher };

	// what the user did. `next` is the default button, its label depends on the page
	// (Continue, Install, Cancel, Save, Play)
	struct Event
	{
		// activated: the user came back to the application from another one, where they may have changed files.
		// options_edited: any change on the launcher page, the lock included
		// the other online services provider: other_url, its file from that address (in `path`); other_file, its file
		// (in `path`)
		enum class Kind { next, back, voice_setup, voice_skip, verify, options_edited, mods_folder, microphone, disc_chosen, patch141_chosen, patch150_chosen, keys_edited, activated, close, other_url, other_file, demos_folder };

		Kind kind;
		// the file of the *_chosen kinds
		std::filesystem::path path;
	};

	// the key page as the user left it. keys are compact (no hyphens), upper case and possibly incomplete
	struct KeyEntry
	{
		std::string key;
		std::string special_forces_key;
		// the checkbox in front of the second row on expansion media
		bool install_special_forces = true;
	};

	enum class KeyStatus { incomplete, invalid, valid, not_installed };
	enum class PatchStatus { checking, verified, failed };
	enum class MicrophoneStatus { skipped, ask, denied, ready, missing, capture_error };

	class Window
	{
		struct Impl;
		std::unique_ptr<Impl> m_Impl;

	public:
		explicit Window(boost::asio::io_context& context);
		~Window();

		// the event loop of the toolkit. it also runs the asio context, so the coroutines run on the thread
		// that owns the window. the window itself appears with the first page (or message): the usual start
		// of an installed game shows none
		void Run();
		void Close();

		// every click, drop and edit ends up here, one at a time and in the order it happened
		task<Event> WaitForEvent();
		// macOS Option key; Linux --launcher (also used by the desktop action).
		bool LauncherRequested() const;

		// each page replaces the one before. the file wells and the key fields keep what the user gave them,
		// so going back and forth loses nothing
		void ShowWelcome();
		void ShowDisc();
		void ShowPatches(bool included141 = false);
		void ShowKeys(bool deluxe, bool back);
		void ShowInstall();
		void ShowVoice(const assistant::VoiceSettings& settings, bool profilesExist);
		// the page of an installed game: the start-up options, Play, the voice setup and the verification.
		// the controls start out as the options say, mods fills the list, Battlefield 2 first; Play needs
		// the client. note is shown in warning colour, e.g. that the game data is not verified
		void ShowLauncher(const LaunchOptions& options, const std::vector<Mod>& mods, bool clientPresent, const std::string& note);

		// nothing can be clicked, chosen or dropped until the next page is shown; the default button reads `label`
		void SetBusy(const std::string& label);
		// the default button of the page (Continue, Install, Save, Play)
		void EnableNext(bool enabled);

		// the line and the bar below the path of a patch well. fraction is 0 to 1 and only shown while checking
		void SetPatch(PatchSlot slot, PatchStatus status, double fraction = 0);

		KeyEntry Keys() const;
		void ClearKeys();
		void SetKeyStatus(KeyEdition row, KeyStatus status);

		void SetProgress(const Progress& progress);
		// replaces the progress text, for the moment between a click on Cancel and the installer stopping
		void SetStatus(const std::string& text);

		// the launcher page as the user left it. the override is the text of the unlocked command line,
		// empty while it is locked
		LaunchOptions Options() const;
		// the read-only command line at the bottom of the launcher page. ignored while it is unlocked:
		// then the user is writing it
		void SetCommandLine(const std::string& line);
		// the kept provider file of "other": choosing Other on the page takes it
		void SetProviders(const std::map<std::string, ProviderServices>& kept);
		// a provider's file was accepted: `online` ("other") with it is now the online services, from `source`
		// ("url", "file") and `url`. a file which was not accepted changes nothing
		void ShowProvider(const std::string& online, const std::string& source, const std::string& url, const ProviderServices& services);
		// the recorded demos the dialog offers, relative to the game folder (Demos/...), the newest first
		void SetDemos(const std::vector<std::filesystem::path>& demos);

		// the sliders and the checkbox. the playback volume has no control and is the one ShowVoice received
		assistant::VoiceSettings Voice() const;
		void SetMicrophone(MicrophoneStatus status);
		// the loudest sample since the last call, 0 to 1, before the capture volume is applied
		void SetLevel(double peak);

		// a provider file from the web: https only, at most provider_file_limit bytes, a short time limit. throws with
		// the reason
		task<std::string> Download(std::string url);

		// a sheet on top of the page, it does not wait for the user
		void Message(const std::string& title, const std::string& detail);
		// opens a directory in the file manager of the system
		void Reveal(const std::filesystem::path& directory);

		// for the two checks of the real window (--smoke-test and --test-disc-selection in main_qt.cpp). the application
		// uses none of them and the fake window of the tests has none of them
		void StayHidden();
		Page Shown() const;
		// the real button or file well, as if the user had used it; the provider kinds as the dialog would send them
		void Click(Event::Kind button);
		void Choose(Event::Kind well, const std::filesystem::path& file);
		void PasteKey(KeyEdition row, const std::string& text);
		void Snapshot(const std::filesystem::path& png);
		// the Advanced Options dialog with the launcher's current options
		void SnapshotAdvanced(const std::filesystem::path& png);
		// the provider dialog of the other provider
		void SnapshotProvider(const std::filesystem::path& png);
	};
}
