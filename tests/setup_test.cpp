// the order of operations of the assistant, without AppKit, media or a real installation: the real Setup class
// runs against the fake machine of fake.h
#include "fake.h"
#include "setup.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
using namespace setup;

namespace {
	using Kind = Event::Kind;
	using Action = FakeMachine::Action;

	const auto key = std::string{ "AAAAAAAAAAAAAAAAAAAA" };
	const auto special_forces_key = std::string{ "SSSSSSSSSSSSSSSSSSSS" };

	unsigned checks = 0;

	void check(bool ok, const char* what)
	{
		if (!ok)
			throw std::runtime_error{ what };

		++checks;
	}

	bool called(const std::string& name)
	{
		return std::ranges::count(g_Machine.calls, name) > 0;
	}

	bool told(const std::string& title)
	{
		return std::ranges::any_of(g_Machine.messages, [&](const std::string& message) { return message.starts_with(title + ": "); });
	}

	Page page(const FakeMachine& machine)
	{
		return machine.pages.empty() ? Page::welcome : machine.pages.back();
	}

	// a click on the default button of a certain page
	Action click(Page on)
	{
		return { Kind::next, {}, {}, [on](const FakeMachine& machine) { return ::page(machine) == on; } };
	}

	// the user closes the window once nothing is going on any more. a close in the middle of something
	// is a plain { Kind::close }
	Action quit()
	{
		return { Kind::close, {}, {}, [](const FakeMachine& machine) {
			return !machine.busy && ::page(machine) != Page::install;
		} };
	}

	// Welcome, the disc, both patches, the key page filled in: everything up to the click on Install
	std::deque<Action> until_install(const std::string& disc, const KeyEntry& keys)
	{
		return {
			click(Page::welcome),
			{ Kind::disc_chosen, disc },
			click(Page::disc),
			{ Kind::patch141_chosen, "patch141.exe" },
			{ Kind::patch150_chosen, "patch150.exe" },
			click(Page::patches),
			{ Kind::keys_edited, {}, keys },
		};
	}

	void append(std::deque<Action>& script, std::initializer_list<Action> actions)
	{
		script.insert(script.end(), actions);
	}

