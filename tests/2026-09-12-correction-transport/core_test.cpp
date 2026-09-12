#include "correction_transport_selftest.h"
#include <cstdio>
#include <cassert>
int main(){
 correction::TestWorkspace work;auto r=correction::run_selftest(work);
 std::printf("checks=%u failed_mask=%08x workspace=%zu sender=%zu stream=%zu\n",r.checks,r.failed_mask,sizeof(work),sizeof(correction::Sender),sizeof(correction::Stream));
 assert(r.passed());
 work.reset(16);
 // Every single-bit flip in a complete envelope must invalidate its outer CRC.
 for(unsigned at=0;at<256;++at)for(unsigned bit=0;bit<8;++bit){auto p=work.packets[0];p.bytes[at]^=uint8_t(1u<<bit);assert(!correction::wire_valid(p));}
 const uint8_t prefix[]={0x52,0x54,0x4d,0x31,1,1,0,0,0x39,0x30,0,0,1,0,0,0,16,0,0,0,16,0,0,0};
 assert(!std::memcmp(prefix,work.packets[0].bytes,sizeof(prefix)));
 correction::Sender sender;assert(sender.begin(12345,0));assert(sender.enqueue(1,work.source,16,0));assert(sender.enqueue(2,work.source,16,10));correction::Packet next;assert(sender.next(next,11)&&correction::u32(next.bytes+12)==2&&sender.superseded==1);sender.committed();assert(!sender.pending());
 // Deliberately valid outer checksums on malformed input must remain bounded.
 struct Guarded {uint32_t first=0xabcd1234;correction::Stream stream;uint32_t last=0x5678abcd;} guarded;
 uint32_t random=17;
 for(unsigned n=0;n<10000;++n){
  auto packet=work.packets[0];random=random*1664525u+1013904223u;unsigned pos=4+random%248;packet.bytes[pos]^=uint8_t((random>>16)|1);correction::seal(packet);
  guarded.stream.select(12345,0);
  for(auto byte:packet.bytes)if(guarded.stream.push(byte,n)){assert(guarded.stream.receiver.size()<=correction::max_rtcm);assert(correction::valid_rtcm(guarded.stream.receiver.data(),guarded.stream.receiver.size()));}
  assert(guarded.first==0xabcd1234&&guarded.last==0x5678abcd);
 }
 std::puts("PASS: local suite, all 2048 single-bit errors, byte-order fixture, newest queued message and 10000 mutated envelopes");
}
