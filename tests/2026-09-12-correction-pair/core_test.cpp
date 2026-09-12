#include "correction_pair_test.h"
#include <cassert>
#include <iostream>
using correctiontest::Engine;
void run(bool radio_loss,uint32_t start=0){
  Engine a,b;assert(a.arm(913220,30,0,start));assert(b.arm(913220,30,1,start));
  assert(!a.arm(913221,30,0,start));
  for(uint32_t elapsed=0;elapsed<40000;elapsed+=10){
    const uint32_t now=start+elapsed;
    for(unsigned node=0;node<2;++node){auto &tx=node?b:a;auto &rx=node?a:b;correction::Packet p;
      if(tx.next(p,now)){
        bool lose=radio_loss&&!std::memcmp(p.bytes,"RTM1",4)&&correction::u32(p.bytes+12)==1;
        if(!lose)for(auto v:p.bytes)rx.byte(v,now);
        tx.committed(p,now);
      }
    }
  }
  assert(a.state==correctiontest::Done&&b.state==correctiontest::Done);
  assert(a.sent==15&&b.accepted==(radio_loss?9u:10u));assert(!b.invalid);
  assert(a.corrupted==3&&a.dropped==2&&a.duplicated==3);assert(b.stream.receiver.stats.wire_errors==3);
  assert(b.stream.receiver.stats.duplicates==3);assert(a.pair_pass()==!radio_loss&&b.pair_pass()==!radio_loss);
}
int main(){
  run(false);run(true);run(false,0xfffff000u);
  Engine a;assert(!a.arm(123,30,0,0));assert(!a.arm(913221,31,0,0));assert(a.arm(913221,30,0,0));a.tick(120000);assert(a.state==correctiontest::Failed);assert(!a.local_pass());
  Engine b;assert(b.arm(913222,30,1,0));a.arm(913223,30,0,120001);correction::Packet p;assert(a.next(p,120001));for(auto v:p.bytes)b.byte(v,1);assert(b.state==correctiontest::Armed);
  // Matching abort is finite and cannot pass; invalid control CRC cannot start a run.
  Engine c,d;c.arm(913224,30,0,0);d.arm(913224,30,1,0);c.next(p,0);p.bytes[252]^=1;for(auto v:p.bytes)d.byte(v,0);assert(d.state==correctiontest::Armed);
  c.abort(1,"cancelled");assert(c.next(p,1));for(auto v:p.bytes)d.byte(v,1);assert(d.state==correctiontest::Failed);
  std::cout<<"PASS paired core: injection/recovery, additional radio loss, timer wrap, mismatch, timeout, corrupt control and abort; workspace "<<sizeof(Engine)<<" bytes\n";
}
