#include "KeyCheck.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned checks=0;
void check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
    ++checks;
}
std::string compact(std::string key) {
    std::string result;
    for(char c:key)if(c!='-')result+=c>='a'&&c<='z'?char(c-'a'+'A'):c;
    return result;
}
void ownerKey(setup::KeyEdition edition,const char* input) {
    using setup::validateKey;
    check(validateKey(edition,input),"Owner key rejected (value withheld)");
    auto key=compact(input);
    check(key.size()==20,"Owner key normalization failed");
    check(validateKey(edition,key),"Compact owner key rejected");
    auto other=edition==setup::KeyEdition::BF2?setup::KeyEdition::SpecialForces:setup::KeyEdition::BF2;
    check(!validateKey(other,key),"Key accepted for wrong product");
    std::string grouped,lower=key;
    for(size_t i=0;i<key.size();++i){if(i&&i%4==0)grouped+='-';grouped+=key[i];}
    for(auto& c:lower)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    check(validateKey(edition,grouped)&&validateKey(edition,lower),"Owner key presentation changed verdict");
    // Single-symbol damage, including checksum symbols and payload symbols.
    // Only a boolean is observed; no key, digest, or derived value is printed.
    for(size_t i=0;i<key.size();++i) {
        auto damaged=key;damaged[i]=key[i]=='A'?'B':'A';
        check(!validateKey(edition,damaged),"Corrupted owner key accepted");
    }
}
}
int main(int argc,char** argv) {
    try {
        using setup::KeyEdition;using setup::validateKey;
        if(argc==2&&std::string(argv[1])=="--owner-keys") {
            const auto bf2=std::getenv("BF2_TEST_KEY"),sf=std::getenv("BF2SF_TEST_KEY");
            if(!bf2&&!sf){std::cout<<"Owner key checks skipped: no key environment variables supplied\n";return 77;}
            if(bf2)ownerKey(KeyEdition::BF2,bf2);
            if(sf)ownerKey(KeyEdition::SpecialForces,sf);
            std::cout<<checks<<" owner key checks passed; values withheld\n";return 0;
        }
        if(argc!=1)return 2;
        for(auto edition:{KeyEdition::BF2,KeyEdition::SpecialForces}) {
            for(auto bad:{"", "A", "AAAAAAAAAAAAAAAAAAA", "AAAAAAAAAAAAAAAAAAAAA",
                         "AAAA-AAAA-AAAA-AAAA-AAA", "AAAA_AAAA_AAAA_AAAA_AAAA",
                         " AAAA-AAAA-AAAA-AAAA-AAAA", "AAAA-AAAA-AAAA-AAAA-AAAA ",
                         "ＡAAA-AAAA-AAAA-AAAA-AAAA", "AAAA-AAAA-AAAA-AAAA-AAA!",
                         "AAAA--AAAA-AAAA-AAAA-AAAA"})
                check(!validateKey(edition,bad),"Invalid key format accepted");
            check(!validateKey(edition,std::string(20,'\0')),"Embedded NUL accepted");
            check(!validateKey(edition,std::string(20,char(0xff))),"Non-ASCII accepted");
            // Format-correct placeholders, all rejected by the retail checksum.
            for(auto bad:{"AAAAAAAAAAAAAAAAAAAA","22222222222222222222",
                          "ABCD-EFGH-1234-5678-IJKL","1111-2222-3333-4444-5555"})
                check(!validateKey(edition,bad),"Format-only placeholder accepted");
            // Damaged checksum positions: neither 0 nor 1 can represent a
            // retail checksum symbol, independent of the remaining payload.
            for(size_t position:{size_t(2),size_t(4),size_t(5),size_t(7),size_t(12),size_t(1),size_t(3)}) {
                std::string damaged(20,'A');damaged[position]='0';
                check(!validateKey(edition,damaged),"Damaged checksum symbol accepted");
            }
            auto damaged=std::string(20,'A');damaged[3]='B';
            check(!validateKey(edition,damaged),"Inconsistent checksum redundancy accepted");
        }
        check(!validateKey(static_cast<KeyEdition>(99),std::string(20,'A')),"Unknown product accepted");
        std::cout<<checks<<" key rejection checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
