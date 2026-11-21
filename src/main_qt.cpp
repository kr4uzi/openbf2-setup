#include "client.h"
#include "installer.h"
#include "microphone.h"
#include "setup.h"
#include "session.h"
#include "store.h"
#include "window.h"
#include "worker.h"
#include "platform_qt.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QSaveFile>
#include <boost/asio/co_spawn.hpp>
#include <iostream>
#include <QStandardPaths>
#if !defined(Q_OS_WIN)
#include <pwd.h>
#include <unistd.h>
#endif

using namespace setup;
namespace {
std::filesystem::path homeDirectory() {
#if defined(Q_OS_WIN)
	return std::filesystem::path{QDir::homePath().toStdWString()};
#else
	auto home = qEnvironmentVariable("HOME").toStdString();
	if (std::filesystem::path{home}.is_absolute()) return home;
	if (auto user = ::getpwuid(::getuid()); user && user->pw_dir && std::filesystem::path{user->pw_dir}.is_absolute()) return user->pw_dir;
	throw std::runtime_error("Cannot determine an absolute home directory.");
#endif
}

#if defined(Q_OS_LINUX)
QString desktopQuote(QString value) {
	// Desktop Exec is not shell syntax. Escape both the string and Exec layers.
	value.replace('%', "%%"); value.replace('\\', "\\\\\\\\"); value.replace('"', "\\\\\"");
	value.replace('`', "\\\\`"); value.replace('$', "\\\\$");
	return '"' + value + '"';
}

void installDesktop(const std::filesystem::path& data, const std::filesystem::path& root) {
	if (qEnvironmentVariableIsSet("FLATPAK_ID")) return; // Flatpak exports its own entry.
	auto executable = qEnvironmentVariable("APPIMAGE");
	if (executable.isEmpty()) executable = QCoreApplication::applicationFilePath();
	if (!QFileInfo(executable).isAbsolute() || executable.contains('\n') || executable.contains('\r')) throw std::runtime_error("Invalid desktop executable path.");
	auto directory = data / "applications";
	std::filesystem::create_directories(directory);
	auto icon = data / "icons/hicolor/256x256/apps/org.openbf2.Setup.png";
	std::filesystem::create_directories(icon.parent_path());
	// QSaveFile replaces atomically and never follows a final symlink.
	QFile source(QString::fromStdString((root / "share/icons/hicolor/256x256/apps/org.openbf2.Setup.png").string()));
	if (!source.open(QIODevice::ReadOnly)) throw std::runtime_error("The application icon is missing.");
	QSaveFile iconFile(QString::fromStdString(icon.string()));
	auto bytes = source.readAll();
	if (!iconFile.open(QIODevice::WriteOnly) || iconFile.write(bytes) != bytes.size() || !iconFile.commit()) throw std::runtime_error("Could not install the application icon.");
	auto exec = desktopQuote(executable);
	auto entry = QString("[Desktop Entry]\nType=Application\nName=OpenBF2\nComment=Install and play Battlefield 2\nExec=%1\nIcon=org.openbf2.Setup\nTerminal=false\nCategories=Game;\nActions=Launcher;\n\n[Desktop Action Launcher]\nName=Launcher and settings\nExec=%1 --launcher\n").arg(exec).toUtf8();
	QSaveFile file(QString::fromStdString((directory / "org.openbf2.Setup.desktop").string()));
	if (!file.open(QIODevice::WriteOnly) || file.write(entry) != entry.size() || !file.commit()) throw std::runtime_error("Could not install the desktop entry.");
}

#endif

// test homes and snapshots only below the scratch directory of the build, never the real home (macOS
// builds define SETUP_UI_TEST_ROOT; a direct child of it which is not a symlink)
std::filesystem::path testPath(const QString& argument) {
#if defined(Q_OS_WIN)
	auto chosen = std::filesystem::absolute(std::filesystem::path{argument.toStdWString()}).lexically_normal();
#else
	auto chosen = std::filesystem::absolute(argument.toStdString()).lexically_normal();
#endif
#if defined(SETUP_UI_TEST_ROOT)
	auto allowed = std::filesystem::weakly_canonical(std::filesystem::path{SETUP_UI_TEST_ROOT});
	if (std::filesystem::weakly_canonical(chosen.parent_path()) != allowed || std::filesystem::is_symlink(std::filesystem::symlink_status(chosen)))
		throw std::runtime_error("Test paths must be direct children of " + allowed.string());
#endif
	return chosen;
}

int smoke(Window& window, const std::filesystem::path& output) {
	if (std::filesystem::exists(output)) throw std::runtime_error("Smoke snapshots need a fresh output directory.");
	std::filesystem::create_directories(output); window.StayHidden();
	window.ShowWelcome(); window.Snapshot(output / "0-welcome.png");
	window.ShowDisc(); window.Snapshot(output / "1-disc.png");
	window.ShowPatches(); window.EnableNext(false); window.Snapshot(output / "2-patches.png");
	window.ShowPatches(true); window.SetPatch(PatchSlot::Patch141, PatchStatus::verified); window.EnableNext(false); window.Snapshot(output / "2-complete-patches.png");
	window.ShowKeys(true, true); window.Snapshot(output / "3-keys.png");
	window.ShowInstall(); window.SetProgress({"patch141", "mods/bf2", 50, 100, 1, 2}); window.Snapshot(output / "4-install.png");
	window.ShowVoice({}, false); window.SetMicrophone(MicrophoneStatus::skipped); window.SetLevel(.35); window.Snapshot(output / "5-voice.png");
	window.ShowLauncher({}, {{"bf2", "Battlefield 2"}}, false, "The game client is not part of this build.");
	window.SetCommandLine(command_line(launch_arguments({}))); window.Snapshot(output / "6-launcher.png");
	LaunchOptions options; options.skip_movies = false;
	window.ShowLauncher(options, {{"bf2", "Battlefield 2"}}, true, "");
	window.SetCommandLine(command_line(launch_arguments(options))); window.Snapshot(output / "7-launcher-fullscreen.png");
	options.fullscreen = false; options.width = 1280; options.height = 720;
	window.ShowLauncher(options, {{"bf2", "Battlefield 2"}}, true, "");
	window.SetCommandLine(command_line(launch_arguments(options))); window.Snapshot(output / "8-launcher-windowed.png");
	options.fullscreen = true; options.override = "+modPath mods/bf2 +noSound 1";
	window.ShowLauncher(options, {{"bf2", "Battlefield 2"}}, true, ""); window.Snapshot(output / "9-launcher-command-line.png");
	options = LaunchOptions{}; options.online = "openspy"; options.provider = parse_provider(provider_preset("openspy")->json); options.provider_folder = "/Users/Player/Library/Application Support/OpenBF2/Setup";
	window.ShowLauncher(options, {{"bf2", "Battlefield 2"}}, true, "");
	window.SetCommandLine(command_line(launch_arguments(options))); window.Snapshot(output / "10-launcher-online.png");
	if (window.Options() != options) throw std::runtime_error("The launcher does not give back the online provider it was shown.");
	options = LaunchOptions{}; options.no_sound = true; options.fullscreen = false; options.window_x = 40; options.window_y = 30;
	options.tutorial_messages = false; options.server = "10.0.0.5"; options.password = "secret";
#if defined(Q_OS_WIN)
	options.skip_dx_check = true;
#endif
	window.ShowLauncher(options, {{"bf2", "Battlefield 2"}}, true, "");
	window.SetCommandLine(command_line(launch_arguments(options))); window.Snapshot(output / "11-launcher-advanced.png");
	window.SetDemos({"Demos/Strike_at_Karkand_2026-11-20.bf2demo", "Demos/Wake_Island_2026-11-18.bf2demo"});
	window.SnapshotAdvanced(output / "12-advanced-options.png");
	if (window.Options() != options) throw std::runtime_error("The launcher does not give back the advanced options it was shown.");
	// another provider's file from the web: an entry per host besides the fallback, holding until a date
	options = LaunchOptions{}; options.online = "other"; options.other_source = "url"; options.other_url = "https://example.org/provider.json";
	options.provider_folder = "/Users/Player/Library/Application Support/OpenBF2/Setup";
	options.provider = parse_provider(R"({"schema": 1, "provider": "Example", "valid_to": "2026-11-27T18:00:00Z", "services": {"*": "example.org", "gpcm": "login.example.org"}})");
	window.ShowLauncher(options, {{"bf2", "Battlefield 2"}}, true, "");
	window.SetCommandLine(command_line(launch_arguments(options))); window.Snapshot(output / "13-launcher-provider.png");
	if (window.Options() != options) throw std::runtime_error("The launcher does not give back the provider file and its services.");
	window.SnapshotProvider(output / "14-other-provider.png");
	return 0;
}
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QApplication::setApplicationName("OpenBF2");
	QApplication::setQuitOnLastWindowClosed(false);
	// latched before anything else: on macOS the Option key counts only while the application opens
	frontend::launcherRequested();
	try {
		auto arguments = app.arguments();
#if defined(Q_OS_MACOS)
		// OpenBF2.app/Contents/MacOS/OpenBF2: the manifests and the client live in the bundle
		auto contents = std::filesystem::path{QCoreApplication::applicationDirPath().toStdString()}.parent_path();
		auto manifests = contents / "Resources/Setup";
		auto layout = macClientLayout(contents);
		const char* usage = "Usage: OpenBF2 [--check-client | --smoke-test NEW_DIRECTORY | --test-home DIRECTORY]\n";
#elif defined(Q_OS_WIN)
		// OpenBF2\OpenBF2.exe with setup\ (the manifests) and the two client packages x64\
		// and x86\ beside it
		auto folder = std::filesystem::path{QCoreApplication::applicationDirPath().toStdWString()};
		auto manifests = folder / "setup";
		auto layout = windowsClientLayout(folder / "x64");
		auto x86 = windowsClientLayout(folder / "x86");
		const char* usage = "Usage: OpenBF2 [--check-client | --smoke-test NEW_DIRECTORY | --test-home DIRECTORY]\n";
#else
		QApplication::setDesktopFileName("org.openbf2.Setup");
		auto root = std::filesystem::path{QCoreApplication::applicationDirPath().toStdString()}.parent_path();
		app.setWindowIcon(QIcon(QString::fromStdString((root / "share/icons/hicolor/256x256/apps/org.openbf2.Setup.png").string())));
		auto manifests = root / "share/openbf2/setup";
		auto layout = linuxClientLayout(root);
		const char* usage = "Usage: OpenBF2 [--launcher | --install-desktop | --check-client | --smoke-test NEW_DIRECTORY]\n";
#endif
		if (arguments.size() == 2 && arguments[1] == "--check-client") {
#if defined(Q_OS_WIN)
			bool present = assistant::clientPresent(layout) || assistant::clientPresent(x86);
#else
			bool present = assistant::clientPresent(layout);
#endif
			std::cout << (present ? "Native client files are present.\n" : "Native client files are missing or not executable.\n");
			return present ? 0 : 1;
		}
		boost::asio::io_context context;
		Worker worker;
		Window window(context);
		if (arguments.size() == 3 && arguments[1] == "--smoke-test") return smoke(window, testPath(arguments[2]));
		auto home = homeDirectory();
#if defined(Q_OS_MACOS)
		if (arguments.size() == 3 && arguments[1] == "--test-home") {
			home = testPath(arguments[2]);
			std::filesystem::create_directories(home);
		}
		else if (arguments.size() != 1) { std::cerr << usage; return 2; }
		auto support = home / "Library/Application Support/OpenBF2";
		auto profiles = support;
#elif defined(Q_OS_WIN)
		// the per-user installation: game data, setup state and keys (Registry.dat) in %LOCALAPPDATA%\OpenBF2,
		// profiles where the game keeps them, Documents\Battlefield 2. A test home moves both
		auto local = std::filesystem::path{QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation).toStdWString()};
		auto documents = std::filesystem::path{QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdWString()};
		if (arguments.size() == 3 && arguments[1] == "--test-home") {
			home = testPath(arguments[2]);
			local = home / "AppData/Local";
			documents = home / "Documents";
		}
		else if (arguments.size() != 1) { std::cerr << usage; return 2; }
		auto support = local / "OpenBF2";
		auto profiles = documents / "Battlefield 2";
		std::filesystem::create_directories(support);
#else
		if (arguments.size() > 2 || (arguments.size() == 2 && arguments[1] != "--launcher" && arguments[1] != "--install-desktop")) {
			std::cerr << usage; return 2;
		}
		auto data = linuxDataHome(home, qEnvironmentVariable("XDG_DATA_HOME").toStdString());
		if (arguments.contains("--install-desktop")) { installDesktop(data, root); return 0; }
		auto support = data / "OpenBF2";
		auto profiles = support;
#endif
		Installer installer(worker, support, manifests);
#if defined(Q_OS_WIN)
		installer.DirectX(folder / "x64");
#endif
		Store store(support, profiles);
		Client client(layout, support, home);
#if defined(Q_OS_WIN)
		client.Alternative(x86);
#endif
		Microphone microphone(true);
		Setup application(window, installer, store, client, microphone);
		int code = 0;
		boost::asio::co_spawn(context, application.Launch(), [&](std::exception_ptr error, int result) {
			code = result;
			if (error) {
				code = 1;
				try { std::rethrow_exception(error); }
				catch (const std::exception& e) { QMessageBox::critical(nullptr, "OpenBF2 has to quit", QString::fromUtf8(e.what())); }
			}
			window.Close();
		});
		window.Run(); return code;
	} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
