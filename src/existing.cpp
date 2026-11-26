#include "existing.h"
#include "digest.h"
#include "install.h"
#include "links.h"
#include "media.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace setup {
namespace {
void check(bool ok, const std::string& error) {if(!ok)throw std::runtime_error(error);}
bool begins(const std::string& text, const std::string& prefix) {return text.compare(0,prefix.size(),prefix)==0;}

// The entry of `directory` named `name` in any case, as it is spelled on disk; nothing if there is none.
// Two entries which differ only in case cannot be told apart, and the game refuses them as well.
std::optional<fs::path> spelled(const fs::path& directory, const std::string& name) {
    std::optional<fs::path> found;std::error_code error;
    for(const auto& entry:fs::directory_iterator(directory,error)) {
        if(lower(entry.path().filename().string())!=lower(name))continue;
        check(!found,"Two entries differ only in case: "+(directory/name).string());
        found=entry.path();
    }
    return found;
}

// A manifest path (lower case) below a generation, following its links. Like the Linux client
// (Platform/Linux/ClientFilePaths.h), a component which exists as written is taken as it is; any other
// is looked up in any case. Directories are looked up once.
class Resolver {
    fs::path root_;
    std::map<std::string,std::optional<fs::path>> directories_;
public:
    explicit Resolver(fs::path root):root_(std::move(root)) {}
    std::optional<fs::path> find(const std::string& relative) {
        auto slash=relative.rfind('/');
        std::optional<fs::path> parent=root_;
        if(slash!=std::string::npos)parent=directory(relative.substr(0,slash));
        if(!parent)return std::nullopt;
        return component(*parent,slash==std::string::npos?relative:relative.substr(slash+1));
    }
private:
    std::optional<fs::path> directory(const std::string& relative) {
        auto key=lower(relative);
        if(auto known=directories_.find(key);known!=directories_.end())return known->second;
        auto slash=relative.rfind('/');
        std::optional<fs::path> parent=root_;
        if(slash!=std::string::npos)parent=directory(relative.substr(0,slash));
        std::optional<fs::path> result;
        if(parent)result=component(*parent,slash==std::string::npos?relative:relative.substr(slash+1));
        std::error_code error;
        if(result&&!fs::is_directory(*result,error))result.reset();
        return directories_[key]=result;
    }
    static std::optional<fs::path> component(const fs::path& parent,const std::string& name) {
        std::error_code error;
        auto exact=parent/name;
        if(fs::exists(fs::symlink_status(exact,error)))return exact;
        return spelled(parent,name);
    }
};

// A regular file, also behind a link (a link to a file in the folder is the user's business).
bool presentFile(const std::optional<fs::path>& path) {
    std::error_code error;
    return path&&fs::is_regular_file(*path,error)&&!error;
}

int64_t modified(const fs::path& path) {return int64_t(fs::last_write_time(path).time_since_epoch().count());}

// Setup/ and GameData/ of the support root: created for the user only, never through a link.
void managedDirectory(const fs::path& directory) {
    fs::path current;
    for(const auto& part:fs::absolute(directory).lexically_normal()) {
        current/=part;
        check(!is_link(fs::symlink_status(current)),"Symlink in OpenBF2's data folder: "+current.string());
        if(!fs::exists(current)){fs::create_directory(current);fs::permissions(current,fs::perms::owner_all);}
        check(fs::is_directory(current),"Not a directory: "+current.string());
    }
}

std::string listed(const std::vector<std::string>& names) {
    std::string text;
    for(size_t i=0;i<names.size()&&i<8;++i)text+="\n  "+names[i];
    if(names.size()>8)text+="\n  … and "+std::to_string(names.size()-8)+" more";
    return text;
}
}

