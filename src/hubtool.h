#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

// openbf2-hubtool (Windows): asks BF2Hub's client DLL, read from the installer the user downloaded, where the retail
// GameSpy names resolve, and prints the answer as a provider file. It needs the retail BF2.exe beside itself and
// takes exactly one argument, the installer's path. The setup runs it from a temporary folder (Installer::
// ProbeBf2hub); these are the parts which do not depend on Windows.
namespace setup {
	struct HubtoolRun
	{
		int exitCode = -1;
		// the tool did not end in time and was stopped
		bool timedOut = false;
		std::string out, err;
	};

	// what a failed run means, in one short line
	std::string hubtool_failure(const HubtoolRun& run);
	// the provider file of a run: exit 0, or exit 6 (bf2hub.json could not be written, the file is printed all the
	// same). checked with parse_provider; anything else throws, with hubtool_failure or the reason as its message
	std::string hubtool_provider(const HubtoolRun& run);
	// the end of the tool's error output, for the details of a failure
	std::string last_lines(const std::string& text, std::size_t count = 20);

	// a file of a content manifest (core/content-*.tsv), its name looked up without regard to case
	struct ManifestFile
	{
		std::string path;
		std::uint64_t size = 0;
		std::string sha256;
	};
	ManifestFile manifest_file(const std::filesystem::path& manifest, const std::string& name);

	// runs `tool installer` in `folder` without a console window, its output collected; stopped after `timeout`.
	// Windows only: elsewhere it throws
	HubtoolRun run_hubtool(const std::filesystem::path& folder, const std::filesystem::path& tool, const std::filesystem::path& installer, std::chrono::milliseconds timeout);
}
