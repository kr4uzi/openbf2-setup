#include "installer.h"
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

task<void> Installer::CheckPatch(std::filesystem::path patch, PatchSlot slot, std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(double)> progress)
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
		verifyPatch(View{ patch }, slot, control);
	});
}

task<Installation> Installer::Install(Media media, bool specialForces, std::function<void(const Progress&)> progress)
{
	auto executor = co_await boost::asio::this_coro::executor;
	auto edition = media.edition == DiscEdition::Deluxe && !specialForces ? DiscEdition::DeluxeBase : media.edition;
	auto options = InstallOptions{ media.disc, media.patch141, media.patch150, m_Support, ::manifest(m_Manifests, edition), {}, specialForces };
	m_Cancelled = false;
	options.control.cancelled = &m_Cancelled;
	options.control.progress = [&](const Progress& p) {
		boost::asio::post(executor, [progress, p] { progress(p); });
	};

	auto game = co_await m_Worker.Run([&] {
		auto platform = nativePlatform();
		return install(options, *platform);
	});

	co_return Installation{ game, edition };
}

task<Installation> Installer::Verify()
{
	m_Cancelled = false;
	co_return co_await m_Worker.Run([&] {
		// a receipt alone is not enough: the client is only started on game data which was checked
		auto platform = nativePlatform();
		auto lock = platform->lock(m_Support / "Setup/Install.lock");
		auto installation = Installation{ assistant::currentGeneration(m_Support), assistant::currentEdition(m_Support) };

		auto control = Control{};
		control.cancelled = &m_Cancelled;
		// the client writes settings and logs beside the data
		verifyManifest(installation.game, ::manifest(m_Manifests, installation.edition), control, true);
		return installation;
	});
}

void Installer::Cancel()
{
	m_Cancelled = true;
}
