#include "setup.h"
#include "client.h"
#include "microphone.h"
#include "store.h"

#include <algorithm>
#include <exception>
#include <string>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
using namespace setup;

namespace {
	// the user closed the window: every page gives up and Launch returns
	struct WindowClosed {};

	// a provider file is active, but the kept file is gone or no longer valid: the game would go to the retail
	// services instead
	bool missingProvider(const LaunchOptions& options)
	{
		return provider_online(options.online) && options.provider.services.empty();
	}

	// `work` beside a `guard` which watches the window meanwhile. it ends when the work ends, with its result or
	// its exception, and the guard is cancelled. (the || of the awaitable operators waits for a success, and
	// a guard never succeeds: a failing work would wait forever)
	template<typename T>
	task<T> guarded(task<T> work, task<void> guard)
	{
		auto executor = co_await boost::asio::this_coro::executor;
		auto [order, error, result, guardError] = co_await boost::asio::experimental::make_parallel_group(
			boost::asio::co_spawn(executor, std::move(work), boost::asio::deferred),
			boost::asio::co_spawn(executor, std::move(guard), boost::asio::deferred))
			.async_wait(boost::asio::experimental::wait_for_one(), boost::asio::use_awaitable);
		if (error)
			std::rethrow_exception(error);

		co_return std::move(result);
	}
}

Setup::Setup(Window& window, Installer& installer, Store& store, Client& client, Microphone& microphone)
	: m_Window{ window }, m_Installer{ installer }, m_Store{ store }, m_Client{ client }, m_Microphone{ microphone }
{
}

task<int> Setup::Launch()
{
	std::exception_ptr failure;
	try {
		auto installation = co_await FindInstallation();
		// a downloaded provider file which no longer holds, before the game may start straight away
		if (installation)
			co_await RefreshProvider();
		if (!installation)
			installation = co_await InstallGame();
		else if (!m_Store.HasKeys(installation->edition)) {
			// the game data is there but Registry.con is not: only the keys are asked for
			co_await EnterMissingKeys(hasSpecialForces(installation->edition));
		}
		else if (!m_Unverified && !m_Window.LauncherRequested() && m_Client.Present() && !m_Store.LoadLaunch().always_show && !missingProvider(m_Store.LoadLaunch())) {
			// the usual start of an installed game: no window, straight into the game, and the application goes
			// with it once the game was played and quit. the Option key, or the choice to see the launcher every
			// time, lead to the pages instead; so does game data which did not pass the quick check
			if (co_await Play(*installation, LoadOptions(*installation))) {
				m_Microphone.Stop();
				co_return 0;
			}
		}

		while (co_await Launcher(*installation))
			installation = co_await InstallGame();
	}
	catch (const WindowClosed&) {
	}
	catch (...) { failure = std::current_exception(); }

	// Detached hash checks still own worker callbacks into this application. Cancel and join them
	// before the window, installer and application can be destroyed.
	m_Closing = true;
	for (auto& patch : m_Patches) if (patch.cancelled) *patch.cancelled = true;
	auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
	while (m_PendingPatchChecks) {
		timer.expires_after(std::chrono::milliseconds{5});
		co_await timer.async_wait(boost::asio::use_awaitable);
	}

	m_Microphone.Stop();
	if (failure) std::rethrow_exception(failure);
	co_return 0;
}