LinkedRole linkedRole(const std::string& manifestPath) {
    auto path=lower(manifestPath);
    if(path=="pylib-2.3.4.zip"||begins(path,"python/")||begins(path,"admin/"))return LinkedRole::script;
    // written by some installers for their language; the Deluxe one has none
    if(path=="mods/bf2/locale")return LinkedRole::localization;
    if(!begins(path,"mods/"))return LinkedRole::unused;
    auto slash=path.find('/',5);
    if(slash==std::string::npos)return LinkedRole::unused;
    auto rest=path.substr(slash+1);
    if(begins(rest,"settings/"))return LinkedRole::settings;
    if(begins(rest,"python/"))return LinkedRole::script;
    if(begins(rest,"localization/"))return LinkedRole::localization;
    return LinkedRole::asset;
}

DiscEdition detectExisting(const fs::path& folder) {
    std::error_code error;
    check(fs::is_directory(folder,error),"Choose the folder of an installed Battlefield 2.");
    auto mods=spelled(folder,"mods");
    auto bf2=mods?spelled(*mods,"bf2"):std::nullopt;
    auto description=bf2?spelled(*bf2,"mod.desc"):std::nullopt;
    check(presentFile(description),"This folder is not a Battlefield 2 installation: it has no mods/bf2/mod.desc. "
        "Choose the folder which holds mods and python (on Steam: Manage, Browse local files).");
    // Special Forces brings its levels; a regular 1.50 has a stub of mods/xpack without any
    auto xpack=spelled(*mods,"xpack");
    auto levels=xpack?spelled(*xpack,"levels"):std::nullopt;
    if(levels)
        for(const auto& entry:fs::directory_iterator(*levels,error))
            if(entry.is_directory(error))return DiscEdition::Complete;
    return DiscEdition::BF2;
}

void verifyLinked(const fs::path& game,const fs::path& manifest,const Control& control,LinkedReport* report) {
    LinkedReport local;auto& out=report?*report:local;out={};
    struct Job {ManifestEntry entry;fs::path path;LinkedRole role;};
    std::vector<Job> jobs;std::vector<std::string> missing,differing;uint64_t total=0;
    Resolver resolver(game);
    for(const auto& e:readContentManifest(manifest)) {
        auto role=linkedRole(e.path);
        if(role==LinkedRole::unused)continue;
        auto path=resolver.find(e.path);
        if(!presentFile(path)) {
            if(role==LinkedRole::asset||role==LinkedRole::settings)missing.push_back(e.path+" (missing)");
            else if(role==LinkedRole::localization)++out.missingLocalization;
            else out.scripts.push_back(e.path);
            continue;
        }
        if(role==LinkedRole::settings)continue;
        // View refuses links: the file itself, wherever the links lead
        jobs.push_back({e,fs::canonical(*path),role});total+=e.size;
    }
    Progress progress{"verify","",0,total,0,jobs.size()};
    std::atomic<size_t> next{0};std::mutex mutex;std::exception_ptr error;
    auto work=[&] {
        try {
            for(;;) {
                size_t index=next.fetch_add(1);if(index>=jobs.size())return;
                if(control.cancelled&&control.cancelled->load())throw std::runtime_error("Cancelled");
                const auto& job=jobs[index];
                View view(job.path);Digest digest;
                view.stream([&](const uint8_t* bytes,size_t n){
                    if(control.cancelled&&control.cancelled->load())throw std::runtime_error("Cancelled");
                    digest.update(bytes,n);
                });
                bool same=view.size()==job.entry.size&&digest.finish()==job.entry.hash;
                std::lock_guard<std::mutex> lock(mutex);
                if(!same) {
                    if(job.role==LinkedRole::script)out.scripts.push_back(job.entry.path);
                    else differing.push_back(job.entry.path);
                }
                ++progress.completedFiles;progress.completedBytes+=job.entry.size;progress.path=job.entry.path;
                if(control.progress)control.progress(progress);
            }
        } catch(...) {std::lock_guard<std::mutex> lock(mutex);if(!error)error=std::current_exception();}
    };
    std::vector<std::thread> workers;
    for(unsigned i=0;i<std::clamp(control.workers,1u,8u);++i)workers.emplace_back(work);
    for(auto& worker:workers)worker.join();
    if(error)std::rethrow_exception(error);
    std::sort(out.scripts.begin(),out.scripts.end());
    std::sort(differing.begin(),differing.end());
    for(const auto& name:differing)missing.push_back(name+" (differs)");
    check(missing.empty(),std::to_string(missing.size())+(missing.size()==1?" game file is missing or differs":" game files are missing or differ")+
        " from Battlefield 2 1.50:"+listed(missing));
}

