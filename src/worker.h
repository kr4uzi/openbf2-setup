#pragma once

#include "task.h"

#include <concepts>
#include <type_traits>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace setup {
	// hashing, unpacking and patching block for minutes. they run on these threads, so the window keeps painting,
	// and the coroutine awaiting them continues on its own thread (the one running the window).
	// the disc and both patches are checked at the same time, hence more than one thread
	class Worker
	{
		boost::asio::thread_pool m_Threads{ 3 };

	public:
		template<std::invocable Work>
		task<std::invoke_result_t<Work&>> Run(Work work)
		{
			co_return co_await boost::asio::co_spawn(m_Threads, [&]() -> task<std::invoke_result_t<Work&>> {
				co_return work();
			}, boost::asio::use_awaitable);
		}
	};
}
