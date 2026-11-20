// Real Qt controls and the real Setup coroutine, with media/process services from fake.cpp.
#include "fake.h"
#include "setup.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <boost/asio/co_spawn.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace setup;
namespace {
	template<typename T> T* find(const char* name) {
		for (auto top : QApplication::topLevelWidgets()) {
			if (top->objectName() == name) if (auto widget = qobject_cast<T*>(top)) return widget;
			if (auto widget = top->findChild<T*>(name)) return widget;
		}
		throw std::runtime_error(std::string("no widget ") + name);
	}
	template<typename T> T* named(QWidget* parent, const char* accessible) {
		for (auto widget : parent->findChildren<T*>()) if (widget->accessibleName() == accessible) return widget;
		throw std::runtime_error(std::string("no control ") + accessible);
	}
	// the Advanced Options dialog: Cancel puts back what was there, OK keeps the changes, Restore Defaults
	// leaves the launcher page alone
	void advancedDialog(Window& window) {
		auto button = find<QPushButton>("advancedOptions"); auto dialog = find<QDialog>("advancedOptionsDialog");
		auto buttons = dialog->findChild<QDialogButtonBox*>();
		auto noSound = dialog->findChildren<QCheckBox*>().first();
		auto before = window.Options();
		button->click();
		if (!dialog->isVisible()) throw std::runtime_error("the advanced options did not open");
		noSound->setChecked(!before.no_sound);
		if (window.Options().no_sound == before.no_sound) throw std::runtime_error("the dialog did not change the options at once");
		buttons->button(QDialogButtonBox::Cancel)->click();
		if (dialog->isVisible() || window.Options() != before) throw std::runtime_error("Cancel did not restore the options");
		button->click(); noSound->setChecked(!before.no_sound); named<QComboBox>(dialog, "Demo file")->setEditText("demos/one.con");
		buttons->button(QDialogButtonBox::Ok)->click();
		auto changed = window.Options();
		if (changed.no_sound == before.no_sound || changed.demo != "demos/one.con") throw std::runtime_error("OK did not keep the changes");
		// joining a server is in the dialog; a demo replaces it
		button->click();
		auto address = named<QLineEdit>(dialog, "Server address"); auto demo = named<QComboBox>(dialog, "Demo file");
		if (address->isEnabled()) throw std::runtime_error("a demo did not turn off joining a server");
		demo->setEditText(QString());
		if (!address->isEnabled()) throw std::runtime_error("joining a server stayed off without a demo");
		address->setText("10.0.0.9"); emit address->textEdited(address->text());
		if (window.Options().server != "10.0.0.9") throw std::runtime_error("the server address in the dialog was not taken");
		buttons->button(QDialogButtonBox::Cancel)->click();
		if (!window.Options().server.empty() || window.Options().demo != "demos/one.con") throw std::runtime_error("Cancel kept the server, or lost the demo");
		auto tutorial = find<QCheckBox>("tutorialMessages");
		if (!tutorial->isChecked()) throw std::runtime_error("the tutorial messages are not on by default");
		tutorial->setChecked(false);
		if (window.Options().tutorial_messages) throw std::runtime_error("the tutorial choice was not taken");
		changed = window.Options();
		button->click(); buttons->button(QDialogButtonBox::RestoreDefaults)->click(); buttons->button(QDialogButtonBox::Ok)->click();
		auto reset = window.Options();
		if (reset.no_sound || !reset.demo.empty() || reset.mod != changed.mod || reset.tutorial_messages)
			throw std::runtime_error("Restore Defaults did not reset the advanced options alone");
	}
}
int main(int argc, char** argv) {
	QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
	const bool complete = app.arguments().contains("--complete");
	g_Machine = FakeMachine{}; g_Machine.microphone_allowed = false;
	g_Machine.provider_files["BF2Hub.json"] = R"({"schema": 1, "provider": "BF2Hub", "generator": "openbf2-hubprobe 0.1",
		"services": {"available": "5.252.33.100", "ms14": "5.252.33.100", "gpcm": "5.252.33.100", "gpsp": "5.252.33.100", "BF2Web": "116.203.14.166"}})";
	g_Machine.provider_files["wrong.json"] = R"({"schema": 2})";
	boost::asio::io_context context;
	Worker worker; Window window(context); window.StayHidden();
	Installer installer(worker, {}, {}); Store store({}); Client client({}, {}, {}); Microphone microphone(false);
	Setup application(window, installer, store, client, microphone);
	int result = 1, stage = 0;
	bool cancelled = false, advancedFailed = false;
	long verifiedBefore = 0;
	QTimer driver;
	// a disabled button ignores a click: under load the flow may not have enabled it yet, so a stage which
	// presses a button advances only once the press happened
	auto pressed = [&](const char* name, Event::Kind kind) {
		if (!find<QPushButton>(name)->isEnabled()) return false;
		window.Click(kind); return true;
	};
	QObject::connect(&driver, &QTimer::timeout, [&] {
		using Kind = Event::Kind;
		auto page = window.Shown();
		if (stage == 0 && page == Page::welcome) { window.Click(Kind::next); ++stage; }
		else if (stage == 1 && page == Page::disc) { window.Choose(Kind::disc_chosen, complete ? "complete.iso" : "deluxe.iso"); window.Click(Kind::next); ++stage; }
		else if (stage == 2 && page == Page::patches) {
			// These arrive before any hash completion. The old selection must lose its result.
			if (!complete) { window.Choose(Kind::patch141_chosen, "wrong.exe"); window.Choose(Kind::patch141_chosen, "patch141.exe"); }
			window.Choose(Kind::patch150_chosen, "patch150.exe"); ++stage;
		}
		else if (stage == 3 && page == Page::patches) window.Click(Kind::next);
		else if (stage == 3 && page == Page::keys) {
			window.PasteKey(KeyEdition::BF2, "AAAA-AAAA-AAAA-AAAA-AAAA");
			window.PasteKey(KeyEdition::SpecialForces, "SSSS-SSSS-SSSS-SSSS-SSSS"); ++stage;
		}
		else if (stage == 4 && page == Page::keys) { if (pressed("nextButton", Kind::next)) ++stage; }
		else if (stage == 5 && page == Page::keys && cancelled) { if (pressed("nextButton", Kind::next)) ++stage; }
		else if (stage >= 5 && page == Page::voice) { window.Click(Kind::voice_skip); stage = 7; }
		else if (stage == 7 && page == Page::launcher) {
			try { advancedDialog(window); } catch (const std::exception& e) { std::cerr << "advanced options: " << e.what() << '\n'; advancedFailed = true; }
			// BF2Hub without a kept file asks for one: the dialog opens and the choice stays until a file is accepted
			try {
				auto choice = find<QComboBox>("onlineServicesChoice");
				choice->setCurrentIndex(choice->findData(QString("bf2hub"))); emit choice->activated(choice->currentIndex());
				// the test window stays hidden and opens no dialog: the dialog was set up for BF2Hub
				if (choice->currentData().toString() != "" || find<QLabel>("providerTitle")->text() != "BF2Hub") throw std::runtime_error("BF2Hub without a file did not ask for one");
				find<QLabel>("providerTitle")->setText(QString());
			} catch (const std::exception& e) { std::cerr << "provider file: " << e.what() << '\n'; advancedFailed = true; }
			window.Choose(Kind::bf2hub_file, "BF2Hub.json"); stage = 71;
		}
		// the online services: BF2Hub's file is in use, its entries are on the command line; a wrong file keeps it;
		// Retail goes back to the game's own
		else if (stage == 71 && page == Page::launcher && window.Options().online == "bf2hub" && !window.Options().provider.services.empty()) {
			try {
				auto line = find<QLineEdit>("commandLine")->text().toStdString();
				if (line.find("+gamespyRedirectavailable 5.252.33.100 +gamespyRedirectms14 5.252.33.100 +gamespyRedirectgpcm 5.252.33.100 +gamespyRedirectgpsp 5.252.33.100 +gamespyRedirectBF2Web 116.203.14.166") == std::string::npos)
					throw std::runtime_error("the command line lacks the provider switches: " + line);
				if (find<QLabel>("onlineServicesStatus")->text() != "From a file") throw std::runtime_error("the status says " + find<QLabel>("onlineServicesStatus")->text().toStdString());
				if (find<QPushButton>("onlineServicesConfigure")->isHidden()) throw std::runtime_error("BF2Hub offers no Configure…");
			} catch (const std::exception& e) { std::cerr << "provider file: " << e.what() << '\n'; advancedFailed = true; }
			window.Choose(Kind::bf2hub_file, "wrong.json"); ++stage;
		}
		else if (stage == 72 && page == Page::launcher && std::ranges::count(g_Machine.calls, std::string("import-provider")) == 2) {
			if (window.Options().online != "bf2hub" || window.Options().provider.services.size() != 5) { std::cerr << "provider file: a refused file dropped the one in use\n"; advancedFailed = true; }
			auto choice = find<QComboBox>("onlineServicesChoice");
			choice->setCurrentIndex(0); emit choice->activated(0);
			if (!window.Options().online.empty() || !find<QPushButton>("onlineServicesConfigure")->isHidden() || !find<QLabel>("onlineServicesStatus")->isHidden())
				{ std::cerr << "provider file: Retail did not go back to the game's own services\n"; advancedFailed = true; }
			// BF2Hub again: its kept file, no dialog
			choice->setCurrentIndex(choice->findData(QString("bf2hub"))); emit choice->activated(choice->currentIndex());
			if (window.Options().online != "bf2hub" || !find<QLabel>("providerTitle")->text().isEmpty()) { std::cerr << "provider file: BF2Hub did not take its kept file\n"; advancedFailed = true; }
			choice->setCurrentIndex(0); emit choice->activated(0);
			++stage;
		}
		// the setup has answered the edit: the command line has no provider switch any more
		else if (stage == 73 && page == Page::launcher && !find<QLineEdit>("commandLine")->text().contains("gamespyRedirect")) stage = 8;
		else if (stage == 8 && page == Page::launcher) {
			verifiedBefore = std::ranges::count(g_Machine.calls, std::string("verify"));
			if (pressed("verifyGameDataButton", Kind::verify)) ++stage;
		}
		// Play only after the verification ran: while it runs, the same button cancels it
		else if (stage == 9 && page == Page::launcher && std::ranges::count(g_Machine.calls, std::string("verify")) > verifiedBefore) {
			if (pressed("nextButton", Kind::next)) ++stage;
		}
		else if (stage == 10 && page == Page::launcher && g_Machine.client_runs) window.Click(Kind::close);
	});
	// Asio jobs can finish inside one bridge tick. Post the cancellation in the same
	// executor as the install's first yield so it exercises guarded cancellation.
	auto cancel = [&]() -> task<void> {
		for (;;) {
			co_await boost::asio::post(boost::asio::use_awaitable);
			if (window.Shown() == Page::install) { window.Click(Event::Kind::next); cancelled = true; co_return; }
		}
	};
	boost::asio::co_spawn(context, cancel(), [](std::exception_ptr) {});
	boost::asio::co_spawn(context, application.Launch(), [&](std::exception_ptr error, int code) {
		result = code;
		if (error) { result = 1; try { std::rethrow_exception(error); } catch (const std::exception& e) { std::cerr << e.what() << '\n'; } }
		if (!cancelled || !g_Machine.installed || !g_Machine.saved_keys || !g_Machine.installed_special_forces.value_or(false) || g_Machine.client_runs != 1) {
			std::cerr << "Qt workflow did not complete cancel/resume, expansion install and Play\n"; result = 1;
		}
		if (complete && (!g_Machine.installed_media || !g_Machine.installed_media->patch141.empty() || !g_Machine.installed || g_Machine.installed->edition != DiscEdition::Complete)) {
			std::cerr << "Complete Edition did not use its included patch\n"; result = 1;
		}
		if (advancedFailed) result = 1;
		driver.stop(); window.Close();
	});
	// a stuck flow fails with its stage and page instead of running into the test timeout
	QTimer watchdog; watchdog.setSingleShot(true);
	QObject::connect(&watchdog, &QTimer::timeout, [&] {
		std::cerr << "Qt workflow stuck at stage " << stage << " on page " << int(window.Shown()) << (cancelled ? ", after cancelling" : "") << '\n';
		std::exit(1);
	});
	watchdog.start(20000);
	driver.start(10); window.Run();
	if (!result) std::cout << "Qt workflow: replacement, keys, cancel/resume, voice skip, advanced options, provider file, verification and Play passed\n";
	return result;
}
