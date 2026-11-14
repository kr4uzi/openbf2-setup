// [TEMP] [CHOSEN, not recovered] Malformed-input and C ABI checks for setup.
#include "install.h"
#include "cabinet.h"
#include "edition.h"
#include "patch.h"
#include "key_check.h"
#include "rtp_bridge.h"
#include "digest.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool value, const char* what) {
    if (!value) throw std::runtime_error(what);
    ++checks;
}
template<class F> void rejects(F f, const char* what) {
    bool threw = false;
    try { f(); } catch (const std::exception&) { threw = true; }
    check(threw, what);
}
void put(setup::Bytes& b, size_t o, uint64_t n, unsigned width) {
    for (unsigned i = 0; i < width; ++i) b.at(o+i) = uint8_t(n >> (i*8));
}
void save(const setup::fs::path& p, const setup::Bytes& bytes) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!out) throw std::runtime_error("test fixture write failed");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("pass a new fixture directory under scratch");
        setup::fs::path root = setup::fs::absolute(argv[1]).lexically_normal();
        auto allowed = setup::fs::canonical(setup::fs::path(SETUP_TEST_ROOT));
        check(root.parent_path() == allowed && !setup::fs::exists(root), "fixtures must be a fresh child of scratch");
        setup::fs::create_directory(root);
        for (bool sha : {false,true}) {
            setup::Digest h(sha);
            check(h.finish() == (sha ? "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" : "d41d8cd98f00b204e9800998ecf8427e"), "empty digest");
            setup::Digest abc(sha);abc.update(reinterpret_cast<const uint8_t*>("a"),1);abc.update(reinterpret_cast<const uint8_t*>("bc"),2);
            check(abc.finish() == (sha ? "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" : "900150983cd24fb0d6963f7d28e17f72"), "streaming abc digest");
            setup::Digest million(sha);std::string chunk(1000,'a');
            for (unsigned i=0;i<1000;++i) million.update(reinterpret_cast<const uint8_t*>(chunk.data()),chunk.size());
            check(million.finish() == (sha ? "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" : "7707d6ae4e027c70eea2a935c2296f21"), "million byte digest");
        }
        for (auto text : {"../escape", "a/../b", "/absolute", "C:\\bad", "a//b", "a/", ".", "a\tb", "dir/NUL.txt", "a.", "a ", "x?.zip"})
            rejects([&] { setup::safePath(text); }, "unsafe pathname accepted");
        check(setup::safePath("mods\\bf2/file.zip") == "mods/bf2/file.zip", "Windows separators");
        save(root / "short", {0,1,2,3});
        setup::View v(root / "short");
        check(v.sub(1,2).all() == setup::Bytes({1,2}), "bounded range contents");
        rejects([&] { v.read(3,2); }, "range overflow accepted");
        rejects([&] { v.sub(UINT64_MAX,2); }, "integer overflow accepted");
        rejects([&] { setup::readIso(v); }, "truncated ISO accepted");
        rejects([&] { setup::readSfx(v); }, "truncated PE accepted");
        setup::fs::create_symlink(root / "short", root / "link");
        rejects([&] { setup::View bad(root / "link"); }, "symlink input accepted");
        // Minimal synthetic PE overlay, independent of game content.
        setup::Bytes pe(512,0);
        pe[0]='M'; pe[1]='Z'; put(pe,60,64,4); put(pe,64,0x4550,4);
        put(pe,68,0x14c,2); put(pe,70,1,2); put(pe,84,2,2); put(pe,88,0x10b,2);
        put(pe,90+16,512,4); put(pe,90+20,0,4);
        std::string fields;
        // Build fields explicitly so every intentional NUL is preserved.
        for (auto s : {"sample", "Disk1\\sample", "", "3"}) { fields += s; fields += '\0'; }
        pe.insert(pe.end(), fields.begin(), fields.end()); pe.insert(pe.end(), {1,2,3});
        save(root / "sfx", pe);
        auto files = setup::readSfx(setup::View(root / "sfx"));
        check(files.size()==1 && files.at("disk1/sample").all()==setup::Bytes({1,2,3}), "valid SFX range");
        pe.pop_back(); save(root / "truncated", pe);
        rejects([&] { setup::readSfx(setup::View(root / "truncated")); }, "truncated SFX payload accepted");
        pe.push_back(3); pe.push_back('x'); save(root / "trailer", pe);
        rejects([&] { setup::readSfx(setup::View(root / "trailer")); }, "SFX trailer accepted");
        // A synthetic format-9 descriptor with stored MD5("abc"). This checks
        // the parser, storage bounds, digest and raw DEFLATE as a single boundary.
        setup::Bytes hdr(0x600,0);
        put(hdr,0,0x28635349,4); put(hdr,4,0x01009500,4);
        put(hdr,12,0x200,4); put(hdr,16,0x400,4);
        put(hdr,0x200+12,0x180,4); put(hdr,0x200+28,1,4);
        put(hdr,0x200+40,1,4); put(hdr,0x200+44,0x80,4);
        put(hdr,0x380,8,4);
        put(hdr,0x400+2,3,8); put(hdr,0x400+10,3,8); put(hdr,0x400+18,1,8);
        put(hdr,0x400+58,0x100,4); put(hdr,0x400+85,1,2);
        const uint8_t md5[] = {0x90,0x01,0x50,0x98,0x3c,0xd2,0x4f,0xb0,0xd6,0x96,0x3f,0x7d,0x28,0xe1,0x7f,0x72};
        for (unsigned i=0;i<16;++i) hdr.at(0x400+26+i)=md5[i];
        std::string cabName="abc.txt";
        std::copy(cabName.begin(),cabName.end(),hdr.begin()+0x480);
        save(root/"data1.hdr",hdr); save(root/"data1.cab",{0,'a','b','c'}); save(root/"data2.cab",{0});
        auto cabFiles=[&] { return setup::Files{{"data1.hdr",setup::View(root/"data1.hdr")},
            {"data1.cab",setup::View(root/"data1.cab")},{"data2.cab",setup::View(root/"data2.cab")}}; };
        auto extract=[&] {
            setup::Bytes bytes;
            setup::Cabinet(cabFiles(),"").extract(0,[&](const uint8_t* data,size_t n) { bytes.insert(bytes.end(),data,data+n); });
            return bytes;
        };
        check(extract()==setup::Bytes({'a','b','c'}),"uncompressed cabinet/digest");
        hdr[0x400+26]^=1; save(root/"data1.hdr",hdr);
        rejects(extract,"bad cabinet MD5 accepted");
        hdr[0x400+26]^=1; put(hdr,0x400,4,2); put(hdr,0x400+10,7,8);
        save(root/"data1.hdr",hdr); save(root/"data1.cab",{0,5,0,0x4b,0x4c,0x4a,0x06,0});
        check(extract()==setup::Bytes({'a','b','c'}),"compressed cabinet/digest");
        save(root/"data1.cab",{0,5,0,0xff,0xff,0xff,0xff,0xff});
        rejects(extract,"invalid DEFLATE accepted");
        save(root/"data1.cab",{0,5,0,0x4b});
        rejects(extract,"truncated cabinet block accepted");
        put(hdr,0x400,2,2); save(root/"data1.hdr",hdr);
        rejects(extract,"unsupported cabinet flags accepted");
        uint8_t into[2]{};v.readInto(1,into,2);check(into[0]==1&&into[1]==2,"bounded reader destination");
        check(obf2_rtp_open_reader(4,nullptr,nullptr)==nullptr,"null RTP reader accepted");
        auto reader=[](void*,size_t,uint8_t*,size_t)->int{return -1;};
        check(obf2_rtp_open_reader(4,reader,nullptr)==nullptr,"failed RTP reader accepted");
        check(obf2_rtp_open_reader(size_t(1024)*1024*1024+1,reader,nullptr)==nullptr,"oversized RTP accepted");
        check(obf2_rtp_open(nullptr,0)==nullptr, "null RTP accepted");
        check(obf2_rtp_open(v.all().data(),4)==nullptr, "malformed RTP accepted");
        check(std::string(obf2_rtp_error()).size()!=0, "RTP error lost");
        check(obf2_rtp_name(nullptr,0,nullptr,0)==-1, "null RTP name accepted");
        RtpBytes output{};
        check(obf2_rtp_apply(nullptr,0,nullptr,0,0,&output)==-1, "null RTP apply accepted");
        obf2_rtp_free(output); obf2_rtp_close(nullptr);
        check(setup::keyFormat("ABCD-EFGH-1234-5678-IJKL"), "grouped key format");
        check(setup::keyFormat("ABCDEFGH12345678IJKL"), "compact key format");
        for(auto bad : {"ABCD-EFGH-1234-5678-IJK!", "A-BCDEFGH12345678IJKL", "ABCDEFGHIJKLMNOPQRS", "ABCDEFGHIJKLMNOPQRSTU"})
            check(!setup::keyFormat(bad), "invalid key format accepted");
        // Edition detection uses cabinet content and maps both client/server data.
        auto entry=[](const std::string& path,const std::string& group){setup::Entry e;e.path=path;e.groups={group};return e;};
        std::vector<setup::Entry> base{entry("Objects_client.zip","Mods - bf2 # objects_client")};
        check(setup::discEdition(base)==setup::DiscEdition::BF2,"base edition detection");
        auto deluxe=base;
        for(auto path:{"Objects_client.zip","Objects_server.zip","Init.con"})deluxe.push_back(entry(path,"Mods - xpack"));
        for(auto path:{"Warlord/client.zip","Warlord/server.zip"})deluxe.push_back(entry(path,"Mods - xpack - levels # client"));
        check(setup::discEdition(deluxe)==setup::DiscEdition::Deluxe,"Deluxe edition detection");
        deluxe.pop_back();rejects([&]{setup::discEdition(deluxe);},"incomplete xpack accepted");
        rejects([&]{setup::discEdition({entry("Objects_client.zip","Mods - xpack")});},"SF without BF2 accepted");
        for(auto row:std::vector<std::vector<std::string>>{
            {"Mods - xpack # objects_client","Objects_client.zip","mods/xpack/Objects_client.zip"},
            {"Mods - xpack - levels","Warlord/server.zip","mods/xpack/Levels/Warlord/server.zip"},
            {"Mods - xpack - python","game/gamemodes/gpm_coop.py","mods/xpack/python/game/gamemodes/gpm_coop.py"},
            {"Mods - xpack - localization English","xpackEnglish.utxt","mods/xpack/Localization/English/xpackEnglish.utxt"},
            {"Menu - XP1","FlashMenu/Cursor/cursor_arrow.ani","mods/xpack/Menu/External/FlashMenu/Cursor/cursor_arrow.ani"},
            {"Movies - English - XP1","intro.bik","mods/xpack/Movies/intro.bik"},
            {"eReg - XP1","config.xml","SupportXP1/config.xml"},
            {"Python - BF2","bf2/stats/constants.py","python/bf2/stats/constants.py"},
            {"MenuObjects - BF2","icon.png","mods/bf2/Objects/icon.png"}})
            check(setup::discDestination(row[0],row[1])==row[2],"disc feature destination");
        check(setup::discDestination("Movies - German - XP1","intro.bik").empty(),"unselected SF movie locale");
        rejects([&]{setup::discDestination("Mods - xpack","../escape");},"unsafe xpack path accepted");
        for(auto slot:{setup::PatchSlot::Patch141,setup::PatchSlot::Patch150}){
            const auto expected=setup::expectedPatch(slot);setup::requirePatchIdentity(slot,expected);check(true,"correct patch pin rejected");
            auto changed=expected;changed.hash[0]=changed.hash[0]=='a'?'b':'a';
            rejects([&]{setup::requirePatchIdentity(slot,changed);},"same-size corrupt patch accepted");
            changed=expected;--changed.size;
            rejects([&]{setup::requirePatchIdentity(slot,changed);},"truncated patch accepted");
            auto other=slot==setup::PatchSlot::Patch141?setup::PatchSlot::Patch150:setup::PatchSlot::Patch141;
            bool clear=false;try{setup::requirePatchIdentity(slot,setup::expectedPatch(other));}catch(const std::exception& e){clear=std::string(e.what()).find("please select the ")!=std::string::npos;}
            check(clear,"wrong-slot patch diagnosis missing");
        }
        check(setup::expectedPatch(setup::PatchSlot::Patch141).size==562044991&&setup::expectedPatch(setup::PatchSlot::Patch150).size==2067515943,"official patch sizes changed");
        rejects([&]{setup::verifyPatch(v,setup::PatchSlot::Patch141);},"unknown patch size accepted");
        // Sparse synthetic file: exercise real streaming and cancellation without game bytes.
        save(root/"patch-sparse",{});setup::fs::resize_file(root/"patch-sparse",562044991);
        std::atomic<bool> stop{false};setup::Control hashControl;hashControl.cancelled=&stop;uint64_t hashed=0;unsigned notifications=0;
        hashControl.progress=[&](const setup::Progress& p){check(p.totalBytes==562044991&&p.completedBytes>=hashed,"patch progress totals");hashed=p.completedBytes;++notifications;if(hashed>=2*1024*1024)stop=true;};
        rejects([&]{setup::verifyPatch(setup::View(root/"patch-sparse"),setup::PatchSlot::Patch141,hashControl);},"patch hashing ignored cancellation");
        check(notifications>=3&&hashed==2*1024*1024,"streaming patch progress/cancellation boundary");
        hashControl.progress={};hashControl.cancelled=nullptr;
        rejects([&]{setup::verifyPatch(setup::View(root/"patch-sparse"),setup::PatchSlot::Patch141,hashControl);},"same-size fake executable accepted by streaming verifier");
        setup::fs::remove(root/"patch-sparse");
        auto game=root/"game";setup::fs::create_directory(game);save(game/"a",{'a','b','c'});
        auto manifest=root/"manifest.tsv";
        {std::ofstream m(manifest);m<<"a\t3\tba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n";}
        setup::verifyManifest(game,manifest);check(true,"valid final manifest");
        save(game/"a",{'x','b','c'});rejects([&]{setup::verifyManifest(game,manifest);},"corrupt final content accepted");
        save(game/"a",{'a','b','c'});save(game/"extra",{});rejects([&]{setup::verifyManifest(game,manifest);},"extra final file accepted");
        setup::verifyManifest(game,manifest,{},true);check(true,"runtime extra file rejected on reopen");
        save(game/"a",{'x','b','c'});rejects([&]{setup::verifyManifest(game,manifest,{},true);},"corrupt content accepted when extras are allowed");
        save(game/"a",{'a','b','c'});setup::fs::remove(game/"a");rejects([&]{setup::verifyManifest(game,manifest,{},true);},"missing file hidden by an extra file");
        save(game/"a",{'a','b','c'});setup::fs::remove(game/"extra");
        {
            // Settings the client saves over shipped ones: fine in a generation in use, never in a fresh one.
            auto settings=root/"game-settings";setup::fs::create_directories(settings/"mods/bf2/Settings");setup::fs::create_directories(settings/"mods/bf2/Other");
            save(settings/"mods/bf2/Settings/controls.con",{'a','b','c'});save(settings/"mods/bf2/Other/a",{'a','b','c'});
            auto shipped=root/"manifest-settings.tsv";
            {std::ofstream m(shipped);m<<"mods/bf2/Settings/controls.con\t3\tba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\nmods/bf2/Other/a\t3\tba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n";}
            setup::verifyManifest(settings,shipped);check(true,"valid manifest with settings");
            save(settings/"mods/bf2/Settings/controls.con",{'c','h','a','n','g','e','d'});save(settings/"Client.lock",{});save(settings/"mods/bf2/Settings/usersettings.con",{'x'});
            rejects([&]{setup::verifyManifest(settings,shipped);},"changed settings accepted in a fresh installation");
            setup::verifyManifest(settings,shipped,{},true);check(true,"settings saved by the client rejected on reopen");
            setup::fs::remove(settings/"mods/bf2/Settings/controls.con");
            rejects([&]{setup::verifyManifest(settings,shipped,{},true);},"missing shipped settings file accepted on reopen");
            save(settings/"mods/bf2/Settings/controls.con",{'c','h','a','n','g','e','d'});
            save(settings/"mods/bf2/Other/a",{'x','b','c'});
            rejects([&]{setup::verifyManifest(settings,shipped,{},true);},"changed game data beside the settings accepted");
            save(settings/"mods/bf2/Other/a",{'a','b','c'});
            // Links to the user's own mods: only directly in mods/, only in a generation in use.
            setup::fs::create_directories(root/"library/custom");save(root/"library/custom/file",{'x'});
            setup::fs::create_directory_symlink(root/"library/custom",settings/"mods/custom");
            setup::verifyManifest(settings,shipped,{},true);check(true,"link to a mod of the user rejected on reopen");
            rejects([&]{setup::verifyManifest(settings,shipped);},"symlink accepted in a fresh installation");
            setup::fs::remove(settings/"mods/custom");setup::fs::create_directory_symlink(root/"library/custom",settings/"mods/bf2/custom");
            rejects([&]{setup::verifyManifest(settings,shipped,{},true);},"symlink below a shipped mod accepted");
            setup::fs::remove(settings/"mods/bf2/custom");
            setup::fs::rename(settings/"mods/bf2/Other",settings/"mods/bf2/settingsx");
            rejects([&]{setup::verifyManifest(settings,shipped,{},true);},"a directory merely named like settings was exempted");
        }
        std::atomic<bool> cancelled{true};setup::Control ctrl;ctrl.cancelled=&cancelled;
        rejects([&]{setup::verifyManifest(game,manifest,ctrl);},"verification ignored cancellation");
        // Special Forces disc content is identified by destination, case-insensitively.
        check(setup::expansionContent("mods/xpack/Objects_client.zip")&&setup::expansionContent("Mods/XPack/Levels/Warlord/client.zip")&&setup::expansionContent("SupportXP1/config.xml"),"expansion destinations not recognised");
        check(!setup::expansionContent("mods/bf2/Objects_client.zip")&&!setup::expansionContent("mods/xpackish/x")&&!setup::expansionContent("Support/ereg-dict-en_US.xml")&&!setup::expansionContent("RendDX9.dll"),"base destinations treated as expansion");
        auto platform=setup::nativePlatform();auto lock=platform->lock(root/"lock");
        rejects([&]{auto second=platform->lock(root/"lock");},"concurrent setup lock accepted");lock.reset();
        {auto again=platform->lock(root/"lock");check(true,"setup lock was not released");}
        save(root/"old",{1});save(root/"pending",{2});platform->syncFile(root/"pending");platform->replace(root/"pending",root/"old");
        check(setup::View(root/"old").all()==setup::Bytes({2}),"atomic pointer replacement");
        std::vector<setup::View> inventory;
        for(unsigned i=0;i<300;++i){auto path=root/("inventory-"+std::to_string(i));save(path,{42});inventory.emplace_back(path);}
        unsigned inventorySum=0;
        for(const auto& view:inventory)view.stream([&](const uint8_t* data,size_t n){if(n!=1)throw std::runtime_error("inventory stream length");inventorySum+=data[0];});
        check(inventorySum==300*42,"lazy large directory inventory");
        check(inventory.front().read(0,1)==setup::Bytes({42}),"stream input did not reopen");
        inventory.clear();
        setup::fs::remove_all(root);
        std::cout << checks << " setup checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
