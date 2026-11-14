#include "store.h"
#include "installer.h"
#include "platform.h"
#include "window.h"
using namespace setup;

Store::Store(std::filesystem::path support)
	: m_Support{ std::move(support) }
{
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
	return assistant::storedKey(m_Support) && (edition != DiscEdition::Deluxe || assistant::storedKey(m_Support, true));
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
	link_mods(library, installation.game);
	return find_mods(installation.game, installation.edition, library);
}

std::string Store::LoadMod() const
{
	return assistant::launchMod(m_Support);
}

void Store::SaveMod(const std::string& mod)
{
	auto platform = nativePlatform();
	assistant::saveLaunchMod(m_Support, mod, *platform);
}

assistant::VoiceSettings Store::LoadVoice() const
{
	try {
		return assistant::loadVoiceSettings(m_Support);
	}
	catch (const std::exception&) {
		return {};
	}
}

void Store::SaveVoice(const assistant::VoiceSettings& settings)
{
	auto platform = nativePlatform();
	assistant::saveVoiceSettings(m_Support, settings, *platform);
}

bool Store::ProfilesExist() const
{
	try {
		return assistant::profilesExist(m_Support);
	}
	catch (const std::exception&) {
		return false;
	}
}
