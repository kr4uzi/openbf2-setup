#pragma once

#include <boost/asio/awaitable.hpp>

namespace setup {
	template<typename T>
	using task = boost::asio::awaitable<T>;
}
