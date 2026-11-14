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

namespace setup {
	// the disc and the two patches the user chose. edition is what is on the disc (BF2 or Deluxe)
	struct Media
	{
		std::filesystem::path disc;
		std::filesystem::path patch141;
		std::filesystem::path patch150;
		DiscEdition edition = DiscEdition::BF2;
	};

	// a verified generation of the game data. edition is what is installed (DeluxeBase: Deluxe media
	// without Special Forces)
	struct Installation
	{
		std::filesystem::path game;
		DiscEdition edition = DiscEdition::BF2;
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

	public:
		Installer(Worker& worker, std::filesystem::path support, std::filesystem::path manifests);

		task<DiscEdition> CheckDisc(std::filesystem::path disc);
		// a check which was replaced by a newer choice is stopped through `cancelled`
		task<void> CheckPatch(std::filesystem::path patch, PatchSlot slot, std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(double)> progress);

		// resumes a paused installation of the same media. the generation is published before this returns
		task<Installation> Install(Media media, bool specialForces, std::function<void(const Progress&)> progress);
		// the current generation, checked file by file against its manifest
		task<Installation> Verify();

		// stops Install and Verify at their next file or patch record, they throw then
		void Cancel();
	};
}
