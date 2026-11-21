#include "launch.h"

#include <cctype>
#include <algorithm>
#include <sstream>
#include <stdexcept>

setup::ClientLayout setup::macClientLayout(const std::filesystem::path& contents)
{
	return {contents / "MacOS/bf2_client", {contents / "MacOS/libdice_py.dylib", contents / "MacOS/libaidll.dylib"}};
}

setup::ClientLayout setup::linuxClientLayout(const std::filesystem::path& root)
{
	return {root / "bin/bf2_client", {root / "lib/libdice_py.so", root / "lib/libaidll.so",
		root / "lib/libdxvk_d3d9.so.0", root / "lib/libSDL3.so.0", root / "lib/libopenal.so.1"}};
}

setup::ClientLayout setup::windowsClientLayout(const std::filesystem::path& folder)
{
	return {folder / "BF2.exe", {folder / "dice_py.dll", folder / "RendDX9.dll", folder / "SwiffPlayer.dll", folder / "BF2Audio.dll"}};
}

std::filesystem::path setup::linuxDataHome(const std::filesystem::path& home, const std::string& xdg)
{
	if (!xdg.empty() && std::filesystem::path{xdg}.is_absolute())
		// Resolve existing symlinks before '..', matching the engine's filesystem traversal.
		return std::filesystem::weakly_canonical(std::filesystem::path{xdg});
	if (!home.is_absolute())
		throw std::invalid_argument{"An absolute home directory is required."};
	return home / ".local/share";
}

using namespace setup;

namespace {
	std::string trim(const std::string& s)
	{
		auto a = s.find_first_not_of(" \t\r\n");
		auto b = s.find_last_not_of(" \t\r\n");
		return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
	}

	// a line of the file must stay one line
	std::string clean(const std::string& value)
	{
		auto result = trim(value);
		std::erase_if(result, [](unsigned char c) { return c < ' '; });
		return result;
	}

	std::string unquoted(const std::string& value)
	{
		auto result = clean(value);
		std::erase(result, '"');
		return result;
	}


	int integer(const std::string& value, int low, int high, int fallback)
	{
		auto text = trim(value);
		if (text.empty() || text.size() > 7 || text.find_first_not_of("-0123456789") != std::string::npos || text.find('-', 1) != std::string::npos || text == "-")
			return fallback;

		auto parsed = std::stoi(text);
		return parsed >= low && parsed <= high ? parsed : fallback;
	}

	unsigned number(const std::string& value, unsigned low, unsigned high, unsigned fallback)
	{
		auto text = trim(value);
		if (text.empty() || text.size() > 6 || text.find_first_not_of("0123456789") != std::string::npos)
			return fallback;

		auto parsed = std::stoul(text);
		return parsed >= low && parsed <= high ? unsigned(parsed) : fallback;
	}
}

std::vector<std::string> setup::launch_arguments(const LaunchOptions& options)
{
	if (!clean(options.override).empty())
		return split_command_line(options.override);

	auto arguments = std::vector<std::string>{ "+modPath", "mods/" + options.mod };
	if (options.skip_movies)
		arguments.insert(arguments.end(), { "+restart", "1" });

	if (!options.fullscreen) {
		arguments.insert(arguments.end(), { "+fullscreen", "0" });
		if (options.width && options.height)
			arguments.insert(arguments.end(), { "+szx", std::to_string(options.width), "+szy", std::to_string(options.height) });
	}

	// a demo replaces joining a server
	auto demo = unquoted(options.demo);
	if (auto server = clean(options.server); !server.empty() && demo.empty()) {
		arguments.insert(arguments.end(), { "+joinServer", server, "+port", std::to_string(options.port) });
		if (auto password = unquoted(options.password); !password.empty())
			arguments.insert(arguments.end(), { "+password", password });
	}

	if (auto player = clean(options.player); !player.empty())
		arguments.insert(arguments.end(), { "+playerName", player });

	// the online services: the file of the chosen provider, which the game reads itself. nothing without a valid one,
	// so the game keeps the retail services
	if (valid_online(options.online) && !options.online.empty() && !options.provider.services.empty() && !options.provider_folder.empty())
		arguments.insert(arguments.end(), { "+onlineProviderConfig", (std::filesystem::path{ options.provider_folder } / provider_config_name(options.online)).string() });

	if (options.no_sound)
		arguments.insert(arguments.end(), { "+noSound", "1" });

	if (options.multi)
		arguments.insert(arguments.end(), { "+multi", "1" });



	if (!options.fullscreen && options.window_x >= 0 && options.window_y >= 0)
		arguments.insert(arguments.end(), { "+wx", std::to_string(options.window_x), "+wy", std::to_string(options.window_y) });

	if (!options.tutorial_messages)
		arguments.insert(arguments.end(), { "+tutorialMessages", "0" });

	if (options.skip_dx_check)
		arguments.insert(arguments.end(), { "+skipDXCheck", "1" });

	if (auto folder = unquoted(options.punkbuster); !folder.empty())
		arguments.insert(arguments.end(), { "+pbPath", folder });

	if (!demo.empty() && !demo_recording(demo))
		arguments.insert(arguments.end(), { "+demo", demo });
	else if (!demo.empty() && !options.demo_config.empty())
		arguments.insert(arguments.end(), { "+demo", options.demo_config });

	return arguments;
}

