#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>

#include "microphone.h"

#include <cmath>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
using namespace setup;

struct Microphone::Impl
{
	bool allowed;
	AVAudioEngine* engine = nil;
	// what the running meter reports to. Stop clears it, so a level which is still on its way is dropped
	std::shared_ptr<std::function<void(double)>> level;
};

Microphone::Microphone(bool allowed)
	: m_Impl{ std::make_unique<Impl>(allowed) }
{
}

Microphone::~Microphone()
{
	Stop();
}

bool Microphone::Allowed() const
{
	return m_Impl->allowed;
}

MicrophoneAccess Microphone::Access() const
{
	switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
	case AVAuthorizationStatusAuthorized:
		return MicrophoneAccess::granted;
	case AVAuthorizationStatusNotDetermined:
		return MicrophoneAccess::undetermined;
	default:
		return MicrophoneAccess::denied;
	}
}

task<bool> Microphone::Request()
{
	// the answer arrives on a thread of the system, the channel brings it back to this one
	using Answer = boost::asio::experimental::concurrent_channel<void(boost::system::error_code, bool)>;
	auto answer = std::make_shared<Answer>(co_await boost::asio::this_coro::executor, 1);
	[AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
		answer->try_send(boost::system::error_code{}, granted == YES);
	}];

	co_return co_await answer->async_receive(boost::asio::use_awaitable);
}

void Microphone::OpenSystemSettings()
{
	[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"]];
}

bool Microphone::Start(std::function<void(double)> level)
{
	if (m_Impl->engine)
		return true;

	@try {
		AVAudioEngine* engine = [AVAudioEngine new];
		AVAudioInputNode* input = engine.inputNode;
		AVAudioFormat* format = [input outputFormatForBus:0];
		if (format.sampleRate <= 0 || format.channelCount == 0)
			return false;

		auto report = std::make_shared<std::function<void(double)>>(std::move(level));
		auto pending = std::weak_ptr{ report };
		[input installTapOnBus:0 bufferSize:2048 format:format block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
			float peak = 0;
			if (buffer.floatChannelData) {
				for (AVAudioChannelCount channel = 0; channel < buffer.format.channelCount; ++channel) {
					const float* samples = buffer.floatChannelData[channel];
					for (AVAudioFrameCount i = 0; i < buffer.frameLength; ++i)
						peak = fmaxf(peak, fabsf(samples[i]));
				}
			}

			// the tap runs on an audio thread, the window is touched on its own thread only
			dispatch_async(dispatch_get_main_queue(), ^{
				if (auto running = pending.lock())
					(*running)(peak);
			});
		}];

		NSError* error = nil;
		if (![engine startAndReturnError:&error]) {
			[input removeTapOnBus:0];
			return false;
		}

		m_Impl->engine = engine;
		m_Impl->level = report;
		return true;
	}
	@catch (NSException*) {
		return false;
	}
}

void Microphone::Stop()
{
	if (!m_Impl->engine)
		return;

	[m_Impl->engine.inputNode removeTapOnBus:0];
	[m_Impl->engine stop];
	m_Impl->engine = nil;
	m_Impl->level.reset();
}
