#include "microphone.h"

#include <SDL3/SDL.h>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>

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
MicrophoneAccess Microphone::Access() const { return MicrophoneAccess::not_required; }
task<bool> Microphone::Request() { co_return m_Impl->allowed; }
void Microphone::OpenSystemSettings() {}
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