bool setup::has_advanced_options(const LaunchOptions& options)
{
	// the online services and the tutorial messages are on the launcher page itself
	auto defaults = LaunchOptions{};
	return options.no_sound || options.multi || options.window_x != defaults.window_x
		|| options.window_y != defaults.window_y || options.skip_dx_check || !options.punkbuster.empty()
		|| !options.demo.empty() || !clean(options.server).empty();
}

std::string setup::command_line(const std::vector<std::string>& arguments)
{
	auto line = std::string{};
	for (const auto& argument : arguments) {
		if (!line.empty())
			line += ' ';

		if (argument.empty() || argument.find_first_of(" \t\"") != std::string::npos) {
			line += '"';
			for (auto c : argument)
				line += c == '"' ? std::string{ "\\\"" } : std::string{ c };

			line += '"';
		}
		else
			line += argument;
	}

	return line;
}

std::vector<std::string> setup::split_command_line(const std::string& line)
{
	auto arguments = std::vector<std::string>{};
	auto current = std::string{};
	auto quoted = false;
	auto pending = false;
	for (auto i = size_t{ 0 }; i < line.size(); ++i) {
		auto c = line[i];
		if (c == '\\' && quoted && i + 1 < line.size() && line[i + 1] == '"')
			current += line[++i];
		else if (c == '"') {
			quoted = !quoted;
			pending = true;
		}
		else if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
			if (pending)
				arguments.push_back(current);

			current.clear();
			pending = false;
		}
		else {
			current += c;
			pending = true;
		}
	}

	if (pending)
		arguments.push_back(current);

	return arguments;
}

std::string setup::argument_mod(const std::vector<std::string>& arguments)
{
	for (auto i = size_t{ 0 }; i + 1 < arguments.size(); ++i) {
		if (arguments[i] != "+modPath")
			continue;

		auto value = arguments[i + 1];
		std::ranges::replace(value, '\\', '/');
		if (value.rfind("mods/", 0) == 0)
			value.erase(0, 5);

		return value;
	}

	return {};
}

bool setup::provider_online(const std::string& online)
{
	return online == "other";
}

bool setup::valid_online(const std::string& online)
{
	return online.empty() || provider_preset(online) || provider_online(online);
}

std::string setup::provider_config_name(const std::string& online)
{
	return online == "openspy" ? "Provider-OpenSpy.json" : online == "bf2hub" ? "Provider-BF2Hub.json" : "Provider-Other.json";
}

