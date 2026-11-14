#include "setup.h"
#include "client.h"
#include "microphone.h"
#include "store.h"

#include <algorithm>
#include <exception>
#include <string>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/this_coro.hpp>
using namespace setup;
using namespace boost::asio::experimental::awaitable_operators;

namespace {
	// the user closed the window: every page gives up and Launch returns
	struct WindowClosed {};
}

Setup::Setup(Window& window, Installer& installer, Store& store, Client& client, Microphone& microphone)
	: m_Window{ window }, m_Installer{ installer }, m_Store{ store }, m_Client{ client }, m_Microphone{ microphone }
{
}

task<int> Setup::Launch()
{
	try {
		auto installation = co_await FindInstallation();
		if (!installation)
			installation = co_await InstallGame();
		else if (!m_Store.HasKeys(installation->edition)) {
			// the game data is there but Registry.con is not: only the keys are asked for
			co_await EnterMissingKeys(installation->edition == DiscEdition::Deluxe);
		}

		co_await Ready(*installation);
	}
	catch (const WindowClosed&) {
	}

	m_Microphone.Stop();
	co_return 0;
}

task<std::optional<Installation>> Setup::FindInstallation()
{
	m_Window.ShowWelcome();
	if (!m_Store.HasInstallation())
		co_return std::nullopt;

	m_Window.SetBusy("Verifying…");
	auto failure = std::string{};
	try {
		auto verified = co_await (m_Installer.Verify() || CancelOnClose());
		co_return std::get<Installation>(verified);
	}
	catch (const std::exception& e) {
		failure = e.what();
	}

	// the assistant offers to install again. the generation which failed stays where it is
	m_Window.ShowWelcome();
	m_Window.Message("Existing installation could not be verified", failure);
	co_return std::nullopt;
}

task<Installation> Setup::InstallGame()
{
	enum class Step { welcome, disc, patches, keys };
	auto step = Step::welcome;
	while (true) {
		switch (step) {
		case Step::welcome:
			co_await Welcome();
			step = Step::disc;
			break;
		case Step::disc:
			step = co_await ChooseDisc() ? Step::patches : Step::welcome;
			break;
		case Step::patches:
			step = co_await ChoosePatches() ? Step::keys : Step::disc;
			break;
		case Step::keys:
			if (!co_await EnterKeys(m_Media.edition == DiscEdition::Deluxe, true)) {
				step = Step::patches;
				break;
			}

			if (auto installation = co_await Install()) {
				co_await VoiceSetup();
				co_return *installation;
			}

			break;
		}
	}
}

task<void> Setup::Ready(const Installation& installation)
{
	// the mod played last
	auto selected = m_Store.LoadMod();
	while (true) {
		// looked up every time: the user may just have put a mod into the library, or taken one out
		auto mods = m_Store.Mods(installation);
		if (std::ranges::find(mods, selected, &Mod::directory) == mods.end())
			selected = mods.empty() ? "bf2" : mods.front().directory;

		m_Window.ShowReady(m_Client.Present(), mods, selected);
		auto event = co_await NextEvent();
		selected = m_Window.SelectedMod();
		if (event.kind == Event::Kind::next)
			co_await Play(installation, selected);
		else if (event.kind == Event::Kind::voice_setup)
			co_await VoiceSetup();
		else if (event.kind == Event::Kind::mods_folder)
			m_Window.Reveal(m_Store.ModLibrary());
	}
}

task<void> Setup::Welcome()
{
	m_Window.ShowWelcome();
	while ((co_await NextEvent()).kind != Event::Kind::next) {
	}
}

task<bool> Setup::ChooseDisc()
{
	m_Window.ShowDisc();
	while (true) {
		auto event = co_await NextEvent();
		if (event.kind == Event::Kind::back)
			co_return false;
		else if (event.kind == Event::Kind::disc_chosen)
			m_Media.disc = event.path;

		if (event.kind != Event::Kind::next)
			continue;

		if (m_Media.disc.empty()) {
			m_Window.Message("Check your setup files", "Choose your DVD or ISO first.");
			continue;
		}

		m_Window.SetBusy("Checking…");
		auto failure = std::string{};
		try {
			m_Media.edition = co_await m_Installer.CheckDisc(m_Media.disc);
			co_return true;
		}
		catch (const std::exception& e) {
			failure = e.what();
		}

		m_Window.ShowDisc();
		m_Window.Message("Check your setup files", failure);
	}
}

