#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The online services of the game: where the GameSpy SDK's names resolve. The game reads them from a provider file,
// "+onlineProviderConfig <file>"; a file it refuses ends its start-up, so the launcher checks every file as the game
// does before it is used.
namespace setup {
	// what the game accepts as a service's value: a dotted IPv4 address, or a host name (labels of
	// letters, digits and '-', not only digits), at most 253 characters. as the game's own check
	// (OnlineProvider.cpp, checkValue)
	bool valid_target(const std::string& target);
	bool valid_ipv4(const std::string& text);
	// the services the game knows, by their retail host-name label, case-sensitive ("BF2Web", not "bf2web")
	const std::vector<std::string>& service_labels();
	// what the game accepts for `key`: "*", the domain which replaces the retail one in every name, must leave room
	// for the longest name formed with it (an IPv4 address, or a domain of at most 230 characters); a label of
	// service_labels takes valid_target; any other key is refused
	bool valid_service(const std::string& key, const std::string& value);

	// a provider file: the provider's name, shown, and its services in the file's order. a key is a label of
	// service_labels, whose name goes to its value; "*" is the fallback for every name without an entry of its own
	struct ProviderServices
	{
		std::string name;
		std::vector<std::pair<std::string, std::string>> services;
		// until when it holds (UTC, "valid_to"); without one it holds for ever
		std::optional<std::chrono::sys_seconds> valid_to;

		bool operator==(const ProviderServices&) const = default;
	};
	// when a provider file stops holding; nothing for one which holds for ever
	std::optional<std::chrono::sys_seconds> provider_expiry(const ProviderServices& provider);
	bool provider_expired(const ProviderServices& provider, std::chrono::sys_seconds now);
	// "2026-11-20T18:00:00Z", the only form of "valid_to"
	std::optional<std::chrono::sys_seconds> parse_utc(const std::string& text);

	// the largest provider file read
	inline constexpr std::size_t provider_file_limit = 64 * 1024;

	// reads and checks a provider file: "schema" 1, "provider" a name (letters, digits, space, '-' and '.', at
	// most 64), a "services" object of keys ("*" or a label of service_labels) and values the game accepts
	// (valid_service), and optionally "valid_to" (a UTC time). other entries are not used, by the game neither.
	// anything else throws, with the reason as its message
	ProviderServices parse_provider(const std::string& json);

	// an address a provider file may be downloaded from: https, a host name, no user or password
	bool valid_provider_url(const std::string& url);

	// the provider files the application carries: the online choice they belong to, and the file's text
	struct ProviderPreset
	{
		const char* online;
		const char* json;
	};
	const std::vector<ProviderPreset>& provider_presets();
	// the preset of an online choice ("openspy", "bf2hub"); none for any other
	const ProviderPreset* provider_preset(const std::string& online);
}
