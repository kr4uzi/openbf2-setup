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
	// another provider's file: a fallback domain and an entry of its own for one service, holding for ever
	const auto example_file = std::string{ R"({"schema": 1, "provider": "Example", "services": {"*": "example.org", "gpcm": "10.0.0.2", "BF2Web": "stats.example.org"}})" };
	// the fake support root's Setup/ (Store::LoadLaunch of the fake), and the argument which names a provider file there
	const auto provider_folder = std::string{ "/support/Setup" };
	std::string provider_argument(const std::string& name)
	{
		return "+onlineProviderConfig " + (std::filesystem::path{ provider_folder } / name).string();
	}

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

	// a call which starts with `name`, e.g. any download
	bool called_any(const std::string& name)
	{
		return std::ranges::any_of(g_Machine.calls, [&](const std::string& call) { return call.starts_with(name); });
	}

	// a message which says `text` anywhere
	bool said(const std::string& text)
	{
		return std::ranges::any_of(g_Machine.messages, [&](const std::string& message) { return message.find(text) != std::string::npos; });
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
			check(g_Machine.stuck == 0, "the application waited for an event which could not come");
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
		append(machine.script, { click(Page::keys), click(Page::voice), click(Page::launcher), quit() });
	}, [] {
		check(g_Machine.pages == std::vector{ Page::welcome, Page::welcome, Page::disc, Page::patches, Page::keys, Page::install, Page::voice, Page::launcher, Page::launcher }, "pages are out of order");
		check(g_Machine.calls == std::vector<std::string>{ "check-disc", "check-patch141", "check-patch150", "install", "save-keys", "save-voice", "run-client" }, "calls are out of order");
		check(g_Machine.installed_media->disc == "bf2.iso" && g_Machine.installed_media->patch141 == "patch141.exe" && g_Machine.installed_media->patch150 == "patch150.exe", "the chosen media did not reach the installer");
		check(g_Machine.installed_special_forces == false, "a regular disc installed Special Forces");
		check(g_Machine.saved_keys->key == key && g_Machine.keys.key.empty(), "the key was not saved and cleared");
		check(g_Machine.phases == std::vector<std::string>{ "inputs", "disc", "patch141", "overlay150", "verify" }, "progress did not reach the window");
		check(g_Machine.messages.empty(), "a message was shown");
		check(g_Machine.client_runs == 1, "Play did not start the game once");
	});

	for (bool specialForces : {true, false}) run("Complete Edition supplies 1.41", [=](FakeMachine& machine) {
		machine.script = {
			click(Page::welcome), {Kind::disc_chosen, "complete.iso"}, click(Page::disc),
			{Kind::patch150_chosen, "patch150.exe"}, click(Page::patches),
			{Kind::keys_edited, {}, {key, specialForces ? special_forces_key : "", specialForces}},
			click(Page::keys), click(Page::voice), quit()
		};
	}, [=] {
		check(g_Machine.included141 && called("check-patch141"), "included 1.41 was not shown and verified");
		check(g_Machine.installed_media && g_Machine.installed_media->patch141.empty(), "included patch became an external file selection");
		check(g_Machine.installed->edition == (specialForces ? DiscEdition::Complete : DiscEdition::CompleteBase), "wrong Complete Edition receipt");
		check(g_Machine.installed_special_forces == specialForces && g_Machine.messages.empty(), "Complete Edition did not install with the chosen features");
	});

	run("changing away from Complete Edition needs an external 1.41 patch", [](FakeMachine& machine) {
		machine.script = {
			click(Page::welcome), {Kind::disc_chosen, "complete.iso"}, click(Page::disc),
			{Kind::patch150_chosen, "patch150.exe"}, click(Page::patches),
			{Kind::back}, {Kind::back}, {Kind::disc_chosen, "bf2.iso"}, click(Page::disc),
			{Kind::close, {}, {}, [](const FakeMachine& m){ return page(m)==Page::patches; }}
		};
	}, [] {
		check(!g_Machine.included141 && !g_Machine.next_enabled, "included verification leaked to a different disc");
		check(!called("install"), "installation started without an external 1.41 patch");
	});

	run("included 1.41 failure blocks Continue", [](FakeMachine& machine) {
		machine.failures["check-patch141"] = 1;
		machine.script = {
			click(Page::welcome), {Kind::disc_chosen, "complete.iso"}, click(Page::disc),
			{Kind::patch150_chosen, "patch150.exe"},
			{Kind::close, {}, {}, [](const FakeMachine& m){ return !m.messages.empty() && m.patches.contains(PatchSlot::Patch150) && m.patches.at(PatchSlot::Patch150)==PatchStatus::verified; }}
		};
	}, [] {
		check(!g_Machine.next_enabled && told("Check your patch file"), "bad included patch was accepted");
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
		check(g_Machine.pages.back() == Page::launcher, "the resumed installation did not finish");
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

	run("closing during the installation cancels it and quits", [](FakeMachine& machine) {
		machine.script = until_install("bf2.iso", { key });
		append(machine.script, { click(Page::keys), { Kind::close } });
	}, [] {
		check(called("cancel") && !called("install") && g_Machine.messages.empty(), "closing did not cancel, or a message was shown on the way out");
		check(g_Machine.pages.back() == Page::install, "a page replaced the install page before the exit");
	});

	run("closing during the verification cancels it and quits", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.quick_record = false;
		machine.script = { { Kind::verify }, { Kind::close } };
	}, [] {
		check(called("cancel") && !called("run-client") && g_Machine.messages.empty(), "closing did not cancel the verification");
	});

	run("existing installation", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.script = { click(Page::launcher), quit() };
	}, [] {
		check(g_Machine.calls == std::vector<std::string>{ "quick-check", "run-client" }, "the quick check was not what preceded Play, or unchanged options were written");
		check(g_Machine.client_options == LaunchOptions{ .provider_folder = provider_folder } && g_Machine.command_line == "+modPath mods/bf2 +restart 1", "Battlefield 2 itself, fullscreen as the game's own default and without movies, was not what Play started");
		// the launcher, and again after the game
		check(g_Machine.pages == std::vector{ Page::launcher, Page::launcher }, "pages are out of order");
	});

	run("the plain start goes straight into the game", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.option_key_held = false;
	}, [] {
		check(g_Machine.calls == std::vector<std::string>{ "quick-check", "run-client" } && g_Machine.client_runs == 1, "the game was not started without a click, or the files were hashed although the record matched");
		check(g_Machine.pages.empty() && g_Machine.messages.empty(), "a page or a message was shown on the way into the game");
	});

	run("the plain start with unverified game data shows the launcher", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.option_key_held = false;
		machine.quick_record = false;
		machine.script = { quit() };
	}, [] {
		check(g_Machine.calls == std::vector<std::string>{ "quick-check" } && g_Machine.pages == std::vector{ Page::launcher }, "the game started, or the files were hashed, without being asked");
		check(g_Machine.launcher_note.starts_with("The game data has not been verified"), "the launcher did not say that the data is unverified");
	});

	run("verification on demand", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.quick_record = false;
		machine.script = { { Kind::verify }, click(Page::launcher), quit() };
	}, [] {
		check(g_Machine.calls == std::vector<std::string>{ "quick-check", "verify", "run-client" }, "the verification did not run on the click, or Play did not follow");
		check(told("Game data verified") && g_Machine.launcher_note.empty(), "the verified data was still marked as unverified");
	});

	run("the plain start comes back to the launcher when the game fails", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.option_key_held = false;
		machine.client_status = 6;
		// nothing to close until the game has failed and the launcher is up
		machine.script = { { Kind::close, {}, {}, [](const FakeMachine& machine) { return !machine.client_running && ::page(machine) == Page::launcher; } } };
	}, [] {
		check(told("Battlefield 2 stopped") && g_Machine.client_runs == 1, "the failure was not reported");
		check(g_Machine.pages == std::vector{ Page::launcher } && !g_Machine.busy, "the launcher was not the first and only page, after the failure");
	});

	run("the plain start without a client stays on the launcher", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.option_key_held = false;
		machine.client_present = false;
		machine.script = { quit() };
	}, [] {
		check(!called("run-client") && ::page(g_Machine) == Page::launcher && !g_Machine.next_enabled, "Play was tried without a client");
	});

	run("the pages can be asked for at every start", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.option_key_held = false;
		machine.stored_launch.always_show = true;
		machine.script = { click(Page::launcher), quit() };
	}, [] {
		check(g_Machine.calls == std::vector<std::string>{ "quick-check", "run-client" } && g_Machine.pages == std::vector{ Page::launcher, Page::launcher }, "the game started before the click");
	});

	run("the plain start asks for missing keys first", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.option_key_held = false;
		machine.script = { { Kind::keys_edited, {}, { key } }, click(Page::keys), quit() };
	}, [] {
		check(g_Machine.saved_keys && !called("run-client") && ::page(g_Machine) == Page::launcher, "the keys were not asked for, or the game started by itself afterwards");
	});

	run("existing installation without keys", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::Deluxe };
		machine.failures["save-keys"] = 1;
		machine.script = { { Kind::keys_edited, {}, { key, special_forces_key, true } }, click(Page::keys), click(Page::keys), quit() };
	}, [] {
		check(g_Machine.deluxe_keys && !g_Machine.back, "the key page was not the Deluxe one without Back");
		check(told("Key could not be saved") && g_Machine.saved_keys, "the keys were not asked for again and saved");
		check(!called("install") && g_Machine.pages.back() == Page::launcher, "the assistant did not go straight to the launcher");
	});

	run("existing installation which is damaged", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.quick_record = false;
		machine.failures["verify"] = 1;
		machine.script = { { Kind::verify }, click(Page::welcome), quit() };
	}, [] {
		check(told("Game data could not be verified"), "the user was not told");
		check(g_Machine.pages.back() == Page::disc && !called("run-client"), "the assistant did not offer to install again");
	});

	run("the game cannot be closed over", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.client_status = 6;
		machine.script = { click(Page::launcher), { Kind::close, {}, {}, [](const FakeMachine& machine) { return machine.client_running; } }, quit() };
	}, [] {
		check(told("Battlefield 2 is running"), "the window could be closed while the game ran");
		check(told("Battlefield 2 stopped") && g_Machine.messages.back().ends_with("status 6."), "the exit status was not shown");
	});

	run("the game does not start", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.failures["run-client"] = 1;
		machine.script = { click(Page::launcher), click(Page::launcher), quit() };
	}, [] {
		check(told("Could not start Battlefield 2") && g_Machine.client_runs == 1, "Play could not be tried again");
	});

	run("no client in this build", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.client_present = false;
		machine.script = { quit() };
	}, [] {
		check(!g_Machine.next_enabled && ::page(g_Machine) == Page::launcher, "Play was offered without a client");
	});

	run("options are chosen, shown and remembered", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::Deluxe };
		machine.keys_stored = true;
		machine.mods = { { "bf2", "Battlefield 2" }, { "xpack", "Battlefield 2: Special Forces" } };
		auto chosen = LaunchOptions{};
		chosen.mod = "xpack";
		chosen.skip_movies = false;
		chosen.fullscreen = false;
		chosen.width = 1280;
		chosen.height = 800;
		chosen.server = "192.168.1.20";
		chosen.player = "Player One";
		// edit, Play
		machine.script = { { Kind::options_edited, {}, {}, {}, chosen }, click(Page::launcher), quit() };
	}, [] {
		check(g_Machine.shown_mods.size() == 2 && g_Machine.shown_options.mod == "xpack", "the launcher did not come back with both mods and the saved choice");
		check(called("save-launch") && g_Machine.stored_launch.mod == "xpack" && g_Machine.stored_launch.width == 1280, "the options were not saved by Play");
		check(g_Machine.command_line == "+modPath mods/xpack +fullscreen 0 +szx 1280 +szy 800 +joinServer 192.168.1.20 +port 16567 +playerName \"Player One\"", "the command line did not follow the saved options");
		check(g_Machine.client_options.mod == "xpack" && !g_Machine.client_options.fullscreen, "Play did not use the edited options");
	});

	run("an edit is kept when the window is closed", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		auto chosen = LaunchOptions{};
		chosen.fullscreen = false;
		machine.script = { { Kind::options_edited, {}, {}, {}, chosen }, { Kind::close } };
	}, [] {
		check(called("save-launch") && !g_Machine.stored_launch.fullscreen && !called("run-client"), "the edit was lost, or the game started");
	});

	run("the unlocked command line is what is started", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		auto chosen = LaunchOptions{};
		chosen.override = "+modPath mods/bf2 +fullscreen 0 +szx 800 +szy 600";
		machine.script = { { Kind::options_edited, {}, {}, {}, chosen }, click(Page::launcher), quit() };
	}, [] {
		check(g_Machine.command_line == "+modPath mods/bf2 +fullscreen 0 +szx 800 +szy 600", "the override was not shown as the command line");
		check(g_Machine.client_options.override == "+modPath mods/bf2 +fullscreen 0 +szx 800 +szy 600", "the override did not reach the client");
	});

	run("OpenSpy and BF2Hub are presets: the plain start passes the preset's file", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.option_key_held = false;
		machine.stored_launch.online = "bf2hub";
	}, [] {
		check(g_Machine.client_runs == 1 && g_Machine.pages.empty() && !called_any("download"), "a preset did not start straight into the game");
		check(g_Machine.client_options.provider == parse_provider(R"({"schema": 1, "provider": "BF2Hub", "services": {"*": "bf2hub.p.bf2.org"}})"), "the BF2Hub preset is not bf2.org's domain");
		check(command_line(launch_arguments(g_Machine.client_options)) == "+modPath mods/bf2 +restart 1 " + provider_argument("Provider-BF2Hub.json"), "the plain start did not pass the preset's file");
	});

	run("OpenSpy is chosen on the page and Play passes its file", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		auto chosen = LaunchOptions{};
		chosen.online = "openspy";
		chosen.provider = parse_provider(provider_preset("openspy")->json);
		chosen.provider_folder = provider_folder;
		machine.script = { { Kind::options_edited, {}, {}, {}, chosen }, click(Page::launcher), quit() };
	}, [] {
		check(g_Machine.stored_launch.online == "openspy" && g_Machine.client_options.provider.services == std::vector<std::pair<std::string, std::string>>{ { "*", "openspy.p.bf2.org" } }, "the OpenSpy choice was not saved, or is not bf2.org's domain");
		check(g_Machine.command_line == "+modPath mods/bf2 +restart 1 " + provider_argument("Provider-OpenSpy.json"), "the command line did not name OpenSpy's file");
	});

	run("another provider's file is kept, and Play passes it", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.provider_files["Example.json"] = example_file;
		machine.script = { { Kind::other_file, "Example.json" }, click(Page::launcher), quit() };
	}, [] {
		check(called("import-provider") && g_Machine.kept_providers["other"] == example_file && g_Machine.shown_provider_online == "other" && g_Machine.shown_provider_source == "file", "the file was not kept, or not shown");
		check(g_Machine.stored_launch.online == "other" && g_Machine.stored_launch.other_source == "file" && g_Machine.client_options.provider.services.size() == 3, "the choice was not saved, or Play did not use it");
		check(g_Machine.command_line == "+modPath mods/bf2 +restart 1 " + provider_argument("Provider-Other.json"), "the command line did not name the kept file");
	});

	run("another provider's file from the web, and a download which fails", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.downloads["https://example.org/p.json"] = R"({"schema": 1, "provider": "Example", "services": {"*": "10.0.0.7"}})";
		machine.script = { { Kind::other_url, "https://unreachable.example/p.json" }, { Kind::other_url, "http://example.org/p.json" }, { Kind::other_url, "https://example.org/p.json" }, click(Page::launcher), quit() };
	}, [] {
		check(std::ranges::count(g_Machine.messages, std::string{ "The provider file was not accepted: The address could not be reached." }) == 1, "a failed download was not said");
		check(told("The provider file was not accepted") && !called("download http://example.org/p.json"), "an address without https was downloaded");
		check(std::ranges::count(g_Machine.busy_labels, std::string{ "Downloading…" }) == 2, "the page did not wait for the downloads");
		check(g_Machine.stored_launch.online == "other" && g_Machine.stored_launch.other_source == "url" && g_Machine.stored_launch.other_url == "https://example.org/p.json", "the other provider's address was not saved");
		check(g_Machine.command_line == "+modPath mods/bf2 +restart 1 " + provider_argument("Provider-Other.json"), "the downloaded file was not passed");
	});

	run("a provider file which is not valid changes nothing", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.provider_files["wrong.json"] = R"({"schema": 1, "provider": "Example", "services": {"available": "5.252.33.100 +multi 1"}})";
		// a label the game does not know ("bf2web" for "BF2Web"): the game would refuse the file and not start
		machine.provider_files["label.json"] = R"({"schema": 1, "provider": "Example", "services": {"bf2web": "stats.example.org"}})";
		machine.script = { { Kind::other_file, "wrong.json" }, { Kind::other_file, "label.json" }, { Kind::other_file, "missing.json" }, click(Page::launcher), quit() };
	}, [] {
		check(g_Machine.kept_providers.empty() && std::ranges::count_if(g_Machine.messages, [](const std::string& m) { return m.starts_with("The provider file was not accepted"); }) == 3, "an invalid or missing provider file was not refused with its reason");
		check(said("is not one the game knows"), "an unknown label was not named as the reason");
		check(g_Machine.client_options.online.empty() && g_Machine.command_line == "+modPath mods/bf2 +restart 1", "a refused provider file reached the game");
	});

	run("a provider whose file is gone opens the launcher", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.option_key_held = false;
		machine.stored_launch.online = "other";
		machine.stored_launch.other_source = "file";
		machine.script = { quit() };
	}, [] {
		check(!called("run-client") && g_Machine.pages == std::vector{ Page::launcher }, "the game started without the provider's services");
		check(g_Machine.launcher_note.starts_with("The provider file is missing"), "the launcher did not say that the provider file is missing");
	});

	run("a provider file starts straight into the game", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.option_key_held = false;
		machine.stored_launch.online = "other";
		machine.stored_launch.other_source = "file";
		machine.kept_providers["other"] = example_file;
	}, [] {
		check(g_Machine.client_runs == 1 && g_Machine.pages.empty(), "the plain start did not go into the game");
		check(command_line(launch_arguments(g_Machine.client_options)) == "+modPath mods/bf2 +restart 1 " + provider_argument("Provider-Other.json"), "the plain start did not pass the kept file");
	});

	run("an expired file from the web is downloaded again before the game starts", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.option_key_held = false;
		machine.stored_launch.online = "other";
		machine.stored_launch.other_source = "url";
		machine.stored_launch.other_url = "https://example.org/p.json";
		machine.kept_providers["other"] = R"({"schema": 1, "provider": "Example", "valid_to": "2020-01-01T00:00:00Z", "services": {"available": "1.1.1.1"}})";
		machine.downloads["https://example.org/p.json"] = example_file;
	}, [] {
		check(called("download https://example.org/p.json") && g_Machine.kept_providers["other"] == example_file, "the expired file was not downloaded again");
		check(g_Machine.client_runs == 1 && g_Machine.client_options.provider.services.size() == 3, "the game did not start with the new file");
	});

	run("an expired file which cannot be downloaded again stays in use, and the launcher says so", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.stored_launch.online = "other";
		machine.stored_launch.other_source = "url";
		machine.stored_launch.other_url = "https://unreachable.example/p.json";
		machine.kept_providers["other"] = R"({"schema": 1, "provider": "Example", "valid_to": "2020-01-01T00:00:00Z", "services": {"available": "1.1.1.1"}})";
		machine.script = { quit() };
	}, [] {
		check(g_Machine.launcher_note.starts_with("The provider file could not be downloaded again"), "the launcher did not say that the old file is used");
		check(g_Machine.command_line.ends_with(provider_argument("Provider-Other.json")), "the old file was not used");
	});

	run("an expired file from this computer is not replaced by itself", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.stored_launch.online = "other";
		machine.stored_launch.other_source = "file";
		machine.kept_providers["other"] = R"({"schema": 1, "provider": "Example", "valid_to": "2020-01-01T00:00:00Z", "services": {"available": "1.1.1.1"}})";
		machine.script = { quit() };
	}, [] {
		check(!called_any("download") && g_Machine.launcher_note.starts_with("The provider file is out of date"), "a file from this computer was downloaded, or the launcher did not say it is out of date");
	});

	run("the dialog offers the game's demos and opens their folder", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.demos = { "Demos/new.bf2demo", "Demos/old.con" };
		machine.script = { { Kind::demos_folder }, quit() };
	}, [] {
		check(g_Machine.shown_demos == std::vector<std::filesystem::path>{ "Demos/new.bf2demo", "Demos/old.con" }, "the dialog did not get the game's demos");
		check(called("demos-folder") && !g_Machine.revealed.empty() && g_Machine.revealed.back() == "Demos", "the demos folder was not opened");
	});

	run("a mod is added while the launcher is open", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.dropped_mods = { { "community", "A Community Mod" } };
		auto offered = [](const FakeMachine& machine) { return machine.shown_mods.size() == 2; };
		// the folder is opened, the user comes back from the file manager and plays
		machine.script = { { Kind::mods_folder }, { Kind::activated, {}, {}, [](const FakeMachine& machine) { return !machine.revealed.empty(); } }, { Kind::next, {}, {}, offered }, quit() };
	}, [] {
		check(g_Machine.revealed == std::vector<std::filesystem::path>{ "CustomMods" }, "the mod library was not opened");
		check(g_Machine.shown_mods.size() == 2 && g_Machine.shown_mods[1].directory == "community", "the new mod was not offered after coming back");
		check(g_Machine.client_options.mod == "bf2", "the selection changed by itself");
	});

	run("a remembered mod which is gone", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.stored_launch.mod = "removed";
		machine.script = { click(Page::launcher), quit() };
	}, [] {
		check(g_Machine.client_options.mod == "bf2", "Battlefield 2 was not the fallback");
	});

	run("the options cannot be saved", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.failures["save-launch"] = 1;
		auto chosen = LaunchOptions{};
		chosen.player = "Someone";
		machine.script = { { Kind::options_edited, {}, {}, {}, chosen }, click(Page::launcher), click(Page::launcher), quit() };
	}, [] {
		check(told("Options could not be saved") && std::ranges::count(g_Machine.calls, "save-launch") == 2, "saving was not tried again");
		check(g_Machine.client_runs == 1, "Play ran without its options saved, or not at all on the second try");
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

	run("closing with a pending patch check", [](FakeMachine& machine) {
		machine.script = { click(Page::welcome), { Kind::disc_chosen, "bf2.iso" }, click(Page::disc),
			{ Kind::patch141_chosen, "patch141.exe" }, { Kind::close } };
	}, [] {
		check(!called("install") && !g_Machine.saved_keys, "closing started an install");
	});

	run("Linux microphone retries without a permission prompt", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.microphone = MicrophoneAccess::not_required;
		machine.failures["start-microphone"] = 1;
		machine.script = { { Kind::voice_setup }, { Kind::microphone }, click(Page::voice), quit() };
	}, [] {
		check(std::ranges::count(g_Machine.calls, "start-microphone") == 2, "capture was not retried");
		check(!called("request-microphone") && !called("open-system-settings"), "Linux retry opened permission settings");
		check(g_Machine.microphone_status == MicrophoneStatus::ready && !g_Machine.metering, "retry or stop failed");
	});

	run("voice setup can be skipped after capture failure", [](FakeMachine& machine) {
		machine.installed = Installation{ "GameData/1.50-7", DiscEdition::BF2 };
		machine.keys_stored = true;
		machine.microphone = MicrophoneAccess::not_required;
		machine.input_device = false;
		machine.script = { { Kind::voice_setup }, { Kind::voice_skip }, quit() };
	}, [] {
		check(!g_Machine.saved_voice && !g_Machine.metering, "skip saved defaults or left capture running");
	});

	std::cout << checks << " checks of the setup application passed\n";
	return 0;
}
