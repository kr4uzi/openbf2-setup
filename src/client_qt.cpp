#include "client.h"
#include "installer.h"
#include "platform.h"
#include "session.h"
#include "process_qt.h"
#include "platform_qt.h"

#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <cerrno>
#if defined(Q_OS_LINUX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(Q_OS_WIN)
#include "directx.h"
#include <QMessageBox>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

using namespace setup;
namespace {
#if defined(Q_OS_LINUX)
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
#endif
struct Exit { int status = 0; std::string error; };
#if defined(Q_OS_WIN)
// The 64-bit client needs Microsoft's 64-bit d3dx9_25.dll (directx.h). When neither the installation from the disc
// nor the system gave it one, Microsoft's DirectX setup from the application folder installs it, with the user's
// consent; Windows asks for administrator rights. Waits without blocking the window
task<void> requireDirectX(const std::filesystem::path& folder) {
	if (d3dxPresent(folder)) co_return;
	const std::string missing = "The 64-bit client needs Microsoft's DirectX 9 runtime (the 64-bit d3dx9_25.dll), which is not installed.";
	auto setup = directxSetup(folder);
	if (!setup) throw std::runtime_error(missing + " Install the DirectX End-User Runtime, or choose the 32-bit client under Client.");
	auto answer = QMessageBox::question(nullptr, "DirectX Runtime Needed",
		QString::fromStdString(missing + "\n\nInstall it now with Microsoft's DirectX setup? Windows asks for administrator rights."),
		QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
	if (answer != QMessageBox::Yes) throw std::runtime_error(missing + " Install it, or choose the 32-bit client under Client.");
	auto file = setup->wstring(), directory = setup->parent_path().wstring();
	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof info;
	info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	info.lpVerb = L"runas";
	info.lpFile = file.c_str();
	info.lpParameters = L"/silent";
	info.lpDirectory = directory.c_str();
	info.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&info) || !info.hProcess)
		throw std::runtime_error(GetLastError() == ERROR_CANCELLED ? "The DirectX setup needs administrator rights, which were not given." : "The DirectX setup did not start.");
	boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor);
	while (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) {
		timer.expires_after(std::chrono::milliseconds(200));
		co_await timer.async_wait(boost::asio::use_awaitable);
	}
	CloseHandle(info.hProcess);
	if (!d3dxPresent(folder)) throw std::runtime_error("The DirectX setup ended, but the 64-bit d3dx9_25.dll is still missing. Choose the 32-bit client under Client.");
}
#endif
}

struct Client::Impl { ClientLayout layout; std::filesystem::path support, home; ClientLayout x86; };
Client::Client(ClientLayout layout, std::filesystem::path support, std::filesystem::path home)
	: m_Impl(std::make_unique<Impl>(std::move(layout), std::move(support), std::move(home))) {}
Client::~Client() = default;
void Client::Alternative(ClientLayout x86) { m_Impl->x86 = std::move(x86); }
bool Client::Present() const { return assistant::clientPresent(m_Impl->layout) || (!m_Impl->x86.executable.empty() && assistant::clientPresent(m_Impl->x86)); }

task<int> Client::Run(const Installation& installation, LaunchOptions options) {
	auto platform = nativePlatform();
	auto lock = platform->lock(m_Impl->support / "Setup/Install.lock");
	auto& layout = options.client == "x86" && !m_Impl->x86.executable.empty() ? m_Impl->x86 : m_Impl->layout;
#if defined(Q_OS_WIN)
	if (&layout == &m_Impl->layout) co_await requireDirectX(layout.executable.parent_path());
#endif
	auto launch = assistant::clientLaunch(layout, m_Impl->support, options);
	if (launch.directory != installation.game)
		throw std::runtime_error("The active installation changed. Reopen OpenBF2 to verify it.");
	co_return co_await runClientProcess(std::move(launch), m_Impl->home);
}

