#include "store.h"
#include "existing.h"
#include <cctype>
#include <algorithm>
#include "links.h"
#include "installer.h"
#include "platform.h"
#include "window.h"
using namespace setup;

Store::Store(std::filesystem::path support, std::filesystem::path profileHome)
	: m_Support{ std::move(support) }, m_ProfileHome{ std::move(profileHome) }
{
	if (m_ProfileHome.empty())
		m_ProfileHome = m_Support;
}

bool Store::HasInstallation() const
{
	auto current = m_Support / "Setup/Current.txt";
	// a symlink is reported as an installation, so that the verification refuses it instead of it being ignored
	return std::filesystem::exists(current) || std::filesystem::is_symlink(std::filesystem::symlink_status(current));
}

bool Store::ValidKey(KeyEdition edition, const std::string& key) const
{
	return validateKey(edition, key);
}

bool Store::HasKeys(DiscEdition edition) const
{
	return assistant::storedKey(m_Support) && (!hasSpecialForces(edition) || assistant::storedKey(m_Support, true));
}

void Store::SaveKeys(const KeyEntry& keys)
{
	auto platform = nativePlatform();
	assistant::saveKeys(m_Support, keys.key, keys.install_special_forces ? keys.special_forces_key : std::string{}, *platform);
}

std::filesystem::path Store::ModLibrary()
{
	auto library = mod_library(m_Support);
	auto error = std::error_code{};
	if (std::filesystem::create_directory(library, error))
		std::filesystem::permissions(library, std::filesystem::perms::owner_all, error);

	return library;
}

std::vector<Mod> Store::Mods(const Installation& installation)
{
	auto library = mod_library(m_Support);
	// an installation used in place brings its own mods first; a mod of the library with the same name stays out
	if (!installation.source.empty())
		linkFolderMods(installation.game, installation.source);
	link_mods(library, installation.game);
	return find_mods(installation.game, installation.edition, library, installation.source);
}

std::vector<std::filesystem::path> Store::Demos(const Installation& installation)
{
	return assistant::findDemos(installation.game);
}

std::filesystem::path Store::DemosFolder(const Installation& installation)
{
	// the game records into Demos/ of its folder, spelled as it is on disk
	auto error = std::error_code{};
	for (const auto& entry : std::filesystem::directory_iterator{ installation.game, error }) {
		auto name = entry.path().filename().string();
		std::ranges::transform(name, name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		if (name == "demos" && entry.is_directory() && !is_link(entry))
			return entry.path();
	}
	std::filesystem::create_directory(installation.game / "Demos");
	return installation.game / "Demos";
}

LaunchOptions Store::LoadLaunch() const
{
	return assistant::loadLaunch(m_Support);
}

void Store::SaveLaunch(const LaunchOptions& options)
{
	auto platform = nativePlatform();
	assistant::saveLaunch(m_Support, options, *platform);
}

ProviderServices Store::ImportProvider(const std::string& online, const std::filesystem::path& file)
{
	auto platform = nativePlatform();
	return assistant::importProvider(m_Support, online, file, *platform);
}

std::map<std::string, ProviderServices> Store::KeptProviders() const
{
	auto kept = std::map<std::string, ProviderServices>{};
	for (auto online : { "other" })
		if (auto services = assistant::storedProvider(m_Support, online))
			kept[online] = *services;
	return kept;
}

ProviderServices Store::KeepProvider(const std::string& online, const std::string& json)
{
	auto platform = nativePlatform();
	return assistant::keepProvider(m_Support, online, json, *platform);
}

assistant::VoiceSettings Store::LoadVoice() const
{
	try {
		return assistant::loadVoiceSettings(m_ProfileHome);
	}
	catch (const std::exception&) {
		return {};
	}
}

void Store::SaveVoice(const assistant::VoiceSettings& settings)
{
	auto platform = nativePlatform();
	assistant::saveVoiceSettings(m_Support, settings, *platform, m_ProfileHome);
}

bool Store::ProfilesExist() const
{
	try {
		return assistant::profilesExist(m_ProfileHome);
	}
	catch (const std::exception&) {
		return false;
	}
}
