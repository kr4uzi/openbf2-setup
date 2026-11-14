#include "install.h"
#include "edition.h"
#include "patch.h"
#include "session.h"
#include "mods.h"
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
            write(game/"mods/bf2/Localization/English/English.utxt",utf16("ID_LANGUAGE   English\r\nMODDESC_BF2_TITLE_LONG   wrong\r\nMODDESC_BF2_TITLE             Battlefield 2\r\n"));
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
            // The launcher's own options file.
            check(session::launchMod(support).empty(),"mod choice before any was made");
            session::saveLaunchMod(support,"xpack",*platform);check(session::launchMod(support)=="xpack"&&read(support/"Setup/Launch.txt")=="mod=xpack\n","mod choice not saved");
            session::saveLaunchMod(support,"Community",*platform);check(session::launchMod(support)=="Community","mod choice not replaced");
            rejects([&]{session::saveLaunchMod(support,"../evil",*platform);},"mod name with a path accepted");
            rejects([&]{session::saveLaunchMod(support,"a\nb",*platform);},"mod name with a line break accepted");
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
