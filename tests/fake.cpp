#include "fake.h"

#include <stdexcept>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
using namespace setup;

FakeMachine setup::g_Machine;

namespace {
	// lets everything else which is ready run first, like a real wait would
	task<void> yield()
	{
		co_await boost::asio::post(boost::asio::use_awaitable);
	}

	void call(const std::string& name)
	{
		g_Machine.calls.push_back(name);
		auto failure = g_Machine.failures.find(name);
		if (failure != g_Machine.failures.end() && failure->second > 0) {
			--failure->second;
			throw std::runtime_error{ name + " failed" };
		}
	}

	void show(Page page, bool nextEnabled)
	{
		g_Machine.pages.push_back(page);
		g_Machine.busy = false;
		g_Machine.busy_label.clear();
		g_Machine.next_enabled = nextEnabled;
	}

	bool deliverable(const FakeMachine::Action& action)
	{
		using Kind = Event::Kind;
		if (action.when && !action.when(g_Machine))
			return false;

		auto page = g_Machine.pages.empty() ? Page::welcome : g_Machine.pages.back();
		switch (action.kind) {
		case Kind::close:
			return true;
		case Kind::next:
			return !g_Machine.busy && g_Machine.next_enabled;
		case Kind::back:
			return !g_Machine.busy && (page == Page::disc || page == Page::patches || (page == Page::keys && g_Machine.back));
		case Kind::voice_setup:
		case Kind::mods_folder:
			return !g_Machine.busy && page == Page::ready;
		case Kind::activated:
			return true;
		case Kind::microphone:
			return page == Page::voice;
		case Kind::disc_chosen:
			return !g_Machine.busy && page == Page::disc;
		case Kind::patch141_chosen:
		case Kind::patch150_chosen:
			return page == Page::patches;
		case Kind::keys_edited:
			return page == Page::keys;
		}

		return false;
	}
}

struct Window::Impl {};

Window::Window(boost::asio::io_context&)
{
}

Window::~Window() = default;

void Window::Run()
{
}

void Window::Close()
{
	g_Machine.closed = true;
}

task<Event> Window::WaitForEvent()
{
	// a script which never becomes deliverable is a test (or an application) which hangs: fail instead
	for (auto i = 0; i < 10000; ++i) {
		if (!g_Machine.script.empty() && ::deliverable(g_Machine.script.front())) {
			auto action = g_Machine.script.front();
			g_Machine.script.pop_front();
			if (action.kind == Event::Kind::keys_edited)
				g_Machine.keys = action.keys;

			co_return Event{ action.kind, action.path };
		}

		co_await ::yield();
	}

	// what the window looked like helps to find the action which never became possible
	auto state = "page " + std::to_string(g_Machine.pages.empty() ? -1 : int(g_Machine.pages.back()))
		+ (g_Machine.busy ? ", busy" : "") + (g_Machine.next_enabled ? ", default button enabled" : ", default button disabled")
		+ ", " + std::to_string(g_Machine.script.size()) + " actions left";
	if (!g_Machine.script.empty())
		state += ", next action " + std::to_string(int(g_Machine.script.front().kind));

	throw std::logic_error{ "the script is stuck (" + state + ")" };
}

void Window::ShowWelcome()
{
	::show(Page::welcome, true);
}

void Window::ShowDisc()
{
	::show(Page::disc, true);
}

void Window::ShowPatches()
{
	::show(Page::patches, false);
}

void Window::ShowKeys(bool deluxe, bool back)
{
	::show(Page::keys, false);
	g_Machine.deluxe_keys = deluxe;
	g_Machine.back = back;
	g_Machine.key_status.clear();
}

void Window::ShowInstall()
{
	// the default button is Cancel
	::show(Page::install, true);
	g_Machine.status.clear();
}

void Window::ShowVoice(const assistant::VoiceSettings&, bool)
{
	::show(Page::voice, true);
	g_Machine.microphone_status.reset();
}

void Window::ShowReady(bool clientPresent, const std::vector<Mod>& mods, const std::string& selected)
{
	::show(Page::ready, clientPresent);
	g_Machine.shown_mods = mods;
	g_Machine.selected_mod = selected;
}

void Window::SetBusy(const std::string& label)
{
	g_Machine.busy = true;
	g_Machine.busy_label = label;
}

void Window::EnableNext(bool enabled)
{
	g_Machine.next_enabled = enabled;
}

std::string Window::SelectedMod() const
{
	return g_Machine.chosen_mod.empty() ? g_Machine.selected_mod : g_Machine.chosen_mod;
}

void Window::SetPatch(PatchSlot slot, PatchStatus status, double)
{
	g_Machine.patches[slot] = status;
}

KeyEntry Window::Keys() const
{
	return g_Machine.keys;
}

void Window::ClearKeys()
{
	g_Machine.keys = {};
}

void Window::SetKeyStatus(KeyEdition row, KeyStatus status)
{
	g_Machine.key_status[row] = status;
}

void Window::SetProgress(const Progress& progress)
{
	if (g_Machine.phases.empty() || g_Machine.phases.back() != progress.phase)
		g_Machine.phases.push_back(progress.phase);
}

void Window::SetStatus(const std::string& text)
{
	g_Machine.status = text;
}

assistant::VoiceSettings Window::Voice() const
{
	return { 1.0, 0.5, 0.2, true };
}

void Window::SetMicrophone(MicrophoneStatus status)
{
	g_Machine.microphone_status = status;
}

void Window::SetLevel(double peak)
{
	g_Machine.levels.push_back(peak);
}

void Window::Message(const std::string& title, const std::string& detail)
{
	g_Machine.messages.push_back(title + ": " + detail);
}

