#include "online.h"

#include <cctype>
#include <memory>
#include <stdexcept>

bool setup::valid_ipv4(const std::string& text)
{
	auto parts = 0;
	auto i = size_t{ 0 };
	while (i < text.size()) {
		auto value = 0;
		auto digits = size_t{ 0 };
		for (; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i, ++digits)
			value = value * 10 + (text[i] - '0');
		if (digits == 0 || digits > 3 || value > 255)
			return false;

		++parts;
		if (i == text.size())
			break;
		if (text[i] != '.' || ++i == text.size())
			return false;
	}

	return parts == 4;
}

bool setup::valid_target(const std::string& target)
{
	if (target.empty() || target.size() > 253)
		return false;
	if (valid_ipv4(target))
		return true;

	// labels of letters, digits and '-' separated by single dots, and not an address
	auto labelStart = true;
	auto onlyDigits = true;
	for (auto c : target) {
		auto u = static_cast<unsigned char>(c);
		if (c == '.') {
			if (labelStart)
				return false;
			labelStart = true;
		}
		else if (std::isalnum(u) || c == '-') {
			labelStart = false;
			if (!std::isdigit(u))
				onlyDigits = false;
		}
		else
			return false;
	}

	return !labelStart && !onlyDigits;
}

namespace {
	// a JSON value, only as far as the provider file needs one
	struct Value;
	using Object = std::vector<std::pair<std::string, std::unique_ptr<Value>>>;
	struct Value
	{
		enum class Kind { object, array, string, number, literal } kind;
		std::string text;  // a string's contents, a number's or a literal's spelling
		Object members;
	};

	class Reader
	{
		const std::string& m_Text;
		size_t m_At = 0;
		int m_Depth = 0;

		[[noreturn]] void fail(const std::string& what) const
		{
			throw std::runtime_error{ "The file is not valid JSON: " + what + " at character " + std::to_string(m_At + 1) + "." };
		}

		void space()
		{
			while (m_At < m_Text.size() && (m_Text[m_At] == ' ' || m_Text[m_At] == '\t' || m_Text[m_At] == '\n' || m_Text[m_At] == '\r'))
				++m_At;
		}

		bool next(char c)
		{
			space();
			if (m_At < m_Text.size() && m_Text[m_At] == c) {
				++m_At;
				return true;
			}

			return false;
		}

		void expect(char c)
		{
			if (!next(c))
				fail(std::string{ "'" } + c + "' expected");
		}

		static void utf8(std::string& out, unsigned code)
		{
			if (code < 0x80)
				out += char(code);
			else if (code < 0x800) {
				out += char(0xC0 | (code >> 6));
				out += char(0x80 | (code & 0x3F));
			}
			else if (code < 0x10000) {
				out += char(0xE0 | (code >> 12));
				out += char(0x80 | ((code >> 6) & 0x3F));
				out += char(0x80 | (code & 0x3F));
			}
			else {
				out += char(0xF0 | (code >> 18));
				out += char(0x80 | ((code >> 12) & 0x3F));
				out += char(0x80 | ((code >> 6) & 0x3F));
				out += char(0x80 | (code & 0x3F));
			}
		}

		unsigned hex4()
		{
			if (m_At + 4 > m_Text.size())
				fail("short \\u escape");

			auto code = 0u;
			for (int i = 0; i < 4; ++i) {
				auto c = m_Text[m_At++];
				code <<= 4;
				if (c >= '0' && c <= '9') code |= unsigned(c - '0');
				else if (c >= 'a' && c <= 'f') code |= unsigned(c - 'a' + 10);
				else if (c >= 'A' && c <= 'F') code |= unsigned(c - 'A' + 10);
				else fail("bad \\u escape");
			}

			return code;
		}

