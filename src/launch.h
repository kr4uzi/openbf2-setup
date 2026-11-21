#pragma once

#include "online.h"

#include <string>
#include <vector>
#include <filesystem>

namespace setup {
	// Absolute paths into an immutable application, independent of the writable generation.
	struct ClientLayout
	{
		std::filesystem::path executable;
		std::vector<std::filesystem::path> required;
	};
	ClientLayout macClientLayout(const std::filesystem::path& contents);
	ClientLayout linuxClientLayout(const std::filesystem::path& root);
	// one client package folder of the Windows application (x64 or x86): BF2.exe and its DLLs
	ClientLayout windowsClientLayout(const std::filesystem::path& folder);
	std::filesystem::path linuxDataHome(const std::filesystem::path& home, const std::string& xdg);
	// how the game is started: the choices of the options page. they only ever go on the command line of the
	// client, like retail's shortcut did; no file of the game is written for them
	struct LaunchOptions
	{
		// the directory below mods/ (what +modPath gets as mods/<mod>)
		std::string mod = "bf2";
		// +restart 1, retail's way to skip the intro movies
		bool skip_movies = true;
		// fullscreen is the game's own default, as in retail: nothing is passed for it. windowed is +fullscreen 0
		bool fullscreen = true;
		// +szx/+szy, windowed only. 0 leaves the size to the game's video settings
		unsigned width = 0;
		unsigned height = 0;
		// +joinServer <host> +port <port>. an empty host starts into the menu
		std::string server;
		unsigned port = 16567;
		// +password <password>, only together with a server and only when there is one. the engine cannot
		// take a double quote in it
		std::string password;
		// +playerName
		std::string player;
		// the free-form command line of the unlocked options page. not empty: it alone is what the game is
		// started with, everything above is ignored
		std::string override;
		// the pages at every start, not only with the Option key. no switch: the application's own
		bool always_show = false;
		// Windows only: which client starts, "x64" or "x86" (the 32-bit one, which PunkBuster servers need)
		std::string client = "x64";

		// the online services: "" the game's own (retail), "openspy" or "bf2hub" (presets the application carries) or
		// "other" (the user's provider file, Setup/Provider-Other.json). the chosen file goes to the game as
		// "+onlineProviderConfig <file>"
		std::string online;
		// where the other provider's file came from: "url" (other_url) or "file"
		std::string other_source, other_url;
		// the provider file of the chosen service: read with the options, never written to Launch.txt
		ProviderServices provider;
		// Setup/ in the support root, where the file the game reads lies (provider_config_name): read with the
		// options, never written to Launch.txt
		std::string provider_folder;

		// the advanced options. each passes its switch only when it differs from the game's default
		bool no_sound = false;        // +noSound 1
		bool multi = false;           // +multi 1: several instances at once
		// the tutorial messages. on follows the profile's own setting (on unless the player turned it off in the
		// game) and passes nothing; off is +tutorialMessages 0 for this session. an engine without the switch
		// ignores it. +help is never passed: it prints the usage and ends the start-up
		bool tutorial_messages = true;
		bool skip_dx_check = false;   // +skipDXCheck 1: no DirectX version check (Windows)
		std::string punkbuster;       // +pbPath, PunkBuster's folder for several instances (Windows, 32-bit client)
		// a recorded demo to play, which replaces joining a server: a .bf2demo, or a .con with its demo options;
		// relative to the game folder (Demos/<name>.bf2demo, where the game records) or absolute. a .con is passed
		// as +demo <con>; for a .bf2demo the launcher writes demo_config, which names it, and passes that
		std::string demo;
		// Setup/Demo.con in the support root: read with the options, never written to Launch.txt
		std::string demo_config;
		// +wx/+wy, windowed only; -1 leaves the position to the system (honoured on Windows, unverified elsewhere)
		int window_x = -1;
		int window_y = -1;

		bool operator==(const LaunchOptions&) const = default;
	};

	// the advanced options differ from their defaults: the launcher opens with them shown
	bool has_advanced_options(const LaunchOptions& options);

	// the online services which take a provider file of the user's ("other"), and whether a choice is one of the
	// launcher's
	bool provider_online(const std::string& online);
	bool valid_online(const std::string& online);
	// the name of the file in provider_folder which the game reads for an online choice: Provider-OpenSpy.json,
	// Provider-BF2Hub.json (the presets, written at each start) or Provider-Other.json (the kept file)
	std::string provider_config_name(const std::string& online);

	// a demo given as its recording (.bf2demo), which needs the .con the launcher writes for it
	bool demo_recording(const std::string& demo);
	// the .con which plays a recording: the game reads "demo.demofile" and plays it back
	std::string demo_config_text(const std::string& recording);

	// the arguments after the client executable
	std::vector<std::string> launch_arguments(const LaunchOptions& options);
	// the arguments as one line: what the options page shows, and what an override starts from. an argument
	// with a space or a quote is double-quoted
	std::string command_line(const std::vector<std::string>& arguments);
	// one line back into arguments: whitespace separates, double quotes keep what is between them together
	std::vector<std::string> split_command_line(const std::string& line);
	// the mod of a command line: the name of +modPath mods/<name>, empty without one
	std::string argument_mod(const std::vector<std::string>& arguments);

	// Setup/Launch.txt: name=value lines, one per choice, "mod=" first as it always was. a missing or unknown
	// line keeps the default; a value out of its range as well
	std::string launch_text(const LaunchOptions& options);
	LaunchOptions parse_launch(const std::string& text);
}
