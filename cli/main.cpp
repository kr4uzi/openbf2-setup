#include "Installer.h"
#include "RtpBridge.h"
#include <array>
#include <csignal>
#include <iostream>
namespace {
std::atomic<bool> cancelled{false};
void cancel(int) { cancelled.store(true); }
setup::Control control() {
    setup::Control c;c.cancelled=&cancelled;
    c.progress=[](const setup::Progress& p) {
        std::cout << p.phase << '\t' << p.completedFiles << '/' << p.totalFiles << '\t' << p.completedBytes << '/' << p.totalBytes << '\t' << p.path << '\n' << std::flush;
    };return c;
}
}
int main(int argc,char** argv) {
    std::signal(SIGINT,cancel);
    try {
        if(argc==4 && std::string(argv[1])=="extract-disc") {setup::exportDisc(argv[2],argv[3],control());return 0;}
        if(argc==7 && std::string(argv[1])=="install") {
            auto platform=setup::nativePlatform();
            auto root=setup::install({argv[2],argv[3],argv[4],argv[5],argv[6],control()},*platform);
            std::cout << "Published " << root << '\n';return 0;
        }
        if(argc==4 && std::string(argv[1])=="verify") {setup::verifyManifest(argv[2],argv[3],control());return 0;}
        if(argc==3 && std::string(argv[1])=="hash") {std::cout<<setup::sha256(setup::View(argv[2]))<<'\n';return 0;}
        if(argc==8 && std::string(argv[1])=="prototype") return setup::prototype({argv[2],argv[3],argv[4],argv[5],argv[6],argv[7]});
        if(argc==5 && std::string(argv[1])=="compare") return setup::compare(argv[2],argv[3],argv[4]);
        if(argc==3 && std::string(argv[1])=="records") {
            auto files=setup::readSfx(setup::View(argv[2]));auto bytes=files.at("disk1/bf2/patch.rtp").all();
            auto p=obf2_rtp_open(bytes.data(),bytes.size());if(!p)throw std::runtime_error(obf2_rtp_error());
            for(size_t i=0;i<obf2_rtp_count(p);++i) {std::array<char,4096> name{};obf2_rtp_name(p,i,name.data(),name.size());std::cout<<name.data()<<'\t'<<obf2_rtp_terminal(p,i)<<'\n';}
            obf2_rtp_close(p);return 0;
        }
        if(argc==3 && (std::string(argv[1])=="iso" || std::string(argv[1])=="sfx")) {
            setup::View input(argv[2]);auto files=std::string(argv[1])=="iso"?setup::readIso(input):setup::readSfx(input);
            for(const auto& f:files)std::cout<<f.first<<'\t'<<f.second.size()<<'\n';return 0;
        }
        std::cerr<<"Usage:\n  bf2_setup install DISC PATCH141 PATCH150 SUPPORT_ROOT MANIFEST\n  bf2_setup extract-disc DISC NEW_OUTPUT\n  bf2_setup verify GAME MANIFEST\n  bf2_setup prototype ISO PATCH141 PATCH150 SCRATCH REFERENCE ACCEPTANCE\n  bf2_setup compare GAME REFERENCE ACCEPTANCE\n  bf2_setup iso|sfx|records|hash INPUT\n";return 2;
    } catch(const std::exception& e) {std::cerr<<"Setup incomplete: "<<e.what()<<'\n';return 1;}
}
