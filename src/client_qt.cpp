#include "client.h"
#include "installer.h"
#include "platform.h"
#include "session.h"
#include "process_qt.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace setup;
namespace {
class Diagnostic {
	int fd = -1;
	size_t written = 0;
public:
	explicit Diagnostic(const std::filesystem::path& support) {
		auto directory = support / "Setup/Logs";
		std::filesystem::path component;
		for (const auto& part : directory) {
			component /= part;
			if (std::filesystem::is_symlink(std::filesystem::symlink_status(component)))
				throw std::runtime_error("A symlink prevents creating private client diagnostics.");
		}
		std::filesystem::create_directories(directory);
		if (::chmod(directory.c_str(), 0700)) throw std::runtime_error("Cannot make client diagnostics private.");
		auto pattern = (directory / "client-XXXXXX.log").string();
		fd = ::mkstemps(pattern.data(), 4);
		if (fd < 0) throw std::runtime_error("Cannot create private client diagnostics.");
		::fcntl(fd, F_SETFD, FD_CLOEXEC);
	}
	~Diagnostic() { if (fd >= 0) ::close(fd); }
	void append(const QByteArray& bytes) {
		// Bound a noisy child; keep draining the process pipes after the limit.
		auto count = std::min(size_t(bytes.size()), size_t(16 * 1024 * 1024) - written);
		size_t offset = 0;
		while (offset < count) {
			auto result = ::write(fd, bytes.constData() + offset, count - offset);
			if (result < 0 && errno == EINTR) continue;
			if (result <= 0) break;
			offset += size_t(result);
		}
		written += offset;
	}
};
struct Exit { int status = 0; std::string error; };
}

struct Client::Impl { ClientLayout layout; std::filesystem::path support, home; };
Client::Client(ClientLayout layout, std::filesystem::path support, std::filesystem::path home)
	: m_Impl(std::make_unique<Impl>(std::move(layout), std::move(support), std::move(home))) {}
Client::~Client() = default;
bool Client::Present() const { return assistant::clientPresent(m_Impl->layout); }

task<int> Client::Run(const Installation& installation, LaunchOptions options) {
	auto platform = nativePlatform();
	auto lock = platform->lock(m_Impl->support / "Setup/Install.lock");
	auto launch = assistant::clientLaunch(m_Impl->layout, m_Impl->support, options);
	if (launch.directory != installation.game)
		throw std::runtime_error("The active installation changed. Reopen OpenBF2 to verify it.");
	co_return co_await runClientProcess(std::move(launch), m_Impl->home);
}

task<int> setup::runClientProcess(assistant::Launch launch, std::filesystem::path home) {
	auto environment = QProcessEnvironment::systemEnvironment();
	environment.insert("HOME", QString::fromStdString(home.string()));
	if (linuxDataHome(home, environment.value("XDG_DATA_HOME").toStdString()) / "OpenBF2" != launch.support)
		throw std::runtime_error("The client and installer data directories differ. Reopen OpenBF2.");
	for (auto variable : {"PYTHONPATH", "PYTHONHOME", "PYTHONCASEOK", "PYTHONSTARTUP", "PYTHONUSERBASE"}) environment.remove(variable);
	environment.insert("DXVK_WSI_DRIVER", "SDL3");
	// Includes dlopen-only AI, DXVK and audio libraries. Never changes the desktop environment.
	auto libraries = launch.executable.parent_path().parent_path() / "lib";
	auto inherited = environment.value("LD_LIBRARY_PATH");
	environment.insert("LD_LIBRARY_PATH", QString::fromStdString(libraries.string()) + (inherited.isEmpty() ? "" : ":" + inherited));

	Diagnostic log(launch.support);
	QProcess child;
	child.setProcessEnvironment(environment);
	child.setWorkingDirectory(QString::fromStdString(launch.directory.string()));
	child.setProcessChannelMode(QProcess::MergedChannels);
	child.setStandardInputFile(QProcess::nullDevice());
	QStringList arguments;
	for (const auto& value : launch.arguments) arguments.append(QString::fromStdString(value));
	using Channel = boost::asio::experimental::channel<void(boost::system::error_code, Exit)>;
	Channel exit(co_await boost::asio::this_coro::executor, 1);
	QObject::connect(&child, &QProcess::readyReadStandardOutput, &child, [&] { log.append(child.readAllStandardOutput()); });
	QObject::connect(&child, &QProcess::errorOccurred, &child, [&](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) exit.try_send(boost::system::error_code{}, Exit{0, "The native client could not start: " + child.errorString().toStdString()});
	});
	QObject::connect(&child, &QProcess::finished, &child, [&](int code, QProcess::ExitStatus status) {
		log.append(child.readAllStandardOutput());
		exit.try_send(boost::system::error_code{}, Exit{status == QProcess::CrashExit ? 128 + code : code, {}});
	});
	child.start(QString::fromStdString(launch.executable.string()), arguments, QIODevice::ReadOnly);
	auto result = co_await exit.async_receive(boost::asio::use_awaitable);
	if (!result.error.empty()) throw std::runtime_error(result.error);
	co_return result.status;
}
