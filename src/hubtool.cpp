#include "hubtool.h"
#include "online.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

std::string setup::hubtool_failure(const HubtoolRun& run)
{
	if (run.timedOut)
		return "BF2Hub did not answer in time.";

	switch (run.exitCode) {
	case 0: return "The BF2Hub tool gave no usable provider file.";
	case 2: return "The BF2Hub installer or the game's BF2.exe could not be used.";
	case 3: return "The BF2Hub tool's sandbox could not start.";
	case 4: return "The BF2Hub client could not be asked.";
	case 5: return "BF2Hub gave no addresses.";
	case 6: return "The BF2Hub tool could not write its file.";
	default: return "The BF2Hub tool failed (exit code " + std::to_string(run.exitCode) + ").";
	}
}

std::string setup::hubtool_provider(const HubtoolRun& run)
{
	if (run.timedOut || (run.exitCode != 0 && run.exitCode != 6))
		throw std::runtime_error{ hubtool_failure(run) };

	// exit 6 still prints the file; an empty output is the failure the exit code names
	auto text = run.out;
	text.erase(text.find_last_not_of(" \t\r\n") + 1);
	if (text.empty())
		throw std::runtime_error{ hubtool_failure(run) };

	parse_provider(text);
	return text + '\n';
}

std::string setup::last_lines(const std::string& text, std::size_t count)
{
	auto lines = std::vector<std::string>{};
	auto stream = std::istringstream{ text };
	for (std::string line; std::getline(stream, line);) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(line);
	}
	while (!lines.empty() && lines.back().empty())
		lines.pop_back();

	auto out = std::string{};
	for (auto i = lines.size() > count ? lines.size() - count : 0; i < lines.size(); ++i)
		out += lines[i] + '\n';
	return out;
}

setup::ManifestFile setup::manifest_file(const std::filesystem::path& manifest, const std::string& name)
{
	auto lower = [](std::string text) { std::ranges::transform(text, text.begin(), [](unsigned char c) { return char(std::tolower(c)); }); return text; };
	auto in = std::ifstream{ manifest };
	for (std::string line; std::getline(in, line);) {
		if (line.empty() || line[0] == '#')
			continue;

		auto stream = std::istringstream{ line };
		auto file = ManifestFile{};
		std::string size;
		if (std::getline(stream, file.path, '\t') && std::getline(stream, size, '\t') && std::getline(stream, file.sha256) && lower(file.path) == lower(name)) {
			file.size = std::stoull(size);
			return file;
		}
	}

	throw std::runtime_error{ "The content manifest has no " + name + "." };
}

#if defined(_WIN32)
namespace {
	// one argument of a command line, as CommandLineToArgvW reads it back
	std::wstring quoted(const std::wstring& argument)
	{
		auto out = std::wstring{ L"\"" };
		auto backslashes = std::size_t{ 0 };
		for (auto c : argument) {
			if (c == L'\\') {
				++backslashes;
				continue;
			}
			out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
			backslashes = 0;
			out += c;
		}
		out.append(backslashes * 2, L'\\');
		return out + L"\"";
	}

	struct Handle
	{
		HANDLE value = nullptr;
		~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
	};

	void pipe(Handle& read, Handle& write)
	{
		auto security = SECURITY_ATTRIBUTES{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		if (!CreatePipe(&read.value, &write.value, &security, 0) || !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0))
			throw std::runtime_error{ "The BF2Hub tool's output could not be connected." };
	}

	// what is waiting in a pipe, without blocking: a child of the tool may hold the pipe open after the tool ended
	bool collect(HANDLE from, std::string& into)
	{
		DWORD available = 0;
		if (!PeekNamedPipe(from, nullptr, 0, nullptr, &available, nullptr) || !available)
			return false;

		auto buffer = std::string(std::min<DWORD>(available, 64 * 1024), '\0');
		DWORD count = 0;
		if (!ReadFile(from, buffer.data(), DWORD(buffer.size()), &count, nullptr))
			return false;
		if (into.size() < 1024 * 1024)
			into.append(buffer.data(), count);
		return count > 0;
	}
}

setup::HubtoolRun setup::run_hubtool(const std::filesystem::path& folder, const std::filesystem::path& tool, const std::filesystem::path& installer, std::chrono::milliseconds timeout)
{
	Handle outRead, outWrite, errRead, errWrite, input;
	pipe(outRead, outWrite);
	pipe(errRead, errWrite);
	auto security = SECURITY_ATTRIBUTES{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
	input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);

	auto startup = STARTUPINFOW{};
	startup.cb = sizeof startup;
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = input.value;
	startup.hStdOutput = outWrite.value;
	startup.hStdError = errWrite.value;
	auto line = quoted(tool.wstring()) + L" " + quoted(installer.wstring());
	auto process = PROCESS_INFORMATION{};
	if (!CreateProcessW(tool.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &process))
		throw std::runtime_error{ "The BF2Hub tool could not be started." };

	Handle processHandle{ process.hProcess }, threadHandle{ process.hThread };
	// only the tool holds the writing ends now: the reads end when it does
	CloseHandle(outWrite.value); outWrite.value = nullptr;
	CloseHandle(errWrite.value); errWrite.value = nullptr;

	// read while the tool runs, so a full pipe never stops it; after it ended, what is left
	auto run = HubtoolRun{};
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (true) {
		auto ended = WaitForSingleObject(processHandle.value, 20) == WAIT_OBJECT_0;
		while (collect(outRead.value, run.out) || collect(errRead.value, run.err)) {}
		if (ended)
			break;
		if (std::chrono::steady_clock::now() >= deadline) {
			run.timedOut = true;
			TerminateProcess(processHandle.value, 1);
			WaitForSingleObject(processHandle.value, 5000);
			break;
		}
	}
	DWORD code = 0;
	GetExitCodeProcess(processHandle.value, &code);
	run.exitCode = int(code);
	return run;
}
#else
setup::HubtoolRun setup::run_hubtool(const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&, std::chrono::milliseconds)
{
	throw std::runtime_error{ "The BF2Hub tool runs only on Windows." };
}
#endif
