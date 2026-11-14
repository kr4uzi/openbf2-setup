#include "patch.h"
#include "digest.h"
#include <stdexcept>

namespace setup {
const PatchIdentity& expectedPatch(PatchSlot slot) {
    static const PatchIdentity first{562044991,"7bfd6236b67c0380acb522d237273d51a2764769a8f9c223098f93350bdd5c64"};
    static const PatchIdentity second{2067515943,"c5b150c96916a4c642daff03da3e9e52a7bccec6a7d6c6492494b57733c77977"};
    return slot==PatchSlot::Patch141?first:second;
}
void requirePatchIdentity(PatchSlot slot,const PatchIdentity& actual) {
    const auto& expected=expectedPatch(slot);
    if(actual.size==expected.size&&actual.hash==expected.hash)return;
    auto otherSlot=slot==PatchSlot::Patch141?PatchSlot::Patch150:PatchSlot::Patch141;
    const auto& other=expectedPatch(otherSlot);
    std::string wanted=slot==PatchSlot::Patch141?"1.41":"1.5";
    if(actual.size==other.size&&actual.hash==other.hash)
        throw std::runtime_error("This is the "+std::string(otherSlot==PatchSlot::Patch141?"1.41":"1.5")+" patch; please select the "+wanted+" patch here.");
    throw std::runtime_error("The selected file does not match the official "+wanted+" patch (size/SHA-256). Choose the original, complete patch executable.");
}
PatchIdentity verifyPatch(const View& view,PatchSlot slot,const Control& control) {
    Progress p{"patch-check",slot==PatchSlot::Patch141?"Patch 1.41":"Patch 1.5",0,view.size(),0,1};
    auto notify=[&]{if(control.cancelled&&control.cancelled->load())throw std::runtime_error("Cancelled; setup can resume");if(control.progress)control.progress(p);};
    notify();
    // Unknown sizes cannot match either pin; avoid hashing arbitrary large files.
    if(view.size()!=expectedPatch(PatchSlot::Patch141).size&&view.size()!=expectedPatch(PatchSlot::Patch150).size)
        requirePatchIdentity(slot,{view.size(),{}});
    Digest hash;
    view.stream([&](const uint8_t* data,size_t n){hash.update(data,n);p.completedBytes+=n;notify();});
    PatchIdentity result{view.size(),hash.finish()};requirePatchIdentity(slot,result);
    p.completedFiles=1;notify();return result;
}
}

namespace setup::assistant {
void validatePatches(const fs::path& a,const fs::path& b,const Control& control) {
    verifyPatch(View(a),PatchSlot::Patch141,control);
    verifyPatch(View(b),PatchSlot::Patch150,control);
}
}
