#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The online services of the game: where the GameSpy SDK's names resolve. The game takes +gamespyRedirect for all
// of them and +gamespyRedirect<label> per retail host-name label (+gamespyRedirectgpcm, ...); an unusable value ends
// its start-up, so nothing is passed which the game would refuse. The launcher takes them from a provider file.
namespace setup {
	// what the game accepts as a value of +gamespyRedirect<label>: a dotted IPv4 address, or a host name (labels of
	// letters, digits and '-', not only digits), at most 253 characters. as the game's own check
	// (OnlineProvider.cpp, checkValue)
	bool valid_target(const std::string& target);
	bool valid_ipv4(const std::string& text);
	// what the game accepts for the switch of `key`: "*" is +gamespyRedirect itself, whose domain must leave room for
	// the longest name formed with it (at most 230 characters); other keys take valid_target
	bool valid_service(const std::string& key, const std::string& value);
	// the switch of a provider file's key: "*" gives +gamespyRedirect, a label +gamespyRedirect<label>
	std::string service_switch(const std::string& key);

	// a provider file: the provider's name, shown, and its services in the file's order. each entry is passed as
	// it is written, "+gamespyRedirect" followed by its key and then its value. the key is a retail host-name
	// label, case-sensitive like the game's switch names ("BF2Web" gives +gamespyRedirectBF2Web); "*" gives
	// +gamespyRedirect, the fallback for every name
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
	// most 64), a "services" object of keys ("*", or letters, digits and '-', at most 32) and values the game
	// accepts (valid_service), and optionally "valid_to" (a UTC time). other entries are not used.
	// anything else throws, with the reason as its message
	ProviderServices parse_provider(const std::string& json);

	// BF2Hub's provider file as OpenBF2 hosts it
	inline constexpr const char* bf2hub_hosted_url = "https://bf2hub.krauzi.de/servers.json";
	// an address a provider file may be downloaded from: https, a host name, no user or password
	bool valid_provider_url(const std::string& url);

	// the provider files the application carries, by provider name
	struct ProviderPreset
	{
		const char* name;
		const char* json;
	};
	const std::vector<ProviderPreset>& provider_presets();
}
