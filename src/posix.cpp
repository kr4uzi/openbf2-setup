#include "platform.h"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <stdexcept>
namespace setup {
namespace {
struct Guard : Lock {
    int fd;
    explicit Guard(const std::filesystem::path& p) : fd(::open(p.c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600)) {
        if(fd<0 || ::flock(fd,LOCK_EX|LOCK_NB)) { if(fd>=0)::close(fd);throw std::runtime_error("Another setup/client holds the installation lock"); }
    }
    ~Guard() override { ::close(fd); }
};
struct Posix : Platform {
    std::unique_ptr<Lock> lock(const std::filesystem::path& p) override { return std::make_unique<Guard>(p); }
    void syncFile(const std::filesystem::path& p) override {
        int fd=::open(p.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
        if(fd<0)throw std::runtime_error("Cannot open file for durability");
        int result=::fsync(fd);::close(fd);
        if(result)throw std::runtime_error("Cannot flush installed file");
    }
    void syncDirectory(const std::filesystem::path& p) override { syncFile(p); }
    void replace(const std::filesystem::path& from,const std::filesystem::path& to) override {
        std::filesystem::rename(from,to);syncDirectory(to.parent_path());
    }
};
}
std::unique_ptr<Platform> nativePlatform() { return std::make_unique<Posix>(); }
}
