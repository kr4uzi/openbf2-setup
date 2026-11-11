#include "Platform.h"
#define NOMINMAX
#include <windows.h>
#include <stdexcept>
namespace setup { namespace {
struct Guard : Lock {
    HANDLE h;
    Guard(const std::filesystem::path& p) : h(CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)) {
        if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Installation is locked");
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(h,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) { CloseHandle(h);throw std::runtime_error("Unsafe installation lock"); }
    }
    ~Guard() override {CloseHandle(h);}
};
struct Windows : Platform {
    std::unique_ptr<Lock> lock(const std::filesystem::path& p) override {return std::make_unique<Guard>(p);}
    void syncFile(const std::filesystem::path& p) override {
        HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open file for durability");
        BOOL ok=FlushFileBuffers(h);CloseHandle(h);if(!ok)throw std::runtime_error("Cannot flush file");
    }
    void syncDirectory(const std::filesystem::path&) override {} // MoveFileEx WRITE_THROUGH is the publication barrier.
    void replace(const std::filesystem::path& a,const std::filesystem::path& b) override {
        if(!MoveFileExW(a.c_str(),b.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish installation");
    }
};
} std::unique_ptr<Platform> nativePlatform(){return std::make_unique<Windows>();} }
