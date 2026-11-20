// A stand-in for openbf2-hubtool in the Windows checks: like the tool, it takes exactly one argument and needs
// BF2.exe in its working folder. The name of the argument says what it does: "...ok.exe" prints a synthetic
// provider file, "...slow.exe" waits far longer than the checks allow, "...fail.exe" fails with exit 5 and 30 lines
// of errors. It writes the argument it received to stderr, so the checks see how the command line arrived.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;

	std::string argument = argv[1];
	std::fprintf(stderr, "argument: %s\n", argument.c_str());
	if (!std::filesystem::is_regular_file("BF2.exe"))
		return 2;

	auto endsWith = [&](const char* tail) { std::string t = tail; return argument.size() >= t.size() && argument.compare(argument.size() - t.size(), t.size(), t) == 0; };
	if (endsWith("slow.exe")) {
		std::this_thread::sleep_for(std::chrono::seconds{ 30 });
		return 0;
	}
	if (endsWith("fail.exe")) {
		for (int i = 1; i <= 30; ++i)
			std::fprintf(stderr, "probe line %d\n", i);
		return 5;
	}

	std::printf("{\"schema\": 1, \"provider\": \"BF2Hub\", \"services\": {\"available\": \"10.0.0.1\", \"gpcm\": \"10.0.0.2\"}}\n");
	return 0;
}
