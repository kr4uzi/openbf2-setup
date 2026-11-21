// The 64-bit DirectX 9 runtime of the Windows x64 client: d3dx9_25.dll of April 2005. The 32-bit client uses the
// copy in the game folder; the 64-bit client needs Microsoft's 64-bit build, which the game's own disc carries in
// directx\Apr2005_d3dx9_25_x64.cab (regular and Deluxe discs). The setup takes it from there, without running
// anything from the disc; without it, Microsoft's DirectX setup from the application's directx\ folder installs it.
#pragma once
#include "media.h"

#include <optional>

namespace setup {
	// the cab on the disc and the DLL inside it, by SHA-256
	inline constexpr const char* d3dx_x64_cab_sha256 = "17e5e775acc77dc971fe4e1984f224c7bb76e401d3615b8847838aa237713bdf";
	inline constexpr const char* d3dx_x64_dll_sha256 = "10b09474bfe4e2bb395472628646bc5f353fbfbec976575c45eeff49984ebaa6";

	// the 64-bit D3DX cab of a disc: a file named ...d3dx9_25_x64.cab anywhere on it whose SHA-256 is the known one.
	// none if the disc has none
	std::optional<View> d3dxCab(const Files& disc);

#if defined(_WIN32)
	// whether the client in `folder` finds a 64-bit d3dx9_25.dll: its own copy, or the system's (System32 of this
	// 64-bit process)
	bool d3dxPresent(const std::filesystem::path& folder);
	// unpacks d3dx9_25.dll from the cab with Windows' cabinet API and puts it into `folder` once its SHA-256 is the
	// known one. `work` is a private folder for the cab's copy; nothing of the cab stays behind
	void extractD3dx(const View& cab, const std::filesystem::path& folder, const std::filesystem::path& work);
	// Microsoft's DirectX setup in the application's directx\ folder beside the client folders, if it is there
	std::optional<std::filesystem::path> directxSetup(const std::filesystem::path& folder);
#endif
}
