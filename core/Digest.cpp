#include "Digest.h"
#include <algorithm>
namespace setup {
namespace {
uint32_t rot(uint32_t x, unsigned n) { return (x >> n) | (x << (32-n)); }
constexpr uint32_t k256[] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
constexpr uint32_t kmd5[] = {
0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
constexpr unsigned shifts[] = {7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21};
}
Digest::Digest(bool sha) : sha_(sha), h_(sha ? std::array<uint32_t,8>{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19} : std::array<uint32_t,8>{0x67452301,0xefcdab89,0x98badcfe,0x10325476,0,0,0,0}) {}
void Digest::compress(const uint8_t* b) {
    uint32_t w[64]{};
    for (unsigned i=0;i<16;++i) for (unsigned j=0;j<4;++j) w[i] |= uint32_t(b[4*i+j]) << (8*(sha_ ? 3-j : j));
    auto a=h_[0],c=h_[2],d=h_[3],v=h_[1];
    if (sha_) {
        for (unsigned i=16;i<64;++i) {
            auto x=w[i-15],y=w[i-2];
            w[i]=w[i-16]+(rot(x,7)^rot(x,18)^(x>>3))+w[i-7]+(rot(y,17)^rot(y,19)^(y>>10));
        }
        auto e=h_[4],f=h_[5],g=h_[6],h=h_[7];
        for (unsigned i=0;i<64;++i) {
            auto t=h+(rot(e,6)^rot(e,11)^rot(e,25))+((e&f)^(~e&g))+k256[i]+w[i];
            auto u=(rot(a,2)^rot(a,13)^rot(a,22))+((a&v)^(a&c)^(v&c));
            h=g;g=f;f=e;e=d+t;d=c;c=v;v=a;a=t+u;
        }
        h_[4]+=e;h_[5]+=f;h_[6]+=g;h_[7]+=h;
    } else {
        for (unsigned i=0;i<64;++i) {
            unsigned round=i/16, index;
            uint32_t f;
            if (!round) { f=(v&c)|(~v&d);index=i; }
            else if (round==1) { f=(d&v)|(~d&c);index=(5*i+1)%16; }
            else if (round==2) { f=v^c^d;index=(3*i+5)%16; }
            else { f=c^(v|~d);index=7*i%16; }
            auto t=d;d=c;c=v;v+=rot(a+f+kmd5[i]+w[index],32-shifts[round*4+i%4]);a=t;
        }
    }
    h_[0]+=a;h_[1]+=v;h_[2]+=c;h_[3]+=d;
}
void Digest::update(const uint8_t* data, size_t n) {
    bytes_+=n;
    while (n) {
        if (!used_ && n>=64) { compress(data);data+=64;n-=64;continue; }
        auto take=std::min(n,64-used_);std::copy_n(data,take,block_.data()+used_);used_+=take;data+=take;n-=take;
        if (used_==64) { compress(block_.data());used_=0; }
    }
}
std::string Digest::finish() {
    uint64_t bits=bytes_*8;
    uint8_t pad[128]{};pad[0]=0x80;
    auto n=used_<56 ? 56-used_ : 120-used_;
    update(pad,n);
    for(unsigned i=0;i<8;++i) pad[i]=uint8_t(bits>>(8*(sha_?7-i:i)));
    update(pad,8);
    static const char hex[]="0123456789abcdef";
    std::string result;
    for (unsigned i=0;i<(sha_?32u:16u);++i) {
        uint8_t b=uint8_t(h_[i/4]>>(8*(sha_?3-i%4:i%4)));
        result+=hex[b>>4];result+=hex[b&15];
    }
    return result;
}
}
