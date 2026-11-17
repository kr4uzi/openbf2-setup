// Real Qt controls and the real Setup coroutine, with media/process services from fake.cpp.
#include "fake.h"
#include "setup.h"
#include <QApplication>
#include <QTimer>
#include <boost/asio/co_spawn.hpp>
#include <iostream>

using namespace setup;
int main(int argc, char** argv) {
	QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
	const bool complete = app.arguments().contains("--complete");
	g_Machine = FakeMachine{}; g_Machine.microphone_allowed = false;
	boost::asio::io_context context;
	Worker worker; Window window(context); window.StayHidden();
	Installer installer(worker, {}, {}); Store store({}); Client client({}, {}, {}); Microphone microphone(false);
	Setup application(window, installer, store, client, microphone);
	int result = 1, stage = 0;
	bool cancelled = false;
	QTimer driver;
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
		else if (stage == 4 && page == Page::keys) { window.Click(Kind::next); ++stage; }
		else if (stage == 5 && page == Page::keys && cancelled) { window.Click(Kind::next); ++stage; }
		else if (stage >= 5 && page == Page::voice) { window.Click(Kind::voice_skip); stage = 7; }
		else if (stage == 7 && page == Page::launcher) { window.Click(Kind::verify); ++stage; }
		else if (stage == 8 && page == Page::launcher) { window.Click(Kind::next); ++stage; }
		else if (stage == 9 && page == Page::launcher && g_Machine.client_runs) window.Click(Kind::close);
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
		driver.stop(); window.Close();
	});
	driver.start(10); window.Run();
	if (!result) std::cout << "Qt workflow: replacement, keys, cancel/resume, voice skip, verification and Play passed\n";
	return result;
}
