#pragma once

#include "client.h"
#include "installer.h"
#include "microphone.h"
#include "store.h"
#include "window.h"

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// the tests do not mock anything: they link the real setup.cpp against fake.cpp instead of the AppKit, media
// and file code. fake.cpp implements the very same classes (Window, Installer, Store, Client, Microphone) on top
// of this description of a machine and its user, and writes down what the application did and showed
namespace setup {
	struct FakeMachine
	{
		// one thing the user does. a click is only delivered once its button can be clicked, a file only once
		// its page is shown: like a user, the script waits for the window
		struct Action
		{
			Event::Kind kind;
			std::filesystem::path path;
			// for keys_edited: what the key page holds afterwards
			KeyEntry keys;
			// what the user waits for on top of that, e.g. a certain page
			std::function<bool(const FakeMachine&)> when;
		};

		// what the machine looks like
		std::map<std::string, DiscEdition> discs{ { "bf2.iso", DiscEdition::BF2 }, { "deluxe.iso", DiscEdition::Deluxe } };
		std::string patch141 = "patch141.exe";
		std::string patch150 = "patch150.exe";
		std::set<std::string> valid_keys{ "AAAAAAAAAAAAAAAAAAAA" };
		std::set<std::string> valid_special_forces_keys{ "SSSSSSSSSSSSSSSSSSSS" };
		// an installation from an earlier run
		std::optional<Installation> installed;
		bool keys_stored = false;
		bool profiles_exist = false;
		std::vector<Mod> mods{ { "bf2", "Battlefield 2" } };
		// what the user puts into the mod library while its folder is open
		std::vector<Mod> dropped_mods;
		// what the user picks in the list of mods before clicking Play, nothing: what the window preselected
		std::string chosen_mod;
		std::string stored_mod;
		bool client_present = true;
		int client_status = 0;
		bool microphone_allowed = true;
		MicrophoneAccess microphone = MicrophoneAccess::undetermined;
		bool microphone_granted_on_request = true;
		bool input_device = true;

		// what goes wrong: how often the call (as it is named in `calls`) throws before it works
		std::map<std::string, int> failures;

		// what the user does
		std::deque<Action> script;

		// what the application did
		std::vector<std::string> calls;
		std::optional<Media> installed_media;
		std::optional<bool> installed_special_forces;
		std::optional<KeyEntry> saved_keys;
		std::optional<assistant::VoiceSettings> saved_voice;
		int client_runs = 0;
		std::string client_mod;
		std::vector<std::filesystem::path> revealed;
		int mod_lookups = 0;
		bool client_running = false;
		bool metering = false;

		// what the window showed
		std::vector<Page> pages;
		bool busy = false;
		std::string busy_label;
		bool back = false;
		bool deluxe_keys = false;
		bool next_enabled = false;
		std::vector<Mod> shown_mods;
		std::string selected_mod;
		KeyEntry keys;
		std::map<PatchSlot, PatchStatus> patches;
		std::map<KeyEdition, KeyStatus> key_status;
		std::vector<std::string> phases;
		std::string status;
		std::optional<MicrophoneStatus> microphone_status;
		std::vector<double> levels;
		std::vector<std::string> messages;
		bool closed = false;
	};

	extern FakeMachine g_Machine;
}
