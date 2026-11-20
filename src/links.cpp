#include "links.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#endif

using namespace setup;

bool setup::is_link(const std::filesystem::file_status& status)
{
#if defined(_MSC_VER)
	// Microsoft's library reports a directory junction as its own file type
	return status.type() == std::filesystem::file_type::symlink || status.type() == std::filesystem::file_type::junction;
#else
	return std::filesystem::is_symlink(status);
#endif
}

bool setup::is_link(const std::filesystem::directory_entry& entry)
{
	auto error = std::error_code{};
	return is_link(entry.symlink_status(error)) && !error;
}

std::filesystem::path setup::link_target(const std::filesystem::path& link, std::error_code& error)
{
	// read_symlink reads junctions too in Microsoft's library
	return std::filesystem::read_symlink(link, error);
}

#if defined(_WIN32)
namespace {
	// REPARSE_DATA_BUFFER's mount point variant, which the SDK only declares for drivers
	struct MountPoint
	{
		DWORD tag;
		WORD length;
		WORD reserved;
		WORD substituteOffset;
		WORD substituteLength;
		WORD printOffset;
		WORD printLength;
		WCHAR names[1];
	};

	struct Handle
	{
		HANDLE value;
		~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
	};
}
#endif

void setup::link_directory(const std::filesystem::path& target, const std::filesystem::path& link, std::error_code& error)
{
	error.clear();
#if defined(_WIN32)
	// a junction is an empty directory with a mount-point reparse tag naming the target as \??\C:\...
	auto absolute = std::filesystem::absolute(target, error);
	if (error)
		return;

	auto substitute = L"\\??\\" + absolute.wstring();
	auto print = absolute.wstring();
	if (!std::filesystem::create_directory(link, error)) {
		if (!error)
			error = std::make_error_code(std::errc::file_exists);
		return;
	}

	auto names = (substitute.size() + 1 + print.size() + 1) * sizeof(WCHAR);
	auto size = offsetof(MountPoint, names) + names;
	auto buffer = std::make_unique<unsigned char[]>(size);
	std::memset(buffer.get(), 0, size);
	auto* point = reinterpret_cast<MountPoint*>(buffer.get());
	point->tag = IO_REPARSE_TAG_MOUNT_POINT;
	point->length = WORD(size - 8);
	point->substituteOffset = 0;
	point->substituteLength = WORD(substitute.size() * sizeof(WCHAR));
	point->printOffset = WORD((substitute.size() + 1) * sizeof(WCHAR));
	point->printLength = WORD(print.size() * sizeof(WCHAR));
	std::memcpy(point->names, substitute.c_str(), substitute.size() * sizeof(WCHAR));
	std::memcpy(point->names + substitute.size() + 1, print.c_str(), print.size() * sizeof(WCHAR));

	{
		auto handle = Handle{ CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
		auto returned = DWORD{ 0 };
		if (handle.value == INVALID_HANDLE_VALUE || !DeviceIoControl(handle.value, FSCTL_SET_REPARSE_POINT, buffer.get(), DWORD(size), nullptr, 0, &returned, nullptr))
			error = std::error_code{ int(GetLastError()), std::system_category() };
	}
	// the handle is closed: the empty directory can go again
	if (error) {
		std::error_code ignored;
		std::filesystem::remove(link, ignored);
	}
#else
	std::filesystem::create_directory_symlink(target, link, error);
#endif
}
