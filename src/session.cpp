#include "links.h"
#include "session.h"
#include "key_check.h"
#include "install.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <sstream>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dpapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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
        current/=part;require(!is_link(fs::symlink_status(current)),"Symlink in setup path");
    }
}
std::string read(const fs::path& path) {
    auto bytes=View(path).all();return {bytes.begin(),bytes.end()};
}
// Exclusive creation with 0600 avoids a plaintext window or stale-file clobber;
// the file is complete and flushed before it replaces the destination.
#if defined(_WIN32)
// Windows: exclusive creation in the user's own folder (its default ACL keeps other users out), written
// in full and flushed before it replaces the destination.
void writePrivately(const fs::path& pending,const fs::path& file,const std::string& text,Platform& platform,const char* what) {
    HANDLE handle=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(handle!=INVALID_HANDLE_VALUE,(std::string("Cannot create private ")+pending.filename().string()+"; check permissions or a stale pending file.").c_str());
    try {
        size_t offset=0;
        while(offset<text.size()){
            DWORD written=0;
            require(WriteFile(handle,text.data()+offset,DWORD(text.size()-offset),&written,nullptr)&&written>0,(std::string("Cannot save the ")+what).c_str());
            offset+=written;
        }
        require(FlushFileBuffers(handle),(std::string("Cannot flush the ")+what).c_str());
        auto closed=CloseHandle(handle);handle=INVALID_HANDLE_VALUE;require(closed,(std::string("Cannot close the ")+what+" file").c_str());
        platform.replace(pending,file);
    } catch(...) {if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);std::error_code ignored;fs::remove(pending,ignored);throw;}
}
// Registry.dat: the Registry.con text protected for the current user with DPAPI (no entropy, no UI),
// as the Windows client reads it (OpenBF2 docs/registry-file.md). At most 64 KiB.
std::string protectForUser(const std::string& text) {
    DATA_BLOB in{DWORD(text.size()),reinterpret_cast<BYTE*>(const_cast<char*>(text.data()))},out{};
    require(CryptProtectData(&in,L"OpenBF2 CD keys",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out),"Cannot protect the CD keys for this user.");
    std::string blob(reinterpret_cast<const char*>(out.pbData),out.cbData);LocalFree(out.pbData);
    require(blob.size()<=64*1024,"The CD key file would be too large.");
    return blob;
}
std::string unprotectForUser(const std::string& blob) {
    DATA_BLOB in{DWORD(blob.size()),reinterpret_cast<BYTE*>(const_cast<char*>(blob.data()))},out{};
    require(blob.size()<=64*1024&&CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out),"The CD key file cannot be read by this user.");
    std::string text(reinterpret_cast<const char*>(out.pbData),out.cbData);LocalFree(out.pbData);
    while(!text.empty()&&text.back()=='\0')text.pop_back();
    if(text.rfind("\xEF\xBB\xBF",0)==0)text.erase(0,3);
    return text;
}
const char* keyFileName="Registry.dat";
const char* keyPendingName="Registry.dat.pending";
std::string readKeyFile(const fs::path& file) {return unprotectForUser(read(file));}
std::string keyFileText(const std::string& text) {return protectForUser(text);}
#else
const char* keyFileName="Registry.con";
const char* keyPendingName="Registry.pending";
std::string readKeyFile(const fs::path& file) {return read(file);}
std::string keyFileText(const std::string& text) {return text;}
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
#endif
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
    noLinks(support/"Setup");noLinks(support/keyFileName);
    auto lock=platform.lock(support/"Setup/Install.lock");
#if !defined(_WIN32)
    noLinks(support/"Profiles");
    fs::create_directories(support/"Profiles");fs::permissions(support/"Profiles",fs::perms::owner_all);