task<bool> Setup::ChoosePatches()
{
	m_Window.ShowPatches();
	m_Window.EnableNext(m_Patches[0].verified && m_Patches[1].verified);
	while (true) {
		auto event = co_await NextEvent();
		if (event.kind == Event::Kind::back)
			co_return false;
		else if (event.kind == Event::Kind::next && m_Patches[0].verified && m_Patches[1].verified)
			co_return true;
		else if (event.kind != Event::Kind::patch141_chosen && event.kind != Event::Kind::patch150_chosen)
			continue;

		// the check takes a while (2 GB of SHA-256) and runs beside the page, so the other patch can be
		// chosen meanwhile. it enables Continue once both are verified
		auto slot = event.kind == Event::Kind::patch141_chosen ? PatchSlot::Patch141 : PatchSlot::Patch150;
		boost::asio::co_spawn(co_await boost::asio::this_coro::executor, CheckPatch(slot, event.path), boost::asio::detached);
	}
}

task<void> Setup::CheckPatch(PatchSlot slot, std::filesystem::path path)
{
	auto& patch = m_Patches[slot == PatchSlot::Patch141 ? 0 : 1];
	if (patch.cancelled)
		*patch.cancelled = true;

	auto cancelled = std::make_shared<std::atomic<bool>>(false);
	patch = Patch{ path, false, cancelled };
	m_Window.SetPatch(slot, PatchStatus::checking);
	m_Window.EnableNext(false);

	auto failure = std::string{};
	try {
		co_await m_Installer.CheckPatch(path, slot, cancelled, [this, slot, cancelled](double fraction) {
			if (!*cancelled)
				m_Window.SetPatch(slot, PatchStatus::checking, fraction);
		});
	}
	catch (const std::exception& e) {
		failure = e.what();
	}

	// the user chose another file meanwhile: its own check reports
	if (*cancelled)
		co_return;

	patch.verified = failure.empty();
	m_Window.SetPatch(slot, patch.verified ? PatchStatus::verified : PatchStatus::failed);
	m_Window.EnableNext(m_Patches[0].verified && m_Patches[1].verified);
	if (!patch.verified)
		m_Window.Message("Check your patch file", failure);
}

task<bool> Setup::EnterKeys(bool deluxe, bool back)
{
	m_Window.ShowKeys(deluxe, back);
	while (true) {
		auto valid = ShowKeyStatus(deluxe);
		auto event = co_await NextEvent();
		if (event.kind == Event::Kind::back && back)
			co_return false;
		else if (event.kind == Event::Kind::next && valid)
			co_return true;
	}
}

task<void> Setup::EnterMissingKeys(bool deluxe)
{
	while (true) {
		co_await EnterKeys(deluxe, false);
		try {
			m_Store.SaveKeys(m_Window.Keys());
			m_Window.ClearKeys();
			co_return;
		}
		catch (const std::exception& e) {
			m_Window.Message("Key could not be saved", e.what());
		}
	}
}

bool Setup::ShowKeyStatus(bool deluxe)
{
	auto keys = m_Window.Keys();
	auto status = [&](KeyEdition edition, const std::string& key) {
		if (m_Store.ValidKey(edition, key))
			return KeyStatus::valid;

		return key.size() == 20 ? KeyStatus::invalid : KeyStatus::incomplete;
	};

	auto base = status(KeyEdition::BF2, keys.key);
	auto specialForces = KeyStatus::not_installed;
	if (deluxe && keys.install_special_forces)
		specialForces = status(KeyEdition::SpecialForces, keys.special_forces_key);

	m_Window.SetKeyStatus(KeyEdition::BF2, base);
	if (deluxe)
		m_Window.SetKeyStatus(KeyEdition::SpecialForces, specialForces);

	auto valid = base == KeyStatus::valid && (specialForces == KeyStatus::valid || specialForces == KeyStatus::not_installed);
	m_Window.EnableNext(valid);
	return valid;
}

