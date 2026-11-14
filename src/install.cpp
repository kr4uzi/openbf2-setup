// [CHOSEN, not recovered] Setup policy for the owner-requested macOS port.
#include "install.h"
#include "cabinet.h"
#include "edition.h"
#include "patch.h"
#include "rtp_bridge.h"
#include "digest.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <chrono>
#include <mutex>
#include <thread>
#include <sstream>
#include <future>
#include <stdexcept>


namespace setup {

namespace {
void check(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
bool begins(const std::string& text, const std::string& prefix) { return text.compare(0, prefix.size(), prefix) == 0; }

// [INFERRED] English feature/group destinations follow the probe's manifest.
// This is deliberately not an InstallScript interpreter or an optional-product installer.
std::string destination(bool disc, const std::string& group, const std::string& path) {
    if (group.empty() || group.front() == '<' || path.empty()) return {};
    std::string root;
    if (disc) return discDestination(group,path);
    {
        static const std::map<std::string, std::string> fixed{
            {"Admin","Admin/standard_admin"}, {"Root - Binaries",""}, {"Root - PunkBuster","pb"},
            {"Mods - bf2","mods/bf2"}, {"Mods - bf2 - level - md5s","mods/bf2/Levels"},
            {"Mods - bf2 - localization +","mods/bf2/Localization"},
            {"Mods - bf2 - menu - external - flashmenu - cursor","mods/bf2/menu/External/FlashMenu/Cursor"},
            {"Mods - bf2 - python - game","mods/bf2/python/game"},
            {"Mods - bf2 - settings - controls","mods/bf2/Settings"}, {"Mods - xpack","mods/xpack"},
            {"Mods - xpack - python - game","mods/xpack/python/game"},
            {"Mods - xpack - objects - weapons +","mods/xpack/Objects/Weapons/Handheld"},
            {"Mods - xpack - settings","mods/xpack/Settings"}, {"Python - bf2 - stats","python/bf2/stats"},
            {"Profiles (Falcon)","Profiles"}, {"Redist","Redist"}, {"Support","Support"}, {"SupportXP1","SupportXP1"}};
        auto it = fixed.find(group);
        if (it != fixed.end()) root = it->second;
        else if (begins(group, "Mods - bf2 - levels - ")) {
            auto rest = group.substr(22);
            if (begins(rest, "af - ") || begins(rest, "ef - ")) rest.erase(0, 5);
            auto info = rest.find(" - info");
            if (info != std::string::npos) rest.replace(info, 7, "/Info");
            root = "mods/bf2/Levels/" + rest;
        } else if (begins(group, "Support - ")) {
            if (group != "Support - English") return {};
            root = "Support";
        } else throw std::runtime_error("unmapped patch group: " + group);
    }
    return safePath(root.empty() ? path : root + "/" + path);
}

std::string digest(const View& source, bool sha) {
    Digest hash(sha);
    source.stream([&](const uint8_t* data, size_t n) { hash.update(data,n); });
    return hash.finish();
}

// One serialized journal writer; worker callbacks are serialized too. Front ends
// must dispatch UI updates to their main thread and never throw from callbacks.
struct Run {
    Control control;
    Platform* platform = nullptr;
    fs::path transaction;
    std::string phase;
    std::mutex mutex;
    Progress progress;
    std::map<std::string,std::string> completed;
    void cancel() const {
        if(control.cancelled && control.cancelled->load()) throw std::runtime_error("Cancelled; setup can resume");
    }
    void begin(const std::string& name,size_t files,uint64_t bytes) {
        cancel();phase=name;progress={name,"",0,bytes,0,files};completed.clear();
        std::ifstream in(transaction/(phase+".journal"));std::string line;
        while(std::getline(in,line)) {auto tab=line.find('\t');if(tab!=std::string::npos)completed[line.substr(0,tab)]=line.substr(tab+1);}
        notify();
    }
    void notify() {if(control.progress)control.progress(progress);}
    bool done(const std::string& relative,const fs::path& path) {
        cancel();auto i=completed.find(lower(relative));if(i==completed.end())return false;
        return i->second=="absent" ? !fs::exists(path) : fs::is_regular_file(path) && digest(View(path),true)==i->second;
    }
    void bytes(size_t n,const std::string& path,uint64_t itemDone=0,uint64_t itemTotal=0) {
        cancel();std::lock_guard<std::mutex> guard(mutex);progress.completedBytes+=n;progress.path=path;
        progress.itemCompletedBytes=itemDone;progress.itemTotalBytes=itemTotal;
        // Bound callback volume to about one update per MiB, plus every file and finished item.
        if(progress.completedBytes/(1024*1024)!=(progress.completedBytes-n)/(1024*1024)||(itemTotal&&itemDone==itemTotal))notify();
    }
    void skip(const std::string& relative) {
        std::lock_guard<std::mutex> guard(mutex);++progress.completedFiles;progress.path=relative;notify();
    }
    void record(const std::string& relative,const fs::path& path,bool absent=false) {
        auto hash=absent?"absent":digest(View(path),true);
        if(!absent && platform) {platform->syncFile(path);platform->syncDirectory(path.parent_path());}
        std::lock_guard<std::mutex> guard(mutex);
        std::ofstream out(transaction/(phase+".journal"),std::ios::app);
        out<<lower(relative)<<'\t'<<hash<<'\n';out.close();check(bool(out),"journal write failed");
        if(platform)platform->syncFile(transaction/(phase+".journal"));
        ++progress.completedFiles;progress.path=relative;notify();
    }
    bool phaseDone() const {return fs::is_regular_file(transaction/(phase+".done"));}
    void finish() {
        cancel();auto path=transaction/(phase+".done");std::ofstream out(path);out<<"complete\n";out.close();check(bool(out),"checkpoint write failed");
        if(platform){platform->syncFile(path);platform->syncDirectory(transaction);}
    }
};

// Only private, fresh staging trees are writable. Refuse symlink components even
// on case-sensitive APFS; preserve existing spelling for case-insensitive overlays.
fs::path stagingPath(const fs::path& root, const std::string& relative) {
    fs::path current = root;
    for (const auto& part : fs::path(safePath(relative))) {
        fs::path next = current / part;
        if (fs::exists(current)) {
            check(fs::is_directory(current) && !fs::is_symlink(fs::symlink_status(current)), "unsafe output directory");
            unsigned count = 0;
            for (const auto& item : fs::directory_iterator(current)) {
                if (lower(item.path().filename().string()) == lower(part.string())) { next = item.path(); ++count; }
            }
            check(count <= 1, "ambiguous case in output");
        }
        check(!fs::is_symlink(fs::symlink_status(next)), "symlink in output path");
        current = next;
    }
    return current;
}

void writeFile(const fs::path& path, const std::function<void(const Sink&)>& extract) {
    fs::create_directories(path.parent_path());
    fs::path temp = path; temp += ".openbf2-part";
    check(!fs::is_symlink(fs::symlink_status(temp)), "unsafe temporary output");
    if(fs::exists(temp)) {check(fs::is_regular_file(temp), "invalid temporary output");fs::remove(temp);}
    std::ofstream stream(temp, std::ios::binary);
    check(bool(stream), "cannot create staged file");
    try {
        extract([&](const uint8_t* data, size_t n) {
            stream.write(reinterpret_cast<const char*>(data), std::streamsize(n));
            check(bool(stream), "write failed (check disk space)");
        });
        stream.close(); check(bool(stream), "close failed");
        fs::rename(temp, path);
    } catch (...) { stream.close(); fs::remove(temp); throw; }
}

std::map<std::string,std::pair<size_t,std::string>> selection(const Cabinet& cab,bool disc,bool expansion=true) {
    std::map<std::string,std::pair<size_t,std::string>> selected;
    for(size_t i=0;i<cab.files.size();++i) {
        const auto& e=cab.files[i];if((e.flags&8)||e.path.empty())continue;
        for(const auto& g:e.groups) {auto dest=destination(disc,g,e.path);if(!dest.empty()&&(expansion||!disc||!expansionContent(dest)))selected[lower(dest)]={i,dest};}
    }
    return selected;
}
void extractCabinet(const Files& container,const std::string& prefix,bool disc,const fs::path& root,Run* run=nullptr,bool expansion=true) {
    Cabinet cab(container,prefix);auto selected=selection(cab,disc,expansion);
    uint64_t total=0;for(const auto& item:selected)total+=cab.files[item.second.first].size;
    if(run){run->begin(disc?"disc":"overlay150",selected.size(),total);if(run->phaseDone())return;}
    // No need to decompress unselected installer features: the complete source
    // identities are verified before writes. Every installed payload keeps MD5.
    struct Job {size_t index;std::string relative;fs::path path;};std::vector<Job> jobs;
    for(const auto& item:selected) {
        auto path=stagingPath(root,item.second.second);fs::create_directories(path.parent_path());
        jobs.push_back({item.second.first,item.second.second,path});
    }
    std::atomic<size_t> next{0};std::exception_ptr error;std::mutex errorMutex;
    auto work=[&] {
        try {
            for(;;) {
                size_t j=next.fetch_add(1);if(j>=jobs.size())return;
                {std::lock_guard<std::mutex> lock(errorMutex);if(error)return;}
                const auto& job=jobs[j];const auto& e=cab.files[job.index];
                if(run&&run->done(job.relative,job.path)){std::lock_guard<std::mutex> lock(run->mutex);++run->progress.completedFiles;run->progress.completedBytes+=e.size;continue;}
                writeFile(job.path,[&](const Sink& sink) {
                    auto emit=[&](const uint8_t* data,size_t n){if(run)run->bytes(n,job.relative);sink(data,n);};
                    if(e.offset||(e.links&1))cab.extract(job.index,emit);
                    else {
                        auto loose=container.find(lower(job.relative));
                        if(disc&&loose==container.end()&&std::find(e.groups.begin(),e.groups.end(),"eReg - XP1")!=e.groups.end())loose=container.find(lower("Support_XP1/"+e.path));
                        if(disc&&loose==container.end()&&std::find(e.groups.begin(),e.groups.end(),"Support - English")!=e.groups.end())loose=container.find(lower("Support/English/"+e.path));
                        check(disc&&loose!=container.end(),"unresolved external cabinet file: "+job.relative);
                        check(loose->second.size()==e.size&&digest(loose->second,false)==e.md5,"loose file checksum mismatch");loose->second.stream(emit);
                    }
                });
                if(run)run->record(job.relative,job.path);
            }
        } catch(...) {std::lock_guard<std::mutex> lock(errorMutex);if(!error)error=std::current_exception();}
    };
    unsigned workers=run?std::clamp(run->control.workers,1u,8u):4;
    std::vector<std::thread> threads;
    for(unsigned i=0;i<workers;++i)threads.emplace_back(work);
    for(auto& t:threads)t.join();if(error)std::rethrow_exception(error);
    if(run)run->finish();
    std::cout<<(disc?"disc":"1.50")<<": "<<selected.size()<<" paths verified\n"<<std::flush;
}

size_t patch141(const Files& container, const fs::path& root, const fs::path& report, const std::set<std::string>& replacements = {}, Run* run = nullptr, bool xp1 = false) {
    const auto& source=container.at(xp1?"disk1/xp1/patch.rtp":"disk1/bf2/patch.rtp");
    struct Reader {const View* source;Run* run;} reader{&source,run};
    auto read=[](void* context,size_t offset,uint8_t* output,size_t length)->int {
        auto& r=*static_cast<Reader*>(context);
        try {if(r.run)r.run->cancel();r.source->readInto(offset,output,length);return 0;}catch(...){return -1;}
    };
    std::unique_ptr<RtpHandle,decltype(&obf2_rtp_close)> patch(obf2_rtp_open_reader(size_t(source.size()),read,&reader),obf2_rtp_close);
    check(bool(patch),std::string("RTP open: ")+obf2_rtp_error());
    if(run){run->begin(xp1?"patch141-xp1":"patch141",obf2_rtp_count(patch.get()),0);if(run->phaseDone())return 0;}
    std::ofstream log(report);
    check(bool(log), "cannot create patch report");
    size_t produced = 0, deleted = 0, errors = 0, superseded = 0;
    for (size_t i = 0; i < obf2_rtp_count(patch.get()); ++i) {
        std::array<char, 4096> name{};
        check(obf2_rtp_name(patch.get(), i, name.data(), name.size()) == 0, obf2_rtp_error());
        auto relative = lower(safePath(name.data()));
        auto path = stagingPath(root, relative);
        if(run && run->done(relative,path)){run->skip(relative);log<<"resumed-verified\t"<<relative<<'\n';continue;}
        if(replacements.count(relative)) {
            ++superseded;log<<"superseded-by-150\t"<<relative<<'\n';if(run)run->skip(relative);continue;
        }
        bool present = fs::exists(path);
        static const std::set<std::string> unselectedHistorical{
            "python/bf2/stats/medal_data.py","linuxreadmes/readme-linux.txt","00000001.tmp","archivescripts/client.lst",
            "demo.cmd","demo_documentation.txt","dice_textureatlastool.exe","fraganalyzer.exe",
            "makeminimap.con","archivescripts/exclude.lst"};
        static const std::set<std::string> unselectedXp1{
            "python_xpack/bf2/stats/constants.py",
            "mods/xpack/localization/japanese/xpackjapanese.utxt",
            "mods/xpack/localization/thai/xpackthai.utxt"};
        if(!present && (unselectedHistorical.count(relative)|| (xp1&&unselectedXp1.count(relative)))) {
            ++deleted;log<<"unselected-historical-absent\t"<<relative<<'\n';if(run)run->record(relative,path,true);continue;
        }
        Bytes source = present ? View(path).all() : Bytes{};
        RtpBytes result{};
        int status = obf2_rtp_apply(patch.get(), i, source.data(), source.size(), present, &result);
        if (status < 0) {
            // This exact retail manifest has a metadata-only historical MODIFY
            // followed by DELETE. Its missing bytes cannot be reconstructed.
            // Remove only the cryptographically identified DVD manifest; do not
            // weaken rtptool's generic DELETE source validation.
            if(relative=="mods/bf2/levels.md5" && present && obf2_rtp_terminal(patch.get(),i)==1 &&
               digest(View(path),true)=="233e5b3dc1375c17b02c9bec4e0a73c396425893968c07da38919858003109ff") {
                fs::remove(path);++deleted;log<<"retired-disc-manifest\t"<<relative<<'\n';if(run)run->record(relative,path,true);continue;
            }
            ++errors; log << "error\t" << name.data() << '\t' << obf2_rtp_error() << '\n';
            std::cerr << "1.41 cannot produce " << name.data() << ": " << obf2_rtp_error() << '\n';
        } else if (status == 1) {
            if (present) fs::remove(path);
            ++deleted; log << "deleted/absent\t" << name.data() << '\n';
            if(run)run->record(relative,path,true);
        } else {
            try { writeFile(path, [&](const Sink& sink) { sink(result.data, result.size); }); }
            catch (...) { obf2_rtp_free(result); throw; }
            obf2_rtp_free(result); ++produced;
            log << "produced\t" << name.data() << '\n';
            if(run)run->record(relative,path);
        }
        if (i % 25 == 0) std::cout << "1.41 " << i + 1 << '/' << obf2_rtp_count(patch.get()) << '\n' << std::flush;
    }
    log.close(); check(bool(log), "patch report write failed");
    std::cout << "1.41: " << produced << " produced; " << deleted << " deleted/absent; " << errors << " errors; " << superseded << " superseded by 1.50\n" << std::flush;
    if(run && !errors)run->finish();
    return errors;
}

Files tree(const fs::path& root) {
    check(fs::is_directory(root) && !fs::is_symlink(fs::symlink_status(root)), "not a regular directory");
    Files result;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        check(!fs::is_symlink(e.symlink_status()), "symlink in comparison tree");
        if (e.is_directory()) continue;
        check(e.is_regular_file(), "non-regular file in comparison");
        auto relative = safePath(e.path().lexically_relative(root).generic_string());
        check(result.emplace(lower(relative), View(e.path())).second, "case collision in comparison tree");
    }
    return result;
}
}

int compare(const fs::path& output, const fs::path& reference, const fs::path& acceptance) {
    auto allowed = fs::canonical(fs::path(OPENBF2_SETUP_SCRATCH));
    auto reportDir = fs::canonical(output.parent_path());
    auto relative=reportDir.lexically_relative(allowed);
    check(!relative.empty() && !relative.is_absolute() && *relative.begin()!=".." &&
          !fs::is_symlink(fs::symlink_status(reportDir / "comparison.tsv")),
          "comparison report must stay under configured developer scratch");
    auto actual = tree(output), expected = tree(reference);
    std::set<std::string> accepted, paths;
    std::ifstream list(acceptance); check(bool(list), "cannot read acceptance path list");
    std::string line;
    while (std::getline(list, line)) if (!line.empty() && line.front() != '#') {
        check(accepted.insert(lower(safePath(line))).second, "duplicate acceptance path");
    }
    check(list.eof() && accepted.size() == 125, "acceptance list must contain exactly 125 paths");
    for (const auto& p : actual) paths.insert(p.first);
    for (const auto& p : expected) paths.insert(p.first);
    for (const auto& p : accepted) { check(expected.count(p), "acceptance reference missing: " + p); paths.insert(p); }
    std::ofstream report(output.parent_path() / "comparison.tsv");
    check(bool(report), "cannot create comparison report");
    report << "path\tstatus\tactual_size\treference_size\tactual_sha256\treference_sha256\tacceptance\n";
    std::map<std::string, size_t> totals;
    size_t acceptedMatch = 0;
    for (const auto& p : paths) {
        auto a = actual.find(p), b = expected.find(p);
        std::string ah = a == actual.end() ? "" : digest(a->second, true);
        std::string bh = b == expected.end() ? "" : digest(b->second, true);
        std::string status = a == actual.end() ? "missing" : b == expected.end() ? "extra" :
            a->second.size() != b->second.size() ? "size_mismatch" : ah != bh ? "hash_mismatch" : "match";
        ++totals[status];
        if (accepted.count(p) && status == "match") ++acceptedMatch;
        report << p << '\t' << status << '\t' << (a == actual.end() ? "" : std::to_string(a->second.size())) << '\t'
            << (b == expected.end() ? "" : std::to_string(b->second.size())) << '\t' << ah << '\t' << bh << '\t' << accepted.count(p) << '\n';
    }
    report.close(); check(bool(report), "comparison report write failed");
    std::cout << "full comparison:";
    for (const auto& t : totals) std::cout << ' ' << t.first << '=' << t.second;
    std::cout << "; acceptance=" << acceptedMatch << "/125\n" << std::flush;
    return acceptedMatch == 125 && totals["match"] == paths.size() ? 0 : 1;
}

std::string sha256(const View& source) {return digest(source,true);}

namespace {
// The client saves game settings into mods/<mod>/settings/ of its working directory,
// as retail does into its game folder. Some of those files are shipped content
// (maplist.con, serversettings.con: the save targets while no profile has redirected
// them), so a generation the game has run in may hold changed ones. It never deletes
// one: a missing file is still a damaged installation.
bool clientSettings(const std::string& path) {
    auto name=lower(path);if(!begins(name,"mods/"))return false;
    auto mod=name.find('/',5);return mod!=std::string::npos&&name.compare(mod,10,"/settings/")==0;
}
}
void verifyManifest(const fs::path& game,const fs::path& manifest,const Control& control,bool allowExtraFiles) {
    std::ifstream in(manifest);check(bool(in),"cannot open final content manifest");
    struct Expected {std::string path,hash;uint64_t size;};std::vector<Expected> expected;
    std::set<std::string> names;std::string line;uint64_t total=0;
    while(std::getline(in,line)) {
        if(line.empty()||line[0]=='#')continue;
        std::istringstream row(line);Expected e;std::string size;
        check(bool(std::getline(row,e.path,'\t'))&&bool(std::getline(row,size,'\t'))&&bool(std::getline(row,e.hash)),"malformed manifest row");
        e.path=safePath(e.path);check(names.insert(lower(e.path)).second,"duplicate manifest pathname");
        check(!size.empty()&&size.find_first_not_of("0123456789")==std::string::npos,"malformed manifest size");
        size_t used=0;e.size=std::stoull(size,&used);check(used==size.size()&&e.hash.size()==64&&e.hash.find_first_not_of("0123456789abcdef")==std::string::npos,"malformed manifest identity");
        check(e.size<=UINT64_MAX-total,"manifest size overflow");total+=e.size;expected.push_back(e);
    }
    check(in.eof()&&!expected.empty(),"empty/unreadable manifest");
    // Inventory first; reject extra files, symlinks and ambiguous case aliases.
    std::map<std::string,fs::path> actual;
    check(fs::is_directory(game)&&!fs::is_symlink(fs::symlink_status(game)),"missing game data directory");
    for(const auto& e:fs::recursive_directory_iterator(game)) {
        // The launcher links the user's own mods into mods/ of the generation in use. Such a
        // link is not followed; one in the place of shipped content leaves that content missing.
        if(allowExtraFiles&&fs::is_symlink(e.symlink_status())&&e.path().parent_path()==game/"mods")continue;
        check(!fs::is_symlink(e.symlink_status()),"symlink in final game data");if(e.is_directory())continue;
        check(e.is_regular_file(),"nonregular game data");
        check(actual.emplace(lower(safePath(e.path().lexically_relative(game).generic_string())),e.path()).second,"case collision in final game data");
    }
    // A generation in use is checked for its shipped files only; see clientSettings.
    check(allowExtraFiles||actual.size()==expected.size(),"final content file count mismatch");
    Progress progress{"verify","",0,total,0,expected.size()};
    std::atomic<size_t> next{0};std::mutex mutex;std::exception_ptr error;
    auto work=[&] {
        try {
            for(;;) {
                size_t index=next.fetch_add(1);if(index>=expected.size())return;
                {std::lock_guard<std::mutex> lock(mutex);if(error)return;}
                if(control.cancelled&&control.cancelled->load())throw std::runtime_error("Cancelled; setup can resume");
                const auto& e=expected[index];auto found=actual.find(lower(e.path));
                if(allowExtraFiles&&found!=actual.end()&&clientSettings(e.path)){std::lock_guard<std::mutex> lock(mutex);++progress.completedFiles;progress.completedBytes+=e.size;progress.path=e.path;if(control.progress)control.progress(progress);continue;}
                check(found!=actual.end(),"missing required file: "+e.path);
                View view(found->second);Digest h;
                view.stream([&](const uint8_t* p,size_t n){if(control.cancelled&&control.cancelled->load())throw std::runtime_error("Cancelled; setup can resume");h.update(p,n);});
                check(view.size()==e.size&&h.finish()==e.hash,"final content identity mismatch: "+e.path);
                std::lock_guard<std::mutex> lock(mutex);++progress.completedFiles;progress.completedBytes+=e.size;progress.path=e.path;if(control.progress)control.progress(progress);
            }
        }catch(...){std::lock_guard<std::mutex> lock(mutex);if(!error)error=std::current_exception();}
    };
    std::vector<std::thread> workers;for(unsigned i=0;i<std::clamp(control.workers,1u,8u);++i)workers.emplace_back(work);
    for(auto& worker:workers)worker.join();if(error)std::rethrow_exception(error);

}
namespace {
void secureDirectory(const fs::path& directory) {
    auto absolute=fs::absolute(directory).lexically_normal();fs::path current;
    for(const auto& part:absolute) {
        current/=part;check(!fs::is_symlink(fs::symlink_status(current)),"symlink in managed directory");
        if(!fs::exists(current)){fs::create_directory(current);fs::permissions(current,fs::perms::owner_all);}
        check(fs::is_directory(current),"managed path is not a directory");
    }
}
void textFile(const fs::path& path,const std::string& text,Platform& platform) {
    writeFile(path,[&](const Sink& sink){sink(reinterpret_cast<const uint8_t*>(text.data()),text.size());});platform.syncFile(path);platform.syncDirectory(path.parent_path());
}

}
void exportDisc(const fs::path& source,const fs::path& output,const Control& control) {
    auto container=readDisc(source);Cabinet cab(container,"");
    auto root=fs::absolute(output).lexically_normal();
    check(!fs::exists(root)&&!fs::is_symlink(fs::symlink_status(root)),"export requires a fresh output directory");
    // Resolve existing parents before creating anything; refuse symlinks just as
    // the production installer does. No output may overlap the read-only input.
    auto parent=fs::canonical(root.parent_path());
    check(parent==root.parent_path(),"symlink in export parent");
    auto input=fs::canonical(source);
    auto relative=parent.lexically_relative(input);
    check(!fs::is_directory(input)||(!relative.empty()&&*relative.begin()==".."),"input/output overlap");
    check(fs::create_directory(root),"cannot create export directory");
    fs::permissions(root,fs::perms::owner_all);
    std::ofstream manifest(root/"manifest.tsv");check(bool(manifest),"cannot create export manifest");
    manifest<<"index\tpath\tsize\tcabinet_path\tcabinet_groups\tstorage\tmd5\tsource\n";
    Progress p{"export-disc","",0,0,0,cab.files.size()};
    for(const auto& e:cab.files)if(!(e.flags&8)&&!e.path.empty())p.totalBytes+=e.size;
    auto notify=[&]{if(control.cancelled&&control.cancelled->load())throw std::runtime_error("Disc export cancelled; remove incomplete export before retrying");if(control.progress)control.progress(p);};
    notify();
    for(size_t i=0;i<cab.files.size();++i) {
        const auto& e=cab.files[i];std::string groups;
        for(const auto& g:e.groups){check(g.find_first_of("\t\r\n|")==std::string::npos,"invalid manifest group");if(!groups.empty())groups+='|';groups+=g;}
        std::string path,storage="invalid-descriptor",origin;
        if(!(e.flags&8)&&!e.path.empty()&&!e.offset&&!(e.links&1)&&!e.size) {
            // InstallShield's layout.bin/data1.hdr descriptors are empty
            // self-references, not copies of the nonempty loose installer files.
            storage="empty-placeholder";
            check(e.md5=="d41d8cd98f00b204e9800998ecf8427e","nonempty placeholder digest");
        } else if(!(e.flags&8)&&!e.path.empty()) {
            // One directory per descriptor retains aliases and duplicate names
            // across languages/groups without guessing installer destinations.
            path="payloads/"+std::to_string(i)+"/"+e.path;
            storage=(e.links&1)?"cabinet-link":e.offset?"cabinet":"loose";
            if(storage=="loose") {
                auto suffix="/"+lower(e.path);
                for(const auto& item:container) {
                    const auto& name=item.first;
                    if(name!=lower(e.path)&&!(name.size()>suffix.size()&&name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0))continue;
                    if(item.second.size()==e.size&&digest(item.second,false)==e.md5){origin=name;break;}
                }
                check(!origin.empty(),"unresolved external cabinet descriptor: "+e.path);
            } else origin=(e.links&1)?"descriptor:"+std::to_string(e.previous):"data"+std::to_string(e.volume)+".cab";
            Digest md5(false);uint64_t size=0;
            writeFile(stagingPath(root,path),[&](const Sink& sink){
                auto emit=[&](const uint8_t* data,size_t n){md5.update(data,n);size+=n;auto previous=p.completedBytes;p.completedBytes+=n;p.path=path;if(p.completedBytes/(1024*1024)!=previous/(1024*1024))notify();sink(data,n);};
                if(storage=="loose")container.at(origin).stream(emit);else cab.extract(i,emit);
            });
            check(size==e.size&&md5.finish()==e.md5,"export size/MD5 mismatch: "+e.path);
        }
        manifest<<i<<'\t'<<path<<'\t'<<e.size<<'\t'<<e.path<<'\t'<<groups<<'\t'<<storage<<'\t'<<e.md5<<'\t'<<origin<<'\n';
        check(bool(manifest),"export manifest write failed");++p.completedFiles;notify();
    }
    manifest.close();check(bool(manifest),"export manifest close failed");
    std::cout<<"Export complete: "<<cab.files.size()<<" descriptors, "<<p.completedBytes<<" bytes; "<<root/"manifest.tsv"<<'\n';
}
fs::path install(const InstallOptions& o,Platform& platform) {
    Run run;run.control=o.control;run.platform=&platform;
    // Refuse source/output overlap before creating a single directory.
    auto support=fs::absolute(o.supportRoot).lexically_normal();
    auto inside=[](const fs::path& a,const fs::path& b){auto r=a.lexically_relative(b);return !r.empty()&&!r.is_absolute()&&*r.begin()!="..";};
    for(const auto& input:{o.disc,o.patch141,o.patch150,o.manifest}) {
        auto path=fs::canonical(input);check(!inside(path,support)&&!inside(support,path),"input/output overlap");
    }
    secureDirectory(support/"Setup");secureDirectory(support/"GameData");
    auto lock=platform.lock(support/"Setup/Install.lock");
    run.transaction=support/"Setup/work";
    // Validate the supported media by full SHA-256, before staging any payload.
    auto disc=readDisc(o.disc);Cabinet discCab(disc,"");
    auto edition=discEdition(discCab.files);bool deluxe=edition==DiscEdition::Deluxe;
    const bool expansion=deluxe&&o.specialForces; // Deluxe media may install the base game alone
    check(discCab.files.size()==(deluxe?3162:1727),"unsupported disc cabinet inventory");
    auto p141=readSfx(View(o.patch141)),p150=readSfx(View(o.patch150));
    std::string identities=deluxe?"openbf2-setup-v2\n":"openbf2-setup-v1\n";
    struct Input {std::string name,expected;View view;};std::vector<Input> inputs;
    if(fs::is_directory(o.disc)) {
        inputs.push_back({"DVD data1.hdr",deluxe?"ae4800c392fa1bf730cbc90823b40ae772fe6b82ba28f15b48333c3cd002a350":"bf2ffc9c72c119c79639df5138c1a3bac2f7df78ae0adb9a124796e1665c5377",disc.at("data1.hdr")});
        inputs.push_back({"DVD data1.cab",deluxe?"27a1a23e868b434517d39c9e68531442ed7674d15e802a0122ffb064bffa78e1":"6ac0645f56d4107b78c4f4c66658a19dff0630f72c0fc13ae86cf9f430f40181",disc.at("data1.cab")});
        inputs.push_back({"DVD data2.cab",deluxe?"7bd10eb4a3740973171d38a952cc2c51bee8818ce923085d466f1cf9295a75b4":"6061bbfa63ed6e6cb98d90dd3cedd1db80a94b85e40bd556449a59e6c94448dc",disc.at("data2.cab")});
    } else inputs.push_back({"DVD image",deluxe?"7b462933353f89f5a7d7282987be61062a92d248fec125050039ba440765e99e":"2116a6b7107fdf1054e3fd86282f5db02b60c01658d7a4ca13e953d747b95854",View(o.disc)});
    inputs.push_back({"Patch 1.41",expectedPatch(PatchSlot::Patch141).hash,View(o.patch141)});
    inputs.push_back({"Patch 1.50",expectedPatch(PatchSlot::Patch150).hash,View(o.patch150)});
    uint64_t inputBytes=0;for(const auto& input:inputs)inputBytes+=input.view.size();run.begin("inputs",inputs.size(),inputBytes);
    std::vector<std::future<std::string>> hashes;
    for(const auto& input:inputs)hashes.push_back(std::async(std::launch::async,[&run,input]{
        Digest hash;uint64_t seen=0;const uint64_t total=input.view.size();
        input.view.stream([&](const uint8_t* bytes,size_t n){seen+=n;run.bytes(n,input.name,seen,total);hash.update(bytes,n);});return hash.finish();
    }));
    std::exception_ptr inputError;
    for(size_t i=0;i<inputs.size();++i) {
        try {auto hash=hashes[i].get();
            if(i>=inputs.size()-2)requirePatchIdentity(i==inputs.size()-2?PatchSlot::Patch141:PatchSlot::Patch150,{inputs[i].view.size(),hash});
            check(hash==inputs[i].expected,"Unsupported or corrupt "+inputs[i].name);identities+=hash+"\n";std::lock_guard<std::mutex> guard(run.mutex);++run.progress.completedFiles;run.notify();}
        catch(...){if(!inputError)inputError=std::current_exception();}
    }
    if(inputError)std::rethrow_exception(inputError);
    identities+=digest(View(o.manifest),true)+"\n";
    if(deluxe)identities+=expansion?"edition=deluxe\n":"edition=deluxe-base\n";
    Cabinet full(p150,"disk1/");check(full.files.size()==381,"unsupported cabinet inventory");
    uint64_t needed=0;for(const auto& item:selection(discCab,true,expansion))needed+=discCab.files[item.second.first].size;
    for(const auto& item:selection(full,false))needed+=full.files[item.second.first].size;
    needed+=uint64_t(2)*1024*1024*1024; // RTP working set + filesystem reserve.
    if(!fs::exists(run.transaction))check(platform.availableSpace(support)>=needed,"Insufficient free space for staging and patch reserve");
    secureDirectory(run.transaction);
    auto identityPath=run.transaction/"Inputs.txt";
    if(fs::exists(identityPath)) {
        auto bytes=View(identityPath).all();check(std::string(bytes.begin(),bytes.end())==identities,"Resume inputs changed; remove only Setup/work to start again");
    } else textFile(identityPath,identities,platform);
    auto publish=[&](const fs::path& generation,const std::string& token) {
        auto receipt=identities+"generation=GameData/"+generation.filename().string()+"\n";
        fs::path receiptPath=support/"Setup"/("Receipt-"+token+".txt");
        textFile(receiptPath,receipt,platform);
        auto pending=support/"Setup/Current.pending";
        textFile(pending,generation.lexically_relative(support).generic_string()+"\n"+receiptPath.filename().string()+"\n",platform);
        auto patchReport=run.transaction/"patch141.tsv";
        if(fs::exists(patchReport))platform.replace(patchReport,support/"Setup"/("Patch141-"+token+".tsv"));
        auto xp1Report=run.transaction/"patch141-xp1.tsv";
        if(fs::exists(xp1Report))platform.replace(xp1Report,support/"Setup"/("Patch141-XP1-"+token+".tsv"));
        platform.replace(pending,support/"Setup/Current.txt"); // commit point
        // Cleanup cannot turn a durably committed installation into a reported
        // failure. A leftover intent is safe to reverify/recover on the next run.
        std::error_code cleanupError;fs::remove_all(run.transaction,cleanupError);
        return generation;
    };
    auto intentPath=run.transaction/"Publish.txt";
    if(fs::exists(intentPath)) {
        auto bytes=View(intentPath).all();std::string token(bytes.begin(),bytes.end());
        check(!token.empty()&&token.find_first_not_of("0123456789")==std::string::npos,"invalid publication intent");
        auto generation=support/"GameData"/("1.50-"+token);
        if(fs::exists(generation)) {verifyManifest(generation,o.manifest,o.control);run.cancel();return publish(generation,token);}
        // Intent was flushed before the generation rename; resume normal work.
    }
    auto game=run.transaction/"game";secureDirectory(game);
    extractCabinet(disc,"",true,game,&run,expansion);
    // The 1.50 installer preserves the original DVD renderer under this alias.
    run.begin("preserveRenderer",1,2115072);
    if(!run.phaseDone()) {
        auto original=stagingPath(game,"RendDX9.dll");check(digest(View(original),true)==(deluxe?"56705ae93e5eb552a80f08dbbeb5488a78bd29156a175aece82c632d8d5f0c3d":"1565c0dcab852ebe00a41a4ec016d3d0540e88d7ac049e16295aabd318e8d2f8"),"original renderer missing before patching");
        writeFile(stagingPath(game,"RendDX9x2.dll"),[&](const Sink& sink){View(original).stream(sink);});
        platform.syncFile(stagingPath(game,"RendDX9x2.dll"));run.finish();
    }
    std::set<std::string> replacements;for(const auto& item:selection(full,false))replacements.insert(item.first);
    check(patch141(p141,game,run.transaction/"patch141.tsv",replacements,&run)==0,"Required 1.41 record failed; see patch141.tsv");
    if(expansion)check(patch141(p141,game,run.transaction/"patch141-xp1.tsv",replacements,&run,true)==0,"Required Special Forces 1.41 record failed; see patch141-xp1.tsv");
    extractCabinet(p150,"disk1/",false,game,&run);
    verifyManifest(game,o.manifest,o.control);run.cancel();
    // Flush all installed directories before publishing a same-volume generation.
    for(const auto& e:fs::recursive_directory_iterator(game))if(e.is_directory())platform.syncDirectory(e.path());
    platform.syncDirectory(game);
    auto token=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    auto generation=support/"GameData"/("1.50-"+token);
    textFile(intentPath,token,platform);
    platform.replace(game,generation);platform.syncDirectory(run.transaction);
    return publish(generation,token);
}

int prototype(const Options& o) {
    // The prototype is intentionally incapable of installing into Applications,
    // a reference tree or a user's live Application Support directory.
    auto allowed = fs::canonical(fs::path(OPENBF2_SETUP_SCRATCH));
    auto parent = fs::canonical(o.output.parent_path());
    check(parent == allowed && !fs::exists(o.output) && !fs::is_symlink(fs::symlink_status(o.output)),
          "output must be a new direct child of SETUP_SCRATCH");
    check(fs::space(allowed).available >= uint64_t(16) * 1024 * 1024 * 1024, "prototype requires 16 GiB free for staging");
    auto iso = readIso(View(o.iso)), p141 = readSfx(View(o.patch141)), p150 = readSfx(View(o.patch150));
    check(iso.size() == 1148 && p141.size() == 17 && p150.size() == 9 && p141.count("disk1/bf2/patch.rtp"),
          "unsupported disc or patch variant; expected the verified BF2 media");
    // A header check prevents swapped patches before staging is created.
    Cabinet disc(iso, ""), full(p150, "disk1/");
    check(disc.files.size() == 1727 && full.files.size() == 381, "unsupported media cabinet inventory");
    fs::create_directory(o.output); fs::permissions(o.output, fs::perms::owner_all);
    auto root = o.output / "game"; fs::create_directory(root);
    std::cout << "ISO files=" << iso.size() << "; SFX records=" << p141.size() << ',' << p150.size() << '\n' << std::flush;
    extractCabinet(iso, "", true, root);
    writeFile(stagingPath(root,"RendDX9x2.dll"),[&](const Sink& sink){View(stagingPath(root,"RendDX9.dll")).stream(sink);});
    std::set<std::string> replacements;
    for(const auto& item:selection(full,false))replacements.insert(item.first);
    auto failures = patch141(p141, root, o.output / "patch141.tsv",replacements);
    extractCabinet(p150, "disk1/", false, root);
    auto compared = compare(root, o.reference, o.acceptance);
    std::cout << "Prototype complete; installation is NOT published. Patch errors=" << failures << '\n';
    return failures || compared ? 1 : 0;
}
}
