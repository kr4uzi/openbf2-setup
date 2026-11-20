#include "install.h"
#include "hubtool.h"
#include "edition.h"
#include "patch.h"
#include "session.h"
#include "mods.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace {
unsigned checks=0;
void check(bool ok,const char* error){if(!ok)throw std::runtime_error(error);++checks;}
template<class F>void rejects(F f,const char* error){bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw,error);}
void write(const setup::fs::path& p,const std::string& text){std::ofstream out(p);out<<text;out.close();if(!out)throw std::runtime_error("fixture write failed");}
std::string read(const setup::fs::path& p){auto b=setup::View(p).all();return {b.begin(),b.end()};}
struct FailReplace : setup::Platform {
    setup::Platform& base;
    explicit FailReplace(setup::Platform& b):base(b){}
    std::unique_ptr<setup::Lock> lock(const setup::fs::path& p)override{return base.lock(p);}
    void syncFile(const setup::fs::path& p)override{base.syncFile(p);}
    void syncDirectory(const setup::fs::path& p)override{base.syncDirectory(p);}
    void replace(const setup::fs::path&,const setup::fs::path&)override{throw std::runtime_error("injected save failure");}
};
struct LowSpace : FailReplace {
    using FailReplace::FailReplace;
    uint64_t availableSpace(const setup::fs::path&)override{return 0;}
    void replace(const setup::fs::path& a,const setup::fs::path& b)override{base.replace(a,b);}
};
}
int main(int argc,char** argv){
    namespace fs=setup::fs;namespace session=setup::assistant;
    try {
        if(argc==7&&std::string(argv[1])=="--media"){
            auto scratch=fs::canonical(argv[2]);auto support=scratch/("lowspace-check-"+std::to_string(getpid()));
            check(!fs::exists(support),"fresh media-check support root required");
            auto edition=session::validateDisc(argv[3]);
            check(edition==(std::string(argv[6]).find("deluxe")!=std::string::npos?setup::DiscEdition::Deluxe:setup::DiscEdition::BF2),"owner disc edition detection");
            uint64_t first=0,second=0;setup::Control progress;
            progress.progress=[&](const setup::Progress& p){if(p.path=="Patch 1.41")first=p.completedBytes;else if(p.path=="Patch 1.5")second=p.completedBytes;};
            session::validatePatches(argv[4],argv[5],progress);
            check(first==562044991&&second==2067515943,"real patch progress did not reach full sizes");
            bool wrongSecond=false;try{setup::verifyPatch(setup::View(argv[4]),setup::PatchSlot::Patch150);}catch(const std::exception& e){wrongSecond=std::string(e.what())=="This is the 1.41 patch; please select the 1.5 patch here.";}
            check(wrongSecond,"second patch selector wrong-slot message");
            bool reversed=false;try{session::validatePatches(argv[5],argv[4]);}catch(const std::exception& e){reversed=std::string(e.what())=="This is the 1.5 patch; please select the 1.41 patch here.";}
            check(reversed,"first patch selector wrong-slot message");
            // A full-size copy with one flipped byte: the size matches, the SHA-256 must not.
            auto corrupt=scratch/("corrupt-patch-"+std::to_string(getpid())+".exe");fs::copy_file(argv[4],corrupt);
            {std::fstream f(corrupt,std::ios::in|std::ios::out|std::ios::binary);f.seekg(-1,std::ios::end);char c=0;f.get(c);c=char(c^0x5a);f.seekp(-1,std::ios::end);f.put(c);f.close();check(bool(f),"corrupt copy write failed");}
            bool corruptRejected=false;try{setup::verifyPatch(setup::View(corrupt),setup::PatchSlot::Patch141);}
            catch(const std::exception& e){corruptRejected=std::string(e.what()).find("does not match the official 1.41 patch")!=std::string::npos;}
            fs::remove(corrupt);check(corruptRejected,"corrupt full-size patch copy was not rejected with the official-patch message");
            auto text=scratch/("not-a-disc-"+std::to_string(getpid())+".txt");write(text,"not a disc\n");
            bool textRejected=false;try{session::validateDisc(text);}catch(const std::exception&){textRejected=true;}
            fs::remove(text);check(textRejected,"plain text file accepted as disc");
            auto platform=setup::nativePlatform();LowSpace low(*platform);bool rejected=false;
            try {setup::install({argv[3],argv[4],argv[5],support,argv[6],{}},low);}
            catch(const std::exception& e){rejected=std::string(e.what())=="Insufficient free space for staging and patch reserve";}
            check(rejected,"real media low-space preflight did not reject");
            check(!fs::exists(support/"Setup/work")&&!fs::exists(support/"Setup/Current.txt")&&fs::is_empty(support/"GameData"),"low space published or staged game data");
            fs::remove_all(support);
            std::cout<<"App selection checks passed on owner media; edition and exact patch identities/progress checked; both wrong-slot messages, a corrupt full-size patch copy and a text file as disc rejected; full input hashes + injected low-space install rejected before staging.\n";return 0;
        }
        if(argc!=2)return 2;
        auto scratch=fs::canonical(argv[1]);auto root=scratch/("session-checks-"+std::to_string(getpid()));
        check(!fs::exists(root),"fresh fixture required");fs::create_directory(root);
        const std::string key="ABCD1234EFGH5678IJKL";
        check(session::normalizedKey(" \tabcd-1234-efgh-5678-ijkl\r\n")==key,"grouped normalization");
        for(auto bad:{"", "ABCD-1234-EFGH-5678-IJK!","ＡBCD1234EFGH5678IJKL","A-BCD1234EFGH5678IJKL"})
            rejects([&]{session::normalizedKey(bad);},"invalid key accepted");
        const std::string name="HKEY_LOCAL_MACHINE\\SOFTWARE\\Electronic Arts\\EA Games\\Battlefield 2\\ergc";
        auto other="; keep me\r\nHKEY_CURRENT_USER\\other\\value = keep\r\n";
        auto registry=std::string(other)+" \t"+setup::lower(name)+"\\@ = old\r\n"+name+" = stale\n";
        auto updated=session::registryWithKey(registry,key);
        check(updated==std::string(other)+name+" = "+key+"\n","case/fallback duplicates or unrelated registry changed");
        check(!session::registryHasKey(updated),"format-only stored key accepted");
        check(!session::registryHasKey(name+"\\@ = "+key),"format-only fallback accepted");
        check(!session::registryHasKey(name+"\\@ = "+key+"\n"+name+" = \n"),"empty named value must override fallback");
        check(!session::registryHasKey(name+" = "+key+"\n"+name+" = invalid"),"last duplicate did not win");
        check(!session::registryHasKey("# "+name+" = "+key),"comment treated as key");
        auto both=session::registryWithKey(updated,"1111-2222-3333-4444-5555",true);
        check(!session::registryHasKey(both)&&!session::registryHasKey(both,true)&&both.find("11112222333344445555")!=std::string::npos,"format-only BF2/SF keys accepted");
        check(!session::registryHasKey(updated,true),"base key substituted for SF");
        auto baseUpdated=session::registryWithKey(both,key);
        check(baseUpdated.find("11112222333344445555")!=std::string::npos&&baseUpdated.find(name+" = "+key)!=std::string::npos,"base serialization altered SF row");
        rejects([&]{session::registryWithKey(both,"bad",true);},"invalid SF key accepted");
        const auto sfName="HKEY_LOCAL_MACHINE\\SOFTWARE\\Electronic Arts\\EA Games\\Battlefield 2 Special Forces\\ergc";
        check(!session::registryHasKey(std::string(sfName)+"\\@ = "+key,true),"format-only SF fallback accepted");
        check(!session::registryHasKey(std::string(sfName)+"\\@ = "+key+"\n"+sfName+" = invalid",true),"SF named precedence ignored");
        auto support=root/"support";fs::create_directories(support/"Setup");fs::create_directories(support/"Profiles");
        write(support/"Profiles/existing","profile");write(support/"Registry.con",registry);
        auto platform=setup::nativePlatform();
        {
            // Mods of a generation: the game's own name a localization key, community mods write their title.
            auto utf16=[](const std::string& ascii){std::string out("\xFF\xFE",2);for(char c:ascii){out+=c;out+='\0';}return out;};
            auto game=root/"mods-game";
            for(auto level:{"mods/bf2/Levels/Dalian_plant/Info","mods/xpack/Levels/Ghost_Town/info","mods/Community/levels/one/info","mods/borrowed","mods/nolevels/levels/empty","mods/.hidden/levels/x/info","mods/nodesc/levels/x/info"})fs::create_directories(game/level);
            fs::create_directories(game/"mods/bf2/Localization/English");
            write(game/"mods/bf2/Localization/English/English.utxt",utf16("ID_LANGUAGE   English\r\nMODDESC_BF2_TITLE_LONG   wrong\r\nMODDESC_BF2_TITLE             \x1b\x1b" "Battlefield 2" "\x1b\x1b\r\n"));
            write(game/"mods/bf2/Mod.desc","<mod>\n\t<title> MODDESC_BF2_TITLE </title>\n</mod>\n");write(game/"mods/bf2/Levels/Dalian_plant/Info/Dalian_plant.desc","");
            write(game/"mods/xpack/mod.desc","<mod><title> MODDESC_XP_TITLE </title><parentmod> bf2 </parentmod></mod>");write(game/"mods/xpack/Levels/Ghost_Town/info/ghost_town.desc","");
            write(game/"mods/Community/mod.desc","<mod><TITLE>A Community Mod</TITLE></mod>");write(game/"mods/Community/levels/one/info/one.desc","");
            write(game/"mods/borrowed/mod.desc","<mod><title>UNKNOWN_KEY</title><importlevels> bf2 </importlevels></mod>");
            write(game/"mods/nolevels/mod.desc","<mod><title>No Levels</title></mod>");
            write(game/"mods/.hidden/mod.desc","<mod><title>Hidden</title></mod>");write(game/"mods/.hidden/levels/x/info/x.desc","");
            write(game/"mods/nodesc/levels/x/info/x.desc","");
            using setup::Mod;
            check(setup::find_mods(game,setup::DiscEdition::BF2)==std::vector<Mod>{{"bf2","Battlefield 2"},{"Community","A Community Mod"},{"borrowed","UNKNOWN_KEY"}},"mods of a regular installation");
            check(setup::find_mods(game,setup::DiscEdition::DeluxeBase).size()==3,"Special Forces offered without being installed");
            check(setup::find_mods(game,setup::DiscEdition::CompleteBase).size()==3,"Complete base edition offered Special Forces");
            check(setup::find_mods(game,setup::DiscEdition::Complete).size()==4,"Complete Edition omitted Special Forces");
            check(setup::find_mods(game,setup::DiscEdition::Deluxe)==std::vector<Mod>{{"bf2","Battlefield 2"},{"xpack","Battlefield 2: Special Forces"},{"Community","A Community Mod"},{"borrowed","UNKNOWN_KEY"}},"mods of a Deluxe installation");
            fs::remove(game/"mods/xpack/Levels/Ghost_Town/info/ghost_town.desc");
            check(setup::find_mods(game,setup::DiscEdition::Deluxe).size()==3,"Special Forces stub without levels offered");
            check(setup::find_mods(root/"missing",setup::DiscEdition::BF2).empty(),"mods of a missing generation");
            // The mod library outside the generation: one link per mod, never over a name the game has.
            auto library=setup::mod_library(root/"library-support");
            check(library.filename()=="CustomMods","mod library must not share a name with the client's mods/ cache directory");
            for(auto level:{"Kept/levels/one/info","bf2/levels/one/info","undescribed/levels/one/info"})fs::create_directories(library/level);
            write(library/"Kept/mod.desc","<mod><title>Kept Across Reinstalls</title></mod>");write(library/"Kept/levels/one/info/one.desc","");
            write(library/"bf2/mod.desc","<mod><title>Impostor</title></mod>");write(library/"bf2/levels/one/info/one.desc","");
            fs::create_directories(root/"elsewhere/levels/one/info");write(root/"elsewhere/mod.desc","<mod><title>Elsewhere</title></mod>");write(root/"elsewhere/levels/one/info/one.desc","");
            fs::create_directory_symlink(root/"elsewhere",game/"mods/elsewhere");
            setup::link_mods(library,game);setup::link_mods(library,game);
            check(fs::is_symlink(game/"mods/Kept")&&fs::read_symlink(game/"mods/Kept")==library/"Kept","mod of the library not linked");
            check(!fs::is_symlink(game/"mods/bf2")&&!fs::exists(fs::symlink_status(game/"mods/undescribed")),"a taken name or a folder without mod.desc was linked");
            auto linked=setup::find_mods(game,setup::DiscEdition::BF2,library);
            check(linked.size()==4&&linked[0]==Mod{"bf2","Battlefield 2"}&&linked[2]==Mod{"Kept","Kept Across Reinstalls"},"linked mod not offered");
            check(setup::find_mods(game,setup::DiscEdition::BF2).size()==3,"a link was followed without a library");
            fs::remove_all(library/"Kept");setup::link_mods(library,game);
            check(!fs::exists(fs::symlink_status(game/"mods/Kept"))&&fs::is_symlink(game/"mods/elsewhere"),"dead link kept or a foreign link removed");
            check(setup::find_mods(game,setup::DiscEdition::BF2,library).size()==3,"a foreign link was offered as a mod");
            fs::remove(game/"mods/elsewhere");
            check(setup::localized(utf16("A  one\r\nAB  two\r\n"),"AB")=="two"&&setup::localized(utf16("A  one\r\n"),"B").empty(),"localization key lookup");
            check(setup::localized(utf16("MODDESC_XP_TITLE  \x1b\x1b" "Battlefield 2: Special Forces" "\x1b\x1b\r\n"),"MODDESC_XP_TITLE")=="Battlefield 2: Special Forces","retail title delimiters leaked into mod name");
            check(setup::localized(utf16("A  \x1b\x1b" "Caf\xe9 [Mod] + Extras" "\x1b\x1b\r\n"),"A")=="Café [Mod] + Extras","title cleanup damaged Unicode or punctuation");
            check(setup::localized(utf16("A  \x1b\x1b\x1b\x1b\r\n"),"A").empty(),"empty delimited localization value not empty");
            // The launcher's own options file.
            // the path of the demo's .con comes with every read, it is not saved
            auto loaded=[&]{auto options=session::loadLaunch(support);check(options.demo_config==(support/"Setup/Demo.con").string(),"the path of the demo's .con");options.demo_config.clear();return options;};
            check(loaded()==setup::LaunchOptions{},"launch options before any were saved");
            {
                setup::LaunchOptions options;options.mod="xpack";options.fullscreen=false;options.width=1280;options.height=800;options.server="10.0.0.2";options.port=16568;options.password="secret 42";options.player="Name With Space";
                session::saveLaunch(support,options,*platform);check(loaded()==options,"launch options not saved");
                check(read(support/"Setup/Launch.txt").rfind("mod=xpack\n",0)==0,"Launch.txt does not start with the mod line");
                options.mod="Community";session::saveLaunch(support,options,*platform);check(session::loadLaunch(support).mod=="Community","launch options not replaced");
                auto arguments=setup::launch_arguments(options);
                check(setup::command_line(arguments)=="+modPath mods/Community +restart 1 +fullscreen 0 +szx 1280 +szy 800 +joinServer 10.0.0.2 +port 16568 +password \"secret 42\" +playerName \"Name With Space\"","launch arguments");
                {auto noServer=options;noServer.server="";check(setup::command_line(setup::launch_arguments(noServer)).find("+password")==std::string::npos,"a password was passed without a server");}
                {auto noPassword=options;noPassword.password="\"";check(setup::command_line(setup::launch_arguments(noPassword)).find("+password")==std::string::npos,"an empty password was passed");}
                {auto quoted=options;quoted.password="se\"cret";check(setup::command_line(setup::launch_arguments(quoted)).find("+password secret ")!=std::string::npos,"a double quote in the password was passed");}
                check(setup::split_command_line(setup::command_line(arguments))==arguments,"command line round trip");
                check(setup::split_command_line("  a  \"b c\" \"\" \"x \\\"y\\\"\" d ")==std::vector<std::string>{"a","b c","","x \"y\"","d"},"command line split");
                check(setup::argument_mod(arguments)=="Community"&&setup::argument_mod({"+fullscreen","1"}).empty(),"mod of the arguments");
                options.fullscreen=true;check(setup::command_line(setup::launch_arguments(options)).find("+szx")==std::string::npos,"a size was passed in fullscreen");
                options.server="";options.player="";options.skip_movies=true;check(setup::command_line(setup::launch_arguments(options))=="+modPath mods/Community +restart 1","the plain arguments carry no fullscreen switch");
                options.override="+modPath mods/bf2 +fullscreen 0";check(setup::launch_arguments(options)==std::vector<std::string>{"+modPath","mods/bf2","+fullscreen","0"},"the override was not what is started");
                session::saveLaunch(support,options,*platform);check(session::loadLaunch(support).override=="+modPath mods/bf2 +fullscreen 0","the override was not saved");
                check(setup::parse_launch("mod=bf2\nwidth=10\nport=70000\n").width==0&&setup::parse_launch("port=70000\n").port==16567,"values out of range kept");
                check(setup::parse_launch("mod=xpack\n").mod=="xpack"&&setup::parse_launch("mod=xpack\n").skip_movies&&setup::parse_launch("mod=xpack\n").fullscreen,"an old Launch.txt does not get the defaults");
                options.always_show=true;check(setup::parse_launch(setup::launch_text(options))==options,"launch text round trip");
                check(!setup::parse_launch("mod=bf2\n").always_show&&setup::parse_launch("always_show=1\n").always_show,"the pages-at-every-start choice");
                {
                    // the online provider and the advanced switches
                    setup::LaunchOptions advanced;advanced.skip_movies=false;
                    check(!setup::has_advanced_options(advanced),"defaults count as advanced");
                    {setup::LaunchOptions online;online.online="openspy";online.provider=setup::parse_provider(setup::provider_presets().front().json);
                     check(!setup::has_advanced_options(online)&&setup::command_line(setup::launch_arguments(online))=="+modPath mods/bf2 +restart 1 +gamespyRedirect openspy.net","the OpenSpy preset, or a provider counted as advanced");}
                    advanced.no_sound=advanced.multi=true;
                    advanced.fullscreen=false;advanced.window_x=10;advanced.window_y=20;
                    advanced.server="10.0.0.2";advanced.password="pw";
                    auto line=setup::command_line(setup::launch_arguments(advanced));
                    check(line.find("+joinServer 10.0.0.2 +port 16567 +password pw")!=std::string::npos&&line.find("+noSound 1 +multi 1 +wx 10 +wy 20")!=std::string::npos,"the advanced arguments");
                    check(setup::has_advanced_options(advanced)&&setup::parse_launch(setup::launch_text(advanced))==advanced,"advanced launch text round trip");
                    {setup::LaunchOptions joining;joining.server="10.0.0.2";check(setup::has_advanced_options(joining),"joining a server, in Advanced Options, did not count as advanced");}
                    advanced.fullscreen=true;check(setup::command_line(setup::launch_arguments(advanced)).find("+wx")==std::string::npos,"a window position was passed in fullscreen");
                    // the further switches of the engine; a demo replaces joining a server
                    advanced.fullscreen=false;advanced.tutorial_messages=false;advanced.skip_dx_check=true;advanced.punkbuster="C:/pb two";
                    line=setup::command_line(setup::launch_arguments(advanced));
                    check(line.find("+wy 20 +tutorialMessages 0 +skipDXCheck 1 +pbPath \"C:/pb two\"")!=std::string::npos,"the further switches");
                    check(setup::parse_launch(setup::launch_text(advanced))==advanced,"further switches round trip");
                    advanced.demo="demos/round.con";line=setup::command_line(setup::launch_arguments(advanced));
                    check(line.find("+demo demos/round.con")!=std::string::npos&&line.find("+joinServer")==std::string::npos,"a demo did not replace joining a server");
                    check(setup::parse_launch(setup::launch_text(advanced))==advanced&&setup::parse_launch("tutorial_messages=7\n").tutorial_messages,"demo round trip, or an invalid tutorial value");
                    {setup::LaunchOptions only;check(only.tutorial_messages&&setup::command_line(setup::launch_arguments(only)).find("tutorial")==std::string::npos,"tutorial messages not on by default, or a switch passed for on");
                     only.tutorial_messages=false;auto off=setup::command_line(setup::launch_arguments(only));
                     check(!setup::has_advanced_options(only)&&off.find("+tutorialMessages 0")!=std::string::npos&&off.find("+help")==std::string::npos,"the tutorial choice counted as advanced, was not passed, or +help was used");}
                    {
                        // a recording is played through the .con the launcher writes for it; a .con as it is
                        setup::LaunchOptions demo;demo.demo="Demos/round 1.BF2Demo";demo.demo_config="/support/Setup/Demo.con";
                        check(setup::demo_recording(demo.demo)&&!setup::demo_recording("Demos/x.con"),"which demos are recordings");
                        check(setup::command_line(setup::launch_arguments(demo)).ends_with("+demo /support/Setup/Demo.con"),"a recording was not played through the written .con");
                        check(setup::demo_config_text("Demos/round 1.BF2Demo")=="rem written by OpenBF2 to play a recording\ndemo.demomode playback\ndemo.demofile Demos/round 1.BF2Demo\n","the .con for a recording");
                        demo.demo_config.clear();check(setup::command_line(setup::launch_arguments(demo)).find("+demo")==std::string::npos,"a recording was passed without its .con");
                        demo.demo="Demos/x.con";check(setup::command_line(setup::launch_arguments(demo)).ends_with("+demo Demos/x.con"),"a .con was not passed as it is");
                        demo.demo_config="/support/Setup/Demo.con";check(setup::parse_launch(setup::launch_text(demo)).demo_config.empty(),"the path of the written .con was saved");
                    }
                    // the server switches are not the launcher's: a Launch.txt of a test build which had them hosts nothing
                    check(setup::parse_launch("host_server=1\nlevel=x\nranked=1\nplay_now=1\nwidescreen_hud=1\n")==setup::LaunchOptions{}&&setup::command_line(setup::launch_arguments(setup::parse_launch("host_server=1\nlevel=x\n"))).find("+hostServer")==std::string::npos,"a hosting key was read");
                    check(setup::parse_launch("window_x=-5\n").window_x==-1,"values out of range kept");
                }
            }
            {setup::LaunchOptions bad;bad.mod="../evil";rejects([&]{session::saveLaunch(support,bad,*platform);},"mod name with a path accepted");}
            {setup::LaunchOptions bad;bad.mod="a\nb";rejects([&]{session::saveLaunch(support,bad,*platform);},"mod name with a line break accepted");}
        }
        {
            // provider files: each entry becomes "+gamespyRedirect" and its key, then its value, in the file's order;
            // the empty key gives the fallback for every name. BF2Hub's file has one entry per host
            const std::string hub=R"({
  "schema": 1,
  "provider": "BF2Hub",
  "generated": "2026-11-19T15:30:00Z",
  "services": {
    "available": "5.252.33.100", "ms14": "5.252.33.100", "gpcm": "5.252.33.100",
    "gpsp": "5.252.33.100", "master": "5.252.33.100",
    "gamestats": "116.203.14.166", "BF2Web": "116.203.14.166", "eapusher": "bf2web.example.org"
  }
})";
            auto parsed=setup::parse_provider(hub);
            check(parsed.name=="BF2Hub"&&parsed.services.size()==8&&parsed.services.front()==std::pair<std::string,std::string>{"available","5.252.33.100"}&&parsed.services.back().second=="bf2web.example.org","the provider file was not read in its order");
            check(setup::parse_provider("\xEF\xBB\xBF"+hub)==parsed,"a byte-order mark was refused");
            auto replace=[&](const std::string& from,const std::string& to){auto text=hub;auto at=text.find(from);if(at==std::string::npos)throw std::runtime_error("fixture edit");return text.replace(at,from.size(),to);};
            // what is not the format, or what the game or its command line cannot take, is refused
            for(auto bad:{replace("\"schema\": 1","\"schema\": 2"),replace("\"schema\": 1","\"schema\": \"1\""),replace("\"schema\": 1","\"schema\": 1.0"),
                          replace("\"BF2Hub\"","\"\""),replace("\"BF2Hub\"","\"BF2Hub/evil\""),replace("\"BF2Hub\"","\"Hub\\nNext\""),replace("\"BF2Hub\"","\""+std::string(65,'a')+"\""),
                          replace("\"BF2Hub\"","7"),replace("\"provider\": \"BF2Hub\",",""),
                          replace("\"gpcm\": \"5.252.33.100\"","\"gpcm\": \"5.252.33.100 \""),replace("\"gpcm\": \"5.252.33.100\"","\"gpcm\": \"x +multi 1\""),
                          replace("\"gpcm\": \"5.252.33.100\"","\"gpcm\": \"a\\\"b\""),replace("\"gpcm\": \"5.252.33.100\"","\"gpcm\": \"\""),replace("\"gpcm\": \"5.252.33.100\"","\"gpcm\": 5"),
                          replace("\"gpcm\": \"5.252.33.100\"","\"gpcm\": \"5.252.33.256\""),replace("\"gpcm\": \"5.252.33.100\"","\"gpcm\": \"1234\""),
                          replace("\"gpcm\": \"5.252.33.100\"","\"gpcm\": \"a..b\""),replace("\"gpcm\": \"5.252.33.100\"","\"gpcm\": \""+std::string(254,'a')+"\""),
                          replace("\"gpcm\":","\"gp cm\":"),replace("\"gpcm\":","\"gpcm+x\":"),replace("\"gpcm\":","\""+std::string(33,'a')+"\":"),
                          replace("\"gpcm\": \"5.252.33.100\",","\"gpcm\": \"5.252.33.100\", \"gpcm\": \"5.252.33.101\","),
                          replace("\"available\":","\"*\": \"openspy.net\", \"*\": \"other.net\", \"available\":"),replace("\"available\":","\"\": \"openspy.net\", \"available\":"),
                          replace("\"available\":","\"*\": \""+std::string(227,'a')+".net\", \"available\":"),replace("\"available\":","\"*\": \"300.1.1.1\", \"available\":"),
                          replace("\"eapusher\": \"bf2web.example.org\"","\"eapusher\": \"bf2web.example.org\","),hub+"x",replace("\"schema\": 1","\"schema\": 01"),
                          replace("2026-11","2026\\q11"),replace("2026-11","2026\\ud800"),replace("2026-11","2026\t11"),replace("\"services\"","\"elsewhere\""),
                          std::string("{\"schema\": 1, \"provider\": \"X\", \"services\": {}}"),std::string("[1]"),std::string(""),
                          std::string("{\"schema\": 1, \"provider\": \"X\", \"services\": {\"available\": \"a.b\"")})
                rejects([&]{setup::parse_provider(bad);},"an invalid provider file was accepted");
            rejects([&]{setup::parse_provider(hub+std::string(setup::provider_file_limit,' '));},"an oversized provider file was read");
            {auto deep=replace("\"generated\"","\"deep\": "+std::string(17,'[')+std::string(17,']')+", \"generated\"");rejects([&]{setup::parse_provider(deep);},"a deeply nested file was read");}
            // a key is passed as written, whichever host it names; '-' for stage-net; a name with spaces and dots
            check(setup::parse_provider(replace("\"master\": \"5.252.33.100\",","\"master\": \"5.252.33.100\", \"Future\": \"x.y\",")).services.size()==9,"a key the setup does not know was dropped");
            check(setup::parse_provider(replace("\"gpsp\":","\"stage-net\": \"5.252.33.100\", \"gpsp\":")).services.size()==9,"a retail label with '-' was refused");
            check(setup::parse_provider(replace("\"BF2Hub\"","\"My Hub 2.0-test\"")).name=="My Hub 2.0-test","a provider name with spaces, dots and '-' was refused");
            setup::LaunchOptions online;online.online="bf2hub";online.provider=parsed;
            auto line=setup::command_line(setup::launch_arguments(online));
            check(line=="+modPath mods/bf2 +restart 1 +gamespyRedirectavailable 5.252.33.100 +gamespyRedirectms14 5.252.33.100 +gamespyRedirectgpcm 5.252.33.100 +gamespyRedirectgpsp 5.252.33.100 +gamespyRedirectmaster 5.252.33.100 +gamespyRedirectgamestats 116.203.14.166 +gamespyRedirectBF2Web 116.203.14.166 +gamespyRedirecteapusher bf2web.example.org","the provider switches");
            online.provider.services[2].second="bad value";check(setup::command_line(setup::launch_arguments(online)).find("+gamespyRedirectgpcm")==std::string::npos,"a value the game refuses was passed");
            online.online="";check(setup::command_line(setup::launch_arguments(online)).find("+gamespyRedirect")==std::string::npos,"switches with the retail services");
            online.online="bf2hub";online.provider={};check(setup::command_line(setup::launch_arguments(online)).find("+gamespyRedirect")==std::string::npos,"switches without a kept provider file");
            // "*": the fallback, a domain or an IPv4 address
            for(auto [value,passed]:{std::pair{"10.1.2.3","+gamespyRedirect 10.1.2.3"},std::pair{"example.net","+gamespyRedirect example.net"}}){
                online.provider=setup::parse_provider(std::string("{\"schema\": 1, \"provider\": \"Mine\", \"services\": {\"*\": \"")+value+"\", \"gpcm\": \"10.9.9.9\"}}");
                check(setup::command_line(setup::launch_arguments(online))==std::string("+modPath mods/bf2 +restart 1 ")+passed+" +gamespyRedirectgpcm 10.9.9.9","the fallback entry");
            }
            check(setup::provider_presets().size()==1&&setup::parse_provider(setup::provider_presets().front().json).name==setup::provider_presets().front().name,"the presets");
            check(setup::parse_provider(setup::provider_presets().front().json).services==std::vector<std::pair<std::string,std::string>>{{"*","openspy.net"}},"the OpenSpy preset");
            // valid_to: until when the file holds; without it for ever. "generated" is only information
            {
                auto timed=setup::parse_provider(replace("\"generated\": \"2026-11-19T15:30:00Z\",","\"generated\": \"2026-11-19T15:30:00Z\", \"valid_to\": \"2026-11-26T15:30:00Z\","));
                auto end=*setup::parse_utc("2026-11-26T15:30:00Z");
                check(timed.valid_to==end&&setup::provider_expiry(timed)==end,"until when a provider file holds");
                check(!setup::provider_expired(timed,end-std::chrono::seconds{1})&&setup::provider_expired(timed,end),"when a provider file runs out");
                check(!setup::provider_expiry(parsed)&&!setup::provider_expired(parsed,end+std::chrono::hours{24*3650}),"a file without valid_to ran out");
                check(!setup::parse_provider(replace("\"generated\": \"2026-11-19T15:30:00Z\",","\"generated\": \"not a time\", \"ttl\": 600,")).valid_to,"generated or an old ttl decided how long a file holds");
                for(auto bad:{replace("\"generated\": \"2026-11-19T15:30:00Z\",","\"valid_to\": \"2026-11-26 15:30\","),replace("\"generated\": \"2026-11-19T15:30:00Z\",","\"valid_to\": 1700000000,"),
                              replace("\"generated\": \"2026-11-19T15:30:00Z\",","\"valid_to\": \"2026-13-01T00:00:00Z\","),replace("\"generated\": \"2026-11-19T15:30:00Z\",","\"valid_to\": \"\",")})
                    rejects([&]{setup::parse_provider(bad);},"an unusable valid_to was accepted");
                check(!setup::parse_utc("2026-02-30T00:00:00Z")&&!setup::parse_utc("2026-11-19T24:00:00Z")&&!setup::parse_utc("2026-11-19T15:30:00")&&setup::parse_utc("2028-02-29T23:59:59Z"),"times of the file");
            }
            // the addresses a provider file may come from
            for(auto good:{"https://bf2hub.krauzi.de/servers.json","https://example.org:8443/a/b.json?x=1"})check(setup::valid_provider_url(good),"a provider address was refused");
            for(auto bad:{"http://example.org/a.json","https://user@example.org/a.json","https://example.org/a b.json","https:///a.json","ftp://x.org","https://example.org/#x","https://300.1.1/a"})check(!setup::valid_provider_url(bad),"an unusable provider address was accepted");
            // Launch.txt keeps the choice and where each provider's file came from, never the entries; older keys are not read
            online.provider=parsed;online.bf2hub_source="hosted";online.other_source="url";online.other_url="https://example.org/p.json";auto text=setup::launch_text(online);
            auto back=setup::parse_launch(text);
            check(text.find("online=bf2hub")!=std::string::npos&&text.find("5.252.33.100")==std::string::npos&&back.online=="bf2hub"&&back.bf2hub_source=="hosted"&&back.other_url=="https://example.org/p.json"&&back.provider.services.empty(),"the provider choice in Launch.txt");
            check(setup::parse_launch("online=evil\nbf2hub_source=x\nother_url=http://x.org/a\n")==setup::LaunchOptions{},"an unusable online choice or source was read");
            check(setup::parse_launch("bf2hub=1\ngamespy=openspy.net\nredirect=10.0.0.1\nprovider=1\n")==setup::LaunchOptions{},"the keys of unreleased test builds were read");
            // the kept copy: replaced only by a valid file, read with the options
            auto files=root/"provider-files";fs::create_directories(files);
            write(files/"hub.json",hub);write(files/"bad.json",replace("\"schema\": 1","\"schema\": 9"));write(files/"big.json",hub+std::string(setup::provider_file_limit,' '));
            check(!session::storedProvider(support,"bf2hub")&&!session::storedProvider(support,"other"),"a provider file before one was chosen");
            check(session::importProvider(support,"bf2hub",files/"hub.json",*platform)==parsed&&read(support/"Setup/Provider-BF2Hub.json")==hub&&session::storedProvider(support,"bf2hub")==parsed,"BF2Hub's file was not kept");
            check(!session::storedProvider(support,"other"),"BF2Hub's file was kept as the other provider's");
            rejects([&]{session::importProvider(support,"bf2hub",files/"bad.json",*platform);},"an invalid provider file was kept");
            rejects([&]{session::importProvider(support,"bf2hub",files/"big.json",*platform);},"an oversized provider file was kept");
            rejects([&]{session::importProvider(support,"bf2hub",files/"missing.json",*platform);},"a missing provider file was kept");
            rejects([&]{session::importProvider(support,"bf2hub",files,*platform);},"a folder was kept as provider file");
            write(files/"old.json",replace("\"generated\": \"2026-11-19T15:30:00Z\",","\"valid_to\": \"2020-01-01T00:00:00Z\","));
            check(session::importProvider(support,"other",files/"old.json",*platform).valid_to==setup::parse_utc("2020-01-01T00:00:00Z"),"a file which has run out was refused: it stays usable, as out of date");
            fs::remove(support/"Setup/Provider-Other.json");
            rejects([&]{session::keepProvider(support,"openspy",hub,*platform);},"a provider file was kept for OpenSpy");
            rejects([&]{session::keepProvider(support,"bf2hub","{}",*platform);},"an invalid provider file was kept");
            check(read(support/"Setup/Provider-BF2Hub.json")==hub&&!fs::exists(support/"Setup/Provider-BF2Hub.pending"),"a refused file changed the kept one");
            auto mine=std::string("{\"schema\": 1, \"provider\": \"Mine\", \"services\": {\"*\": \"10.0.0.7\"}}");
            check(session::keepProvider(support,"other",mine,*platform).name=="Mine"&&read(support/"Setup/Provider-Other.json")==mine&&session::storedProvider(support,"bf2hub")==parsed,"the other provider's file was not kept beside BF2Hub's");
            auto saved=session::loadLaunch(support);
            setup::LaunchOptions chosen=saved;chosen.online="bf2hub";session::saveLaunch(support,chosen,*platform);
            check(session::loadLaunch(support).provider==parsed,"the options were read without BF2Hub's services");
            chosen.online="other";session::saveLaunch(support,chosen,*platform);
            check(session::loadLaunch(support).provider.name=="Mine","the options were read without the other provider's services");
            chosen.online="openspy";session::saveLaunch(support,chosen,*platform);
            check(session::loadLaunch(support).provider.services.front().second=="openspy.net","the options were read without OpenSpy's preset");
            chosen.online="bf2hub";session::saveLaunch(support,chosen,*platform);
            write(support/"Setup/Provider-BF2Hub.json","{}");check(!session::storedProvider(support,"bf2hub")&&session::loadLaunch(support).online=="bf2hub"&&session::loadLaunch(support).provider.services.empty(),"a broken kept file gave services");
            fs::remove(support/"Setup/Provider-BF2Hub.json");fs::create_symlink(files/"hub.json",support/"Setup/Provider-BF2Hub.json");
            check(!session::storedProvider(support,"bf2hub"),"a symlinked provider file was read");
            fs::remove(support/"Setup/Provider-BF2Hub.json");fs::remove(support/"Setup/Provider-Other.json");session::saveLaunch(support,saved,*platform);
        }
        {
            // the demos of the game folder: its Demos folder in any spelling, recordings and .con files, newest first
            auto game=root/"demo-game";fs::create_directories(game/"demos/sub");
            write(game/"demos/old.bf2demo","1");write(game/"demos/new.BF2DEMO","2");write(game/"demos/play.con","3");write(game/"demos/notes.txt","4");
            setup::fs::last_write_time(game/"demos/old.bf2demo",setup::fs::file_time_type::clock::now()-std::chrono::hours(2));
            setup::fs::last_write_time(game/"demos/play.con",setup::fs::file_time_type::clock::now()-std::chrono::hours(1));
            fs::create_symlink(game/"demos/old.bf2demo",game/"demos/link.bf2demo");
            auto demos=session::findDemos(game);
            check(demos==std::vector<fs::path>{"demos/new.BF2DEMO","demos/play.con","demos/old.bf2demo"},"the demos of the game folder");
            check(session::findDemos(root/"missing").empty(),"demos of a missing game folder");
            check(session::loadLaunch(support).demo_config==(support/"Setup/Demo.con").string(),"the options were read without the path of the demo's .con");
        }
        {
            // openbf2-hubtool: what its exit codes mean, and which runs give a provider file (synthetic output)
            const std::string json="{\"schema\": 1, \"provider\": \"BF2Hub\", \"services\": {\"available\": \"10.0.0.1\", \"gpcm\": \"10.0.0.2\"}}";
            setup::HubtoolRun run;run.exitCode=0;run.out=json+"\r\n";
            check(setup::hubtool_provider(run)==json+"\n"&&setup::parse_provider(setup::hubtool_provider(run)).services.size()==2,"a run with exit 0 gave no provider file");
            run.exitCode=6;check(setup::hubtool_provider(run)==json+"\n","exit 6, which still prints the file, was refused");
            for(auto [code,line]:{std::pair{2,"The BF2Hub installer or the game's BF2.exe could not be used."},std::pair{3,"The BF2Hub tool's sandbox could not start."},
                                  std::pair{4,"The BF2Hub client could not be asked."},std::pair{5,"BF2Hub gave no addresses."},std::pair{6,"The BF2Hub tool could not write its file."},
                                  std::pair{9,"The BF2Hub tool failed (exit code 9)."}}){
                setup::HubtoolRun failed;failed.exitCode=code;failed.out=code==6?"":json;
                check(setup::hubtool_failure(failed)==line,"an exit code's meaning");
                bool refused=false;try{setup::hubtool_provider(failed);}catch(const std::exception& e){refused=std::string(e.what())==line;}
                check(refused,"a failed run gave a provider file, or not its exit code's meaning");
            }
            {setup::HubtoolRun late;late.timedOut=true;late.exitCode=0;late.out=json;bool refused=false;try{setup::hubtool_provider(late);}catch(const std::exception& e){refused=std::string(e.what())=="BF2Hub did not answer in time.";}check(refused,"a stopped run gave a provider file");}
            for(auto out:{std::string(""),std::string("  \n"),std::string("not json"),std::string("{\"schema\": 1, \"provider\": \"BF2Hub\", \"services\": {\"gpcm\": \"x +multi 1\"}}")}){
                setup::HubtoolRun bad;bad.exitCode=0;bad.out=out;rejects([&]{setup::hubtool_provider(bad);},"a run with no valid provider file on stdout was accepted");
            }
            std::string err;for(int i=1;i<=30;++i)err+="line "+std::to_string(i)+"\r\n";err+="\n\n";
            auto tail=setup::last_lines(err);
            check(tail.rfind("line 11\n",0)==0&&tail.ends_with("line 30\n")&&std::count(tail.begin(),tail.end(),'\n')==20,"the last 20 lines of the error output");
            check(setup::last_lines("only\n")=="only\n"&&setup::last_lines("").empty(),"short error output");
            // BF2.exe of the content contract, looked up without regard to case
            auto manifest=root/"manifest.tsv";
            write(manifest,"# contract\nother.dll\t1\t00\nBF2.exe\t6556160\t2f08d8fced27d8512fc0d0681c33a0eb066947af08d156b191744cc89f4ac257\n");
            auto bf2=setup::manifest_file(manifest,"bf2.exe");
            check(bf2.path=="BF2.exe"&&bf2.size==6556160&&bf2.sha256=="2f08d8fced27d8512fc0d0681c33a0eb066947af08d156b191744cc89f4ac257","BF2.exe of the manifest");
            rejects([&]{setup::manifest_file(manifest,"missing.exe");},"a file missing from the manifest was found");
            if(!setup::fs::exists("C:/"))rejects([&]{setup::run_hubtool(root,root/"tool",root/"installer",std::chrono::seconds(1));},"the BF2Hub tool ran outside Windows");
        }
        check(!session::storedKey(support),"legacy format-only registry accepted");
        for(auto invalidKey:{key,std::string("bad")}) {
            rejects([&]{session::saveKey(support,invalidKey,*platform);},"invalid key saved");
            check(read(support/"Registry.con")==registry&&!fs::exists(support/"Registry.pending"),"rejected key changed registry");
        }
        rejects([&]{session::saveKeys(support,key,"11112222333344445555",*platform);},"format-only Deluxe keys saved");
        check(read(support/"Registry.con")==registry&&read(support/"Profiles/existing")=="profile", "rejection changed saved data");
        fs::create_directories(support/"GameData/1.50-1234");write(support/"Setup/Current.txt","GameData/1.50-1234\nReceipt-1234.txt\n");
        write(support/"Setup/Receipt-1234.txt","openbf2-setup-v1\n");
        check(session::currentGeneration(support)==support/"GameData/1.50-1234","generation resolution");
        auto contents=root/"Contents";fs::create_directories(contents/"MacOS");
        check(!session::clientPresent(contents),"absent client present");
        for(auto item:{"bf2_client","libdice_py.dylib","libaidll.dylib"})write(contents/"MacOS"/item,"synthetic placeholder, never executed");
        check(!session::clientPresent(contents),"nonexecutable client present");fs::permissions(contents/"MacOS/bf2_client",fs::perms::owner_all);
        rejects([&]{session::clientLaunch(contents,support);},"launch accepted legacy format-only key");
        check(session::currentEdition(support)==setup::DiscEdition::BF2,"v1 edition compatibility");
        write(support/"Setup/Receipt-1234.txt","openbf2-setup-v3\nedition=complete\n");
        check(session::currentEdition(support)==setup::DiscEdition::Complete,"Complete Edition receipt detection");
        check(std::string(session::manifestName(setup::DiscEdition::Complete))=="content-complete-150","Complete Edition manifest selection");
        write(support/"Setup/Receipt-1234.txt","openbf2-setup-v3\nedition=complete-base\n");
        check(session::currentEdition(support)==setup::DiscEdition::CompleteBase,"Complete Edition base receipt detection");
        check(std::string(session::manifestName(setup::DiscEdition::CompleteBase))=="content-150","Complete Edition base manifest selection");
        write(support/"Setup/Receipt-1234.txt","openbf2-setup-v2\nedition=deluxe\n");
        check(session::currentEdition(support)==setup::DiscEdition::Deluxe,"Deluxe receipt detection");
        write(support/"Setup/Receipt-1234.txt","openbf2-setup-v2\nedition=deluxe-base\n");
        check(session::currentEdition(support)==setup::DiscEdition::DeluxeBase,"Deluxe-base receipt detection");
        {bool sfDemanded=false;try{session::clientLaunch(contents,support);}catch(const std::exception& e){sfDemanded=std::string(e.what()).find("Special Forces")!=std::string::npos;}check(!sfDemanded,"Deluxe-base launch demanded a Special Forces key");}
        check(std::string(session::manifestName(setup::DiscEdition::BF2))=="content-150"&&std::string(session::manifestName(setup::DiscEdition::Deluxe))=="content-deluxe-150"&&std::string(session::manifestName(setup::DiscEdition::DeluxeBase))=="content-deluxe-base-150","manifest names");
        write(support/"Setup/Receipt-1234.txt","openbf2-setup-v2\nedition=deluxe\n");
        rejects([&]{session::clientLaunch(contents,support);},"Deluxe launch without SF key accepted");
        write(support/"Registry.con",both);
        check(!session::storedKey(support)&&!session::storedKey(support,true),"legacy Deluxe placeholders accepted");
        rejects([&]{session::clientLaunch(contents,support);},"Deluxe launch accepted invalid keys");
        fs::remove(contents/"MacOS/libaidll.dylib");rejects([&]{session::clientLaunch(contents,support);},"missing dylib launch accepted");
        fs::create_symlink(support/"Profiles/existing",contents/"MacOS/libaidll.dylib");check(!session::clientPresent(contents),"symlink dylib present");
        for(auto bad:{"GameData/1.50-1234/../escape\n","GameData/1.50-\n","GameData/1.50-1234","../escape\n","GameData/1.50-1234/child\n"}){
            write(support/"Setup/Current.txt",bad);rejects([&]{session::currentGeneration(support);},"malformed receipt accepted");
        }
        write(support/"Setup/Current.txt","GameData/1.50-1234\n../receipt.txt\n");
        rejects([&]{session::currentEdition(support);},"edition receipt escaped Setup");
        write(support/"Setup/Current.txt","GameData/1.50-1234\n");fs::rename(support/"GameData",support/"MovedData");fs::create_directory_symlink(support/"MovedData",support/"GameData");
        rejects([&]{session::currentGeneration(support);},"intermediate generation symlink accepted");
        // Voice defaults: the one file retail's BF2VoiceSetup writes, four plain lines.
        {
            session::VoiceSettings defaults;
            check(session::voiceSettingsText(defaults)=="1\n1\n0.1\n0\n","voice default text");
            session::VoiceSettings custom;custom.captureVolume=0.75;custom.captureThreshold=0.25;custom.boost=true;
            check(session::voiceSettingsText(custom)=="1\n0.75\n0.25\n1\n","voice custom text");
            session::VoiceSettings tiny;tiny.captureThreshold=0.123456789;
            check(session::voiceSettingsText(tiny)=="1\n1\n0.123457\n0\n","voice text uses default stream precision");
            session::VoiceSettings high;high.captureVolume=1.5;rejects([&]{session::voiceSettingsText(high);},"capture volume above 1 accepted");
            session::VoiceSettings low;low.captureThreshold=-0.1;rejects([&]{session::voiceSettingsText(low);},"negative threshold accepted");
            auto parsed=session::parseVoiceSettings("0.5\n0.75\n0.25\n1\n");
            check(parsed.playbackVolume==0.5&&parsed.captureVolume==0.75&&parsed.captureThreshold==0.25&&parsed.boost,"voice parse round trip");
            auto partial=session::parseVoiceSettings("0.5\n");
            check(partial.playbackVolume==0.5&&partial.captureVolume==1.0&&partial.captureThreshold==0.1&&!partial.boost,"short voice file keeps defaults");
            auto garbage=session::parseVoiceSettings("true\n");
            check(garbage.playbackVolume==1.0&&garbage.captureVolume==1.0,"unparsable voice file keeps defaults");
            auto range=session::parseVoiceSettings("1\n7\n0.2\n2\n");
            check(range.captureVolume==1.0&&range.captureThreshold==0.2&&!range.boost,"out-of-range voice values keep defaults");
            auto voiceSupport=root/"voice";fs::create_directories(voiceSupport/"Setup");
            check(session::loadVoiceSettings(voiceSupport).captureThreshold==0.1,"missing voice file loads defaults");
            check(!session::profilesExist(voiceSupport),"profiles reported without a Profiles directory");
            session::saveVoiceSettings(voiceSupport,custom,*platform);
            auto voiceFile=voiceSupport/"Profiles/BattleCommoDefaultUserSettings.con";
            check(read(voiceFile)=="1\n0.75\n0.25\n1\n","voice file content");
            check((fs::status(voiceSupport/"Profiles").permissions()&fs::perms::all)==fs::perms::owner_all,"Profiles directory is not 0700");
            check(!fs::exists(voiceSupport/"Profiles/BattleCommoDefaultUserSettings.pending"),"voice pending file left behind");
            auto loaded=session::loadVoiceSettings(voiceSupport);
            check(loaded.captureVolume==0.75&&loaded.captureThreshold==0.25&&loaded.boost,"voice file reload");
            check(!session::profilesExist(voiceSupport),"the voice file counted as a profile");
            fs::create_directories(voiceSupport/"Profiles/default");
            check(session::profilesExist(voiceSupport),"the client's default profile directory not detected");
            fs::remove(voiceSupport/"Profiles/default");fs::create_directories(voiceSupport/"Profiles/0001");
            check(session::profilesExist(voiceSupport),"numeric profile directory not detected");
            check(session::loadVoiceSettings(voiceSupport).captureVolume==0.75,"profile directory broke voice reload");
            FailReplace failing(*platform);
            rejects([&]{session::saveVoiceSettings(voiceSupport,defaults,failing);},"failed voice replace reported success");
            check(read(voiceFile)=="1\n0.75\n0.25\n1\n"&&!fs::exists(voiceSupport/"Profiles/BattleCommoDefaultUserSettings.pending"),"failed voice save changed or littered Profiles");
            fs::remove(voiceFile);fs::create_symlink(root/"elsewhere",voiceFile);
            rejects([&]{session::loadVoiceSettings(voiceSupport);},"symlinked voice file read");
            fs::remove(voiceFile);fs::remove_all(voiceSupport/"Profiles");fs::create_directory_symlink(root,voiceSupport/"Profiles");
            rejects([&]{session::saveVoiceSettings(voiceSupport,defaults,*platform);},"symlinked Profiles directory written");
            rejects([&]{session::profilesExist(voiceSupport);},"symlinked Profiles directory scanned");
        }
        auto invalid=root/"invalid-media";write(invalid,"invalid input");
        rejects([&]{session::validateDisc(invalid);},"wrong disc accepted");
        rejects([&]{session::validatePatches(invalid,invalid);},"wrong patches accepted");
        fs::remove_all(root);std::cout<<checks<<" setup session checks passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
