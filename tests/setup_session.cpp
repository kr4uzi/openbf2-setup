#include "SetupSession.h"
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
        auto invalid=root/"invalid-media";write(invalid,"invalid input");
        rejects([&]{session::validateDisc(invalid);},"wrong disc accepted");
        rejects([&]{session::validatePatches(invalid,invalid);},"wrong patches accepted");
        fs::remove_all(root);std::cout<<checks<<" setup session checks passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
