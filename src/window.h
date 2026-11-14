#pragma once

#include "edition.h"
#include "key_check.h"
#include "mods.h"
#include "patch.h"
#include "session.h"
#include "task.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <boost/asio/io_context.hpp>

namespace setup {
	enum class Page { welcome, disc, patches, keys, install, voice, ready };

	// what the user did. `next` is the default button, its label depends on the page
	// (Continue, Install, Cancel, Save, Play)
	struct Event
	{
		// activated: the user came back to the application from another one, where they may have changed files
		enum class Kind { next, back, voice_setup, mods_folder, microphone, disc_chosen, patch141_chosen, patch150_chosen, keys_edited, activated, close };

		Kind kind;
		// the file of the three *_chosen kinds
		std::filesystem::path path;
	};

	// the key page as the user left it. keys are compact (no hyphens), upper case and possibly incomplete
	struct KeyEntry
	{
		std::string key;
		std::string special_forces_key;
		// the checkbox in front of the second row, Deluxe media only
		bool install_special_forces = true;
	};

	enum class KeyStatus { incomplete, invalid, valid, not_installed };
	enum class PatchStatus { checking, verified, failed };
	enum class MicrophoneStatus { skipped, ask, denied, ready, missing };

	class Window
	{
		struct Impl;
		std::unique_ptr<Impl> m_Impl;

	public:
		explicit Window(boost::asio::io_context& context);
		~Window();

		// the event loop of the toolkit. it also runs the asio context, so the coroutines run on the thread
		// that owns the window
		void Run();
		void Close();

		// every click, drop and edit ends up here, one at a time and in the order it happened
		task<Event> WaitForEvent();

		// each page replaces the one before. the file wells and the key fields keep what the user gave them,
		// so going back and forth loses nothing
		void ShowWelcome();
		void ShowDisc();
		void ShowPatches();
		void ShowKeys(bool deluxe, bool back);
		void ShowInstall();
		void ShowVoice(const assistant::VoiceSettings& settings, bool profilesExist);
		// the mods can be chosen from if there is more than Battlefield 2 itself. selected is the directory
		// of the one which is chosen to begin with
		void ShowReady(bool clientPresent, const std::vector<Mod>& mods, const std::string& selected);

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

		// the directory of the mod chosen on the Ready page
		std::string SelectedMod() const;

		// the sliders and the checkbox. the playback volume has no control and is the one ShowVoice received
		assistant::VoiceSettings Voice() const;
		void SetMicrophone(MicrophoneStatus status);
		// the loudest sample since the last call, 0 to 1, before the capture volume is applied
		void SetLevel(double peak);

		// a sheet on top of the page, it does not wait for the user
		void Message(const std::string& title, const std::string& detail);
		// opens a directory in the file manager of the system
		void Reveal(const std::filesystem::path& directory);

		// for the two checks of the real window (--smoke-test and --test-disc-selection in main.mm). the application
		// uses none of them and the fake window of the tests has none of them
		void StayHidden();
		Page Shown() const;
		// the real button or file well, as if the user had used it
		void Click(Event::Kind button);
		void Choose(Event::Kind well, const std::filesystem::path& file);
		void PasteKey(KeyEdition row, const std::string& text);
		void Snapshot(const std::filesystem::path& png);
	};
}
