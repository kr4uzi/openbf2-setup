#import <AppKit/AppKit.h>

#include "client.h"
#include "installer.h"
#include "microphone.h"
#include "setup.h"
#include "store.h"
#include "window.h"
#include "worker.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
using namespace setup;

namespace {
	std::filesystem::path path(NSString* s)
	{
		return s.length ? std::filesystem::path{ s.fileSystemRepresentation } : std::filesystem::path{};
	}

	void require(bool ok, const char* what)
	{
		if (!ok)
			throw std::runtime_error{ what };
	}

	// --smoke-test: every page of the real window rendered into a PNG, plus what the window does on its own
	// (spreading a pasted key over its row, reading the voice controls). nothing is shown on the screen and
	// neither the application nor the microphone is involved
	int smoke_test(Window& window, const std::filesystem::path& directory)
	{
		window.ShowWelcome();
		window.Snapshot(directory / "wizard-0.png");
		window.ShowDisc();
		window.Snapshot(directory / "wizard-1.png");
		window.ShowPatches();
		window.Snapshot(directory / "wizard-2.png");
		window.ShowKeys(false, true);
		window.Snapshot(directory / "wizard-3.png");
		window.ShowInstall();
		window.SetProgress(Progress{ "disc", "mods/bf2/Objects_client.zip", 400'000'000, 1'000'000'000, 400, 1000 });
		window.Snapshot(directory / "wizard-4.png");
		window.ShowVoice({}, false);
		window.SetMicrophone(MicrophoneStatus::skipped);
		window.SetLevel(0.35);
		window.Snapshot(directory / "wizard-5.png");
		window.ShowReady(false, { { "bf2", "Battlefield 2" } }, "bf2");
		require(window.SelectedMod() == "bf2", "the only mod is not the selected one");
		window.Snapshot(directory / "wizard-6.png");
		window.ShowReady(true, { { "bf2", "Battlefield 2" }, { "xpack", "Battlefield 2: Special Forces" }, { "twin", "Battlefield 2" } }, "xpack");
		require(window.SelectedMod() == "xpack", "the remembered mod is not the selected one");
		window.Snapshot(directory / "wizard-mods-6.png");

		window.ShowKeys(false, true);
		require(window.Keys().key.empty(), "the key page did not start empty");
		window.PasteKey(KeyEdition::BF2, "ABCD-EFGH-1234-5678-IJKL");
		require(window.Keys().key == "ABCDEFGH12345678IJKL", "CD key paste failed");
		require(!window.Keys().install_special_forces && window.Keys().special_forces_key.empty(), "a regular disc offered Special Forces");

		window.ShowKeys(true, true);
		window.PasteKey(KeyEdition::SpecialForces, "1111-2222-3333-4444-5555");
		auto keys = window.Keys();
		require(keys.special_forces_key == "11112222333344445555" && keys.key == "ABCDEFGH12345678IJKL", "Special Forces key paste crossed rows");
		require(keys.install_special_forces, "Deluxe must install Special Forces by default");
		window.SetKeyStatus(KeyEdition::BF2, KeyStatus::valid);
		window.SetKeyStatus(KeyEdition::SpecialForces, KeyStatus::invalid);
		window.Snapshot(directory / "wizard-deluxe-3.png");
		window.ClearKeys();
		require(window.Keys().key.empty() && window.Keys().special_forces_key.empty(), "the keys were not cleared");

		window.ShowVoice({ 1.0, 0.5, 0.1, false }, true);
		require(assistant::voiceSettingsText(window.Voice()) == "1\n0.5\n0.1\n0\n", "Voice settings text from the controls is wrong");

		std::cout << "Seven wizard screens plus Deluxe key and mod screens rendered; grouped paste, mod selection and the voice settings passed\n";
		return 0;
	}

	// --test-disc-selection: the real application behind the real buttons and the real file wells, from
	// Welcome to the patch page, with the disc of SETUP_TEST_ISO. with SETUP_TEST_PATCH141 and
	// SETUP_TEST_PATCH150 it goes on through both patch checks to the key page
	task<int> disc_selection_test(Window& window, std::filesystem::path disc)
	{
		auto timer = boost::asio::steady_timer{ co_await boost::asio::this_coro::executor };
		auto shows = [&](Page page, int seconds) -> task<bool> {
			for (auto i = 0; i < seconds * 20; ++i) {
				if (window.Shown() == page)
					co_return true;

				timer.expires_after(std::chrono::milliseconds{ 50 });
				co_await timer.async_wait(boost::asio::use_awaitable);
			}

			co_return window.Shown() == page;
		};

		require(window.Shown() == Page::welcome, "Expected a fresh Welcome page");
		window.Click(Event::Kind::next);
		require(co_await shows(Page::disc, 5), "Welcome click did not reach disc selection");
		window.Choose(Event::Kind::disc_chosen, disc);
		window.Click(Event::Kind::next);
		require(co_await shows(Page::patches, 60), "Disc click failed or timed out before reaching patches");

		std::cout << "Wizard clicks passed: Welcome -> disc selection -> Checking -> patches\n";

		auto patch141 = std::getenv("SETUP_TEST_PATCH141");
		auto patch150 = std::getenv("SETUP_TEST_PATCH150");
		if (!patch141 || !patch150)
			co_return 0;

		window.Choose(Event::Kind::patch141_chosen, patch141);
		window.Choose(Event::Kind::patch150_chosen, patch150);
		// Continue does nothing until both checks have passed
		for (auto i = 0; i < 300 && window.Shown() == Page::patches; ++i) {
			window.Click(Event::Kind::next);
			co_await shows(Page::keys, 1);
		}

		require(window.Shown() == Page::keys, "The patch checks failed or timed out before reaching the key page");
		std::cout << "Wizard clicks passed: both patches verified -> key page\n";
		co_return 0;
	}
}

int main(int argc, const char** argv)
{
	@autoreleasepool {
		auto mode = argc >= 2 ? std::string{ argv[1] } : std::string{};
		if (!(argc == 1 || (argc == 2 && mode == "--test-disc-selection") || (argc == 3 && (mode == "--smoke-test" || mode == "--test-home"))))
			return 2;

		auto discTest = mode == "--test-disc-selection";
		auto smokeTest = mode == "--smoke-test";
		auto disc = std::getenv("SETUP_TEST_ISO");
		if (discTest && (!disc || !*disc)) {
			std::cout << "SKIP: set SETUP_TEST_ISO to the owner's DVD ISO path\n";
			return 77;
		}

		// test homes and snapshots only below the scratch directory of the build, never the real home
		auto home = ::path(NSHomeDirectory());
		auto testHome = std::filesystem::path{};
		auto snapshots = std::filesystem::path{};
		if (argc == 3 || discTest) {
			auto allowed = std::filesystem::canonical(std::filesystem::path{ SETUP_UI_TEST_ROOT });
			auto chosen = discTest ? allowed / "disc-selection-home" : std::filesystem::absolute(argv[2]).lexically_normal();
			if (chosen.parent_path() != allowed || std::filesystem::is_symlink(std::filesystem::symlink_status(chosen)))
				return 2;

			if (smokeTest) {
				if (std::filesystem::exists(chosen))
					return 2;

				std::filesystem::create_directory(chosen);
				snapshots = chosen;
				testHome = chosen / "home";
			}
			else
				testHome = chosen;

			std::filesystem::create_directories(testHome);
			home = testHome;
		}

		auto support = home / "Library/Application Support/OpenBF2";
		auto contents = ::path([NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Contents"]);
		auto manifests = ::path([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Setup"]);

		auto context = boost::asio::io_context{};
		auto window = Window{ context };
		try {
			if (smokeTest) {
				window.StayHidden();
				return ::smoke_test(window, snapshots);
			}
		}
		catch (const std::exception& e) {
			std::cerr << e.what() << '\n';
			return 1;
		}

		auto worker = Worker{};
		auto installer = Installer{ worker, support, manifests };
		auto store = Store{ support };
		auto client = Client{ contents, support, home };
		// tests never touch the microphone or its permission prompt
		auto microphone = Microphone{ !discTest };
		auto application = Setup{ window, installer, store, client, microphone };

		auto exitCode = 0;
		auto finish = [&](std::exception_ptr error, int code) {
			exitCode = code;
			try {
				if (error)
					std::rethrow_exception(error);
			}
			catch (const std::exception& e) {
				std::cerr << e.what() << '\n';
				exitCode = 1;
				if (!discTest) {
					NSAlert* alert = [NSAlert new];
					alert.messageText = @"OpenBF2 has to quit";
					alert.informativeText = @(e.what()) ?: @"An unexpected error occurred.";
					[alert runModal];
				}
			}

			window.Close();
		};

		if (discTest) {
			// the application runs beside its test and is simply left behind when the test is done
			window.StayHidden();
			boost::asio::co_spawn(context, application.Launch(), [](std::exception_ptr, int) {});
			boost::asio::co_spawn(context, ::disc_selection_test(window, disc), finish);
		}
		else
			boost::asio::co_spawn(context, application.Launch(), finish);

		window.Run();
		return exitCode;
	}
}
