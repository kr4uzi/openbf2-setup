#include "directx.h"
#include "digest.h"

#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <fdi.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include <fstream>
#endif

namespace fs = std::filesystem;

namespace {
	std::string sha256(const setup::View& view)
	{
		setup::Digest digest;
		view.stream([&](const uint8_t* bytes, size_t size) { digest.update(bytes, size); });
		return digest.finish();
	}
}

std::optional<setup::View> setup::d3dxCab(const Files& disc)
{
	static const std::string name = "d3dx9_25_x64.cab";
	for (const auto& [path, view] : disc)
		if (path.size() >= name.size() && path.compare(path.size() - name.size(), name.size(), name) == 0 && sha256(view) == d3dx_x64_cab_sha256)
			return view;
	return std::nullopt;
}

#if defined(_WIN32)
namespace {
	std::wstring wide(const std::string& utf8)
	{
		auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), int(utf8.size()), nullptr, 0);
		if (length <= 0)
			return {};
		std::wstring text(size_t(length), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), int(utf8.size()), text.data(), length);
		return text;
	}

	std::string narrow(const std::wstring& text)
	{
		auto length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
		std::string utf8(size_t(std::max(length, 0)), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), utf8.data(), length, nullptr, nullptr);
		return utf8;
	}

	// the cabinet API passes file names through as given: they are UTF-8 here, opened with the wide CRT calls
	FNALLOC(allocate) { return std::malloc(cb); }
	FNFREE(release) { std::free(pv); }
	int openWide(const std::wstring& path, int flags, int share, int mode)
	{
		int handle = -1;
		return ::_wsopen_s(&handle, path.c_str(), flags | _O_BINARY | _O_NOINHERIT, share, mode) == 0 ? handle : -1;
	}

	FNOPEN(openFile) { return openWide(wide(pszFile), oflag, _SH_DENYWR, pmode); }
	FNREAD(readFile) { return UINT(::_read(int(hf), pv, cb)); }
	FNWRITE(writeFile) { return UINT(::_write(int(hf), pv, cb)); }
	FNCLOSE(closeFile) { return ::_close(int(hf)); }
	FNSEEK(seekFile) { return ::_lseek(int(hf), dist, seektype); }

	struct Extraction
	{
		fs::path target;
		bool found = false;
	};

	FNFDINOTIFY(notify)
	{
		auto& extraction = *static_cast<Extraction*>(pfdin->pv);
		switch (fdint) {
		case fdintCOPY_FILE:
			// only the DLL; the catalog, the INFs and infinst.exe are not needed
			if (_stricmp(pfdin->psz1, "d3dx9_25.dll") != 0)
				return 0;
			extraction.found = true;
			return openWide(extraction.target.wstring(), _O_CREAT | _O_EXCL | _O_WRONLY, _SH_DENYRW, _S_IREAD | _S_IWRITE);
		case fdintCLOSE_FILE_INFO:
			::_close(int(pfdin->hf));
			return TRUE;
		default:
			return 0;
		}
	}
}

bool setup::d3dxPresent(const fs::path& folder)
{
	std::error_code error;
	if (fs::is_regular_file(folder / "d3dx9_25.dll", error))
		return true;
	wchar_t system[MAX_PATH]{};
	auto length = GetSystemDirectoryW(system, MAX_PATH);
	return length > 0 && length < MAX_PATH && fs::is_regular_file(fs::path{ system } / "d3dx9_25.dll", error);
}

void setup::extractD3dx(const View& cab, const fs::path& folder, const fs::path& work)
{
	if (sha256(cab) != d3dx_x64_cab_sha256)
		throw std::runtime_error("The DirectX cab on the disc is not the known one.");
	fs::create_directories(work);
	auto copy = work / "Apr2005_d3dx9_25_x64.cab";
	auto unpacked = work / "d3dx9_25.dll";
	fs::remove(copy);
	fs::remove(unpacked);
	{
		std::ofstream out{ copy, std::ios::binary };
		cab.stream([&](const uint8_t* bytes, size_t size) { out.write(reinterpret_cast<const char*>(bytes), std::streamsize(size)); });
		if (!out)
			throw std::runtime_error("Cannot write the DirectX cab's copy.");
	}

	ERF erf{};
	auto context = FDICreate(allocate, release, openFile, readFile, writeFile, closeFile, seekFile, cpuUNKNOWN, &erf);
	if (!context) {
		fs::remove(copy);
		throw std::runtime_error("Windows' cabinet API is not available.");
	}
	Extraction extraction{ unpacked };
	// the cab's folder and name are one string for FDICopy: all of it goes into the name, the folder stays empty
	auto name = narrow(copy.wstring());
	char folderName[1]{};
	auto copied = FDICopy(context, name.data(), folderName, 0, notify, nullptr, &extraction);
	FDIDestroy(context);
	fs::remove(copy);
	if (!copied || !extraction.found) {
		fs::remove(unpacked);
		throw std::runtime_error("The DirectX cab could not be unpacked.");
	}
	if (sha256(View{ unpacked }) != d3dx_x64_dll_sha256) {
		fs::remove(unpacked);
		throw std::runtime_error("The unpacked d3dx9_25.dll is not the known one.");
	}
	fs::create_directories(folder);
	fs::rename(unpacked, folder / "d3dx9_25.dll");
}

std::optional<fs::path> setup::directxSetup(const fs::path& folder)
{
	auto setup = folder.parent_path() / "directx" / "DXSETUP.exe";
	std::error_code error;
	if (fs::is_regular_file(setup, error))
		return setup;
	return std::nullopt;
}
#endif
