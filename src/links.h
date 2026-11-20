#pragma once

#include <filesystem>
#include <system_error>

// Links in the file system, the same way on every platform: a symbolic link, and on Windows also a
// directory junction. User mods are linked into the game data with them, and the setup refuses
// links wherever it does not expect one.
namespace setup {
	bool is_link(const std::filesystem::file_status& status);
	bool is_link(const std::filesystem::directory_entry& entry);
	// the target of a link made by link_directory (or any symbolic link); empty with `error` set otherwise
	std::filesystem::path link_target(const std::filesystem::path& link, std::error_code& error);
	// `link` leads to the directory `target`: a symbolic link, on Windows a directory junction, which
	// needs neither administrator rights nor developer mode
	void link_directory(const std::filesystem::path& target, const std::filesystem::path& link, std::error_code& error);
}