void Window::Reveal(const std::filesystem::path& directory)
{
	g_Machine.revealed.push_back(directory);
	// the user drops a mod into the folder which just opened
	g_Machine.mods.insert(g_Machine.mods.end(), g_Machine.dropped_mods.begin(), g_Machine.dropped_mods.end());
	g_Machine.dropped_mods.clear();
}

Installer::Installer(Worker& worker, std::filesystem::path support, std::filesystem::path manifests)
	: m_Worker{ worker }, m_Support{ std::move(support) }, m_Manifests{ std::move(manifests) }
{
}

task<DiscEdition> Installer::CheckDisc(std::filesystem::path disc)
{
	::call("check-disc");
	co_await ::yield();
	auto known = g_Machine.discs.find(disc.filename().string());
	if (known == g_Machine.discs.end())
		throw std::runtime_error{ "This disc does not contain supported Battlefield 2 content." };

	co_return known->second;
}

task<void> Installer::CheckPatch(std::filesystem::path patch, PatchSlot slot, std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(double)> progress)
{
	::call(slot == PatchSlot::Patch141 ? "check-patch141" : "check-patch150");
	co_await ::yield();
	progress(0.5);
	co_await ::yield();
	if (*cancelled)
		throw std::runtime_error{ "Cancelled; setup can resume" };
	else if (patch.filename().string() != (slot == PatchSlot::Patch141 ? g_Machine.patch141 : g_Machine.patch150))
		throw std::runtime_error{ "The selected file does not match the official patch." };
}

task<Installation> Installer::Install(Media media, bool specialForces, std::function<void(const Progress&)> progress)
{
	m_Cancelled = false;
	for (auto phase : { "inputs", "disc", "patch141", "overlay150", "verify" }) {
		progress(Progress{ phase });
		co_await ::yield();
		if (m_Cancelled)
			throw std::runtime_error{ "Cancelled; setup can resume" };
	}

	::call("install");
	g_Machine.installed_media = media;
	g_Machine.installed_special_forces = specialForces;
	auto edition = media.edition == DiscEdition::Deluxe && !specialForces ? DiscEdition::DeluxeBase : media.edition;
	g_Machine.installed = Installation{ "GameData/1.50-1", edition };
	co_return *g_Machine.installed;
}

task<Installation> Installer::Verify()
{
	m_Cancelled = false;
	co_await ::yield();
	if (m_Cancelled)
		throw std::runtime_error{ "Cancelled; setup can resume" };

	::call("verify");
	co_return g_Machine.installed.value();
}

void Installer::Cancel()
{
	g_Machine.calls.push_back("cancel");
	m_Cancelled = true;
}

Store::Store(std::filesystem::path support)
	: m_Support{ std::move(support) }
{
}

bool Store::HasInstallation() const
{
	return g_Machine.installed.has_value();
}

bool Store::ValidKey(KeyEdition edition, const std::string& key) const
{
	return (edition == KeyEdition::BF2 ? g_Machine.valid_keys : g_Machine.valid_special_forces_keys).contains(key);
}

bool Store::HasKeys(DiscEdition) const
{
	return g_Machine.keys_stored;
}

void Store::SaveKeys(const KeyEntry& keys)
{
	::call("save-keys");
	g_Machine.saved_keys = keys;
	g_Machine.keys_stored = true;
}

std::filesystem::path Store::ModLibrary()
{
	return "CustomMods";
}

std::vector<Mod> Store::Mods(const Installation&)
{
	++g_Machine.mod_lookups;
	return g_Machine.mods;
}

std::string Store::LoadMod() const
{
	return g_Machine.stored_mod;
}

void Store::SaveMod(const std::string& mod)
{
	::call("save-mod");
	g_Machine.stored_mod = mod;
}

assistant::VoiceSettings Store::LoadVoice() const
{
	return {};
}

void Store::SaveVoice(const assistant::VoiceSettings& settings)
{
	::call("save-voice");
	g_Machine.saved_voice = settings;
}

bool Store::ProfilesExist() const
{
	return g_Machine.profiles_exist;
}

struct Client::Impl {};

Client::Client(std::filesystem::path, std::filesystem::path, std::filesystem::path)
{
}

Client::~Client() = default;

bool Client::Present() const
{
	return g_Machine.client_present;
}

task<int> Client::Run(const Installation&, std::string mod)
{
	::call("run-client");
	++g_Machine.client_runs;
	g_Machine.client_mod = mod;
	g_Machine.client_running = true;
	// the game runs for a moment, long enough for the user to try to close the window
	for (auto i = 0; i < 5; ++i)
		co_await ::yield();

	g_Machine.client_running = false;
	co_return g_Machine.client_status;
}

struct Microphone::Impl {};

Microphone::Microphone(bool)
{
}

Microphone::~Microphone() = default;

bool Microphone::Allowed() const
{
	return g_Machine.microphone_allowed;
}

MicrophoneAccess Microphone::Access() const
{
	return g_Machine.microphone;
}

task<bool> Microphone::Request()
{
	::call("request-microphone");
	co_await ::yield();
	g_Machine.microphone = g_Machine.microphone_granted_on_request ? MicrophoneAccess::granted : MicrophoneAccess::denied;
	co_return g_Machine.microphone_granted_on_request;
}

void Microphone::OpenSystemSettings()
{
	g_Machine.calls.push_back("open-system-settings");
}

bool Microphone::Start(std::function<void(double)> level)
{
	if (!g_Machine.input_device)
		return false;

	g_Machine.metering = true;
	level(0.25);
	return true;
}

void Microphone::Stop()
{
	g_Machine.metering = false;
}
