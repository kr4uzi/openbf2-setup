#include "mods.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
using namespace setup;

namespace {
	std::string lowercase(std::string text)
	{
		std::ranges::transform(text, text.begin(), [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c + 32 : c); });
		return text;
	}

	std::string trim(const std::string& text)
	{
		auto first = text.find_first_not_of(" \t\r\n");
		auto last = text.find_last_not_of(" \t\r\n");
		return first == std::string::npos ? std::string{} : text.substr(first, last - first + 1);
	}

	// the game was made for a file system which ignores case, its files are spelled every which way
	// (Mod.desc, mod.desc). symlinks are never followed
	std::optional<std::filesystem::path> child(const std::filesystem::path& directory, const std::string& name)
	{
		auto error = std::error_code{};
		for (const auto& entry : std::filesystem::directory_iterator{ directory, error }) {
			if (!entry.is_symlink() && ::lowercase(entry.path().filename().string()) == name)
				return entry.path();
		}

		return std::nullopt;
	}

	std::string read(const std::filesystem::path& file)
	{
		// a description or a localization file of many megabytes is not one
		auto error = std::error_code{};
		if (std::filesystem::file_size(file, error) > 16 * 1024 * 1024 || error)
			return {};

		auto stream = std::ifstream{ file, std::ios::binary };
		return { std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
	}

	void append_utf8(std::string& text, char32_t c)
	{
		if (c < 0x80)
			text += char(c);
		else if (c < 0x800) {
			text += char(0xC0 | (c >> 6));
			text += char(0x80 | (c & 0x3F));
		}
		else if (c < 0x10000) {
			text += char(0xE0 | (c >> 12));
			text += char(0x80 | ((c >> 6) & 0x3F));
			text += char(0x80 | (c & 0x3F));
		}
		else {
			text += char(0xF0 | (c >> 18));
			text += char(0x80 | ((c >> 12) & 0x3F));
			text += char(0x80 | ((c >> 6) & 0x3F));
			text += char(0x80 | (c & 0x3F));
		}
	}

	std::string utf8(const std::string& utf16)
	{
		auto bigEndian = utf16.size() >= 2 && utf16[0] == '\xFE' && utf16[1] == '\xFF';
		auto unit = [&](size_t i) {
			auto a = static_cast<unsigned char>(utf16[i]);
			auto b = static_cast<unsigned char>(utf16[i + 1]);
			return char32_t(bigEndian ? (a << 8) | b : (b << 8) | a);
		};

		auto text = std::string{};
		for (auto i = size_t{ 0 }; i + 1 < utf16.size(); i += 2) {
			auto c = unit(i);
			if (c == 0xFEFF)
				continue;

			// two units for everything beyond the basic plane
			if (c >= 0xD800 && c < 0xDC00 && i + 3 < utf16.size()) {
				auto low = unit(i + 2);
				if (low >= 0xDC00 && low < 0xE000) {
					c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
					i += 2;
				}
			}

			::append_utf8(text, c);
		}

		return text;
	}

	// a symlink in mods/ which was made by link_mods: it names a directory of the library by its full path
	bool leads_into(const std::filesystem::path& link, const std::filesystem::path& library)
	{
		auto error = std::error_code{};
		auto target = std::filesystem::read_symlink(link, error);
		return !error && !library.empty() && target.is_absolute() && target.parent_path() == library && target.filename() == link.filename();
	}

	// titles of the two mods which come with the game, should their localization files be missing
	std::string known_title(const std::string& directory)
	{
		if (directory == "bf2")
			return "Battlefield 2";
		else if (directory == "xpack")
			return "Battlefield 2: Special Forces";

		return {};
	}

	// a mod without a level cannot be played: its own levels (levels/<name>/info/<name>.desc), or the
	// ones it says it borrows from another mod
	bool has_levels(const std::filesystem::path& mod, const std::string& description)
	{
		if (!mod_value(description, "importlevels").empty())
			return true;

		auto levels = ::child(mod, "levels");
		if (!levels)
			return false;

		auto error = std::error_code{};
		for (const auto& entry : std::filesystem::directory_iterator{ *levels, error }) {
			if (entry.is_symlink() || !entry.is_directory())
				continue;

			auto info = ::child(entry.path(), "info");
			if (info && ::child(*info, ::lowercase(entry.path().filename().string()) + ".desc"))
				return true;
		}

		return false;
	}

	// the game's own mods name a key (MODDESC_XP_TITLE) which the game looks up in its localization: the
	// English one of the mod, or of Battlefield 2, turns it into text. mods of the community write the
	// title itself
	std::string title(const std::filesystem::path& game, const std::filesystem::path& mod, const std::string& description)
	{
		auto name = mod.filename().string();
		auto written = mod_value(description, "title");
		auto isKey = !written.empty() && written.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string::npos;
		if (!isKey)
			return written.empty() ? name : written;

		for (const auto& root : { mod, game / "mods" / "bf2" }) {
			auto localization = ::child(root, "localization");
			auto english = localization ? ::child(*localization, "english") : std::nullopt;
			if (!english)
				continue;

			auto error = std::error_code{};
			for (const auto& entry : std::filesystem::directory_iterator{ *english, error }) {
				if (entry.is_symlink() || ::lowercase(entry.path().extension().string()) != ".utxt")
					continue;

				if (auto text = localized(::read(entry.path()), written); !text.empty())
					return text;
			}
		}

		// like the game: a key nobody knows is shown as it is
		auto known = ::known_title(::lowercase(name));
		return known.empty() ? written : known;
	}
}