task<int> setup::runClientProcess(assistant::Launch launch, std::filesystem::path home) {
	auto environment = QProcessEnvironment::systemEnvironment();
	// the client derives its support root from HOME, so both look at the same files (a test home moves
	// everything). the embedded Python 2.3 would read these, and a foreign PYTHONPATH breaks the scripts
#if !defined(Q_OS_WIN)
	environment.insert("HOME", QString::fromStdString(home.string()));
#endif
	for (auto variable : {"PYTHONPATH", "PYTHONHOME", "PYTHONCASEOK", "PYTHONSTARTUP", "PYTHONUSERBASE"}) environment.remove(variable);
#if defined(Q_OS_LINUX)
	if (linuxDataHome(home, environment.value("XDG_DATA_HOME").toStdString()) / "OpenBF2" != launch.support)
		throw std::runtime_error("The client and installer data directories differ. Reopen OpenBF2.");
	environment.insert("DXVK_WSI_DRIVER", "SDL3");
	// Includes dlopen-only AI, DXVK and audio libraries. Never changes the desktop environment.
	auto libraries = launch.executable.parent_path().parent_path() / "lib";
	auto inherited = environment.value("LD_LIBRARY_PATH");
	environment.insert("LD_LIBRARY_PATH", QString::fromStdString(libraries.string()) + (inherited.isEmpty() ? "" : ":" + inherited));
#endif

	QProcess child;
	child.setProcessEnvironment(environment);
	child.setWorkingDirectory(QString::fromStdString(launch.directory.string()));
	child.setProcessChannelMode(QProcess::MergedChannels);
	child.setStandardInputFile(QProcess::nullDevice());
#if defined(Q_OS_LINUX)
	Diagnostic log(launch.support);
#else
	// the client's own log is what it prints: into the game folder next to the renderer's log, afresh
	// with every run (docs/client-artifact.md)
	child.setStandardOutputFile(QString::fromStdString((launch.directory / "OpenBF2Client.log").string()), QIODevice::Truncate);
#endif
	QStringList arguments;
	for (const auto& value : launch.arguments) arguments.append(QString::fromStdString(value));
	using Channel = boost::asio::experimental::channel<void(boost::system::error_code, Exit)>;
	Channel exit(co_await boost::asio::this_coro::executor, 1);
#if defined(Q_OS_LINUX)
	// the start of the output stays at hand too: a refused provider file is said there (exit code 42)
	QByteArray start;
	auto keep = [&](const QByteArray& bytes) { log.append(bytes); if (start.size() < 64 * 1024) start.append(bytes.left(64 * 1024 - start.size())); };
	QObject::connect(&child, &QProcess::readyReadStandardOutput, &child, [&] { keep(child.readAllStandardOutput()); });
#endif
	QObject::connect(&child, &QProcess::errorOccurred, &child, [&](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) exit.try_send(boost::system::error_code{}, Exit{0, "The native client could not start: " + child.errorString().toStdString()});
	});
	QObject::connect(&child, &QProcess::finished, &child, [&](int code, QProcess::ExitStatus status) {
#if defined(Q_OS_LINUX)
		keep(child.readAllStandardOutput());
#endif
		exit.try_send(boost::system::error_code{}, Exit{status == QProcess::CrashExit ? 128 + code : code, {}});
	});
	child.start(QString::fromStdString(launch.executable.string()), arguments, QIODevice::ReadOnly);

	// the game gets the foreground from the application which started it, so its first clicks count.
	// Until its window is up (macOS, platform_mac.mm), twenty seconds at most; the client also asks
	// for activation itself
	if (child.waitForStarted(5000)) {
		auto timer = boost::asio::steady_timer{ co_await boost::asio::this_coro::executor };
		for (int attempt = 0; attempt < 200 && child.state() == QProcess::Running; ++attempt) {
			if (frontend::handOverActivation(child.processId()))
				break;

			timer.expires_after(std::chrono::milliseconds{ 100 });
			co_await timer.async_wait(boost::asio::use_awaitable);
		}
	}

	auto result = co_await exit.async_receive(boost::asio::use_awaitable);
	if (!result.error.empty()) throw std::runtime_error(result.error);
	// exit code 42: the game refused its online services file and says why in one line of its output
	if (result.status == 42) {
#if defined(Q_OS_LINUX)
		auto output = start.toStdString();
#else
		auto output = std::string{};
		if (QFile file(QString::fromStdString((launch.directory / "OpenBF2Client.log").string())); file.open(QIODevice::ReadOnly))
			output = file.read(64 * 1024).toStdString();
#endif
		if (auto refusal = provider_refusal(output); !refusal.empty())
			throw std::runtime_error("The game did not accept the online services file:\n" + refusal);
	}
	co_return result.status;
}
