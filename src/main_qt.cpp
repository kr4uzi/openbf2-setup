#include "client.h"
#include "installer.h"
#include "microphone.h"
#include "setup.h"
#include "session.h"
#include "store.h"
#include "window.h"
#include "worker.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QSaveFile>
#include <boost/asio/co_spawn.hpp>
#include <iostream>
#include <pwd.h>
#include <unistd.h>

using namespace setup;
namespace {
std::filesystem::path homeDirectory() {
	auto home = qEnvironmentVariable("HOME").toStdString();
	if (std::filesystem::path{home}.is_absolute()) return home;
	if (auto user = ::getpwuid(::getuid()); user && user->pw_dir && std::filesystem::path{user->pw_dir}.is_absolute()) return user->pw_dir;
	throw std::runtime_error("Cannot determine an absolute home directory.");
}

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
	options.fullscreen = true; options.override = "+modPath mods/bf2 +gamespy openspy.net";
	window.ShowLauncher(options, {{"bf2", "Battlefield 2"}}, true, ""); window.Snapshot(output / "9-launcher-command-line.png");
	return 0;
}
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QApplication::setApplicationName("OpenBF2");
	QApplication::setDesktopFileName("org.openbf2.Setup");
	QApplication::setQuitOnLastWindowClosed(false);
	try {
		auto root = std::filesystem::path{QCoreApplication::applicationDirPath().toStdString()}.parent_path();
		app.setWindowIcon(QIcon(QString::fromStdString((root / "share/icons/hicolor/256x256/apps/org.openbf2.Setup.png").string())));
		auto arguments = app.arguments();
		if (arguments.size() == 2 && arguments[1] == "--check-client") {
			bool present = assistant::clientPresent(linuxClientLayout(root));
			std::cout << (present ? "Native client files are present.\n" : "Native client files are missing or not executable.\n");
			return present ? 0 : 1;
		}
		boost::asio::io_context context;
		Worker worker;
		Window window(context);
		if (arguments.size() == 3 && arguments[1] == "--smoke-test") return smoke(window, arguments[2].toStdString());
		if (arguments.size() > 2 || (arguments.size() == 2 && arguments[1] != "--launcher" && arguments[1] != "--install-desktop")) {
			std::cerr << "Usage: OpenBF2 [--launcher | --install-desktop | --check-client | --smoke-test NEW_DIRECTORY]\n"; return 2;
		}
		auto home = homeDirectory();
		auto data = linuxDataHome(home, qEnvironmentVariable("XDG_DATA_HOME").toStdString());
		if (arguments.contains("--install-desktop")) { installDesktop(data, root); return 0; }
		auto support = data / "OpenBF2";
		Installer installer(worker, support, root / "share/openbf2/setup");
		Store store(support);
		Client client(linuxClientLayout(root), support, home);
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
