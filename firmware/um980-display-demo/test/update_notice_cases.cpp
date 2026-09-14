#include "../src/update_notice.h"
#include <cassert>
#include <cstdio>
#include <random>
using namespace update_notice;
Message unpack(const Packet &p) { Message m; assert(decode(p.bytes,sizeof(p.bytes),m)); return m; }
Message phase(Kind kind,uint32_t attempt,uint32_t sequence,uint32_t duration=60000) {
  Message m; m.from=1;m.to=2;m.session=7777777;m.attempt=attempt;m.sequence=sequence;m.kind=kind;
  m.duration=kind==Kind::Prepare||kind==Kind::Updating?duration:0;return m;
}
int main() {
  Packet packet; auto prepare=phase(Kind::Prepare,1,1); assert(encode(prepare,packet));
  // Every single-bit wire corruption, including CRC, is rejected.
  for(unsigned i=0;i<packet_size*8;++i){auto broken=packet;broken.bytes[i/8]^=1u<<(i%8);Message m;assert(!decode(broken.bytes,packet_size,m));}
  Message m;assert(!decode(packet.bytes,packet_size-1,m));assert(!decode(nullptr,packet_size,m));
  for(unsigned i=26;i<36;++i){auto reserved=packet;reserved.bytes[i]=1;correction::put32(reserved.bytes+36,correction::crc32(reserved.bytes,36));assert(!decode(reserved.bytes,packet_size,m));}
  auto bad=prepare;bad.from=0;assert(!encode(bad,packet));bad=prepare;bad.duration=max_notice_ms+1;assert(!encode(bad,packet));
  bad=prepare;bad.kind=Kind(99);assert(!encode(bad,packet));

  Peer peer;assert(peer.select(2,7777777));Message ack;
  assert(peer.receive(prepare,100,ack));assert(peer.state()==State::Preparing);
  assert(std::strstr(peer.label(),"paused")==nullptr);
  auto updating=phase(Kind::Updating,1,2);
  assert(peer.receive(updating,1000,ack));assert(peer.state()==State::Updating);
  bad=updating;bad.duration++;assert(!peer.receive(bad,1001,ack)); // A retry cannot change its contents.
  for(uint32_t now=1500;now<60100;now+=500) assert(peer.receive(updating,now,ack));
  peer.tick(60100);assert(peer.state()==State::Overdue);assert(!peer.receive(updating,60101,ack));
  assert(peer.select(2,7777777));assert(!peer.receive(prepare,60102,ack)); // No reset on repeated selection.
  bad=phase(Kind::Prepare,2,1);bad.session++;assert(!peer.receive(bad,60103,ack));
  bad=phase(Kind::Prepare,2,1);bad.from=2;bad.to=1;assert(!peer.receive(bad,60103,ack));
  assert(!peer.recovered(1,true,false));assert(!peer.recovered(1,false,true));
  assert(!peer.recovered(2,true,true));assert(peer.recovered(1,true,true));
  assert(!peer.receive(updating,61000,ack)); // A late notice cannot reopen recovered state.

  // Cancellation can overtake prepare/update. Keep a tombstone through retries.
  assert(peer.receive(phase(Kind::Cancel,2,3),62000,ack));assert(peer.state()==State::None);
  assert(!peer.receive(phase(Kind::Prepare,2,1),62001,ack));
  assert(!peer.receive(phase(Kind::Updating,2,2),62002,ack));
  assert(peer.receive(phase(Kind::Cancel,2,3),62003,ack));
  assert(!peer.receive(phase(Kind::Reconnected,3,3),62004,ack));
  // Missing prepare still allows the actual Updating notice; role stays pinned.
  assert(peer.receive(phase(Kind::Updating,3,2),63000,ack));
  bad=phase(Kind::Reconnected,3,3);bad.role=1;assert(!peer.receive(bad,63001,ack));
  assert(peer.receive(phase(Kind::Reconnected,3,3),64000,ack));assert(peer.state()==State::Reconnecting);
  assert(!peer.recovered(3,true,false));assert(peer.recovered(3,true,true));

  Peer wrap;assert(wrap.select(2,7777777));
  assert(wrap.receive(prepare,UINT32_MAX-999,ack));wrap.tick(59000);assert(wrap.state()==State::Overdue);

  Sender sender;assert(sender.begin(1,false,7777777,10,60000,0));
  assert(!sender.begin(1,false,7777777,11,60000,1)); // No second concurrent transaction.
  unsigned transmissions=0;
  for(uint32_t now=0;now<10000;++now)if(sender.next(packet,now)){sender.committed(now);++transmissions;}
  assert(transmissions==6);assert(!sender.acknowledged());
  assert(sender.transition(Kind::Updating,10000));assert(sender.next(packet,10000));
  const auto update=unpack(packet);assert(update.duration==50000);assert(peer.receive(update,10000,ack));
  auto wrong=ack;wrong.acknowledged=Kind::Prepare;assert(!sender.receive(wrong,10001));
  wrong=ack;wrong.attempt++;assert(!sender.receive(wrong,10001));
  wrong=ack;wrong.sequence--;assert(!sender.receive(wrong,10001));
  wrong=ack;wrong.session++;assert(!sender.receive(wrong,10001));
  assert(sender.receive(ack,10001));assert(sender.acknowledged());assert(!sender.next(packet,10002));
  assert(!sender.receive(ack,60000));
  assert(sender.transition(Kind::Cancel,11000));assert(!sender.transition(Kind::Cancel,11001));
  sender.close();assert(!sender.begin(1,false,7777777,10,60000,12000));
  assert(sender.begin(1,false,7777777,11,60000,12000));

  // Model loss/duplicates/reordering with real encoded packets. For every run,
  // duplicated or delayed notices must terminate by the first notice's deadline.
  std::mt19937 random(980);
  for(unsigned run=0;run<1000;++run) {
    Peer target;target.select(2,7777777);Message response;
    const auto p=phase(Kind::Prepare,1,1,10000),u=phase(Kind::Updating,1,2,9000);
    uint32_t first=0;bool seen=false;
    for(uint32_t now=0;now<20000;now+=100) {
      const auto candidate=(random()%3)?u:p;
      if(random()%4 && target.receive(candidate,now,response) && !seen){first=now;seen=true;}
      target.tick(now);
      if(seen && now-first>=10000)assert(target.state()==State::Overdue);
    }
    assert(seen && target.state()==State::Overdue);
  }
  std::puts("PASS: update notice wire corruption, bounded retry/loss, role/session/attempt isolation, reorder/cancel tombstones, deadline and timer wrap, recovery quality gate (1000 fault runs)");
}
