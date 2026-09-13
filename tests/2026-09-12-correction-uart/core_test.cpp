#include "correction_pair_test.h"
#include <cassert>
#include <iostream>
using correctiontest::Engine;
void transfer(Engine &tx,Engine &rx,uint32_t now,bool lose){
  correction::Packet packet;if(!tx.next(packet,now))return;
  const bool missing=lose&&!std::memcmp(packet.bytes,"RTM1",4)&&correction::u32(packet.bytes+12)==1;
  if(!missing)for(auto value:packet.bytes)rx.byte(value,now);
  tx.committed(packet,now);
}
void pair(uint8_t profile,uint16_t seconds,bool lose,uint32_t start){
  Engine base,rover;assert(base.arm(913230,seconds,0,start,profile));assert(rover.arm(913230,seconds,1,start,profile));
  for(uint32_t elapsed=0;elapsed<uint32_t(seconds)*1000+10000;elapsed+=10){
    transfer(base,rover,start+elapsed,lose);transfer(rover,base,start+elapsed,false);
  }
  assert(base.state==correctiontest::Done&&rover.state==correctiontest::Done);
  assert(base.sent==seconds/2u);assert(rover.accepted==rover.expected()-(lose?1:0));
  assert(base.pair_pass()==!lose&&rover.pair_pass()==!lose);assert(rover.invalid==0);
  assert(rover.first_missing()==(lose?1u:0u));assert(base.first_missing()==0);
  if(profile==correctiontest::Clean){assert(!base.dropped&&!base.corrupted&&!base.duplicated);assert(!rover.stream.receiver.stats.wire_errors&&!rover.stream.receiver.stats.duplicates);}
}
int main(){
  for(auto profile:{correctiontest::Clean,correctiontest::Injected})for(auto seconds:{30,60,120,300}){pair(profile,seconds,false,0);pair(profile,seconds,true,0xfffff000u);}
  Engine a,b;assert(!a.arm(913231,30,0,0,2));assert(a.arm(913231,30,0,0,correctiontest::Clean));assert(b.arm(913231,30,1,0,correctiontest::Injected));
  for(uint32_t now=0;now<121000;now+=10){transfer(a,b,now,false);transfer(b,a,now,false);}
  assert(a.state==correctiontest::Failed&&b.state==correctiontest::Failed);assert(!a.sent&&!b.accepted);
  // A stalled output adapter must expire a partially transmitted message and recover.
  Engine c,d;c.arm(913232,30,0,0,correctiontest::Clean);d.arm(913232,30,1,0,correctiontest::Clean);
  for(uint32_t now=0;now<40000;now+=10){
    correction::Packet p;if(c.next(p,now)){
      bool stall=!std::memcmp(p.bytes,"RTM1",4)&&correction::u32(p.bytes+12)==1&&correction::u16(p.bytes+18)>0;
      if(!stall){for(auto value:p.bytes)d.byte(value,now);c.committed(p,now);}
    }
    transfer(d,c,now,false);
  }
  assert(c.tx_expired==1&&d.accepted==14&&!d.invalid&&!c.pair_pass()&&!d.pair_pass());
  std::cout<<"PASS clean/injected profiles: all durations, loss, timer wrap, profile isolation, missing sequence and stalled-adapter expiry/recovery; workspace "<<sizeof(Engine)<<" bytes\n";
}