std::string linkedStamp(const fs::path& game,const fs::path& manifest) {
    std::string text;Resolver resolver(game);
    for(const auto& e:readContentManifest(manifest)) {
        if(linkedRole(e.path)==LinkedRole::unused)continue;
        auto path=resolver.find(e.path);
        if(!presentFile(path))continue;
        text+=e.path+"\t"+std::to_string(fs::file_size(*path))+"\t"+std::to_string(modified(*path))+"\n";
    }
    return text;
}

bool linkedStampMatches(const fs::path& game,const fs::path& manifest,const std::string& stamp) {
    try {
        std::map<std::string,std::pair<uint64_t,int64_t>> recorded;std::istringstream lines(stamp);std::string line;
        while(std::getline(lines,line)) {
            std::istringstream row(line);std::string path,size,time;
            if(!std::getline(row,path,'\t')||!std::getline(row,size,'\t')||!std::getline(row,time))return false;
            recorded[lower(path)]={std::stoull(size),std::stoll(time)};
        }
        Resolver resolver(game);
        for(const auto& e:readContentManifest(manifest)) {
            auto role=linkedRole(e.path);
            if(role==LinkedRole::unused)continue;
            auto path=resolver.find(e.path);auto present=presentFile(path);
            auto found=recorded.find(lower(e.path));
            if(found==recorded.end()) {
                // only a language or a script which was not there may be missing: one which appeared since is checked first
                if(role==LinkedRole::asset||role==LinkedRole::settings||present)return false;
                continue;
            }
            if(!present)return false;
            if(role==LinkedRole::settings)continue;
            if(fs::file_size(*path)!=found->second.first||modified(*path)!=found->second.second)return false;
            if(role==LinkedRole::asset&&found->second.first!=e.size)return false;
        }
        return true;
    } catch(const std::exception&) {return false;}
}

bool leadsIntoFolder(const fs::path& link,const fs::path& folder) {
    std::error_code error;
    auto target=link_target(link,error);
    if(error||!target.is_absolute())return false;
    auto mods=spelled(folder,"mods");
    return mods&&target.parent_path().lexically_normal()==mods->lexically_normal()&&target.filename()==link.filename();
}

void linkFolderMods(const fs::path& game,const fs::path& folder) {
    auto mods=spelled(folder,"mods");
    if(!mods)return;
    std::error_code error;
    fs::create_directory(game/"mods",error);
    std::set<std::string> taken;
    for(const auto& entry:fs::directory_iterator(game/"mods",error)) {
        // a mod which left the folder would only be a dead entry in the game's list of mods
        if(is_link(entry)&&leadsIntoFolder(entry.path(),folder)&&!fs::is_directory(entry.path(),error))
            fs::remove(entry.path(),error);
        else
            taken.insert(lower(entry.path().filename().string()));
    }
    for(const auto& entry:fs::directory_iterator(*mods,error)) {
        auto name=entry.path().filename().string();
        if(name.empty()||name[0]=='.'||!entry.is_directory(error))continue;
        if(taken.insert(lower(name)).second)link_directory(entry.path(),game/"mods"/name,error);
    }
}

