#pragma once
#include "correction_transport.h"
namespace ota_package {
constexpr size_t header_size=128, identity_size=64;
constexpr uint32_t max_image=0x640000;
struct Header {uint8_t bytes[header_size]{};};
inline uint32_t size(const Header &h){return correction::u32(h.bytes+8);}
inline uint32_t identity_offset(const Header &h){return correction::u32(h.bytes+12);}
inline bool valid(const Header &h,uint8_t unit,uint32_t slot){
  const auto *b=h.bytes;const auto n=size(h),offset=identity_offset(h);
  if(std::memcmp(b,"TPK1",4)||b[4]!=1||b[5]!=unit||b[6]!=1||b[7]||
     n<256||n>max_image||n>slot||offset>n-identity_size||
     correction::u32(b+124)!=correction::crc32(b,124))return false;
  bool text=false,ended=false;
  for(unsigned i=48;i<80;++i){if(!b[i])ended=true;else {if(ended||b[i]<33||b[i]>126)return false;text=true;}}
  if(!text||!ended)return false;
  for(unsigned i=80;i<124;++i)if(b[i])return false;
  return true;
}
inline void identity(const Header &h,uint8_t *out){
  std::memset(out,0,identity_size);std::memcpy(out,"TOPORTK_FW_V1",13);
  out[16]=h.bytes[5];out[17]=h.bytes[6];std::memcpy(out+20,h.bytes+48,32);
}
class IdentityCheck {
  uint8_t expected_[identity_size]{};uint32_t offset_=0,seen_=0;bool matches_=true;
 public:
  void begin(const Header &h){identity(h,expected_);offset_=identity_offset(h);seen_=0;matches_=true;}
  bool feed(const uint8_t *p,size_t n,uint32_t position){
    for(size_t i=0;i<n;++i)if(uint64_t(position)+i>=offset_&&uint64_t(position)+i<offset_+identity_size){
      if(p[i]!=expected_[position+i-offset_])matches_=false;++seen_;
    }
    return matches_;
  }
  bool complete()const{return matches_&&seen_==identity_size;}
};
} // namespace ota_package