		std::string string()
		{
			space();
			if (m_At >= m_Text.size() || m_Text[m_At] != '"')
				fail("a string expected");

			++m_At;
			auto out = std::string{};
			while (true) {
				if (m_At >= m_Text.size())
					fail("unterminated string");

				auto c = static_cast<unsigned char>(m_Text[m_At++]);
				if (c == '"')
					return out;
				if (c < 0x20)
					fail("a control character in a string");
				if (c != '\\') {
					out += char(c);
					continue;
				}

				if (m_At >= m_Text.size())
					fail("unterminated escape");

				switch (m_Text[m_At++]) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					auto code = hex4();
					if (code >= 0xD800 && code < 0xDC00) {
						if (m_At + 2 > m_Text.size() || m_Text[m_At] != '\\' || m_Text[m_At + 1] != 'u')
							fail("a lone surrogate");
						m_At += 2;
						auto low = hex4();
						if (low < 0xDC00 || low >= 0xE000)
							fail("a lone surrogate");
						code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
					}
					else if (code >= 0xDC00 && code < 0xE000)
						fail("a lone surrogate");
					utf8(out, code);
					break;
				}
				default:
					fail("an unknown escape");
				}
			}
		}

	public:
		explicit Reader(const std::string& text) : m_Text{ text } {}

		std::unique_ptr<Value> value()
		{
			if (++m_Depth > 16)
				fail("nested too deeply");

			space();
			if (m_At >= m_Text.size())
				fail("a value expected");

			auto result = std::make_unique<Value>();
			auto c = m_Text[m_At];
			if (c == '{') {
				result->kind = Value::Kind::object;
				++m_At;
				if (!next('}')) {
					do {
						auto name = string();
						for (const auto& member : result->members)
							if (member.first == name)
								fail("the key \"" + name + "\" twice");
						expect(':');
						result->members.emplace_back(std::move(name), value());
					} while (next(','));
					expect('}');
				}
			}
			else if (c == '[') {
				result->kind = Value::Kind::array;
				++m_At;
				if (!next(']')) {
					do
						result->members.emplace_back(std::string{}, value());
					while (next(','));
					expect(']');
				}
			}
			else if (c == '"') {
				result->kind = Value::Kind::string;
				result->text = string();
			}
			else if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
				// -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
				result->kind = Value::Kind::number;
				auto start = m_At;
				auto digits = [this] {
					auto from = m_At;
					while (m_At < m_Text.size() && std::isdigit(static_cast<unsigned char>(m_Text[m_At])))
						++m_At;
					return m_At - from;
				};
				if (m_Text[m_At] == '-')
					++m_At;
				auto leadingZero = m_At < m_Text.size() && m_Text[m_At] == '0';
				auto whole = digits();
				if (whole == 0 || (leadingZero && whole > 1))
					fail("a malformed number");
				if (m_At < m_Text.size() && m_Text[m_At] == '.') {
					++m_At;
					if (digits() == 0)
						fail("a malformed number");
				}
				if (m_At < m_Text.size() && (m_Text[m_At] == 'e' || m_Text[m_At] == 'E')) {
					++m_At;
					if (m_At < m_Text.size() && (m_Text[m_At] == '+' || m_Text[m_At] == '-'))
						++m_At;
					if (digits() == 0)
						fail("a malformed number");
				}
				result->text = m_Text.substr(start, m_At - start);
			}
			else {
				result->kind = Value::Kind::literal;
				for (auto literal : { "true", "false", "null" })
					if (m_Text.compare(m_At, std::char_traits<char>::length(literal), literal) == 0) {
						result->text = literal;
						m_At += result->text.size();
					}
				if (result->text.empty())
					fail("an unknown value");
			}

			--m_Depth;
			return result;
		}

		void end()
		{
			space();
			if (m_At != m_Text.size())
				fail("text after the end");
		}
	};

	const Value* member(const Value& object, const std::string& name)
	{
		for (const auto& [key, value] : object.members)
			if (key == name)
				return value.get();

		return nullptr;
	}

}

bool setup::valid_service(const std::string& key, const std::string& value)
{
	// "battlefield2.available." is the longest name part the game puts before a domain
	if (key == "*")
		return valid_ipv4(value) || (valid_target(value) && value.size() <= 253 - 23);

	return valid_target(value);
}

std::string setup::service_switch(const std::string& key)
{
	return key == "*" ? std::string{ "+gamespyRedirect" } : "+gamespyRedirect" + key;
}