std::filesystem::path setup::mod_library(const std::filesystem::path& support)
{
	return support / "CustomMods";
}

void setup::link_mods(const std::filesystem::path& library, const std::filesystem::path& game)
{
	auto error = std::error_code{};
	auto taken = std::set<std::string>{};
	for (const auto& entry : std::filesystem::directory_iterator{ game / "mods", error }) {
		// a link whose mod is gone would only be a dead entry in the game's own list of mods
		if (entry.is_symlink() && ::leads_into(entry.path(), library) && !std::filesystem::is_directory(entry.path(), error))
			std::filesystem::remove(entry.path(), error);
		else
			taken.insert(::lowercase(entry.path().filename().string()));
	}

	for (const auto& entry : std::filesystem::directory_iterator{ library, error }) {
		auto name = entry.path().filename().string();
		if (entry.is_symlink() || !entry.is_directory() || name.starts_with('.') || !::child(entry.path(), "mod.desc"))
			continue;

		if (taken.insert(::lowercase(name)).second)
			std::filesystem::create_directory_symlink(entry.path(), game / "mods" / name, error);
	}
}

std::vector<Mod> setup::find_mods(const std::filesystem::path& game, DiscEdition edition, const std::filesystem::path& library)
{
	auto mods = std::vector<Mod>{};
	auto error = std::error_code{};
	for (const auto& entry : std::filesystem::directory_iterator{ game / "mods", error }) {
		if (!entry.is_directory() || (entry.is_symlink() && !::leads_into(entry.path(), library)))
			continue;

		auto name = entry.path().filename().string();
		auto file = ::child(entry.path(), "mod.desc");
		if (!file || name.starts_with('.') || (::lowercase(name) == "xpack" && edition != DiscEdition::Deluxe))
			continue;

		auto description = ::read(*file);
		if (::has_levels(entry.path(), description))
			mods.push_back(Mod{ name, ::title(game, entry.path(), description) });
	}

	// Battlefield 2, Special Forces, then the rest by name
	auto rank = [](const Mod& mod) {
		auto name = ::lowercase(mod.directory);
		return name == "bf2" ? 0 : name == "xpack" ? 1 : 2;
	};
	std::ranges::sort(mods, [&](const Mod& a, const Mod& b) {
		return rank(a) != rank(b) ? rank(a) < rank(b) : ::lowercase(a.title) < ::lowercase(b.title);
	});
	return mods;
}

std::string setup::mod_value(const std::string& description, const std::string& tag)
{
	auto tags = ::lowercase(description);
	auto open = "<" + tag + ">";
	auto begin = tags.find(open);
	auto end = tags.find("</" + tag + ">");
	if (begin == std::string::npos || end == std::string::npos || end < begin)
		return {};

	return ::trim(description.substr(begin + open.size(), end - begin - open.size()));
}

std::string setup::localized(const std::string& utxt, const std::string& key)
{
	auto text = ::utf8(utxt);
	auto line = size_t{ 0 };
	while (line < text.size()) {
		auto end = text.find('\n', line);
		if (end == std::string::npos)
			end = text.size();

		// the key is the first word of its line
		if (text.compare(line, key.size(), key) == 0 && line + key.size() < end && (text[line + key.size()] == ' ' || text[line + key.size()] == '\t'))
			return ::trim(text.substr(line + key.size(), end - line - key.size()));

		line = end + 1;
	}

	return {};
}
