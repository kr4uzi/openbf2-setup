#include "installer.h"
#include "directx.h"
#include "existing.h"
#include "install.h"
#include "session.h"

#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
using namespace setup;

namespace {
	std::filesystem::path manifest(const std::filesystem::path& manifests, DiscEdition edition)
	{
		return manifests / (std::string{ assistant::manifestName(edition) } + ".tsv");
	}

	// the record of the last full verification of a generation, Setup/Verified-<generation>.tsv
	std::filesystem::path stamp(const std::filesystem::path& support, const std::filesystem::path& game)
	{
		return support / "Setup" / ("Verified-" + game.filename().string() + ".tsv");
	}

	// a stamp which cannot be written costs the next start a full verification, nothing more. a linked generation
	// (existing.h) is stamped through its links
	void recordVerification(const std::filesystem::path& support, const Installation& installation, const std::filesystem::path& manifest, Platform& platform)
	{
		try {
			auto text = installation.source.empty() ? verificationStamp(installation.game, manifest) : linkedStamp(installation.game, manifest);
			writeTextFile(::stamp(support, installation.game), text, platform);
		}
		catch (const std::exception&) {
		}
	}

	bool recentlyVerified(const std::filesystem::path& support, const Installation& installation, const std::filesystem::path& manifest)
	{
		auto file = ::stamp(support, installation.game);
		if (std::filesystem::is_symlink(std::filesystem::symlink_status(file)) || !std::filesystem::is_regular_file(file))
			return false;

		auto bytes = View{ file }.all();
		auto text = std::string{ bytes.begin(), bytes.end() };
		return installation.source.empty() ? stampMatches(installation.game, manifest, text) : linkedStampMatches(installation.game, manifest, text);
	}

	Installation current(const std::filesystem::path& support)
	{
		auto source = assistant::linkedSource(support);
		return { assistant::currentGeneration(support), assistant::currentEdition(support), source ? *source : std::filesystem::path{} };
	}
}

Installer::Installer(Worker& worker, std::filesystem::path support, std::filesystem::path manifests)
	: m_Worker{ worker }, m_Support{ std::move(support) }, m_Manifests{ std::move(manifests) }
{
}

task<DiscEdition> Installer::CheckDisc(std::filesystem::path disc)
{
	co_return co_await m_Worker.Run([&] {
		return assistant::validateDisc(disc);
	});
}

task<void> Installer::CheckPatch(std::filesystem::path patch, PatchSlot slot, std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(double)> progress, bool included)
{
	auto executor = co_await boost::asio::this_coro::executor;
	auto control = Control{};
	control.cancelled = cancelled.get();
	control.progress = [&](const Progress& p) {
		// one update per 8 MiB is plenty for a bar
		if (p.completedBytes % (8 * 1024 * 1024) && p.completedBytes != p.totalBytes)
			return;

		auto fraction = p.totalBytes ? double(p.completedBytes) / double(p.totalBytes) : 0.0;
		boost::asio::post(executor, [progress, fraction] { progress(fraction); });
	};

	co_await m_Worker.Run([&] {
		verifyPatch(included ? includedPatch141(readDisc(patch)) : View{ patch }, slot, control);
	});
}

task<Installation> Installer::Install(Media media, bool specialForces, std::function<void(const Progress&)> progress)
{
	auto executor = co_await boost::asio::this_coro::executor;
	auto edition = installedEdition(media.edition, specialForces);
	auto options = InstallOptions{ media.disc, media.patch141, media.patch150, m_Support, ::manifest(m_Manifests, edition), {}, specialForces };
	m_Cancelled = false;
	options.control.cancelled = &m_Cancelled;
	options.control.progress = [&](const Progress& p) {
		boost::asio::post(executor, [progress, p] { progress(p); });
	};

	auto game = co_await m_Worker.Run([&] {
		auto platform = nativePlatform();
		auto installed = install(options, *platform);
		// the installation verified every file before it published them: the first start need not again
		::recordVerification(m_Support, Installation{ installed, edition }, options.manifest, *platform);
#if defined(_WIN32)
		// the game is installed whatever comes of this: without the runtime, starting the 64-bit client offers
		// Microsoft's DirectX setup instead
		if (!m_DirectX.empty() && !d3dxPresent(m_DirectX))
			try {
				if (auto cab = d3dxCab(readDisc(media.disc)))
					extractD3dx(*cab, m_DirectX, m_Support / "Setup/work-directx");
			} catch (const std::exception&) {}
#endif
		return installed;
	});

	co_return Installation{ game, edition };
}

task<DiscEdition> Installer::CheckExisting(std::filesystem::path folder)
{
	co_return co_await m_Worker.Run([&] {
		return detectExisting(folder);
	});
}

task<LinkResult> Installer::Link(std::filesystem::path folder, DiscEdition edition, std::function<void(const Progress&)> progress)
{
	auto executor = co_await boost::asio::this_coro::executor;
	auto options = LinkOptions{ folder, m_Support, ::manifest(m_Manifests, edition), edition, {} };
	m_Cancelled = false;
	options.control.cancelled = &m_Cancelled;
	options.control.progress = [&](const Progress& p) {
		boost::asio::post(executor, [progress, p] { progress(p); });
	};

	co_return co_await m_Worker.Run([&] {
		auto platform = nativePlatform();
		auto report = LinkedReport{};
		auto game = linkExisting(options, *platform, &report);
		auto installation = Installation{ game, edition, std::filesystem::canonical(folder) };
		::recordVerification(m_Support, installation, options.manifest, *platform);
		return LinkResult{ installation, report.scripts, report.missingLocalization };
	});
}

Installation Installer::Current() const
{
	return ::current(m_Support);
}

task<std::optional<Installation>> Installer::QuickCheck()
{
	co_return co_await m_Worker.Run([&]() -> std::optional<Installation> {
		try {
			auto platform = nativePlatform();
			auto lock = platform->lock(m_Support / "Setup/Install.lock");
			auto installation = ::current(m_Support);
			if (::recentlyVerified(m_Support, installation, ::manifest(m_Manifests, installation.edition)))
				return installation;
		}
		catch (const std::exception&) {
			// whatever is wrong, the full verification reports it
		}

		return std::nullopt;
	});
}

task<Installation> Installer::Verify()
{
	m_Cancelled = false;
	co_return co_await m_Worker.Run([&] {
		// a receipt alone is not enough: the client is only started on game data which was checked
		auto platform = nativePlatform();
		auto lock = platform->lock(m_Support / "Setup/Install.lock");
		auto installation = ::current(m_Support);
		auto manifest = ::manifest(m_Manifests, installation.edition);

		auto control = Control{};
		control.cancelled = &m_Cancelled;
		if (installation.source.empty())
			// the client writes settings and logs beside the data
			verifyManifest(installation.game, manifest, control, true);
		else {
			// an installation used in place: its mods may have changed meanwhile
			linkFolderMods(installation.game, installation.source);
			verifyLinked(installation.game, manifest, control);
		}
		::recordVerification(m_Support, installation, manifest, *platform);
		return installation;
	});
}

void Installer::Cancel()
{
	m_Cancelled = true;
}
