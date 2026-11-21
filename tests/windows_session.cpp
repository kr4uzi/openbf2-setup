// The Windows parts of the session helpers: the CD key file protected for the user (Registry.dat), the voice
// defaults in the game's profile folder, directory junctions for the user's mods, and the client choice.
// A real CD key never belongs in the repository: the saved-and-read-back check runs only with the owner's
// own key in BF2_TEST_KEY, as tests/key_check.cpp does.
#include "launch.h"
#include "links.h"
#include "mods.h"
#include "platform.h"
#include "session.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dpapi.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace session = setup::assistant;

namespace {
	unsigned checks = 0;

	void check(bool ok, const char* what)
	{
		if (!ok)
			throw std::runtime_error{ what };

		++checks;
	}

	template<typename F>
	void rejects(F f, const char* what)
	{
		auto threw = false;
		try { f(); } catch (const std::exception&) { threw = true; }
		check(threw, what);
	}

	void write(const fs::path& path, const std::string& bytes)
	{
		std::ofstream out{ path, std::ios::binary };
		out << bytes;
		if (!out)
			throw std::runtime_error{ "fixture write failed" };
	}

	std::string read(const fs::path& path)
	{
		std::ifstream in{ path, std::ios::binary };
		return { std::istreambuf_iterator<char>{ in }, {} };
	}

