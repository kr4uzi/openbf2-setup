#include "key_check.h"
#include <array>
#include <cstdint>
#include <utility>

namespace setup {
namespace {
// Independently reconstructed from the retail helpers. See docs/cdkey-check.md.
constexpr std::string_view checksumAlphabet="23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
constexpr std::string_view intermediateAlphabet="64382957JKLMNPQRSTUVWXYZABCDEFGH";
constexpr std::array<size_t,7> positions{2,4,5,7,12,1,3};

uint32_t crcByte(uint32_t state, unsigned char byte) {
    state^=byte;
    for(unsigned bit=0;bit<8;++bit)
        state=(state>>1)^((state&1)?0xedb88320u:0u);
    return state;
}
}

bool validateKey(KeyEdition edition, std::string_view key) {
    uint32_t product;
    switch(edition) {
        case KeyEdition::BF2: product=119491;break;
        case KeyEdition::SpecialForces: product=121351;break;
        default:return false;
    }
    if(key.size()!=20&&key.size()!=24)return false;
    std::array<char,20> compact{};
    size_t count=0;
    for(size_t i=0;i<key.size();++i) {
        char c=key[i];
        if(key.size()==24&&(i+1)%5==0) {if(c!='-')return false;continue;}
        if(c>='a'&&c<='z')c=char(c-'a'+'A');
        if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')))return false;
        compact[count++]=c;
    }
    for(size_t i=0;i<positions.size();++i)std::swap(compact[positions[i]],compact[13+i]);

    // Read the supplied checksum. Its redundant bits are part of the check.
    std::array<uint32_t,7> claimed{};
    for(size_t i=0;i<claimed.size();++i) {
        auto value=checksumAlphabet.find(compact[13+i]);
        if(value==std::string_view::npos)return false;
        claimed[i]=uint32_t(value);
    }
    if((claimed[6]&7)!=(claimed[0]&7))return false;
    uint32_t expected=(claimed[0]<<27)|(claimed[1]<<22)|(claimed[2]<<17)|
                      (claimed[3]<<12)|(claimed[4]<<7)|(claimed[5]<<2)|(claimed[6]>>3);

    uint32_t a=1,b=0,crc=product;
    for(size_t i=0;i<13;++i) {
        auto c=static_cast<unsigned char>(compact[i]);
        a=(a+c)%65521;b=(b+a)%65521;
        crc=crcByte(crc,c);
    }
    const uint32_t intermediate=((b<<16)|a)^product;
    // Feed the retail intermediate representation directly into the checksum;
    // never construct or return a replacement key.
    crc=crcByte(crc,intermediateAlphabet[(intermediate>>30)|((intermediate&7)<<2)]);
    for(int shift=25;shift>=0;shift-=5)
        crc=crcByte(crc,intermediateAlphabet[(intermediate>>shift)&31]);
    return crc==expected;
}
bool keyFormat(const std::string& key) {
    if(key.size()!=20 && key.size()!=24)return false;
    for(size_t i=0;i<key.size();++i) {
        char c=key[i];
        if(key.size()==24 && (i+1)%5==0) {if(c!='-')return false;continue;}
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')))return false;
    }
    return true;
}
}
