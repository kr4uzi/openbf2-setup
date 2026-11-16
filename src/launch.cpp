#include "launch.h"
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

	if (auto server = clean(options.server); !server.empty()) {
		arguments.insert(arguments.end(), { "+joinServer", server, "+port", std::to_string(options.port) });
		if (auto password = unquoted(options.password); !password.empty())
			arguments.insert(arguments.end(), { "+password", password });
	}

	if (auto player = clean(options.player); !player.empty())
		arguments.insert(arguments.end(), { "+playerName", player });

	return arguments;
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
		<< "always_show=" << (options.always_show ? 1 : 0) << '\n';
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
	}

	return options;
}