task<std::optional<Installation>> Setup::Install()
{
	auto keys = m_Window.Keys();
	auto specialForces = m_Media.edition == DiscEdition::Deluxe && keys.install_special_forces;
	m_Media.patch141 = m_Patches[0].path;
	m_Media.patch150 = m_Patches[1].path;

	m_Window.ShowInstall();
	auto title = std::string{ "Setup paused" };
	auto failure = std::string{};
	try {
		auto progress = [this](const Progress& progress) { m_Window.SetProgress(progress); };
		auto installed = co_await (m_Installer.Install(m_Media, specialForces, progress) || CancelOnClose());

		// from here on the game is installed, whatever happens to the key
		title = "Key could not be saved";
		m_Store.SaveKeys(keys);
		m_Window.ClearKeys();
		co_return std::get<Installation>(installed);
	}
	catch (const std::exception& e) {
		failure = e.what();
	}

	if (title == "Setup paused")
		failure += "\nYour previous installation is retained. Choose Install to retry or resume.";

	m_Window.ShowKeys(m_Media.edition == DiscEdition::Deluxe, true);
	m_Window.Message(title, failure);
	co_return std::nullopt;
}

task<void> Setup::VoiceSetup()
{
	m_Window.ShowVoice(m_Store.LoadVoice(), m_Store.ProfilesExist());
	ShowMicrophone();
	while (true) {
		auto event = co_await NextEvent();
		if (event.kind == Event::Kind::microphone) {
			// asked once by the system. after a refusal only the user can change it, in the system settings
			if (m_Microphone.Access() == MicrophoneAccess::undetermined)
				co_await m_Microphone.Request();
			else
				m_Microphone.OpenSystemSettings();

			ShowMicrophone();
		}

		if (event.kind != Event::Kind::next)
			continue;

		try {
			m_Store.SaveVoice(m_Window.Voice());
			m_Microphone.Stop();
			co_return;
		}
		catch (const std::exception& e) {
			m_Window.Message("Voice settings could not be saved", e.what());
		}
	}
}

void Setup::ShowMicrophone()
{
	if (!m_Microphone.Allowed()) {
		m_Window.SetMicrophone(MicrophoneStatus::skipped);
		return;
	}

	switch (m_Microphone.Access()) {
	case MicrophoneAccess::undetermined:
		m_Window.SetMicrophone(MicrophoneStatus::ask);
		break;
	case MicrophoneAccess::denied:
		m_Window.SetMicrophone(MicrophoneStatus::denied);
		break;
	case MicrophoneAccess::granted:
		auto started = m_Microphone.Start([this](double peak) { m_Window.SetLevel(peak); });
		m_Window.SetMicrophone(started ? MicrophoneStatus::ready : MicrophoneStatus::missing);
		break;
	}
}

task<void> Setup::Play(const Installation& installation, std::string mod)
{
	m_Window.SetBusy("Play");

	auto failure = std::string{};
	try {
		m_Store.SaveMod(mod);
		auto finished = co_await (m_Client.Run(installation, mod) || RefuseClose());
		if (auto status = std::get<int>(finished); status != 0)
			m_Window.Message("Battlefield 2 stopped", "The client exited with status " + std::to_string(status) + ".");

		co_return;
	}
	catch (const std::exception& e) {
		failure = e.what();
	}

	m_Window.Message("Could not start Battlefield 2", failure);
}

task<Event> Setup::NextEvent()
{
	auto event = co_await m_Window.WaitForEvent();
	if (event.kind == Event::Kind::close)
		throw WindowClosed{};

	co_return event;
}

// beside an installation or a verification: Cancel and closing the window both stop it at its next file.
// the window stays, the stopped work reports itself
task<void> Setup::CancelOnClose()
{
	while (true) {
		auto event = co_await m_Window.WaitForEvent();
		if (event.kind != Event::Kind::next && event.kind != Event::Kind::close)
			continue;

		m_Installer.Cancel();
		m_Window.SetStatus("Cancelling after the current patch record…");
	}
}

// beside a running game: the window cannot be closed
task<void> Setup::RefuseClose()
{
	while (true) {
		auto event = co_await m_Window.WaitForEvent();
		if (event.kind == Event::Kind::close)
			m_Window.Message("Battlefield 2 is running", "Quit the game before closing this window.");
	}
}
