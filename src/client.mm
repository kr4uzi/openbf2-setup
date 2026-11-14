#import <Foundation/Foundation.h>

#include "client.h"
#include "installer.h"
#include "platform.h"
#include "session.h"

#include <memory>
#include <stdexcept>
#include <boost/asio/experimental/concurrent_channel.hpp>
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

task<int> Client::Run(const Installation& installation, std::string mod)
{
	auto platform = nativePlatform();
	auto lock = platform->lock(m_Impl->support / "Setup/Install.lock");
	auto launch = assistant::clientLaunch(m_Impl->contents, m_Impl->support, mod);
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

	// the handler runs on a thread of its own, the channel brings the exit status back to this one
	using Exit = boost::asio::experimental::concurrent_channel<void(boost::system::error_code, int)>;
	auto exit = std::make_shared<Exit>(co_await boost::asio::this_coro::executor, 1);
	task.terminationHandler = ^(NSTask* child) {
		exit->try_send(boost::system::error_code{}, child.terminationStatus);
	};

	NSError* error = nil;
	if (![task launchAndReturnError:&error])
		throw std::runtime_error{ error.localizedDescription.UTF8String ?: "the client could not be started" };

	co_return co_await exit->async_receive(boost::asio::use_awaitable);
}
