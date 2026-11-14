#import <AppKit/AppKit.h>

#include "client.h"
#include "installer.h"
#include "platform.h"
#include "session.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
using namespace setup;

namespace {
	NSString* text(const std::string& s)
	{
		return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding] ?: @"";
	}
}

struct Client::Impl
{
	std::filesystem::path contents;
	std::filesystem::path support;
	std::filesystem::path home;
};

Client::Client(std::filesystem::path contents, std::filesystem::path support, std::filesystem::path home)
	: m_Impl{ std::make_unique<Impl>(std::move(contents), std::move(support), std::move(home)) }
{
}

Client::~Client() = default;

bool Client::Present() const
{
	return assistant::clientPresent(m_Impl->contents);
}

task<int> Client::Run(const Installation& installation, LaunchOptions options)
{
	auto platform = nativePlatform();
	auto lock = platform->lock(m_Impl->support / "Setup/Install.lock");
	auto launch = assistant::clientLaunch(m_Impl->contents, m_Impl->support, options);
	if (launch.directory != installation.game)
		throw std::runtime_error{ "The active installation changed. Reopen OpenBF2 to verify it." };

	NSTask* task = [NSTask new];
	task.executableURL = [NSURL fileURLWithPath:text(launch.executable.string())];
	task.currentDirectoryURL = [NSURL fileURLWithPath:text(launch.directory.string())];
	NSMutableArray<NSString*>* arguments = [NSMutableArray array];
	for (const auto& argument : launch.arguments)
		[arguments addObject:text(argument)];
	task.arguments = arguments;

	NSMutableDictionary<NSString*, NSString*>* environment = [NSProcessInfo.processInfo.environment mutableCopy];
	// the client takes everything from its working directory and from HOME, nothing else is handed to it.
	// the embedded Python 2.3 would read these two, and a foreign PYTHONPATH can break the game scripts
	environment[@"HOME"] = ::text(m_Impl->home.string());
	[environment removeObjectsForKeys:@[ @"PYTHONPATH", @"PYTHONCASEOK" ]];
	task.environment = environment;

	// the client's own log is what it prints: it goes into the game folder, next to the renderer's log, and
	// starts afresh with every run. a folder which refuses the file costs the log, not the game
	NSString* logPath = ::text((launch.directory / "OpenBF2Client.log").string());
	NSFileHandle* log = nil;
	if ([NSFileManager.defaultManager createFileAtPath:logPath contents:nil attributes:@{ NSFilePosixPermissions: @0600 }]) {
		log = [NSFileHandle fileHandleForWritingAtPath:logPath];
		task.standardOutput = log;
		task.standardError = log;
	}

	// the handler runs on a thread of its own, the channel brings the exit status back to this one
	using Exit = boost::asio::experimental::concurrent_channel<void(boost::system::error_code, int)>;
	auto exit = std::make_shared<Exit>(co_await boost::asio::this_coro::executor, 1);
	task.terminationHandler = ^(NSTask* child) {
		exit->try_send(boost::system::error_code{}, child.terminationStatus);
	};

	NSError* error = nil;
	if (![task launchAndReturnError:&error])
		throw std::runtime_error{ error.localizedDescription.UTF8String ?: "the client could not be started" };

	// activation is cooperative since macOS 14: the game gets the front from the application which started it,
	// so its first clicks count. the child turns up as a running application a moment after the launch, and
	// its window a while later; the hand-off is repeated until the game is active
	auto timer = boost::asio::steady_timer{ co_await boost::asio::this_coro::executor };
	auto started = std::chrono::steady_clock::now();
	auto elapsed = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(); };
	auto seen = -1LL;
	auto outcome = std::string{ "the game never turned up as an application" };
	for (auto attempt = 0; attempt < 50 && task.running; ++attempt) {
		NSRunningApplication* game = [NSRunningApplication runningApplicationWithProcessIdentifier:task.processIdentifier];
		if (game && seen < 0)
			seen = elapsed();

		if (game && game.active) {
			outcome = "active after " + std::to_string(elapsed()) + " ms";
			break;
		}

		// the user went to another application meanwhile: their choice
		NSRunningApplication* front = NSWorkspace.sharedWorkspace.frontmostApplication;
		if (front && front.processIdentifier != NSProcessInfo.processInfo.processIdentifier && front.processIdentifier != task.processIdentifier) {
			outcome = "another application came to the front after " + std::to_string(elapsed()) + " ms";
			break;
		}

		if (game) {
			if (@available(macOS 14.0, *))
				[NSApp yieldActivationToApplication:game];

			[game activateWithOptions:0];
			outcome = "not active after " + std::to_string(elapsed()) + " ms (frontmost pid " + std::to_string(front ? front.processIdentifier : 0) + ", launcher active " + (NSApp.active ? "yes" : "no") + ")";
		}

		timer.expires_after(std::chrono::milliseconds{ 100 });
		co_await timer.async_wait(boost::asio::use_awaitable);
	}

	// what the launcher saw, next to what the client says, for reports
	if (log) {
		auto line = "[launcher] child seen after " + std::to_string(seen) + " ms; " + outcome + "\n";
		@try {
			[log writeData:[NSData dataWithBytes:line.data() length:line.size()]];
		}
		@catch (NSException*) {
		}
	}

	co_return co_await exit->async_receive(boost::asio::use_awaitable);
}