#endif
    auto file=support/keyFileName;
    auto updated=registryWithKey(fs::exists(file)?readKeyFile(file):"",normalized);
    if(!sfNormalized.empty())updated=registryWithKey(updated,sfNormalized,true);
    std::error_code stale;fs::remove(support/keyPendingName,stale);
    writePrivately(support/keyPendingName,file,keyFileText(updated),platform,"key");
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
void saveVoiceSettings(const fs::path& support,const VoiceSettings& settings,Platform& platform,const fs::path& profileHome) {
    auto text=voiceSettingsText(settings);
    auto home=profileHome.empty()?support:profileHome;
    noLinks(support/"Setup");noLinks(home/"Profiles");
    auto lock=platform.lock(support/"Setup/Install.lock");
    fs::create_directories(home/"Profiles");
#if !defined(_WIN32)
    fs::permissions(home/"Profiles",fs::perms::owner_all);
#endif
    writePrivately(home/"Profiles/BattleCommoDefaultUserSettings.pending",home/"Profiles/BattleCommoDefaultUserSettings.con",text,platform,"voice settings");
}
bool profilesExist(const fs::path& support) {
    noLinks(support/"Profiles");
    if(!fs::is_directory(support/"Profiles"))return false;
    // Retail numbers profile directories (0001); the native client names its
    // first one "default". Any directory under Profiles/ is a profile.
    for(const auto& entry:fs::directory_iterator(support/"Profiles"))
        if(entry.is_directory()&&!is_link(entry))return true;
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
    auto lines="\n"+metadata;
    if(lines.find("\nedition=deluxe\n")!=std::string::npos)return DiscEdition::Deluxe;
    if(lines.find("\nedition=deluxe-base\n")!=std::string::npos)return DiscEdition::DeluxeBase;
    if(lines.find("\nedition=complete\n")!=std::string::npos)return DiscEdition::Complete;
    if(lines.find("\nedition=complete-base\n")!=std::string::npos)return DiscEdition::CompleteBase;
    return DiscEdition::BF2;
}
bool storedKey(const fs::path& support,bool sf) {
    noLinks(support/keyFileName);
    if(!fs::exists(support/keyFileName))return false;
    try {return registryHasKey(readKeyFile(support/keyFileName),sf);}
    catch(const std::exception&) {return false;}
}
bool clientPresent(const ClientLayout& layout) {
    if(!layout.executable.is_absolute())return false;
    auto files=layout.required;
    files.push_back(layout.executable);
    for(const auto& path:files){
        std::error_code error;
        if(is_link(fs::symlink_status(path,error))||error||!fs::is_regular_file(path,error)||error)return false;
    }
#if defined(_WIN32)
    return true;
#else
    return ::access(layout.executable.c_str(),X_OK)==0;
#endif
}
Launch clientLaunch(const ClientLayout& layout,const fs::path& support,const LaunchOptions& options) {
    require(clientPresent(layout),"The native client or its libraries are missing from this build.");
    require(storedKey(support),"Enter your CD key before playing.");
    if(hasSpecialForces(currentEdition(support)))require(storedKey(support,true),"Enter your Special Forces CD key before playing.");
    auto generation=currentGeneration(support);
    auto arguments=launch_arguments(options);
    require(!arguments.empty(),"The command line is empty.");
    auto mod=argument_mod(arguments);
    if(!mod.empty()||options.override.empty()) {
        require(!mod.empty()&&mod.find_first_of("/\\")==std::string::npos&&mod!="."&&mod!=".."&&safePath(mod)==mod,"The selected mod has an invalid name.");
        // A mod of the user is a link into the mod library; any other link is refused.
        auto directory=generation/"mods"/mod;std::error_code unreadable;
        require(fs::is_directory(directory)&&(!is_link(fs::symlink_status(directory))||link_target(directory,unreadable)==mod_library(support)/mod),"The selected mod is not installed.");
    }
    // a recording is played through the .con which names it
    if(options.override.empty()&&demo_recording(options.demo)&&!options.demo_config.empty()) {
        auto platform=nativePlatform();
        noLinks(options.demo_config);
        writeTextFile(options.demo_config,demo_config_text(options.demo),*platform);
    }
    return {layout.executable,generation,support,arguments};
}
Launch clientLaunch(const fs::path& contents,const fs::path& support,const std::string& mod) {
    LaunchOptions options;options.mod=mod;options.skip_movies=false;options.fullscreen=false;
    auto launch=clientLaunch(contents,support,options);
    launch.arguments={"+modPath","mods/"+mod};
    return launch;
}
LaunchOptions loadLaunch(const fs::path& support) {
    auto options=LaunchOptions{};
    try {
        noLinks(support/"Setup/Launch.txt");
        options=parse_launch(read(support/"Setup/Launch.txt"));
    } catch(const std::exception&) {}
    if(options.online=="openspy")
        options.provider=parse_provider(provider_presets().front().json);
    else if(provider_online(options.online))
        if(auto provider=storedProvider(support,options.online))options.provider=*provider;
    options.demo_config=(support/"Setup/Demo.con").string();
    return options;
}
fs::path providerFile(const fs::path& support,const std::string& online) {
    require(provider_online(online),"Only BF2Hub and another provider keep a provider file.");
    return support/"Setup"/(online=="bf2hub"?"Provider-BF2Hub.json":"Provider-Other.json");
}
ProviderServices keepProvider(const fs::path& support,const std::string& online,const std::string& json,Platform& platform) {
    auto file=providerFile(support,online);
    auto services=parse_provider(json);
    auto pending=file;pending.replace_extension(".pending");
    noLinks(file);noLinks(pending);
    auto lock=platform.lock(support/"Setup/Install.lock");
    std::error_code stale;fs::remove(pending,stale);
    writePrivately(pending,file,json,platform,"provider file");
    return services;
}
ProviderServices importProvider(const fs::path& support,const std::string& online,const fs::path& file,Platform& platform) {
    std::error_code error;
    require(fs::is_regular_file(file,error),"The provider file cannot be read: it is not a file.");
    auto size=fs::file_size(file,error);
    require(!error&&size<=provider_file_limit,"The file is larger than a provider file can be.");
    return keepProvider(support,online,read(file),platform);
}
std::vector<fs::path> findDemos(const fs::path& game) {
    auto demos=std::vector<std::pair<fs::file_time_type,fs::path>>{};
    std::error_code error;
    for(const auto& folder:fs::directory_iterator(game,error)) {
        auto name=folder.path().filename().string();
        std::ranges::transform(name,name.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(name!="demos"||is_link(folder)||!folder.is_directory())continue;
        for(const auto& entry:fs::directory_iterator(folder.path(),error)) {
            auto extension=entry.path().extension().string();
            std::ranges::transform(extension,extension.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(is_link(entry)||!entry.is_regular_file()||(extension!=".bf2demo"&&extension!=".con"))continue;
            demos.emplace_back(entry.last_write_time(error),folder.path().filename()/entry.path().filename());
        }
    }
    std::ranges::sort(demos,[](const auto& a,const auto& b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
    auto result=std::vector<fs::path>{};
    for(auto& demo:demos)result.push_back(std::move(demo.second));
    return result;
}
std::optional<ProviderServices> storedProvider(const fs::path& support,const std::string& online) {
    try {
        auto file=providerFile(support,online);
        noLinks(file);
        if(!fs::is_regular_file(file)||fs::file_size(file)>provider_file_limit)return std::nullopt;
        return parse_provider(read(file));
    } catch(const std::exception&) {}
    return std::nullopt;
}
void saveLaunch(const fs::path& support,const LaunchOptions& options,Platform& platform) {
    require(!options.mod.empty()&&options.mod.find_first_of("/\\\r\n")==std::string::npos,"The selected mod has an invalid name.");
    noLinks(support/"Setup/Launch.txt");noLinks(support/"Setup/Launch.pending");
    auto lock=platform.lock(support/"Setup/Install.lock");
    std::error_code stale;fs::remove(support/"Setup/Launch.pending",stale);
    writePrivately(support/"Setup/Launch.pending",support/"Setup/Launch.txt",launch_text(options),platform,"launch options");
}
}