void removeGeneration(const fs::path& game) {
    for(const auto& entry:fs::directory_iterator(game)) {
        if(is_link(entry))fs::remove(entry.path());
        else if(entry.is_directory())removeGeneration(entry.path());
        else fs::remove(entry.path());
    }
    fs::remove(game);
}

fs::path linkExisting(const LinkOptions& o,Platform& platform,LinkedReport* report) {
    auto folder=fs::canonical(o.folder);
    auto support=fs::absolute(o.supportRoot).lexically_normal();
    auto inside=[](const fs::path& a,const fs::path& b){auto r=a.lexically_relative(b);return !r.empty()&&!r.is_absolute()&&*r.begin()!="..";};
    check(!inside(folder,support)&&!inside(support,folder)&&folder!=support,"The installation cannot be inside OpenBF2's own data folder.");
    auto detected=detectExisting(folder);
    check(!hasSpecialForces(o.edition)||hasSpecialForces(detected),"This installation has no Special Forces.");
#if !defined(_WIN32)
    // the game saves its settings into mods/<mod>/settings/ of the folder, as Battlefield 2 does there
    auto bf2=*spelled(*spelled(folder,"mods"),"bf2");
    auto settings=spelled(bf2,"settings");
    check(::access((settings?*settings:bf2).c_str(),W_OK)==0,"OpenBF2 cannot write to this installation (a read-only drive or folder?). "
        "The game saves its settings there, as Battlefield 2 does.");
#endif
    managedDirectory(support/"Setup");managedDirectory(support/"GameData");
    auto lock=platform.lock(support/"Setup/Install.lock");
    auto token=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    auto generation=support/"GameData"/("1.50-"+token);
    check(!fs::exists(fs::symlink_status(generation)),"The game data folder exists already: "+generation.string());
    fs::create_directory(generation);fs::permissions(generation,fs::perms::owner_all);
    try {
        linkFolderMods(generation,folder);
        check(presentFile(Resolver(generation).find("mods/bf2/mod.desc")),"The installation's mods could not be linked into "+generation.string());
        // the Python search path of the game (pylib-2.3.4.zip, python, admin), spelled as in the folder
        for(const char* name:{"python","admin"}) {
            auto directory=spelled(folder,name);std::error_code error;
            if(!directory||!fs::is_directory(*directory,error))continue;
            link_directory(*directory,generation/directory->filename(),error);
            check(!error,"Cannot link "+directory->string()+": "+error.message());
        }
        if(auto zip=spelled(folder,"pylib-2.3.4.zip");presentFile(zip)) {
            auto target=generation/zip->filename();std::error_code error;
#if defined(_WIN32)
            // a symbolic link to a file needs developer mode there; a hard link only the same volume
            fs::create_hard_link(*zip,target,error);
            if(error)fs::copy_file(*zip,target);
#else
            fs::create_symlink(*zip,target,error);
            check(!error,"Cannot link "+zip->string()+": "+error.message());
#endif
        }
        verifyLinked(generation,o.manifest,o.control,report);
        auto source=folder.generic_u8string();
        auto receipt=std::string("openbf2-setup-linked-v1\n")+"source="+std::string(source.begin(),source.end())+"\n"+sha256(View(o.manifest))+"\n";
        if(o.edition==DiscEdition::Complete)receipt+="edition=complete\n";
        else if(o.edition==DiscEdition::CompleteBase)receipt+="edition=complete-base\n";
        receipt+="generation=GameData/"+generation.filename().string()+"\n";
        auto receiptPath=support/"Setup"/("Receipt-"+token+".txt");
        writeTextFile(receiptPath,receipt,platform);
        auto pending=support/"Setup/Current.pending";
        writeTextFile(pending,generation.lexically_relative(support).generic_string()+"\n"+receiptPath.filename().string()+"\n",platform);
        platform.replace(pending,support/"Setup/Current.txt"); // commit point
    } catch(...) {
        try {removeGeneration(generation);} catch(...) {}
        throw;
    }
    return generation;
}
}
