#include "microphone.h"

#include <SDL3/SDL.h>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include <QDesktopServices>
#include <QUrl>
#endif
#if defined(Q_OS_MACOS)
#include <QCoreApplication>
#include <QPermissions>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#endif

using namespace setup;
struct Microphone::Impl {
	bool allowed, initialized = false;
	SDL_AudioStream* stream = nullptr;
	QTimer timer;
	std::function<void(double)> level;
	std::function<void()> error;
	explicit Impl(bool allowed) : allowed(allowed) {}
};
Microphone::Microphone(bool allowed) : m_Impl(std::make_unique<Impl>(allowed)) {
	QObject::connect(&m_Impl->timer, &QTimer::timeout, &m_Impl->timer, [this] {
		auto& i = *m_Impl;
		int available = SDL_GetAudioStreamAvailable(i.stream);
		SDL_AudioSpec spec;
		if (available < 0 || !SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(i.stream), &spec, nullptr)) {
			Stop(); if (i.error) i.error(); return;
		}
		std::array<float, 4096> buffer;
		double peak = 0;
		// SDL converts to mono floating-point. Polling keeps all UI callbacks on the Qt thread.
		for (int count = 0; count < 16 && available > 0; ++count) {
			int received = SDL_GetAudioStreamData(i.stream, buffer.data(), int(sizeof(buffer)));
			if (received < 0) { Stop(); if (i.error) i.error(); return; }
			if (!received) break;
			for (int sample = 0; sample < received / int(sizeof(float)); ++sample)
				if (std::isfinite(buffer[sample])) peak = std::max(peak, double(std::abs(buffer[sample])));
			available -= received;
		}
		if (i.level) i.level(std::clamp(peak, 0.0, 1.0));
	});
}
Microphone::~Microphone() { Stop(); }
bool Microphone::Allowed() const { return m_Impl->allowed; }
#if defined(Q_OS_MACOS)
// macOS asks the user once (the bundle's NSMicrophoneUsageDescription is the question); afterwards only
// System Settings can change the answer. The client inherits the grant as a child process.
MicrophoneAccess Microphone::Access() const
{
	switch (qApp->checkPermission(QMicrophonePermission{})) {
	case Qt::PermissionStatus::Granted: return MicrophoneAccess::granted;
	case Qt::PermissionStatus::Denied: return MicrophoneAccess::denied;
	case Qt::PermissionStatus::Undetermined: break;
	}
	return MicrophoneAccess::undetermined;
}

task<bool> Microphone::Request()
{
	if (!m_Impl->allowed)
		co_return false;

	using Answer = boost::asio::experimental::channel<void(boost::system::error_code, bool)>;
	auto answer = std::make_shared<Answer>(co_await boost::asio::this_coro::executor, 1);
	qApp->requestPermission(QMicrophonePermission{}, &m_Impl->timer, [answer](const QPermission& permission) {
		answer->try_send(boost::system::error_code{}, permission.status() == Qt::PermissionStatus::Granted);
	});
	co_return co_await answer->async_receive(boost::asio::use_awaitable);
}

void Microphone::OpenSystemSettings()
{
	QDesktopServices::openUrl(QUrl{"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"});
}
#else
// Linux and Windows ask no per-application question; Windows has one switch for all desktop apps
MicrophoneAccess Microphone::Access() const { return MicrophoneAccess::not_required; }
task<bool> Microphone::Request() { co_return m_Impl->allowed; }
#if defined(Q_OS_WIN)
void Microphone::OpenSystemSettings() { QDesktopServices::openUrl(QUrl{"ms-settings:privacy-microphone"}); }
#else
void Microphone::OpenSystemSettings() {}
#endif
#endif
void Microphone::OnError(std::function<void()> error) { m_Impl->error = std::move(error); }
bool Microphone::Start(std::function<void(double)> level) {
	auto& i = *m_Impl;
	if (!i.allowed) return false;
	if (i.stream) { i.level = std::move(level); return true; }
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
	i.initialized = true;
	SDL_AudioSpec spec{SDL_AUDIO_F32, 1, 48000};
	i.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, nullptr, nullptr);
	if (!i.stream || !SDL_ResumeAudioStreamDevice(i.stream)) { Stop(); return false; }
	i.level = std::move(level); i.timer.start(40); return true;
}
void Microphone::Stop() {
	auto& i = *m_Impl; i.timer.stop();
	if (i.stream) SDL_DestroyAudioStream(i.stream);
	i.stream = nullptr; i.level = {};
	if (i.initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
	i.initialized = false;
}
