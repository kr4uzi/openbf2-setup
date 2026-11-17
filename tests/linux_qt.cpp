#include "client.h"
#include "microphone.h"
#include "platform.h"
#include "process_qt.h"
#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QProgressBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <SDL3/SDL.h>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

using namespace setup;
namespace {
unsigned checks = 0;
void check(bool ok, const char* error) { if (!ok) throw std::runtime_error(error); ++checks; }
void write(const std::filesystem::path& path, const std::string& value) { std::filesystem::create_directories(path.parent_path()); std::ofstream out(path); out << value; if (!out) throw std::runtime_error("fixture write failed"); }
task<void> pause() { boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor); timer.expires_after(std::chrono::milliseconds(30)); co_await timer.async_wait(boost::asio::use_awaitable); }

task<int> exercise(Window& window, std::filesystem::path root) {
	using Kind = Event::Kind;
	check(window.LauncherRequested(), "--launcher was ignored");
	check(linuxDataHome("/home/example", "") == "/home/example/.local/share", "empty XDG fallback");
	check(linuxDataHome("/home/example", "relative") == "/home/example/.local/share", "relative XDG fallback");
	check(linuxDataHome("/home/example", "/data with spaces") == "/data with spaces", "absolute XDG override");
	std::filesystem::create_directories(root / "actual/child");
	std::filesystem::create_directory_symlink(root / "actual/child", root / "xdg-link");
	check(linuxDataHome("/home/example", (root / "xdg-link/../data").string()) == root / "actual/data", "XDG symlink traversal differs from client");
	auto layout = linuxClientLayout(root / "application with spaces");
	check(!assistant::clientPresent(layout), "missing Linux artifact accepted");
	write(layout.executable, "#!/bin/sh\nexit 0\n");
	for (auto& file : layout.required) write(file, "library fixture");
	check(!assistant::clientPresent(layout), "nonexecutable Linux artifact accepted");
	std::filesystem::permissions(layout.executable, std::filesystem::perms::owner_all);
	check(assistant::clientPresent(layout), "complete Linux layout rejected");
	std::filesystem::remove(layout.required.back()); check(!assistant::clientPresent(layout), "missing dlopen library accepted");

	window.ShowWelcome(); check(window.Shown() == Page::welcome, "welcome page"); window.Click(Kind::next);
	check((co_await window.WaitForEvent()).kind == Kind::next, "Continue event");
	window.ShowDisc(); window.Choose(Kind::disc_chosen, root / "disc with spaces.iso");
	check((co_await window.WaitForEvent()).path == root / "disc with spaces.iso", "file selection lost spaces");
	// Exercise the same picker for files and directories, including navigation and
	// cancellation. Portal requests use Qt's widget fallback in these isolated tests.
	write(root / "chosen.iso", "not game media");
	std::filesystem::create_directories(root / "DVD with spaces");
	write(root / "DVD with spaces/nested.ISO", "not game media");
	QPushButton* chooser = nullptr;
	for (auto top : QApplication::topLevelWidgets()) for (auto button : top->findChildren<QPushButton*>()) if (button->text() == "Choose DVD / ISO…") chooser = button;
	check(chooser != nullptr, "Choose button missing");
	const bool portal = qEnvironmentVariableIsSet("FLATPAK_ID");
	check(bool(chooser->menu()) == portal, "Flatpak portal choices missing or desktop picker split");
	auto chooseWith = [&](int action, const std::function<void(QFileDialog*)>& drive) {
		QTimer timer;
		QObject::connect(&timer, &QTimer::timeout, [&] {
			for (auto top : QApplication::topLevelWidgets())
				if (auto dialog = qobject_cast<QFileDialog*>(top); dialog && dialog->isVisible()) drive(dialog);
		});
		timer.start(25);
		if (portal) chooser->menu()->actions().at(action)->trigger(); else chooser->click();
	};
	auto typePath = [&](QFileDialog* dialog, const std::filesystem::path& path) {
		if (auto name = dialog->findChild<QLineEdit*>("fileNameEdit")) name->setText(QString::fromStdString(path.string()));
		dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Open)->click();
	};
	chooseWith(0, [&](QFileDialog* dialog) { typePath(dialog, root / "chosen.iso"); });
	check((co_await window.WaitForEvent()).path == root / "chosen.iso", "ISO selection lost");
	// Select a folder row and click Choose: this must accept it, rather than enter it.
	bool prepared = false;
	chooseWith(1, [&](QFileDialog* dialog) {
		if (!prepared) { dialog->setDirectory(QString::fromStdString(root.string())); dialog->setViewMode(QFileDialog::List); prepared = true; return; }
		auto view = dialog->findChild<QListView*>("listView");
		auto model = qobject_cast<QFileSystemModel*>(view->model());
		auto index = model->index(QString::fromStdString((root / "DVD with spaces").string()));
		if (!index.isValid() || !view->visualRect(index).isValid()) return;
		view->setCurrentIndex(index);
		view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Open)->click();
	});
	check((co_await window.WaitForEvent()).path == root / "DVD with spaces", "DVD folder selection lost");
	if (!portal) {
		int step = 0;
		chooseWith(0, [&](QFileDialog* dialog) {
			if (step == 0) { dialog->setDirectory(QString::fromStdString(root.string())); dialog->setViewMode(QFileDialog::List); step = 1; return; }
			if (step == 1) {
				auto view = dialog->findChild<QListView*>("listView");
				auto model = qobject_cast<QFileSystemModel*>(view->model());
				auto index = model->index(QString::fromStdString((root / "DVD with spaces").string()));
				if (!index.isValid() || !view->visualRect(index).isValid()) return;
				QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(index).center());
				QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(index).center());
				step = 2; return;
			}
			if (dialog->directory().absolutePath() == QString::fromStdString((root / "DVD with spaces").string())) {
				step = 3; typePath(dialog, root / "DVD with spaces/nested.ISO");
			}
		});
		check(step == 3 && (co_await window.WaitForEvent()).path == root / "DVD with spaces/nested.ISO", "folder double-click did not navigate");
	}
	chooseWith(0, [](QFileDialog* dialog) { dialog->reject(); });
	window.Click(Kind::next);
	check((co_await window.WaitForEvent()).kind == Kind::next, "cancelled picker emitted a media selection");
	if (portal) { std::cout << checks << " portal picker routing checks passed (widget fallback)\n"; co_return 0; }
	QWidget* well = nullptr;
	for (auto top : QApplication::topLevelWidgets()) if ((well = top->findChild<QWidget*>("discWell"))) break;
	check(well != nullptr, "actual disc well missing");
	QMimeData mime; mime.setUrls({QUrl::fromLocalFile(QString::fromStdString((root / "dropped DVD").string()))});
	QDragEnterEvent enter(QPoint(5,5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(well, &enter); check(enter.isAccepted(), "disc drag rejected");
	QDropEvent drop(QPointF(5,5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(well, &drop); check((co_await window.WaitForEvent()).path == root / "dropped DVD", "disc drop event");
	window.ShowPatches(); window.Choose(Kind::patch141_chosen, "first.exe"); window.Choose(Kind::patch141_chosen, "replacement.exe");
	check((co_await window.WaitForEvent()).path == "first.exe", "first patch selection"); check((co_await window.WaitForEvent()).path == "replacement.exe", "patch replacement order");
	window.SetPatch(PatchSlot::Patch141, PatchStatus::checking, .5); window.SetPatch(PatchSlot::Patch141, PatchStatus::verified);
	window.ShowPatches(true);
	QWidget* includedWell = nullptr;
	for (auto top : QApplication::topLevelWidgets()) if ((includedWell = top->findChild<QWidget*>("patch141Well"))) break;
	check(includedWell && !includedWell->acceptDrops(), "included patch accepts a replacement drop");
	check(!includedWell->findChild<QPushButton*>()->isEnabled(), "included patch still asks for a file");
	bool includedLabel = false;
	for (auto label : includedWell->findChildren<QLabel*>()) includedLabel |= label->text().contains("Included on disc");
	check(includedLabel, "included patch source is not shown");
	window.Choose(Kind::patch141_chosen, "ignored.exe"); window.Choose(Kind::patch150_chosen, "second.exe");
	check((co_await window.WaitForEvent()).path == "second.exe", "included patch can be replaced");
	window.ShowPatches(false);
	check(includedWell->acceptDrops() && includedWell->findChild<QPushButton*>()->isEnabled(), "manual patch selection did not return");
	window.ShowKeys(true, true); window.PasteKey(KeyEdition::BF2, "abcd-1234-efgh-5678-ijkl");
	check((co_await window.WaitForEvent()).kind == Kind::keys_edited, "key edit event");
	check(window.Keys().key == "ABCD1234EFGH5678IJKL", "grouped key paste");
	QLineEdit* keyField = nullptr;
	for (auto top : QApplication::topLevelWidgets()) for (auto field : top->findChildren<QLineEdit*>()) if (field->accessibleName() == "Battlefield 2 CD key group 1") keyField = field;
	check(keyField != nullptr, "real key field missing");
	QApplication::clipboard()->setText("zzzz-1111-yyyy-2222-xxxx"); QTest::keyClick(keyField, Qt::Key_V, Qt::ControlModifier);
	co_await window.WaitForEvent(); check(window.Keys().key == "ZZZZ1111YYYY2222XXXX", "clipboard whole-key paste failed");
	window.PasteKey(KeyEdition::SpecialForces, "ssss-ssss-ssss-ssss-ssss"); co_await window.WaitForEvent();
	QCheckBox* sf = nullptr;
	for (auto top : QApplication::topLevelWidgets()) for (auto box : top->findChildren<QCheckBox*>()) if (box->text() == "Install Special Forces") sf = box;
	check(sf != nullptr, "Deluxe checkbox missing"); sf->click(); co_await window.WaitForEvent(); check(!window.Keys().install_special_forces, "Deluxe deselection ignored");
	window.ShowDisc(); window.ShowKeys(true, true); check(window.Keys().key.size() == 20, "Back lost keys");
	window.ClearKeys(); check(window.Keys().key.empty() && window.Keys().special_forces_key.empty(), "keys not cleared");
	window.ShowInstall();
	std::array<QProgressBar*, 4> stages{};
	QLabel* installStatus = nullptr;
	for (auto top : QApplication::topLevelWidgets()) {
		if (auto status = top->findChild<QLabel*>("installStatus")) installStatus = status;
		for (size_t row = 0; row < stages.size(); ++row)
			if (auto bar = top->findChild<QProgressBar*>(QString("installProgress%1").arg(row))) stages[row] = bar;
	}
	check(installStatus && std::all_of(stages.begin(), stages.end(), [](auto bar) { return bar && bar->value() == 0 && bar->format() == "Waiting"; }), "installation stages missing or not reset");
	window.SetProgress({"inputs", "Patch 1.41", 50, 100, 0, 3});
	check(installStatus->text() == "Checking your media… 50%" && stages[0]->value() == 0, "input hashing advanced an install stage");
	window.SetProgress({"disc", "data2.cab", 40, 100, 2, 10});
	check(stages[0]->value() == 400 && stages[1]->value() == 0, "DVD progress missing");
	window.SetProgress({"preserveRenderer", "RendDX9.dll", 0, 100, 0, 1});
	check(stages[0]->value() == 1000 && stages[0]->format() == "Done", "renderer preservation reset DVD progress");
	window.SetProgress({"patch141", "mods/bf2", 30, 100, 2, 10});
	check(stages[0]->value() == 1000 && stages[1]->value() == 300 && stages[2]->value() == 0, "1.41 progress changed other stages");
	window.SetProgress({"patch141-xp1", "mods/xpack", 0, 0, 20, 40});
	check(stages[1]->value() == 500, "Special Forces file-count progress missing");
	window.SetProgress({"overlay150", "mods/bf2", 75, 100, 0, 1});
	check(stages[1]->format() == "Done" && stages[2]->value() == 750 && stages[3]->value() == 0, "1.5 progress changed other stages");
	window.SetProgress({"verify", "mods/bf2", 30, 100, 0, 1});
	check(stages[2]->format() == "Done" && stages[3]->value() == 300, "final verification progress missing");
	window.Click(Kind::next);
	check((co_await window.WaitForEvent()).kind == Kind::next, "Cancel event"); window.SetStatus("Cancelling…");
	window.SetProgress({"verify", "mods/bf2", 90, 100, 0, 1});
	check(installStatus->text() == "Cancelling…" && stages[3]->value() == 300, "late progress overwrote cancellation");
	window.ShowInstall();
	check(std::all_of(stages.begin(), stages.end(), [](auto bar) { return bar->value() == 0 && bar->format() == "Waiting"; }), "resume retained stale stage progress");
	window.ShowVoice({.7, .4, .2, true}, true); check(window.Voice().playbackVolume == .7 && window.Voice().boost && window.Voice().captureVolume == .4, "voice controls");
	window.SetMicrophone(MicrophoneStatus::capture_error); window.Click(Kind::microphone); check((co_await window.WaitForEvent()).kind == Kind::microphone, "microphone retry action");
	window.Click(Kind::voice_skip); check((co_await window.WaitForEvent()).kind == Kind::voice_skip, "voice skip action");
	LaunchOptions options; options.fullscreen = false; options.width = 1280; options.height = 720; options.server = "127.0.0.1"; options.port = 19643; options.player = "Player with spaces"; options.always_show = true;
	window.ShowLauncher(options, {{"bf2", "Battlefield 2"}}, true, ""); check(window.Options() == options, "launcher options changed");
	window.SetCommandLine(command_line(launch_arguments(options)));
	QToolButton* lock = nullptr;
	for (auto top : QApplication::topLevelWidgets()) if (auto control = top->findChild<QToolButton*>("commandLineLock")) lock = control;
	check(lock != nullptr, "command-line lock missing");
	auto shell = lock->window();
	auto line = shell->findChild<QLineEdit*>("commandLine");
	auto size = shell->findChild<QWidget*>("windowSize");
	auto full = shell->findChild<QCheckBox*>("fullscreen");
	auto always = shell->findChild<QCheckBox*>("alwaysShowLauncher");
	auto hint = shell->findChild<QLabel*>("launcherHint");
	auto voice = shell->findChild<QPushButton*>("voiceSetupButton");
	auto verify = shell->findChild<QPushButton*>("verifyGameDataButton");
	check(line && size && full && always && hint && voice && verify, "launcher controls missing");
	check(line->isReadOnly() && !size->isHidden() && hint->isHidden(), "launcher control visibility differs from saved options");
	lock->click(); check((co_await window.WaitForEvent()).kind == Kind::options_edited, "unlock did not report an edit");
	check(!line->isReadOnly() && !full->isEnabled() && always->isEnabled(), "manual arguments did not disable generated options");
	line->setText("+modPath mods/bf2 +fullscreen 0"); window.SetCommandLine("must not replace manual arguments");
	check(window.Options().override == "+modPath mods/bf2 +fullscreen 0", "manual arguments were overwritten");
	lock->click(); co_await window.WaitForEvent();
	check(line->isReadOnly() && full->isEnabled() && window.Options().override.empty(), "locking did not restore controls");
	full->click(); co_await window.WaitForEvent(); check(size->isHidden(), "fullscreen did not hide window size");
	full->click(); co_await window.WaitForEvent(); check(!size->isHidden(), "windowed mode did not restore window size");
	always->click(); co_await window.WaitForEvent(); check(!hint->isHidden(), "launcher hint stayed hidden");
	window.Click(Kind::verify); check((co_await window.WaitForEvent()).kind == Kind::verify, "verify button");
	window.SetBusy("Playing…"); check(!voice->isEnabled() && !verify->isEnabled() && !lock->isEnabled(), "busy launcher left actions enabled");
	window.ShowVoice({}, false); check(voice->isHidden() && verify->isHidden(), "launcher footer leaked into voice setup");
	window.ShowLauncher(options, {{"bf2", "Battlefield 2"}}, true, ""); window.Click(Kind::next); check((co_await window.WaitForEvent()).kind == Kind::next, "Play did not reenable");

	// Real SDL capture, with an explicit synthetic driver: no host microphone is touched.
	qputenv("SDL_AUDIO_DRIVER", "dummy"); Microphone microphone(true); int levels = 0;
	check(microphone.Access() == MicrophoneAccess::not_required, "Linux asked for macOS permission");
	check(microphone.Start([&](double) { ++levels; }), "SDL dummy capture did not start");
	co_await pause(); co_await pause(); check(levels > 0, "SDL meter never dispatched on Qt"); microphone.Stop();
	int stopped = levels; co_await pause(); check(levels == stopped, "SDL callback survived Stop");
	SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "nonexistent-test-driver", SDL_HINT_OVERRIDE); check(!microphone.Start([](double) {}), "bad SDL driver reported ready");
	SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE); check(microphone.Start([](double) {}), "SDL retry did not recover"); microphone.Stop();

	auto home = root / "home with spaces"; auto data = home / ".local/share"; auto support = data / "OpenBF2";
	auto game = support / "GameData/1.50-1"; std::filesystem::create_directories(game); std::filesystem::create_directories(support / "Setup");
	qputenv("XDG_DATA_HOME", QByteArray::fromStdString(data.string())); qputenv("PYTHONHOME", "bad"); qputenv("PYTHONPATH", "bad"); qputenv("OPENBF2_TEST_KEEP", "desktop-value");
	write(layout.executable, "#!/bin/sh\n[ -z \"$PYTHONHOME$PYTHONPATH\" ] || exit 20\n[ \"$OPENBF2_TEST_KEEP\" = desktop-value ] || exit 21\n[ \"$DXVK_WSI_DRIVER\" = SDL3 ] || exit 22\n[ \"$1\" = 'one argument with spaces' ] || exit 23\n[ \"$2\" = '$(touch SHOULD_NOT_EXIST)' ] || exit 24\nprintf 'process fixture\\n'\npwd > actual-cwd\nsleep 0.1\nexit 7\n");
	assistant::Launch launch{layout.executable, game, support, {"one argument with spaces", "$(touch SHOULD_NOT_EXIST)"}};
	auto platform = nativePlatform();
	{
		auto lock = platform->lock(support / "Setup/Install.lock"); bool locked = false;
		try { auto second = platform->lock(support / "Setup/Install.lock"); } catch (...) { locked = true; }
		check(locked, "installation lock allowed duplicate launch");
		QTimer probe; int probes = 0; QObject::connect(&probe, &QTimer::timeout, [&] { try { auto second = platform->lock(support / "Setup/Install.lock"); } catch (...) { ++probes; } }); probe.start(10);
		check((co_await runClientProcess(launch, home)) == 7, "nonzero child exit lost"); check(probes > 0, "process blocked Qt or lost lock");
	}
	{ auto released = platform->lock(support / "Setup/Install.lock"); }
	check(std::filesystem::is_regular_file(game / "actual-cwd") && !std::filesystem::exists(game / "SHOULD_NOT_EXIST"), "working directory or argument vector failed");
	check(qEnvironmentVariable("PYTHONHOME") == "bad", "parent environment modified");
	for (const auto& log : std::filesystem::directory_iterator(support / "Setup/Logs")) {
		struct stat mode{}; check(::stat(log.path().c_str(), &mode) == 0 && (mode.st_mode & 0777) == 0600, "diagnostics not private");
	}
	launch.executable = root / "missing client"; bool failed = false;
	try { co_await runClientProcess(launch, home); } catch (const std::exception&) { failed = true; } check(failed, "process start failure not reported");
	std::cout << checks << " Linux Qt, SDL and process checks passed\n"; co_return 0;
}
}

int main(int argc, char** argv) {
	QTemporaryDir temporary; if (!temporary.isValid()) return 1;
	qputenv("HOME", (temporary.path() + "/home").toUtf8());
	qputenv("XDG_CONFIG_HOME", (temporary.path() + "/config").toUtf8());
	qputenv("XDG_CACHE_HOME", (temporary.path() + "/cache").toUtf8());
	qputenv("XDG_DATA_HOME", (temporary.path() + "/data").toUtf8());
	std::filesystem::create_directories(qEnvironmentVariable("HOME").toStdString());
	if (argc > 2 && std::string_view(argv[2]) == "--portal-picker") qputenv("FLATPAK_ID", "org.openbf2.Setup");
	QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
	boost::asio::io_context context; Window window(context); window.StayHidden(); int result = 1;
	QTimer::singleShot(10000, [&] { std::cerr << "Qt test timed out after " << checks << " checks\n"; app.exit(1); });
	boost::asio::co_spawn(context, exercise(window, temporary.path().toStdString()), [&](std::exception_ptr error, int code) {
		result = code; if (error) { result = 1; try { std::rethrow_exception(error); } catch (const std::exception& e) { std::cerr << e.what() << '\n'; } } window.Close();
	});
	window.Run(); return result;
}
