#include "SetupSession.h"
#include <fcntl.h>
#include <cerrno>
#include <unistd.h>
#include <sstream>
#include <stdexcept>

namespace setup::assistant {
namespace {
std::string outputName(bool sf) {return std::string("HKEY_LOCAL_MACHINE\\SOFTWARE\\Electronic Arts\\EA Games\\")+(sf?"Battlefield 2 Special Forces":"Battlefield 2")+"\\ergc";}
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
std::string trim(const std::string& s) {
    auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");
    return a==std::string::npos?"":s.substr(a,b-a+1);
}
std::string name(const std::string& row) {
    if(row.find('\0')!=std::string::npos||row.find('=')==std::string::npos)return {};
    return lower(trim(row.substr(0,row.find('='))));
}
void noLinks(const fs::path& path) {
    fs::path current;
    for(const auto& part:fs::absolute(path).lexically_normal()){
        current/=part;require(!fs::is_symlink(fs::symlink_status(current)),"Symlink in setup path");
    }
}
std::string read(const fs::path& path) {
    auto bytes=View(path).all();return {bytes.begin(),bytes.end()};
}
// Exclusive creation with 0600 avoids a plaintext window or stale-file clobber;
// the file is complete and flushed before it replaces the destination.
void writePrivately(const fs::path& pending,const fs::path& file,const std::string& text,Platform& platform,const char* what) {
    int fd=::open(pending.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
    require(fd>=0,(std::string("Cannot create private ")+pending.filename().string()+"; check permissions or a stale pending file.").c_str());
    try {
        size_t offset=0;
        while(offset<text.size()){
            auto count=::write(fd,text.data()+offset,text.size()-offset);
            if(count<0&&errno==EINTR)continue;
            require(count>0,(std::string("Cannot save the ")+what).c_str());offset+=size_t(count);
        }
        require(::fsync(fd)==0,(std::string("Cannot flush the ")+what).c_str());
        int result=::close(fd);fd=-1;require(result==0,(std::string("Cannot close the ")+what+" file").c_str());
        platform.replace(pending,file);
    } catch(...) {if(fd>=0)::close(fd);std::error_code ignored;fs::remove(pending,ignored);throw;}
}
}
std::string normalizedKey(const std::string& input) {
    auto key=trim(input);require(keyFormat(key),"Enter five groups of four letters or numbers.");
    std::string result;
    for(char c:key)if(c!='-')result+=c>='a'&&c<='z'?char(c-'a'+'A'):c;
    return result;
}
std::string registryWithKey(const std::string& registry,const std::string& key,bool sf) {
    auto keyName=lower(outputName(sf));
    auto replacement=outputName(sf)+" = "+normalizedKey(key)+"\n";
    std::istringstream input(registry);std::string row,updated;
    while(std::getline(input,row)){
        auto n=name(row);if(n!=keyName&&n!=keyName+"\\@")updated+=row+"\n";
    }
    return updated+replacement;
}
bool registryHasKey(const std::string& registry,bool sf) {
    auto keyName=lower(outputName(sf));
    std::istringstream input(registry);std::string row,named,fallback;bool hasNamed=false;
    while(std::getline(input,row)){
        auto n=name(row);
        if(n==keyName){named=trim(row.substr(row.find('=')+1));hasNamed=true;}
        else if(n==keyName+"\\@")fallback=trim(row.substr(row.find('=')+1));
    }
    return validateKey(sf?KeyEdition::SpecialForces:KeyEdition::BF2,hasNamed?named:fallback);
}
void saveKey(const fs::path& support,const std::string& key,Platform& platform) {saveKeys(support,key,"",platform);}
void saveKeys(const fs::path& support,const std::string& key,const std::string& sfKey,Platform& platform) {
    const auto normalized=normalizedKey(key);
    const auto sfNormalized=sfKey.empty()?"":normalizedKey(sfKey);
    require(validateKey(KeyEdition::BF2,normalized),"Battlefield 2: invalid CD key.");
    if(!sfNormalized.empty())require(validateKey(KeyEdition::SpecialForces,sfNormalized),"Special Forces: invalid CD key.");
    // Setup creates support before this call; keep all key writes under its lock.
    noLinks(support/"Setup");noLinks(support/"Profiles");noLinks(support/"Registry.con");
    auto lock=platform.lock(support/"Setup/Install.lock");
    fs::create_directories(support/"Profiles");fs::permissions(support/"Profiles",fs::perms::owner_all);
    auto file=support/"Registry.con";
    auto updated=registryWithKey(fs::exists(file)?read(file):"",normalized);
    if(!sfNormalized.empty())updated=registryWithKey(updated,sfNormalized,true);
    writePrivately(support/"Registry.pending",file,updated,platform,"key");
}
std::string voiceSettingsText(const VoiceSettings& v) {
    for(double value:{v.playbackVolume,v.captureVolume,v.captureThreshold})
        require(value>=0.0&&value<=1.0,"Voice settings must be between 0 and 1.");
    std::ostringstream out; // default precision 6, like the client's writer
    out<<v.playbackVolume<<'\n'<<v.captureVolume<<'\n'<<v.captureThreshold<<'\n'<<(v.boost?1:0)<<'\n';
    return out.str();
}
VoiceSettings parseVoiceSettings(const std::string& text) {
    VoiceSettings v;std::istringstream in(text.substr(0,512));
    auto number=[&](double& target){double value=0;if(in>>value){if(value>=0.0&&value<=1.0)target=value;return true;}return false;};
    if(!number(v.playbackVolume))return v;
    if(!number(v.captureVolume))return v;
    if(!number(v.captureThreshold))return v;
    int boost=0;if(in>>boost&&(boost==0||boost==1))v.boost=boost==1;
    return v;
}
VoiceSettings loadVoiceSettings(const fs::path& support) {
    auto file=support/"Profiles/BattleCommoDefaultUserSettings.con";noLinks(file);
    if(!fs::is_regular_file(file))return {};
    try {return parseVoiceSettings(read(file));}catch(const std::exception&){return {};}
}
void saveVoiceSettings(const fs::path& support,const VoiceSettings& settings,Platform& platform) {
    auto text=voiceSettingsText(settings);
    noLinks(support/"Setup");noLinks(support/"Profiles");
    auto lock=platform.lock(support/"Setup/Install.lock");
    fs::create_directories(support/"Profiles");fs::permissions(support/"Profiles",fs::perms::owner_all);
    writePrivately(support/"Profiles/BattleCommoDefaultUserSettings.pending",support/"Profiles/BattleCommoDefaultUserSettings.con",text,platform,"voice settings");
}
bool profilesExist(const fs::path& support) {
    noLinks(support/"Profiles");
    if(!fs::is_directory(support/"Profiles"))return false;
    // Retail numbers profile directories (0001); the native client names its
    // first one "default". Any directory under Profiles/ is a profile.
    for(const auto& entry:fs::directory_iterator(support/"Profiles"))
        if(entry.is_directory()&&!entry.is_symlink())return true;
    return false;
}
fs::path currentGeneration(const fs::path& support) {
    noLinks(support/"Setup/Current.txt");
    auto receipt=read(support/"Setup/Current.txt");auto end=receipt.find('\n');
    require(end!=std::string::npos,"Invalid generation receipt");
    auto relative=receipt.substr(0,end);const std::string prefix="GameData/1.50-";
    require(relative.rfind(prefix,0)==0&&relative.size()>prefix.size()&&
            relative.find_first_not_of("0123456789",prefix.size())==std::string::npos,"Invalid generation receipt");
    auto root=support/relative;noLinks(root);require(fs::is_directory(root),"Missing game data generation");return root;
}
DiscEdition currentEdition(const fs::path& support) {
    // v1 receipts predate edition support and are the verified base-game edition.
    noLinks(support/"Setup/Current.txt");
    auto current=read(support/"Setup/Current.txt");auto start=current.find('\n');
    if(start==std::string::npos||start+1==current.size())return DiscEdition::BF2;
    auto end=current.find('\n',start+1);auto receipt=current.substr(start+1,end-start-1);
    require(receipt.rfind("Receipt-",0)==0&&receipt.size()>12&&receipt.substr(receipt.size()-4)==".txt"&&receipt.substr(8,receipt.size()-12).find_first_not_of("0123456789")==std::string::npos,"Invalid edition receipt");
    auto path=support/"Setup"/receipt;noLinks(path);
    auto metadata=read(path);
    return ("\n"+metadata).find("\nedition=deluxe\n")!=std::string::npos?DiscEdition::Deluxe:DiscEdition::BF2;
}
bool storedKey(const fs::path& support,bool sf) {
    noLinks(support/"Registry.con");
    return fs::exists(support/"Registry.con")&&registryHasKey(read(support/"Registry.con"),sf);
}
DiscEdition validateDisc(const fs::path& path) {return detectDisc(path);}
void validatePatches(const fs::path& a,const fs::path& b,const Control& control) {
    verifyPatch(View(a),PatchSlot::Patch141,control);
    verifyPatch(View(b),PatchSlot::Patch150,control);
}
bool clientPresent(const fs::path& contents) {
    for(auto name:{"bf2_client","libdice_py.dylib","libaidll.dylib"}){
        auto path=contents/"MacOS"/name;
        if(fs::is_symlink(fs::symlink_status(path))||!fs::is_regular_file(path))return false;
    }
    return ::access((contents/"MacOS/bf2_client").c_str(),X_OK)==0;
}
Launch clientLaunch(const fs::path& contents,const fs::path& support) {
    require(clientPresent(contents),"The native client or its libraries are missing from this build.");
    require(storedKey(support),"Enter your CD key before playing.");
    if(currentEdition(support)==DiscEdition::Deluxe)require(storedKey(support,true),"Enter your Special Forces CD key before playing.");
    return {contents/"MacOS/bf2_client",currentGeneration(support),support,{"+modPath","mods/bf2"}};
}
std::vector<WarmupRun> warmupRuns(const fs::path& support) {
    // strike_at_karkand covers the bf2 and xpack day shader set; devils_perch the night set (xpack content).
    std::vector<WarmupRun> runs{{"Battlefield 2 maps","strike_at_karkand","mods/bf2"}};
    if(currentEdition(support)==DiscEdition::Deluxe)runs.push_back({"Special Forces night maps","devils_perch","mods/xpack"});
    return runs;
}
std::string defaultProfileName(const fs::path& support) {
    auto file=support/"Profiles/global.con";noLinks(file);
    if(!fs::is_regular_file(file))return {};
    std::istringstream in(read(file));std::string row;
    while(std::getline(in,row)){
        const std::string command="GlobalSettings.setDefaultUser";
        if(row.rfind(command,0)!=0)continue;
        auto value=trim(row.substr(command.size()));
        if(value.size()>=2&&value.front()=='"'&&value.back()=='"')value=value.substr(1,value.size()-2);
        if(value.find_first_of("\r\n\"")!=std::string::npos)return {};
        return value;
    }
    return {};
}
std::vector<std::string> warmupArguments(const WarmupRun& run,const std::string& profile) {
    require(!run.level.empty()&&run.level.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,"Invalid warm-up level");
    std::vector<std::string> arguments{"+modPath",run.mod,"+warmupLevel",run.level};
    if(!profile.empty()){arguments.push_back("+playerName");arguments.push_back(profile);}
    return arguments;
}
Launch warmupLaunch(const fs::path& contents,const fs::path& support,const WarmupRun& run) {
    auto launch=clientLaunch(contents,support);
    if(run.mod=="mods/xpack")require(fs::is_directory(launch.directory/"mods/xpack"),"Special Forces content is not installed.");
    launch.arguments=warmupArguments(run,defaultProfileName(support));
    return launch;
}
bool parseWarmupLine(const std::string& line,WarmupProgress& progress) {
    const std::string prefix="warmup: ";
    if(line.rfind(prefix,0)!=0)return false;
    auto rest=line.substr(prefix.size());
    if(rest.rfind("effects ",0)==0){
        unsigned finished=0,started=0;char slash=0;std::istringstream in(rest.substr(8));
        if(in>>finished>>slash>>started&&slash=='/'&&finished<=started){progress.finished=finished;progress.started=started;}
        return true;
    }
    if(rest.rfind("done ",0)==0||rest=="done"){
        unsigned finished=0,started=0;char slash=0;std::string word;std::istringstream in(rest);
        if(in>>word>>word>>finished>>slash>>started&&slash=='/'&&finished<=started){progress.finished=finished;progress.started=started;}
        progress.stage="done";return true;
    }
    // Stage words only: "level loaded 61.2 s" -> "level loaded", "start strike_at_karkand" -> "start".
    std::istringstream in(rest);std::string first,second;in>>first;
    static const char* twoWord[]={"level","player","join","world","failed"};
    for(auto word:twoWord)if(first==word&&in>>second){first+=" "+second;break;}
    progress.stage=first;
    return true;
}
std::string warmupOutcome(int status) {
    switch(status){
        case 0:return "";
        case 2:return "The game could not start the level.";
        case 3:return "Timed out before all shaders were built.";
        case 4:return "Finished, but some effects could not be built.";
        default:return "The game stopped with status "+std::to_string(status)+".";
    }
}
}
