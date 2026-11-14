#pragma once

#include <string>
#include <vector>

namespace setup {
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

		bool operator==(const LaunchOptions&) const = default;
	};

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