	std::string protect(const std::string& text)
	{
		DATA_BLOB in{ DWORD(text.size()), reinterpret_cast<BYTE*>(const_cast<char*>(text.data())) }, out{};
		if (!CryptProtectData(&in, L"test", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
			throw std::runtime_error{ "CryptProtectData failed" };

		std::string blob(reinterpret_cast<const char*>(out.pbData), out.cbData);
		LocalFree(out.pbData);
		return blob;
	}

	std::string unprotect(const std::string& blob)
	{
		DATA_BLOB in{ DWORD(blob.size()), reinterpret_cast<BYTE*>(const_cast<char*>(blob.data())) }, out{};
		if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
			throw std::runtime_error{ "CryptUnprotectData failed" };

		std::string text(reinterpret_cast<const char*>(out.pbData), out.cbData);
		LocalFree(out.pbData);
		return text;
	}
}

int main(int argc, char** argv)
{
	try {
		if (argc != 2)
			throw std::runtime_error{ "usage: windows_session_checks SCRATCH" };

		auto root = fs::absolute(argv[1]) / ("windows-session-" + std::to_string(GetCurrentProcessId()));
		fs::remove_all(root);
		auto support = root / "AppData/Local/OpenBF2";
		auto profiles = root / "Documents/Battlefield 2";
		fs::create_directories(support / "Setup");
		auto platform = setup::nativePlatform();

		// the key file: absent, unreadable, plain text and a protected file without a valid key all count as no key
		check(!session::storedKey(support), "a key without a key file");
		write(support / "Registry.dat", "HKEY_LOCAL_MACHINE\\SOFTWARE\\Electronic Arts\\EA Games\\Battlefield 2\\ergc = AAAAAAAAAAAAAAAAAAAA\n");
		check(!session::storedKey(support), "a plain-text Registry.dat was read");
		write(support / "Registry.dat", protect("HKEY_LOCAL_MACHINE\\SOFTWARE\\Electronic Arts\\EA Games\\Battlefield 2\\ergc = 1111-2222-3333-4444-5555\n"));
		check(!session::storedKey(support), "a format-only key in Registry.dat passed the retail check");
		fs::remove(support / "Registry.dat");
		rejects([&] { session::saveKey(support, "1111-2222-3333-4444-5555", *platform); }, "a format-only key was saved");
		check(!fs::exists(support / "Registry.dat") && !fs::exists(support / "Registry.dat.pending"), "a refused key left a file behind");
		if (auto key = std::getenv("BF2_TEST_KEY"); key && *key) {
			session::saveKey(support, key, *platform);
			auto blob = read(support / "Registry.dat");
			check(session::storedKey(support), "the saved key was not read back");
			check(blob.find(session::normalizedKey(key)) == std::string::npos, "Registry.dat holds the key in plain text");
			check(unprotect(blob).find("Battlefield 2\\ergc = " + session::normalizedKey(key)) != std::string::npos, "Registry.dat is not the Registry.con text");
			check(!fs::exists(support / "Registry.con"), "a Registry.con was written on Windows");
			std::cout << "owner key: saved protected and read back\n";
		}

		// the voice defaults go to the game's profile folder, not the support root
		session::VoiceSettings voice{ 1.0, 0.75, 0.25, true };
		session::saveVoiceSettings(support, voice, *platform, profiles);
		check(read(profiles / "Profiles/BattleCommoDefaultUserSettings.con") == "1\n0.75\n0.25\n1\n", "voice defaults not in the profile folder");
		check(!fs::exists(support / "Profiles"), "voice defaults went to the support root");
		check(session::loadVoiceSettings(profiles).captureThreshold == 0.25, "voice defaults not read back from the profile folder");
		check(!session::profilesExist(profiles), "the voice file counted as a profile");
		fs::create_directory(profiles / "Profiles/0001");
		check(session::profilesExist(profiles), "a profile directory was not seen");

		// the user's mods: a junction into the library, made without administrator rights
		auto library = setup::mod_library(support);
		auto game = support / "GameData/1.50-1";
		fs::create_directories(library / "community/levels/demo/info");
		fs::create_directories(game / "mods/bf2");
		write(library / "community/mod.desc", "<title>Community</title>");
		write(library / "community/levels/demo/info/demo.desc", "");
		setup::link_mods(library, game);
		auto link = game / "mods/community";
		check(setup::is_link(fs::symlink_status(link)), "the mod was not linked");
		std::error_code error;
		check(setup::link_target(link, error) == fs::absolute(library / "community") && !error, "the junction does not lead into the library");
		check(fs::exists(link / "mod.desc"), "the junction does not resolve");
		auto mods = setup::find_mods(game, setup::DiscEdition::BF2, library);
		check(mods.size() == 1 && mods[0].directory == "community", "the linked mod was not offered");
		fs::remove_all(library / "community");
		setup::link_mods(library, game);
		check(!fs::exists(fs::symlink_status(link)), "the junction of a removed mod stayed");

		// the provider file is kept with the Windows writer, and a preset is written where the game reads it
		write(root / "other.json", R"({"schema": 1, "provider": "Example", "services": {"*": "example.org", "gpcm": "10.0.0.2", "BF2Web": "stats.example.org"}})");
		check(session::importProvider(support, "other", root / "other.json", *platform).services.size() == 3 && session::storedProvider(support, "other"), "the provider file was not kept");
		check(!fs::exists(support / "Setup/Provider-Other.pending"), "the provider pending file stayed");
		{
			setup::LaunchOptions online;
			online.online = "bf2hub";
			online.provider_folder = (support / "Setup").string();
			session::writeProviderPreset(online, *platform);
			check(read(support / "Setup/Provider-BF2Hub.json") == setup::provider_preset("bf2hub")->json, "the BF2Hub preset was not written");
			online.provider = setup::parse_provider(setup::provider_preset("bf2hub")->json);
			auto arguments = setup::launch_arguments(online);
			check(arguments.size() >= 2 && arguments[arguments.size() - 2] == "+onlineProviderConfig" && fs::path{ arguments.back() } == support / "Setup/Provider-BF2Hub.json", "the command line does not name the preset's file");
		}

		// the client choice
		setup::LaunchOptions options;
		check(options.client == "x64", "the 64-bit client is not the default");
		options.client = "x86";
		check(setup::parse_launch(setup::launch_text(options)).client == "x86" && setup::parse_launch("client=other\n").client == "x64", "client choice round trip");
		auto layout = setup::windowsClientLayout(root / "app/x86");
		check(layout.executable == root / "app/x86/BF2.exe" && !session::clientPresent(layout), "the x86 layout");

		fs::remove_all(root);
		std::cout << checks << " Windows session checks passed\n";
		return 0;
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
