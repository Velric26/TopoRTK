#include "link_diagnostic_core.h"
#include <cassert>
#include <cstdio>
using namespace linktest;
void pair(unsigned fault,uint32_t offset=0){
  Engine a,b;Config c;c.run=123456;c.seconds=30;c.rate=3000;c.mode=2;
  assert(a.arm(c,0,offset)&&b.arm(c,1,offset));
  for(uint32_t tick=0;tick<39000;++tick){
    uint32_t now=offset+tick;Packet p;
    if(a.next(p,now)){a.transmitted(p,now);if(p.kind==Data&&p.sequence==3&&fault==1){}else{if(p.kind==Data&&p.sequence==3&&fault==2)p.payload[20]^=1;b.receive(p,now);}}
    if(b.next(p,now)){b.transmitted(p,now);a.receive(p,now);}
  }
  assert(a.state==Done&&b.state==Done&&a.peer_result&&b.peer_result);
  assert(a.pair_pass()==!fault&&b.pair_pass()==!fault);
  if(fault)assert(b.received==planned(c,0)-1);
  if(fault==2)assert(b.errors==1);
  assert(!a.arm(c,0,40000)); // no replay/restart with the previous run ID
}
int main(){
  assert(crc("123456789",9)==0xcbf43926u);
  pair(0);pair(1);pair(2);pair(0,0xfffff000u);
  Engine a;Config c;c.run=654321;c.seconds=30;c.rate=1000;
  assert(a.arm(c,0,0));a.tick(120000);assert(a.state==Failed&&!a.local_pass());
  c.run=111111;assert(a.arm(c,0,121000));a.abort(122000,"cancelled");assert(!a.pair_pass());
  c.seconds=65535;assert(!valid_config(c));
  std::puts("PASS: CRC vector, clean pair, dropped/corrupt data, wraparound, peer timeout, cancellation, replay and bounds");
}