std::optional<std::chrono::sys_seconds> setup::parse_utc(const std::string& text)
{
	// YYYY-MM-DDTHH:MM:SSZ
	if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[19] != 'Z')
		return std::nullopt;
	for (auto i : { 0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18 })
		if (!std::isdigit(static_cast<unsigned char>(text[i])))
			return std::nullopt;

	auto number = [&](int at, int count) { return std::stoi(text.substr(at, count)); };
	auto date = std::chrono::year{ number(0, 4) } / std::chrono::month(unsigned(number(5, 2))) / std::chrono::day(unsigned(number(8, 2)));
	auto hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
	if (!date.ok() || hour > 23 || minute > 59 || second > 59)
		return std::nullopt;

	return std::chrono::sys_days{ date } + std::chrono::hours{ hour } + std::chrono::minutes{ minute } + std::chrono::seconds{ second };
}

std::optional<std::chrono::sys_seconds> setup::provider_expiry(const ProviderServices& provider)
{
	return provider.valid_to;
}

bool setup::provider_expired(const ProviderServices& provider, std::chrono::sys_seconds now)
{
	auto expiry = provider_expiry(provider);
	return expiry && now >= *expiry;
}

bool setup::valid_provider_url(const std::string& url)
{
	// https://host[:port]/path, the host a name; nothing which could carry a user, a password or a fragment
	if (url.size() > 2048 || url.rfind("https://", 0) != 0 || url.find_first_of(" \t\r\n\"\\@#") != std::string::npos)
		return false;

	auto rest = url.substr(8);
	auto host = rest.substr(0, rest.find_first_of("/:?"));
	return !host.empty() && valid_target(host);
}

const std::vector<setup::ProviderPreset>& setup::provider_presets()
{
	// OpenSpy serves the retail names under its own domain
	static const std::vector<ProviderPreset> presets{
		{ "OpenSpy", R"({"schema": 1, "provider": "OpenSpy", "services": {"*": "openspy.net"}})" },
	};
	return presets;
}

setup::ProviderServices setup::parse_provider(const std::string& json)
{
	if (json.size() > provider_file_limit)
		throw std::runtime_error{ "The file is larger than a provider file can be." };

	// one leading UTF-8 byte-order mark is allowed
	auto text = json.rfind("\xEF\xBB\xBF", 0) == 0 ? json.substr(3) : json;
	auto reader = Reader{ text };
	auto root = reader.value();
	reader.end();
	if (root->kind != Value::Kind::object)
		throw std::runtime_error{ "The file is not a provider file: it holds no object." };

	auto schema = member(*root, "schema");
	if (!schema || schema->kind != Value::Kind::number || schema->text != "1")
		throw std::runtime_error{ "The file is not a provider file of a known version: \"schema\" must be 1." };

	auto result = ProviderServices{};
	auto provider = member(*root, "provider");
	auto named = provider && provider->kind == Value::Kind::string && !provider->text.empty() && provider->text.size() <= 64;
	for (auto c : named ? provider->text : std::string{})
		named = named && (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '.');
	if (!named)
		throw std::runtime_error{ "The file names no provider: \"provider\" must be a name of letters, digits, spaces, '-' and '.'." };
	result.name = provider->text;

	auto services = member(*root, "services");
	if (!services || services->kind != Value::Kind::object || services->members.empty())
		throw std::runtime_error{ "The file names no services." };

	for (const auto& [key, value] : services->members) {
		// the key ends the switch's name: letters, digits and '-' (stage-net), so the command line stays one switch;
		// "*" is the switch without a label
		auto name = key == "*" || (!key.empty() && key.size() <= 32);
		for (auto c : key == "*" ? std::string{} : key)
			name = name && (std::isalnum(static_cast<unsigned char>(c)) || c == '-');
		if (!name)
			throw std::runtime_error{ "The service key \"" + key.substr(0, 40) + "\" is not \"*\" or a name of letters, digits and '-'." };
		if (value->kind != Value::Kind::string || !valid_service(key, value->text))
			throw std::runtime_error{ key == "*"
				? std::string{ "The service \"*\" has no usable value: it must be an IPv4 address or a domain name." }
				: "The service \"" + key + "\" has no usable value: it must be an IPv4 address or a host name." };

		result.services.emplace_back(key, value->text);
	}

	// until when the file holds; without "valid_to" for ever
	if (auto validTo = member(*root, "valid_to")) {
		if (validTo->kind == Value::Kind::string)
			result.valid_to = parse_utc(validTo->text);
		if (!result.valid_to)
			throw std::runtime_error{ "The file's \"valid_to\" is not a UTC time such as 2026-11-27T18:00:00Z." };
	}

	return result;
}