	// runs the application on the described machine until it returns. every script ends by closing the window
	void run(const char* name, const std::function<void(FakeMachine&)>& describe, const std::function<void()>& verify)
	{
		g_Machine = FakeMachine{};
		describe(g_Machine);

		auto context = boost::asio::io_context{};
		auto worker = Worker{};
		auto window = Window{ context };
		auto installer = Installer{ worker, {}, {} };
		auto store = Store{ {} };
		auto client = Client{ {}, {}, {} };
		auto microphone = Microphone{ true };
		auto application = Setup{ window, installer, store, client, microphone };

		auto failure = std::exception_ptr{};
		auto exitCode = -1;
		boost::asio::co_spawn(context, application.Launch(), [&](std::exception_ptr error, int code) {
			failure = error;
			exitCode = code;
		});
		context.run();

		try {
			if (failure)
				std::rethrow_exception(failure);

			check(exitCode == 0, "the application did not return 0");
			check(g_Machine.script.empty(), "the script was not played to its end");
			verify();
		}
		catch (const std::exception& e) {
			std::cerr << name << ": " << e.what() << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	run("fresh installation", [](FakeMachine& machine) {
		machine.script = until_install("bf2.iso", { key });
		append(machine.script, { click(Page::keys), click(Page::voice), click(Page::ready), quit() });
	}, [] {
		check(g_Machine.pages == std::vector{ Page::welcome, Page::welcome, Page::disc, Page::patches, Page::keys, Page::install, Page::voice, Page::ready, Page::ready }, "pages are out of order");
		check(g_Machine.calls == std::vector<std::string>{ "check-disc", "check-patch141", "check-patch150", "install", "save-keys", "save-voice", "save-mod", "run-client" }, "calls are out of order");
		check(g_Machine.installed_media->disc == "bf2.iso" && g_Machine.installed_media->patch141 == "patch141.exe" && g_Machine.installed_media->patch150 == "patch150.exe", "the chosen media did not reach the installer");
		check(g_Machine.installed_special_forces == false, "a regular disc installed Special Forces");
		check(g_Machine.saved_keys->key == key && g_Machine.keys.key.empty(), "the key was not saved and cleared");
		check(g_Machine.phases == std::vector<std::string>{ "inputs", "disc", "patch141", "overlay150", "verify" }, "progress did not reach the window");
		check(g_Machine.messages.empty(), "a message was shown");
		check(g_Machine.client_runs == 1, "Play did not start the game once");
	});

	run("deluxe disc with both keys", [](FakeMachine& machine) {
		machine.script = until_install("deluxe.iso", { key, special_forces_key, true });
		append(machine.script, { click(Page::keys), click(Page::voice), quit() });
	}, [] {
		check(g_Machine.deluxe_keys, "the second key row was not shown");
		check(g_Machine.installed_special_forces == true && g_Machine.installed->edition == DiscEdition::Deluxe, "Special Forces was not installed");
		check(g_Machine.saved_keys->special_forces_key == special_forces_key, "the Special Forces key was not saved");
	});

	run("deluxe disc without Special Forces", [](FakeMachine& machine) {
		machine.script = until_install("deluxe.iso", { key, {}, false });
		append(machine.script, { click(Page::keys), click(Page::voice), quit() });
	}, [] {
		check(g_Machine.installed_special_forces == false && g_Machine.installed->edition == DiscEdition::DeluxeBase, "the base game was not installed alone");
		check(g_Machine.saved_keys && !g_Machine.saved_keys->install_special_forces, "the key entry lost the checkbox");
	});

	run("install stays disabled until the keys are valid", [](FakeMachine& machine) {
		machine.script = until_install("deluxe.iso", { key, "SSSS", true });
		append(machine.script, { quit() });
	}, [] {
		check(!g_Machine.next_enabled, "Install was enabled");
		check(g_Machine.key_status[KeyEdition::BF2] == KeyStatus::valid && g_Machine.key_status[KeyEdition::SpecialForces] == KeyStatus::incomplete, "the key rows show the wrong state");
		check(!called("install"), "the installation started");
	});

	run("a wrong key of full length is invalid", [](FakeMachine& machine) {
		machine.script = until_install("bf2.iso", { "BBBBBBBBBBBBBBBBBBBB" });
		append(machine.script, { quit() });
	}, [] {
		check(g_Machine.key_status[KeyEdition::BF2] == KeyStatus::invalid && !g_Machine.next_enabled, "an invalid key was accepted");
	});

	run("wrong disc", [](FakeMachine& machine) {
		machine.script = { click(Page::welcome), click(Page::disc), { Kind::disc_chosen, "holiday.iso" }, click(Page::disc), quit() };
	}, [] {
		check(g_Machine.messages.size() == 2 && told("Check your setup files"), "the user was not told twice");
		check(g_Machine.pages.back() == Page::disc && !g_Machine.busy, "the disc page was not shown again");
	});

	run("wrong patch", [](FakeMachine& machine) {
		auto failed = [](const FakeMachine& machine) { return machine.patches.contains(PatchSlot::Patch141) && machine.patches.at(PatchSlot::Patch141) == PatchStatus::failed; };
		machine.script = { click(Page::welcome), { Kind::disc_chosen, "bf2.iso" }, click(Page::disc), { Kind::patch141_chosen, "patch150.exe" }, { Kind::patch150_chosen, "patch150.exe" }, { Kind::patch141_chosen, "patch141.exe", {}, failed }, click(Page::patches), quit() };
	}, [] {
		check(told("Check your patch file"), "the user was not told about the patch");
		check(g_Machine.patches[PatchSlot::Patch141] == PatchStatus::verified && g_Machine.patches[PatchSlot::Patch150] == PatchStatus::verified, "the corrected choice was not verified");
		check(g_Machine.pages.back() == Page::keys, "Continue did not lead to the key page");
	});

	run("back keeps the choices", [](FakeMachine& machine) {
		machine.script = until_install("bf2.iso", { key });
		append(machine.script, { { Kind::back }, { Kind::back }, click(Page::disc), click(Page::patches), click(Page::keys), click(Page::voice), quit() });
	}, [] {
		check(std::ranges::count(g_Machine.calls, "check-disc") == 2, "the disc was not checked again after Back");
		check(std::ranges::count(g_Machine.calls, "check-patch141") == 1, "a verified patch was checked again");
		check(called("install") && g_Machine.installed_media->patch150 == "patch150.exe", "the installation did not use the kept choices");
	});

	run("cancel pauses, install resumes", [](FakeMachine& machine) {
		machine.script = until_install("bf2.iso", { key });
		// Install, Cancel, Install again, Save
		append(machine.script, { click(Page::keys), click(Page::install), click(Page::keys), click(Page::voice), quit() });
	}, [] {
		check(called("cancel") && told("Setup paused"), "the installation was not paused");
		check(std::ranges::count(g_Machine.pages, Page::install) == 2 && called("install"), "the installation was not resumed");
		check(g_Machine.pages.back() == Page::ready, "the resumed installation did not finish");
	});

	run("a failing installation can be retried", [](FakeMachine& machine) {
		machine.failures["install"] = 1;
		machine.script = until_install("bf2.iso", { key });
		append(machine.script, { click(Page::keys), click(Page::keys), click(Page::voice), quit() });
	}, [] {
		check(told("Setup paused") && !told("Key could not be saved"), "the failure was not reported as a pause");
		check(std::ranges::count(g_Machine.calls, "install") == 2 && g_Machine.saved_keys, "the retry did not install");
	});

	run("the key cannot be saved", [](FakeMachine& machine) {
		machine.failures["save-keys"] = 1;
		machine.script = until_install("bf2.iso", { key });
		append(machine.script, { click(Page::keys), quit() });
	}, [] {
		check(told("Key could not be saved") && g_Machine.pages.back() == Page::keys, "the user was not sent back to the key page");
		check(g_Machine.keys.key == key, "the typed key was lost");
	});

	run("closing during the installation cancels it", [](FakeMachine& machine) {
		machine.script = until_install("bf2.iso", { key });
		append(machine.script, { click(Page::keys), { Kind::close }, quit() });
	}, [] {
		check(called("cancel") && !called("install") && told("Setup paused"), "closing did not cancel");
	});

	run("existing installation", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.script = { click(Page::ready), quit() };
	}, [] {
		check(g_Machine.calls == std::vector<std::string>{ "verify", "save-mod", "run-client" }, "the installation was not verified before Play");
		check(g_Machine.client_mod == "bf2", "Battlefield 2 itself was not what Play started");
		check(g_Machine.pages == std::vector{ Page::welcome, Page::ready, Page::ready }, "pages are out of order");
	});

	run("existing installation without keys", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::Deluxe };
		machine.failures["save-keys"] = 1;
		machine.script = { { Kind::keys_edited, {}, { key, special_forces_key, true } }, click(Page::keys), click(Page::keys), quit() };
	}, [] {
		check(g_Machine.deluxe_keys && !g_Machine.back, "the key page was not the Deluxe one without Back");
		check(told("Key could not be saved") && g_Machine.saved_keys, "the keys were not asked for again and saved");
		check(!called("install") && g_Machine.pages.back() == Page::ready, "the assistant did not go straight to Ready");
	});

	run("existing installation which is damaged", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.failures["verify"] = 1;
		machine.script = { click(Page::welcome), quit() };
	}, [] {
		check(told("Existing installation could not be verified"), "the user was not told");
		check(g_Machine.pages.back() == Page::disc && !called("run-client"), "the assistant did not offer to install again");
	});

	run("the game cannot be closed over", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.client_status = 6;
		machine.script = { click(Page::ready), { Kind::close, {}, {}, [](const FakeMachine& machine) { return machine.client_running; } }, quit() };
	}, [] {
		check(told("Battlefield 2 is running"), "the window could be closed while the game ran");
		check(told("Battlefield 2 stopped") && g_Machine.messages.back().ends_with("status 6."), "the exit status was not shown");
	});

	run("the game does not start", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.failures["run-client"] = 1;
		machine.script = { click(Page::ready), click(Page::ready), quit() };
	}, [] {
		check(told("Could not start Battlefield 2") && g_Machine.client_runs == 1, "Play could not be tried again");
	});

	run("no client in this build", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.client_present = false;
		machine.script = { quit() };
	}, [] {
		check(!g_Machine.next_enabled && ::page(g_Machine) == Page::ready, "Play was offered without a client");
	});

	run("a mod is chosen and remembered", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::Deluxe };
		machine.keys_stored = true;
		machine.mods = { { "bf2", "Battlefield 2" }, { "xpack", "Battlefield 2: Special Forces" } };
		machine.chosen_mod = "xpack";
		machine.script = { click(Page::ready), quit() };
	}, [] {
		check(g_Machine.shown_mods.size() == 2 && g_Machine.client_mod == "xpack", "the chosen mod was not started");
		check(g_Machine.stored_mod == "xpack" && g_Machine.selected_mod == "xpack", "the choice was not remembered for the next time");
	});

	run("a mod is added while the application is open", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.dropped_mods = { { "community", "A Community Mod" } };
		auto offered = [](const FakeMachine& machine) { return machine.shown_mods.size() == 2; };
		// the folder is opened, the user comes back from the file manager and plays
		machine.script = { { Kind::mods_folder }, { Kind::activated, {}, {}, [](const FakeMachine& machine) { return !machine.revealed.empty(); } }, { Kind::next, {}, {}, offered }, quit() };
	}, [] {
		check(g_Machine.revealed == std::vector<std::filesystem::path>{ "CustomMods" }, "the mod library was not opened");
		check(g_Machine.shown_mods.size() == 2 && g_Machine.shown_mods[1].directory == "community", "the new mod was not offered after coming back");
		check(g_Machine.client_mod == "bf2", "the selection changed by itself");
	});

	run("a remembered mod which is gone", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.stored_mod = "removed";
		machine.script = { click(Page::ready), quit() };
	}, [] {
		check(g_Machine.client_mod == "bf2", "Battlefield 2 was not the fallback");
	});

	run("the choice cannot be saved", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.failures["save-mod"] = 1;
		machine.script = { click(Page::ready), quit() };
	}, [] {
		check(told("Could not start Battlefield 2") && g_Machine.client_runs == 0, "the game started although the launch options could not be written");
	});

	run("voice setup asks for the microphone", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.script = { { Kind::voice_setup }, { Kind::microphone }, click(Page::voice), quit() };
	}, [] {
		check(called("request-microphone") && g_Machine.microphone_status == MicrophoneStatus::ready, "the microphone was not asked for and started");
		check(g_Machine.levels == std::vector{ 0.25 }, "the level did not reach the window");
		check(g_Machine.saved_voice && g_Machine.saved_voice->captureVolume == 0.5 && !g_Machine.metering, "the settings were not saved or the meter kept running");
	});

	run("voice setup with the microphone denied", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.microphone = MicrophoneAccess::denied;
		machine.failures["save-voice"] = 1;
		machine.script = { { Kind::voice_setup }, { Kind::microphone }, click(Page::voice), click(Page::voice), quit() };
	}, [] {
		check(called("open-system-settings") && g_Machine.microphone_status == MicrophoneStatus::denied, "the system settings were not offered");
		check(told("Voice settings could not be saved") && g_Machine.saved_voice, "saving was not tried again");
	});

	run("tests never touch the microphone", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.microphone_allowed = false;
		machine.microphone = MicrophoneAccess::granted;
		machine.script = { { Kind::voice_setup }, quit() };
	}, [] {
		check(g_Machine.microphone_status == MicrophoneStatus::skipped && g_Machine.levels.empty(), "the microphone was used");
	});

	std::cout << checks << " checks of the setup application passed\n";
	return 0;
}
