// An existing installation used in place (src/existing.h), on real files: a small installation tree with
// its own manifest, linked, verified, changed and linked again. No game content.
#include "digest.h"
#include "existing.h"
#include "install.h"
#include "links.h"
#include "media.h"
#include "mods.h"
#include "session.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
unsigned checks = 0;
void check(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); ++checks; }
template<class F> std::string failure(F f) { try { f(); } catch (const std::exception& e) { return e.what(); } return {}; }
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << text; out.close();
    if (!out) throw std::runtime_error("fixture write failed: " + path.string());
}
std::string read(const fs::path& path) { auto bytes = setup::View(path).all(); return {bytes.begin(), bytes.end()}; }
std::string hash(const std::string& text) { setup::Digest digest; digest.update(reinterpret_cast<const uint8_t*>(text.data()), text.size()); return digest.finish(); }
// every file of a tree with its size and contents: what "nothing was written there" is compared with
std::map<std::string, std::string> snapshot(const fs::path& root) {
    std::map<std::string, std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file()) files[entry.path().lexically_relative(root).generic_string()] = read(entry.path());
    return files;
}
}

int main(int argc, char** argv) {
    namespace session = setup::assistant;
    using setup::LinkedRole;
    try {
        if (argc != 2) throw std::runtime_error("usage: bf2_setup_existing_checks SCRATCH");
        fs::create_directories(argv[1]);
        auto root = fs::canonical(argv[1]) / ("existing-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        auto folder = root / "Battlefield 2", support = root / "support", manifest = root / "content.tsv";
        fs::create_directories(support);

        check(setup::linkedRole("mods/bf2/objects_client.zip") == LinkedRole::asset, "assets are strict");
        check(setup::linkedRole("mods/xpack/levels/x/client.zip") == LinkedRole::asset, "Special Forces assets are strict");
        check(setup::linkedRole("mods/bf2/settings/maplist.con") == LinkedRole::settings, "settings only exist");
        check(setup::linkedRole("mods/bf2/localization/german/german.utxt") == LinkedRole::localization, "languages are optional");
        check(setup::linkedRole("mods/bf2/locale") == LinkedRole::localization, "the installer's locale file is optional");
        check(setup::linkedRole("mods/bf2/python/game/scoringcommon.py") == LinkedRole::script, "mod scripts are reported");
        check(setup::linkedRole("python/bf2/gamelogic.py") == LinkedRole::script && setup::linkedRole("admin/default.py") == LinkedRole::script
            && setup::linkedRole("pylib-2.3.4.zip") == LinkedRole::script, "the Python search path is scripts");
        for (auto unused : {"bf2.exe", "binkw32.dll", "pb/pbcl.dll", "adminutils/remoteconsole.py", "support/index.htm", "mods/readme.txt"})
            check(setup::linkedRole(unused) == LinkedRole::unused, std::string("never read: ") + unused);

        // an installation as Windows spells it, with a mod of the user and binaries BF2Hub patched
        std::map<std::string, std::string> shipped{
            {"mods/bf2/mod.desc", "<title>MODDESC_BF2</title>"},
            {"mods/bf2/levels/strike/info/strike.desc", "level"},
            {"mods/bf2/objects_client.zip", "objects"},
            {"mods/bf2/settings/maplist.con", "maplist"},
            {"mods/bf2/localization/english/english.utxt", "english"},
            {"mods/bf2/localization/german/german.utxt", "german"},
            {"mods/bf2/python/game/scoringcommon.py", "scoring"},
            {"python/bf2/gamelogic.py", "logic"},
            {"admin/default.py", "admin"},
            {"pylib-2.3.4.zip", "pylib"},
            {"bf2.exe", "retail executable"},
            {"pb/pbcl.dll", "punkbuster"}};
        std::string rows;
        for (const auto& [path, text] : shipped) rows += path + "\t" + std::to_string(text.size()) + "\t" + hash(text) + "\n";
        write(manifest, rows);
        write(folder / "mods/bf2/mod.desc", "<title>MODDESC_BF2</title>");
        write(folder / "mods/bf2/Levels/Strike/Info/Strike.desc", "level");
        write(folder / "mods/bf2/Objects_client.zip", "objects");
        write(folder / "mods/bf2/settings/maplist.con", "my own maplist");
        write(folder / "mods/bf2/Localization/English/english.utxt", "english");
        write(folder / "mods/bf2/python/game/scoringCommon.py", "scoring");
        write(folder / "python/bf2/GameLogic.py", "logic");
        write(folder / "Admin/default.py", "admin");
        write(folder / "pylib-2.3.4.zip", "pylib");
        write(folder / "BF2.exe", "patched executable");
        write(folder / "bf2hbc.dll", "a proxy");
        write(folder / "mods/mymod/mod.desc", "<title>My Mod</title>");
        write(folder / "mods/mymod/levels/x/info/x.desc", "level");

        check(setup::detectExisting(folder) == setup::DiscEdition::BF2, "a regular installation");
        write(folder / "mods/xpack/mod.desc", "<title>MODDESC_XP_TITLE</title>");
        check(setup::detectExisting(folder) == setup::DiscEdition::BF2, "the 1.50 stub of Special Forces has no levels");
        fs::create_directories(folder / "mods/xpack/Levels/Devils_Perch");
        check(setup::detectExisting(folder) == setup::DiscEdition::Complete, "Special Forces with its levels");
        fs::remove_all(folder / "mods/xpack");
        check(failure([&] { setup::detectExisting(root); }).find("no mods/bf2/mod.desc") != std::string::npos, "a folder without the game is refused");

        auto before = snapshot(folder);
        auto platform = setup::nativePlatform();
        setup::LinkedReport report;
        auto game = setup::linkExisting({folder, support, manifest, setup::DiscEdition::BF2, {}}, *platform, &report);
        check(report.missingLocalization == 1 && report.scripts.empty(), "German is missing, the scripts match");
        check(session::currentGeneration(support) == game && session::currentEdition(support) == setup::DiscEdition::BF2, "the linked generation is current");
        check(session::linkedSource(support) == folder, "the receipt names the folder");
        check(setup::is_link(fs::symlink_status(game / "mods/bf2")) && setup::leadsIntoFolder(game / "mods/bf2", folder), "mods/bf2 is a link into the folder");
        check(setup::leadsIntoFolder(game / "mods/mymod", folder), "the folder's own mods are linked");
        check(setup::is_link(fs::symlink_status(game / "python")) && setup::is_link(fs::symlink_status(game / "Admin")), "the script folders are linked, spelled as in the folder");
        check(read(fs::canonical(game / "pylib-2.3.4.zip")) == "pylib", "pylib-2.3.4.zip is there");
        check(!fs::exists(game / "BF2.exe") && !fs::exists(game / "bf2hbc.dll") && !fs::exists(game / "pb"), "no binary of the folder is linked");
        auto mods = setup::find_mods(game, setup::DiscEdition::BF2, {}, folder);
        check(mods.size() == 2 && mods[0].directory == "bf2" && mods[1].directory == "mymod", "the game offers Battlefield 2 and the folder's mod");
        check(snapshot(folder) == before, "nothing was written into the folder");

        auto stamp = setup::linkedStamp(game, manifest);
        check(setup::linkedStampMatches(game, manifest, stamp), "the stamp matches right after the verification");
        write(folder / "mods/bf2/settings/maplist.con", "saved by the game");
        check(setup::linkedStampMatches(game, manifest, stamp), "saved settings keep the stamp");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        write(folder / "python/bf2/GameLogic.py", "changed logic");
        check(!setup::linkedStampMatches(game, manifest, stamp), "a changed script needs the full verification");
        setup::verifyLinked(game, manifest, {}, &report);
        check(report.scripts == std::vector<std::string>{"python/bf2/gamelogic.py"}, "a changed script is reported, not refused");
        stamp = setup::linkedStamp(game, manifest);
        write(folder / "mods/bf2/Localization/German/german.utxt", "not german");
        check(!setup::linkedStampMatches(game, manifest, stamp), "a language which appeared is checked");
        check(failure([&] { setup::verifyLinked(game, manifest); }).find("mods/bf2/localization/german/german.utxt (differs)") != std::string::npos, "a present language must match");
        fs::remove_all(folder / "mods/bf2/Localization/German");
        write(folder / "mods/bf2/Objects_client.zip", "modified");
        check(!setup::linkedStampMatches(game, manifest, stamp), "a changed asset fails the stamp");
        auto changed = failure([&] { setup::verifyLinked(game, manifest); });
        check(changed.find("1 game file is missing or differs from Battlefield 2 1.50") != std::string::npos && changed.find("mods/bf2/objects_client.zip (differs)") != std::string::npos, "a changed asset is refused and named");
        fs::remove(folder / "mods/bf2/Objects_client.zip");
        check(failure([&] { setup::verifyLinked(game, manifest); }).find("mods/bf2/objects_client.zip (missing)") != std::string::npos, "a missing asset is refused and named");

        // a failed link leaves the current installation alone
        auto current = read(support / "Setup/Current.txt");
        check(!failure([&] { setup::linkExisting({folder, support, manifest, setup::DiscEdition::BF2, {}}, *platform); }).empty(), "a damaged installation is not linked");
        check(read(support / "Setup/Current.txt") == current, "Current.txt stays");
        check(std::distance(fs::directory_iterator(support / "GameData"), fs::directory_iterator{}) == 1, "the failed generation was removed");
        write(folder / "mods/bf2/Objects_client.zip", "objects");
        setup::verifyLinked(game, manifest);

        // mods come and go with the folder
        write(folder / "mods/newmod/mod.desc", "<title>New</title>");
        write(folder / "mods/newmod/levels/y/info/y.desc", "level");
        fs::remove_all(folder / "mods/mymod");
        setup::linkFolderMods(game, folder);
        check(setup::leadsIntoFolder(game / "mods/newmod", folder) && !fs::exists(fs::symlink_status(game / "mods/mymod")), "the links follow the folder's mods");

        check(!failure([&] { setup::linkExisting({folder, support, manifest, setup::DiscEdition::Complete, {}}, *platform); }).empty(), "Special Forces cannot be linked from an installation without it");
        check(failure([&] { setup::linkExisting({support / "GameData", support, manifest, setup::DiscEdition::BF2, {}}, *platform); }).find("inside OpenBF2's own data folder") != std::string::npos, "the support root is refused");
#if !defined(_WIN32)
        if (::geteuid() != 0) {
            fs::permissions(folder / "mods/bf2/settings", fs::perms::owner_read | fs::perms::owner_exec);
            auto readOnly = failure([&] { setup::linkExisting({folder, support, manifest, setup::DiscEdition::BF2, {}}, *platform); });
            fs::permissions(folder / "mods/bf2/settings", fs::perms::owner_all);
            check(readOnly.find("cannot write to this installation") != std::string::npos, "a read-only installation is refused");
        }
#endif

        before = snapshot(folder);
        setup::removeGeneration(game);
        check(!fs::exists(game) && snapshot(folder) == before, "removing the generation leaves the folder alone");
        fs::remove_all(root);
        std::cout << "existing installation checks: " << checks << " passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "existing installation checks failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
