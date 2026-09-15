#include "update_package.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
int main(int argc,char **argv){
  assert(argc==3);std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});
  assert(data.size()>128);ota_package::Header h;std::memcpy(h.bytes,data.data(),128);
  const auto unit=uint8_t(argv[2][0]-'0');assert(ota_package::valid(h,unit,0x640000));assert(!ota_package::valid(h,3-unit,0x640000));
  assert(data.size()==ota_package::size(h)+128);ota_package::IdentityCheck check;check.begin(h);
  for(size_t i=128;i<data.size();i+=197)assert(check.feed(data.data()+i,std::min(size_t(197),data.size()-i),i-128));
  assert(check.complete());
  const auto offset=128+ota_package::identity_offset(h);assert(!std::memcmp(data.data()+offset,"TOPORTK_FW_V1",13));
  data[offset+16]=3-unit;check.begin(h);assert(!check.feed(data.data()+128,data.size()-128,0));assert(!check.complete());
  auto bad=h;bad.bytes[48]^=1;correction::put32(bad.bytes+124,correction::crc32(bad.bytes,124));
  check.begin(bad);data[offset+16]=unit;assert(!check.feed(data.data()+128,data.size()-128,0));
  std::puts("PASS: compiled image package size/target, embedded identity/version and streamed metadata check");
}