bool setup::demo_recording(const std::string& demo)
{
	auto name = clean(demo);
	std::ranges::transform(name, name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return name.ends_with(".bf2demo");
}

std::string setup::demo_config_text(const std::string& recording)
{
	// the game takes the rest of a line as the path, spaces included
	return "rem written by OpenBF2 to play a recording\ndemo.demomode playback\ndemo.demofile " + unquoted(recording) + "\n";
}

std::string setup::launch_text(const LaunchOptions& options)
{
	auto text = std::ostringstream{};
	text << "mod=" << options.mod << '\n'
		<< "skip_movies=" << (options.skip_movies ? 1 : 0) << '\n'
		<< "fullscreen=" << (options.fullscreen ? 1 : 0) << '\n'
		<< "width=" << options.width << '\n'
		<< "height=" << options.height << '\n'
		<< "server=" << clean(options.server) << '\n'
		<< "port=" << options.port << '\n'
		<< "password=" << unquoted(options.password) << '\n'
		<< "player=" << clean(options.player) << '\n'
		<< "override=" << clean(options.override) << '\n'
		<< "always_show=" << (options.always_show ? 1 : 0) << '\n'
		<< "client=" << (options.client == "x86" ? "x86" : "x64") << '\n'
		<< "online=" << (valid_online(options.online) ? options.online : std::string{}) << '\n'
		<< "other_source=" << clean(options.other_source) << '\n'
		<< "other_url=" << (valid_provider_url(clean(options.other_url)) ? clean(options.other_url) : std::string{}) << '\n'
		<< "no_sound=" << (options.no_sound ? 1 : 0) << '\n'
		<< "multi=" << (options.multi ? 1 : 0) << '\n'
		<< "window_x=" << options.window_x << '\n'
		<< "window_y=" << options.window_y << '\n'
		<< "tutorial_messages=" << (options.tutorial_messages ? 1 : 0) << '\n'
		<< "skip_dx_check=" << (options.skip_dx_check ? 1 : 0) << '\n'
		<< "punkbuster=" << unquoted(options.punkbuster) << '\n'
		<< "demo=" << unquoted(options.demo) << '\n';
	return text.str();
}

LaunchOptions setup::parse_launch(const std::string& text)
{
	auto options = LaunchOptions{};
	auto lines = std::istringstream{ text };
	auto line = std::string{};
	while (std::getline(lines, line)) {
		auto equals = line.find('=');
		if (equals == std::string::npos)
			continue;

		auto name = trim(line.substr(0, equals));
		auto value = line.substr(equals + 1);
		if (name == "mod") {
			if (auto mod = trim(value); !mod.empty())
				options.mod = mod;
		}
		else if (name == "skip_movies")
			options.skip_movies = trim(value) != "0";
		else if (name == "fullscreen")
			options.fullscreen = trim(value) != "0";
		else if (name == "width")
			options.width = number(value, 320, 16384, 0);
		else if (name == "height")
			options.height = number(value, 200, 16384, 0);
		else if (name == "server")
			options.server = clean(value);
		else if (name == "port")
			options.port = number(value, 1, 65535, 16567);
		else if (name == "password")
			options.password = unquoted(value);
		else if (name == "player")
			options.player = clean(value);
		else if (name == "override")
			options.override = clean(value);
		else if (name == "always_show")
			options.always_show = trim(value) == "1";
		else if (name == "client")
			options.client = trim(value) == "x86" ? "x86" : "x64";
		else if (name == "online")
			options.online = valid_online(trim(value)) ? trim(value) : std::string{};
		else if (name == "other_source")
			options.other_source = trim(value) == "url" || trim(value) == "file" ? trim(value) : std::string{};
		else if (name == "other_url")
			options.other_url = valid_provider_url(trim(value)) ? trim(value) : std::string{};

		else if (name == "no_sound")
			options.no_sound = trim(value) == "1";
		else if (name == "multi")
			options.multi = trim(value) == "1";
		else if (name == "window_x")
			options.window_x = integer(value, -1, 32767, -1);
		else if (name == "window_y")
			options.window_y = integer(value, -1, 32767, -1);
		else if (name == "tutorial_messages")
			options.tutorial_messages = trim(value) != "0";
		else if (name == "skip_dx_check")
			options.skip_dx_check = trim(value) == "1";
		else if (name == "punkbuster")
			options.punkbuster = unquoted(value);
		else if (name == "demo")
			options.demo = unquoted(value);

	}

	return options;
}
