// Optional owner-media fault injection. All mutations are confined to an existing
// scratch support tree created by the CLI; no input/reference is ever modified.
#include "Installer.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const setup::fs::path& p){auto b=setup::View(p).all();return {b.begin(),b.end()};}
void write(const setup::fs::path& p,const std::string& text){std::ofstream out(p);out<<text;out.close();check(bool(out),"metadata write failed");}
struct FailPublication : setup::Platform {
    setup::Platform& delegate;
    explicit FailPublication(setup::Platform& p):delegate(p){}
    std::unique_ptr<setup::Lock> lock(const setup::fs::path& p)override{return delegate.lock(p);}
    void syncFile(const setup::fs::path& p)override{delegate.syncFile(p);}
    void syncDirectory(const setup::fs::path& p)override{delegate.syncDirectory(p);}
    void replace(const setup::fs::path& a,const setup::fs::path& b)override{
        if(b.filename()=="Current.txt")throw std::runtime_error("injected receipt-switch failure");delegate.replace(a,b);
    }
};
}
int main(int argc,char** argv){
    try {
        namespace fs=setup::fs;auto scratch=fs::canonical(SETUP_TEST_ROOT);
        if(argc==7&&std::string(argv[1])=="--deluxe-base") {
            // Owner Deluxe media installed without Special Forces: cancel once during
            // disc staging, refuse the transaction for an install with Special Forces,
            // resume, and check the published generation against the base manifest.
            auto support=fs::absolute(argv[5]).lexically_normal();
            check(support.parent_path()==scratch&&!fs::exists(support),"SUPPORT must be a new direct child of the scratch root");
            std::atomic<bool> cancelled{false};auto platform=setup::nativePlatform();
            setup::InstallOptions options{argv[2],argv[3],argv[4],support,argv[6],{}};options.specialForces=false;options.control.cancelled=&cancelled;
            options.control.progress=[&](const setup::Progress& p){if(p.phase=="disc"&&p.completedFiles>=100)cancelled=true;};
            bool stopped=false;try{setup::install(options,*platform);}catch(const std::exception& e){stopped=std::string(e.what())=="Cancelled; setup can resume";}
            check(stopped,"cancellation during the base-only disc phase was not honoured");
            check(fs::exists(support/"Setup/work/Inputs.txt"),"transaction journal missing after cancellation");
            setup::InstallOptions withExpansion=options;withExpansion.specialForces=true;withExpansion.control={};
            bool refused=false;try{setup::install(withExpansion,*platform);}catch(const std::exception& e){refused=std::string(e.what())=="Resume inputs changed; remove only Setup/work to start again";}
            check(refused,"the Special Forces choice did not invalidate the base-only transaction");
            cancelled=false;options.control.progress={};
            auto root=setup::install(options,*platform);
            check(!fs::exists(support/"Setup/work")&&fs::exists(support/"Setup/Current.txt"),"base-only install not published cleanly");
            check(!fs::exists(root/"SupportXP1/config.xml")&&!fs::exists(root/"mods/xpack/Levels")&&!fs::exists(root/"mods/xpack/Common_client.zip"),"Special Forces disc content installed although unchecked");
            size_t xpack=0;if(fs::exists(root/"mods/xpack"))for(const auto& e:fs::recursive_directory_iterator(root/"mods/xpack"))if(e.is_regular_file())++xpack;
            check(xpack==19,"mods/xpack must hold exactly the 19 files of the 1.50 overlay");
            std::string receipt;bool xp1Report=false;
            for(const auto& e:fs::directory_iterator(support/"Setup")){auto name=e.path().filename().string();if(name.rfind("Receipt-",0)==0)receipt=read(e.path());if(name.rfind("Patch141-XP1-",0)==0)xp1Report=true;}
            check(receipt.find("\nedition=deluxe-base\n")!=std::string::npos&&receipt.find("\nedition=deluxe\n")==std::string::npos,"receipt does not record the base-only Deluxe installation");
            check(!xp1Report,"XP1 patch report written for a base-only install");
            setup::verifyManifest(root,argv[6]);
            fs::remove_all(support);
            std::cout<<"Deluxe media without Special Forces: cancel/refuse/resume passed; generation verified against the base manifest, 19 overlay xpack files, deluxe-base receipt, no XP1 report\n";return 0;
        }
        if(argc==7&&std::string(argv[1])=="--cancel-resume") {
            // Owner-media cancel/resume: cancel during disc staging, again during the
            // 1.41 patch, then resume to a verified generation in a fresh support root.
            auto support=fs::absolute(argv[5]).lexically_normal();
            check(support.parent_path()==scratch&&!fs::exists(support),"SUPPORT must be a new direct child of the scratch root");
            std::atomic<bool> cancelled{false};auto platform=setup::nativePlatform();
            setup::InstallOptions options{argv[2],argv[3],argv[4],support,argv[6],{}};options.control.cancelled=&cancelled;
            auto cancelDuring=[&](const std::string& phase,size_t files){
                cancelled=false;options.control.progress=[&,phase,files](const setup::Progress& p){if(p.phase==phase&&p.completedFiles>=files)cancelled=true;};
                bool stopped=false;try{setup::install(options,*platform);}catch(const std::exception& e){stopped=std::string(e.what())=="Cancelled; setup can resume";}
                check(stopped,("cancellation during "+phase+" was not honoured").c_str());
                check(fs::exists(support/"Setup/work"),"transaction missing after cancellation");
                check(!fs::exists(support/"Setup/Current.txt"),"cancelled install published a generation");
            };
            cancelDuring("disc",200);cancelDuring("patch141",40);
            cancelled=false;options.control.progress={};
            auto root=setup::install(options,*platform);
            check(!fs::exists(support/"Setup/work"),"resumed transaction not cleaned");
            check(fs::exists(support/"Setup/Current.txt"),"resumed install not published");
            setup::verifyManifest(root,argv[6]);
            fs::remove_all(support);
            std::cout<<"Cancelled during disc staging and during patch 1.41; resume completed and verified; support root removed\n";return 0;
        }
        check(argc==6,"Usage: transaction_checks ISO PATCH141 PATCH150 SCRATCH_SUPPORT MANIFEST\n       transaction_checks --cancel-resume ISO PATCH141 PATCH150 NEW_SUPPORT MANIFEST");
        auto support=fs::canonical(argv[4]);
        auto relative=support.lexically_relative(scratch);check(!relative.empty()&&!relative.is_absolute()&&*relative.begin()!="..","support must be under scratch");
        auto setupRoot=support/"Setup";auto current=read(setupRoot/"Current.txt");
        auto newline=current.find('\n');check(newline!=std::string::npos,"invalid active pointer");
        auto active=support/setup::safePath(current.substr(0,newline));
        auto receiptName=current.substr(newline+1);receiptName.erase(receiptName.find('\n'));
        auto receipt=read(setupRoot/setup::safePath(receiptName));auto end=receipt.find("generation=");check(end!=std::string::npos,"invalid receipt");
        auto identities=receipt.substr(0,end);
        auto transaction=setupRoot/"work";check(!fs::exists(transaction),"transaction already exists");
        auto generation=support/"GameData/1.50-99990001";check(!fs::exists(generation),"fault-test generation already exists");
        fs::create_directory(generation);
        // Link only our own verified scratch outputs. No additional game bytes.
        for(const auto& e:fs::recursive_directory_iterator(active)) {
            check(!fs::is_symlink(e.symlink_status()),"symlink in test generation");auto dest=generation/e.path().lexically_relative(active);
            if(e.is_directory())fs::create_directory(dest);else fs::create_hard_link(e.path(),dest);
        }
        fs::create_directory(transaction);write(transaction/"Inputs.txt",identities);write(transaction/"Publish.txt","99990001");
        auto platform=setup::nativePlatform();FailPublication failing(*platform);
        setup::InstallOptions options{argv[1],argv[2],argv[3],support,argv[5],{}};
        bool failed=false;try{setup::install(options,failing);}catch(const std::exception& e){failed=std::string(e.what())=="injected receipt-switch failure";}
        check(failed,"publication failure was not injected");check(read(setupRoot/"Current.txt")==current,"failed publication changed active pointer");
        check(fs::exists(active),"old generation disappeared");
        auto recovered=setup::install(options,*platform);check(recovered==generation,"publication intent did not recover");
        check(!fs::exists(transaction),"recovered transaction not cleaned");
        setup::verifyManifest(recovered,argv[5]);
        std::cout<<"Publication failure retained prior generation; durable intent retry passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
