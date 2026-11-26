#pragma once

#include "edition.h"
#include "patch.h"
#include "progress.h"
#include "task.h"
#include "worker.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace setup {
	// The selected media. Complete Edition supplies patch141 from inside the disc.
	struct Media
	{
		std::filesystem::path disc;
		std::filesystem::path patch141;
		std::filesystem::path patch150;
		DiscEdition edition = DiscEdition::BF2;
	};

	// A verified generation; the Base editions omit Special Forces.
	struct Installation
	{
		std::filesystem::path game;
		DiscEdition edition = DiscEdition::BF2;
		// the folder of an existing installation which the generation uses in place (existing.h); empty for
		// one installed from disc
		std::filesystem::path source;
	};

	// What linking an existing installation tolerated (existing.h): changed or missing scripts, missing languages.
	struct LinkResult
	{
		Installation installation;
		std::vector<std::string> scripts;
		size_t missingLocalization = 0;
	};

	// the long-running work of the setup core. every call runs on the worker and reports its progress
	// on the thread of the caller
	class Installer
	{
		Worker& m_Worker;
		std::filesystem::path m_Support;
		// the directory with the content manifests (content-150.tsv, ...)
		std::filesystem::path m_Manifests;
		std::atomic<bool> m_Cancelled{ false };
		// Windows: the folder of the 64-bit client, which gets the disc's 64-bit d3dx9_25.dll (directx.h)
		std::filesystem::path m_DirectX;

	public:
		Installer(Worker& worker, std::filesystem::path support, std::filesystem::path manifests);
		// Windows: after an installation, the 64-bit client in `folder` gets the disc's 64-bit DirectX runtime
		// unless it finds one already
		void DirectX(std::filesystem::path folder) { m_DirectX = std::move(folder); }

		task<DiscEdition> CheckDisc(std::filesystem::path disc);
		// a check which was replaced by a newer choice is stopped through `cancelled`
		task<void> CheckPatch(std::filesystem::path patch, PatchSlot slot, std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(double)> progress, bool included = false);

		// resumes a paused installation of the same media. the generation is published before this returns
		task<Installation> Install(Media media, bool specialForces, std::function<void(const Progress&)> progress);
		// an existing installation the user chose: Complete when it has Special Forces, BF2 otherwise. throws
		// with the reason a folder is not one
		task<DiscEdition> CheckExisting(std::filesystem::path folder);
		// uses the installation in `folder` in place: a generation of links to its mods and scripts, verified and
		// published before this returns. the folder is never written
		task<LinkResult> Link(std::filesystem::path folder, DiscEdition edition, std::function<void(const Progress&)> progress);

		// the current generation as Setup/Current.txt and the receipt name it, unchecked
		Installation Current() const;
		// the current generation against the record of its last full verification: sizes and times, in a
		// moment. nothing if there is no record or a file differs: then only Verify can say
		task<std::optional<Installation>> QuickCheck();
		// the current generation, checked file by file against its manifest; the record is written afterwards
		task<Installation> Verify();

		// stops Install and Verify at their next file or patch record, they throw then
		void Cancel();
	};
}
