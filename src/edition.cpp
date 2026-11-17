#include "edition.h"
#include <set>
#include <stdexcept>

namespace setup {
// English disc feature destinations, checked against both owner inventories.
bool expansionContent(const std::string& destination) {
    auto path=lower(destination);
    return path.rfind("mods/xpack/",0)==0||path.rfind("supportxp1/",0)==0;
}
std::string discDestination(const std::string& group,const std::string& path) {
    if(group.empty()||group.front()=='<'||path.empty())return {};
    auto begins=[](const std::string& text,const std::string& prefix){return text.rfind(prefix,0)==0;};
    auto check=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    std::string root;
    static const std::map<std::string, std::string> fixed{
        {"Python - BF2","python"}, {"Menu - BF2","mods/bf2/Menu/External"}, {"MenuObjects - BF2","mods/bf2/Objects"},
        {"Menu - XP1","mods/xpack/Menu/External"}, {"MenuObjects - XP1","mods/xpack/Objects"},
        {"eReg - XP1","SupportXP1"}, {"Movie - Misc - XP1","mods/xpack/Movies"}, {"Movies - English - XP1","mods/xpack/Movies"},
        {"Admin","Admin"}, {"Main",""}, {"MainServer",""}, {"Python","python"},
        {"RendDX9",""}, {"SafeDisc",""}, {"Voip files",""}, {"FireWall",""},
        {"Profiles","Profiles"}, {"Cursors","mods/bf2/Menu"}, {"Menu","mods/bf2/Menu/External"},
        {"MenuObjects","mods/bf2/Objects"}, {"Redist # GameSpy","Redist"}, {"PunkBuster","pb"},
        {"PunkBuster - htm","pb/htm"}, {"eReg","Support"}, {"Shortcuts",""},
        {"DiceMovie","mods/bf2/Movies"}, {"Movie - Misc","mods/bf2/Movies"},
        {"Redist # MicCalibrate",""}, {"AdminUtils - Server","adminutils"}};
    auto it = fixed.find(group);
    if (it != fixed.end()) root = it->second;
    else if (begins(group, "Mods - xpack")) {
        auto translated=std::string("Mods - bf2")+group.substr(12);
        auto mapped=discDestination(translated,path);
        return mapped.empty()?mapped:std::string("mods/xpack")+mapped.substr(8);
    }
    else if (begins(group, "Mods - bf2 - localization ")) root = "mods/bf2/Localization/" + group.substr(26);
    else if (begins(group, "Mods - bf2 - levels")) root = "mods/bf2/Levels";
    else if (begins(group, "Mods - bf2 - ")) {
        auto suffix = group.substr(13);
        check(suffix == "AI" || suffix == "Settings" || suffix == "python" || suffix == "shaders" || suffix == "Logs", "unmapped disc group: " + group);
        root = "mods/bf2/" + suffix;
    } else if (begins(group, "Mods - bf2")) root = "mods/bf2";
    else if (group == "Movies - English") root = "mods/bf2/Movies";
    else if (group == "Support - English") root = "Support";
    else return {};
    return safePath(root.empty()?path:root+"/"+path);
}
DiscEdition discEdition(const std::vector<Entry>& entries) {
    bool base=false,xpack=false;std::set<std::string> sf;
    for(const auto& e:entries)for(const auto& group:e.groups){
        if(group.rfind("Mods - bf2",0)==0&&lower(e.path)=="objects_client.zip")base=true;
        if(group.rfind("Mods - xpack",0)==0){xpack=true;sf.insert(lower(discDestination(group,e.path)));}
    }
    if(!base)throw std::runtime_error("This disc does not contain supported Battlefield 2 content.");
    if(!xpack)return DiscEdition::BF2;
    for(auto path:{"mods/xpack/objects_client.zip","mods/xpack/objects_server.zip","mods/xpack/init.con","mods/xpack/levels/warlord/client.zip","mods/xpack/levels/warlord/server.zip"})
        if(!sf.count(path))throw std::runtime_error("Incomplete Special Forces content on this disc.");
    return DiscEdition::Deluxe;
}
DiscEdition detectDisc(const fs::path& path) {
    return discContents(readDisc(path)).edition;
}
std::string specialForcesDestination(const std::string& group,const std::string& path) {
    if(path.empty())return {};
    static const std::map<std::string,std::string> roots{
        {"Python_Xpack","python_xpack"}, {"Xpack Cursors","mods/xpack/Menu"},
        {"Xpack Menu","mods/xpack/Menu/External"}, {"Xpack MenuObjects","mods/xpack/Objects"},
        {"Movies - xpack - English","mods/xpack/Movies"}, {"Xpack - IntroMovie","mods/xpack/Movies"},
        {"eReg","SupportXP1"}, {"Support - English","SupportXP1"}};
    auto it=roots.find(group);
    if(it!=roots.end())return safePath(it->second+"/"+path);
    return group.rfind("Mods - xpack",0)==0?discDestination(group,path):std::string{};
}
View includedPatch141(const Files& files) {
    auto patch=files.find("installers/bf2 patch 1.41.exe");
    if(patch==files.end())throw std::runtime_error("The Complete Edition's included 1.41 patch is missing.");
    return patch->second;
}
DiscContents discContents(const Files& files) {
    if(files.count("installers/bf2/data1.hdr")) {
        auto component=[&](const std::string& prefix){
            Files result;for(const auto& [name,view]:files)if(name.rfind(prefix,0)==0)result.emplace(name.substr(prefix.size()),view);
            return result;
        };
        DiscContents result{DiscEdition::Complete,component("installers/bf2/"),component("installers/sf/")};
        Cabinet base(result.base,""),sf(result.specialForces,"");
        if(base.files.size()!=1727||sf.files.size()!=1508||discEdition(base.files)!=DiscEdition::BF2)
            throw std::runtime_error("This Complete Edition is not supported.");
        // Reuse the expansion completeness checks with the combined inventories.
        auto combined=base.files;combined.insert(combined.end(),sf.files.begin(),sf.files.end());
        if(discEdition(combined)!=DiscEdition::Deluxe)
            throw std::runtime_error("Incomplete Special Forces content on this disc.");
        includedPatch141(files);
        return result;
    }
    Cabinet cab(files,"");auto edition=discEdition(cab.files);
    if(cab.files.size()!=(edition==DiscEdition::Deluxe?3162:1727))throw std::runtime_error("This DVD edition is not supported.");
    return {edition,files,{}};
}
}

namespace setup::assistant {
const char* manifestName(DiscEdition edition) {
    switch(edition){
        case DiscEdition::Deluxe:return "content-deluxe-150";
        case DiscEdition::DeluxeBase:return "content-deluxe-base-150";
        case DiscEdition::Complete:return "content-complete-150";
        default:return "content-150";
    }
}
DiscEdition validateDisc(const fs::path& path) {return detectDisc(path);}
}