task<std::optional<Installation>> Setup::FindInstallation()
{
	if (!m_Store.HasInstallation()) {
		m_Window.ShowWelcome();
		co_return std::nullopt;
	}

	// the files as they were after the last full check: a moment, and nothing to show for it
	if (auto installation = co_await m_Installer.QuickCheck())
		co_return installation;

	// no record, or a file differs: the launcher says so and offers the full verification
	try {
		m_Unverified = true;
		co_return m_Installer.Current();
	}
	catch (const std::exception& e) {
		m_Window.ShowWelcome();
		m_Window.Message("Existing installation could not be read", e.what());
		co_return std::nullopt;
	}
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
			if (!co_await EnterKeys(hasSpecialForces(m_Media.edition), true)) {
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

task<bool> Setup::Launcher(const Installation& installation)
{
	ReloadLauncher(installation);
	while (true) {
		auto event = co_await m_Window.WaitForEvent();
		switch (event.kind) {
		case Event::Kind::options_edited:
			m_Window.SetCommandLine(command_line(launch_arguments(m_Window.Options())));
			break;
		case Event::Kind::mods_folder:
			m_Window.Reveal(m_Store.ModLibrary());
			break;
		case Event::Kind::demos_folder:
			try {
				m_Window.Reveal(m_Store.DemosFolder(installation));
			}
			catch (const std::exception& e) {
				m_Window.Message("The demos folder could not be opened", e.what());
			}
			break;
		case Event::Kind::other_url:
		case Event::Kind::other_file:
			if (!SaveOptions())
				break;
			co_await ConfigureProvider(installation, event);
			break;
		case Event::Kind::activated:
			// back from the file manager: the list of mods may have changed, the edits stay
			ShowLauncher(installation, m_Window.Options());
			break;
		case Event::Kind::voice_setup:
			if (SaveOptions()) {
				co_await VoiceSetup();
				ReloadLauncher(installation);
			}
			break;
		case Event::Kind::verify:
			if (!SaveOptions())
				break;

			if (!co_await VerifyGameData())
				co_return true;

			ReloadLauncher(installation);
			break;
		case Event::Kind::next:
			if (SaveOptions()) {
				co_await Play(installation, m_Window.Options());
				ReloadLauncher(installation);
			}
			break;
		case Event::Kind::close:
			// the edits go with the user
			SaveOptions();
			throw WindowClosed{};
		default:
			break;
		}
	}
}

task<bool> Setup::VerifyGameData()
{
	m_Window.SetBusy("Verifying…");
	auto failure = std::string{};
	try {
		co_await ::guarded(m_Installer.Verify(), CancelOnClose());
		m_Unverified = false;
		m_Window.Message("Game data verified", "Every installed file matches the installation record.");
		co_return true;
	}
	catch (const std::exception& e) {
		failure = e.what();
	}

	// the window was closed meanwhile: the stopped verification is the end of it
	if (m_Closing)
		throw WindowClosed{};

	// the assistant offers to install again. the generation which failed stays where it is
	m_Window.ShowWelcome();
	m_Window.Message("Game data could not be verified", failure + "\nInstall again from your disc to repair it.");
	co_return false;
}

task<void> Setup::ConfigureProvider(const Installation& installation, const Event& event)
{
	using Kind = Event::Kind;
	auto online = std::string{ "other" };
	auto source = event.kind == Kind::other_url ? "url" : "file";
	auto url = event.kind == Kind::other_url ? event.path.string() : std::string{};
	auto services = std::optional<ProviderServices>{};
	auto failure = std::string{};
	try {
		if (event.kind == Kind::other_file)
			services = m_Store.ImportProvider(online, event.path);
		else {
			if (!valid_provider_url(url))
				throw std::runtime_error{ "The address is not one a provider file can come from: it must start with https://." };
			// a download takes a while: the page waits
			m_Window.SetBusy("Downloading…");
			services = m_Store.KeepProvider(online, co_await m_Window.Download(url));
		}
	}
	catch (const std::exception& e) {
		failure = e.what();
	}

	ReloadLauncher(installation);
	if (services) {
		m_Window.ShowProvider(online, source, url, *services);
		m_RefreshFailed = false;
		SaveOptions();
		m_Window.SetCommandLine(command_line(launch_arguments(m_Window.Options())));
	}
	else
		m_Window.Message("The provider file was not accepted", failure);
}

task<void> Setup::RefreshProvider()
{
	// a downloaded file which no longer holds is downloaded again; the old one stays in use when that fails
	auto options = m_Store.LoadLaunch();
	auto url = options.online == "other" && options.other_source == "url" ? options.other_url : std::string{};
	auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
	if (url.empty() || (!options.provider.services.empty() && !provider_expired(options.provider, now)))
		co_return;

	try {
		m_Store.KeepProvider(options.online, co_await m_Window.Download(url));
		m_RefreshFailed = false;
	}
	catch (const std::exception&) {
		m_RefreshFailed = true;
	}
}

LaunchOptions Setup::LoadOptions(const Installation& installation)
{
	auto options = m_Store.LoadLaunch();
	// looked up every time: the user may just have put a mod into the library, or taken one out
	auto mods = m_Store.Mods(installation);
	if (std::ranges::find(mods, options.mod, &Mod::directory) == mods.end())
		options.mod = mods.empty() ? "bf2" : mods.front().directory;

	return options;
}

void Setup::ShowLauncher(const Installation& installation, const LaunchOptions& options)
{
	auto note = std::string{};
	if (!m_Client.Present())
		note = "The game client is not part of this build.";
	else if (m_Unverified)
		note = "The game data has not been verified since it changed.";
	else if (missingProvider(options))
		note = "The provider file is missing or no longer valid. Choose it again in Online services.";
	else if (provider_online(options.online) && provider_expired(options.provider, std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())))
		note = m_RefreshFailed ? "The provider file could not be downloaded again; the old one is used."
			: "The provider file is out of date. Update it in Online services.";

	m_Window.ShowLauncher(options, m_Store.Mods(installation), m_Client.Present(), note);
	m_Window.SetDemos(m_Store.Demos(installation));
	m_Window.SetProviders(m_Store.KeptProviders());
	m_Window.SetCommandLine(command_line(launch_arguments(options)));
}

void Setup::ReloadLauncher(const Installation& installation)
{
	m_Saved = LoadOptions(installation);
	ShowLauncher(installation, m_Saved);
}

bool Setup::SaveOptions()
{
	auto options = m_Window.Options();
	if (options == m_Saved)
		return true;

	try {
		m_Store.SaveLaunch(options);
		m_Saved = options;
		return true;
	}
	catch (const std::exception& e) {
		m_Window.Message("Options could not be saved", e.what());
		return false;
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
	const bool included = m_Media.edition == DiscEdition::Complete;
	auto& first = m_Patches[0];
	if (first.included != included || (included && first.path != m_Media.disc)) {
		if (first.cancelled) *first.cancelled = true;
		first = {};
	}
	m_Window.ShowPatches(included);
	if (first.verified) m_Window.SetPatch(PatchSlot::Patch141, PatchStatus::verified);
	m_Window.EnableNext(m_Patches[0].verified && m_Patches[1].verified);
	if (included && !first.verified) {
		++m_PendingPatchChecks;
		boost::asio::co_spawn(co_await boost::asio::this_coro::executor, CheckPatch(PatchSlot::Patch141, m_Media.disc, true), boost::asio::detached);
	}
	while (true) {
		auto event = co_await NextEvent();
		if (event.kind == Event::Kind::back)
			co_return false;
		else if (event.kind == Event::Kind::next && m_Patches[0].verified && m_Patches[1].verified)
			co_return true;
		else if (event.kind != Event::Kind::patch141_chosen && event.kind != Event::Kind::patch150_chosen)
			continue;
		if (included && event.kind == Event::Kind::patch141_chosen)
			continue;

		// the check takes a while (2 GB of SHA-256) and runs beside the page, so the other patch can be
		// chosen meanwhile. it enables Continue once both are verified
		auto slot = event.kind == Event::Kind::patch141_chosen ? PatchSlot::Patch141 : PatchSlot::Patch150;
		++m_PendingPatchChecks;
		boost::asio::co_spawn(co_await boost::asio::this_coro::executor, CheckPatch(slot, event.path), boost::asio::detached);
	}
}

task<void> Setup::CheckPatch(PatchSlot slot, std::filesystem::path path, bool included)
{
	struct Finished { unsigned& count; ~Finished() { --count; } } finished{m_PendingPatchChecks};
	if (m_Closing) co_return;
	auto& patch = m_Patches[slot == PatchSlot::Patch141 ? 0 : 1];
	if (patch.cancelled)
		*patch.cancelled = true;

	auto cancelled = std::make_shared<std::atomic<bool>>(false);
	patch = Patch{ path, false, cancelled, included };
	m_Window.SetPatch(slot, PatchStatus::checking);
	if (m_Window.Shown() == Page::patches) m_Window.EnableNext(false);

	auto failure = std::string{};
	try {
		co_await m_Installer.CheckPatch(path, slot, cancelled, [this, slot, cancelled](double fraction) {
			if (!*cancelled)
				m_Window.SetPatch(slot, PatchStatus::checking, fraction);
		}, included);
	}
	catch (const std::exception& e) {
		failure = e.what();
	}

	// the user chose another file meanwhile: its own check reports
	if (*cancelled)
		co_return;

	patch.verified = failure.empty();
	m_Window.SetPatch(slot, patch.verified ? PatchStatus::verified : PatchStatus::failed);
	if (m_Window.Shown() == Page::patches) m_Window.EnableNext(m_Patches[0].verified && m_Patches[1].verified);
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
	auto specialForces = hasSpecialForces(m_Media.edition) && keys.install_special_forces;
	m_Media.patch141 = m_Patches[0].included ? std::filesystem::path{} : m_Patches[0].path;
	m_Media.patch150 = m_Patches[1].path;

	m_Window.ShowInstall();
	auto title = std::string{ "Setup paused" };
	auto failure = std::string{};
	try {
		auto progress = [this](const Progress& progress) { m_Window.SetProgress(progress); };
		auto installed = co_await ::guarded(m_Installer.Install(m_Media, specialForces, progress), CancelOnClose());

		// from here on the game is installed, whatever happens to the key
		title = "Key could not be saved";
		m_Store.SaveKeys(keys);
		m_Window.ClearKeys();
		co_return installed;
	}
	catch (const std::exception& e) {
		failure = e.what();
	}

	// the window was closed meanwhile: the paused installation resumes next time
	if (m_Closing)
		throw WindowClosed{};

	if (title == "Setup paused")
		failure += "\nYour previous installation is retained. Choose Install to retry or resume.";

	m_Window.ShowKeys(hasSpecialForces(m_Media.edition), true);
	m_Window.Message(title, failure);
	co_return std::nullopt;
}

task<void> Setup::VoiceSetup()
{
	m_Window.ShowVoice(m_Store.LoadVoice(), m_Store.ProfilesExist());
	m_Microphone.OnError([this] { m_Window.SetMicrophone(MicrophoneStatus::capture_error); });
	ShowMicrophone();
	while (true) {
		auto event = co_await NextEvent();
		if (event.kind == Event::Kind::microphone) {
			// asked once by the system. after a refusal only the user can change it, in the system settings
			if (m_Microphone.Access() == MicrophoneAccess::undetermined)
				co_await m_Microphone.Request();
			else if (m_Microphone.Access() == MicrophoneAccess::denied)
				m_Microphone.OpenSystemSettings();
			else
				m_Microphone.Stop();

			ShowMicrophone();
		}

		if (event.kind == Event::Kind::voice_skip) {
			m_Microphone.Stop();
			co_return;
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
	case MicrophoneAccess::not_required:
		auto started = m_Microphone.Start([this](double peak) { m_Window.SetLevel(peak); });
		m_Window.SetMicrophone(started ? MicrophoneStatus::ready : MicrophoneStatus::missing);
		break;
	}
}

task<bool> Setup::Play(const Installation& installation, const LaunchOptions& options)
{
	m_Window.SetBusy("Playing…");

	auto failure = std::string{};
	try {
		auto status = co_await ::guarded(m_Client.Run(installation, options), RefuseClose());
		if (status != 0)
			m_Window.Message("Battlefield 2 stopped", "The client exited with status " + std::to_string(status) + ".");

		co_return status == 0;
	}
	catch (const std::exception& e) {
		failure = e.what();
	}

	m_Window.Message("Could not start Battlefield 2", failure);
	co_return false;
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

		if (event.kind == Event::Kind::close)
			m_Closing = true;

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
