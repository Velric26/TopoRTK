#include "pair_session.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using pair_session::Engine;
using pair_session::Transport;
using correction::Packet;
namespace {
uint32_t rng=0x7e1a2b39u;
uint32_t random_word(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
struct Sent {bool from_rover;Packet packet;};
struct Pair {
  Engine base,rover;
  uint32_t now=100;
  bool drop_base=false,drop_rover=false,duplicate=false;
  unsigned lose[2][7]{};
  std::vector<Sent> history;
  void begin(Transport transport=Transport::Radio){
    base.begin(1,false,transport,101,now,random_word);
    rover.begin(2,true,transport,202,now,random_word);
  }
  void send(Engine &source,Engine &destination,bool from_rover){
    Packet packet,again;
    if(!source.next(packet,now))return;
    assert(source.next(again,now));
    assert(!std::memcmp(packet.bytes,again.bytes,sizeof(packet.bytes)));
    source.committed(packet,now);
    history.push_back({from_rover,packet});
    unsigned &remaining=lose[from_rover?1:0][packet.bytes[17]];
    if(remaining){--remaining;return;}
    if(from_rover?drop_rover:drop_base)return;
    destination.receive(packet,now);
    if(duplicate)destination.receive(packet,now);
  }
  void step(){
    base.tick(now);rover.tick(now);
    send(base,rover,false);send(rover,base,true);
    now+=50;
  }
  void run(uint32_t duration){for(uint32_t elapsed=0;elapsed<duration;elapsed+=50)step();}
  void connected() const{
    const auto b=base.snapshot(now),r=rover.snapshot(now);
    assert(b.connected&&r.connected&&b.established&&r.established);
    assert(b.session>999999&&b.session==r.session);
    assert(b.peer_boot==r.local_boot&&r.peer_boot==b.local_boot);
  }
  Packet packet(bool from_rover,uint8_t kind) const{
    for(const auto &sent:history)if(sent.from_rover==from_rover&&sent.packet.bytes[17]==kind)return sent.packet;
    assert(false);return Packet{};
  }
};
void startup_orders(){
  for(auto transport:{Transport::WiFi,Transport::Radio}){
    for(unsigned order=0;order<3;++order){
      Pair p;
      if(order==0)p.begin(transport);
      if(order==1){
        p.base.begin(1,false,transport,101,p.now,random_word);p.run(6000);
        assert(!p.base.snapshot(p.now).connected);
        p.rover.begin(2,true,transport,202,p.now,random_word);
      }
      if(order==2){
        p.rover.begin(2,true,transport,202,p.now,random_word);p.run(6000);
        assert(!p.rover.snapshot(p.now).connected);
        p.base.begin(1,false,transport,101,p.now,random_word);
      }
      p.run(10000);p.connected();
    }
  }
}
void loss_and_backpressure(){
  Pair p;p.begin();p.duplicate=true;
  for(auto &direction:p.lose)for(unsigned kind=1;kind<=6;++kind)direction[kind]=2;
  // Preparing output repeatedly never uses a retry or invents connectivity.
  Packet first,repeated;assert(p.rover.next(first,p.now));
  for(unsigned i=0;i<100;++i){assert(p.rover.next(repeated,p.now+200));assert(!std::memcmp(first.bytes,repeated.bytes,sizeof(first.bytes)));}
  assert(!p.rover.snapshot(p.now).connected);
  p.run(18000);p.connected();
  // Acceptance on Base is not connection evidence before Rover gets Confirm.
  Pair q;q.begin();q.lose[0][5]=100;q.run(6000);
  assert(q.base.snapshot(q.now).established);
  assert(!q.base.snapshot(q.now).connected&&!q.rover.snapshot(q.now).established);
  q.lose[0][5]=0;q.run(6000);q.connected();
}
void codec_boundaries(){
  Pair p;p.begin();p.run(6000);p.connected();
  const Packet hello=p.packet(true,1);
  const auto generation=p.base.snapshot(p.now).generation;
  const auto session=p.base.snapshot(p.now).session;
  // Malformed or misdirected control is refused without touching the session.
  auto reject=[&](Packet bad){correction::seal(bad);assert(!p.base.receive(bad,p.now));assert(p.base.snapshot(p.now).generation==generation);};
  // A recognized duplicate of the accepted solicitation is idempotent: it may be
  // reported accepted, but it never changes the proven identity.
  auto inert=[&](Packet bad){correction::seal(bad);p.base.receive(bad,p.now);const auto s=p.base.snapshot(p.now);assert(s.generation==generation&&s.session==session&&s.connected);};
  for(unsigned i=52;i<252;++i){Packet bad=hello;bad.bytes[i]=1;reject(bad);}
  for(unsigned offset:{6u,7u,8u,22u,23u,44u,48u}){Packet bad=hello;bad.bytes[offset]=1;reject(bad);}
  for(unsigned offset:{18u,19u}){Packet bad=hello;bad.bytes[offset]=3;reject(bad);}
  {Packet bad=hello;bad.bytes[20]=0;reject(bad);}
  {Packet bad=hello;bad.bytes[21]=0;reject(bad);}
  {Packet bad=hello;bad.bytes[17]=7;reject(bad);}
  {Packet bad=hello;bad.bytes[16]=3;reject(bad);}
  {Packet bad=hello;bad.bytes[4]=3;reject(bad);}
  {Packet bad=hello;correction::put32(bad.bytes+24,0);reject(bad);}
  {Packet bad=hello;correction::put32(bad.bytes+32,0);reject(bad);}
  {Packet bad=hello;correction::put32(bad.bytes+40,1000000);reject(bad);}
  {Packet bad=hello;correction::put32(bad.bytes+28,0);inert(bad);}
  {Packet bad=hello;correction::put32(bad.bytes+36,1);inert(bad);}
  {Packet bad=hello;bad.bytes[33]^=1;assert(!p.base.receive(bad,p.now));}
  const Packet offer=p.packet(false,3);
  {Packet bad=offer;correction::put32(bad.bytes+40,999999);correction::seal(bad);assert(!p.rover.receive(bad,p.now));}
  p.connected();
}
void explicit_errors(){
  Pair p;p.begin();p.run(6000);
  Packet old=p.packet(true,1);old.bytes[16]=2;correction::seal(old);
  Engine waiting;waiting.begin(1,false,Transport::Radio,101,p.now,random_word);
  assert(!waiting.receive(old,p.now));assert(!std::strcmp(waiting.snapshot(p.now).reason,"protocol_incompatible"));
  old.bytes[16]=1;old.bytes[4]=2;correction::seal(old);
  assert(!waiting.receive(old,p.now));assert(!std::strcmp(waiting.snapshot(p.now).reason,"protocol_incompatible"));
  waiting.incompatible(p.now);
  p.base=waiting;p.run(10000);p.connected();
  // A recognized old frame cannot tear down a good session or renew its age.
  const auto active=p.base.snapshot(p.now);assert(!p.base.receive(old,p.now));
  assert(p.base.snapshot(p.now).session==active.session&&p.base.snapshot(p.now).connected);
  Pair same;
  same.base.begin(1,false,Transport::Radio,11,same.now,random_word);
  same.rover.begin(2,false,Transport::Radio,22,same.now,random_word);
  same.run(2000);
  assert(!same.base.snapshot(same.now).established&&!same.rover.snapshot(same.now).established);
  assert(!std::strcmp(same.base.snapshot(same.now).reason,"same_role"));
  Pair absent;absent.base.begin(1,false,Transport::Radio,11,absent.now,random_word);absent.run(21000);
  assert(!std::strcmp(absent.base.snapshot(absent.now).reason,"peer_unreachable"));
  absent.rover.begin(2,true,Transport::Radio,22,absent.now,random_word);absent.run(10000);absent.connected();
}
void outage_and_replay(){
  Pair p;p.begin();p.run(8000);p.connected();
  const auto b=p.base.snapshot(p.now),r=p.rover.snapshot(p.now);
  const auto old=p.history;
  p.drop_base=p.drop_rover=true;
  for(unsigned i=0;i<200;++i){
    p.step();
    for(const auto &sent:old){
      if(sent.packet.bytes[17]!=6)continue;
      (sent.from_rover?p.base:p.rover).receive(sent.packet,p.now);
    }
  }
  assert(!p.base.snapshot(p.now).connected&&!p.rover.snapshot(p.now).connected);
  assert(p.base.snapshot(p.now).session==b.session&&p.base.snapshot(p.now).generation==b.generation);
  assert(p.rover.snapshot(p.now).session==r.session&&p.rover.snapshot(p.now).generation==r.generation);
  p.drop_base=p.drop_rover=false;p.run(6000);p.connected();
  assert(p.base.snapshot(p.now).session==b.session&&p.rover.snapshot(p.now).generation==r.generation);
  // A receive-only path is not a mutual peer exchange.
  p.drop_base=true;p.run(6000);
  assert(!p.base.snapshot(p.now).connected&&!p.rover.snapshot(p.now).connected);
  p.drop_base=false;p.run(6000);p.connected();
}
void restarts_and_old_candidates(){
  Pair p;p.begin();p.run(8000);p.connected();
  const auto old=p.history;
  auto previous=p.base.snapshot(p.now).session;
  p.rover.begin(2,true,Transport::Radio,203,p.now,random_word);p.run(10000);p.connected();
  assert(p.base.snapshot(p.now).session!=previous);previous=p.base.snapshot(p.now).session;
  p.base.begin(1,false,Transport::Radio,102,p.now,random_word);p.run(10000);p.connected();
  assert(p.base.snapshot(p.now).session!=previous);
  const auto b=p.base.snapshot(p.now),r=p.rover.snapshot(p.now);
  // Replay every previous kind while ordinary traffic still flows. Discovery
  // can solicit a candidate but cannot replace the proven production identity.
  for(unsigned i=0;i<100;++i){
    for(const auto &sent:old)(sent.from_rover?p.base:p.rover).receive(sent.packet,p.now);
    p.step();
    assert(p.base.snapshot(p.now).generation==b.generation);
    assert(p.rover.snapshot(p.now).generation==r.generation);
  }
  p.connected();
  // Same boot, changed software roles: new challenges, not boot-derived session.
  p.base.begin(1,true,Transport::Radio,102,p.now,random_word);
  p.rover.begin(2,false,Transport::Radio,203,p.now,random_word);
  p.run(10000);p.connected();assert(p.base.snapshot(p.now).session!=b.session);
  // Re-entering the same role and boot must also reject the prior transcript.
  const auto swapped=p.history;const auto session=p.base.snapshot(p.now).session;
  p.base.begin(1,true,Transport::Radio,102,p.now,random_word);
  for(const auto &sent:swapped)if(!sent.from_rover)p.rover.receive(sent.packet,p.now);
  p.run(12000);p.connected();assert(p.base.snapshot(p.now).session!=session);
}
void replayed_hello_cannot_rotate_session(){
  Pair p;p.begin();p.run(8000);p.connected();
  const auto initial=p.base.snapshot(p.now);
  // The Base publishes one untargeted discovery Hello before establishment.
  const Packet discovery=p.packet(false,1);
  assert(discovery.bytes[17]==1&&!correction::u32(discovery.bytes+28)&&!correction::u32(discovery.bytes+36));
  // A solicited reply binds the observed boot and echoes its discovery token.
  p.rover.receive(discovery,p.now);
  Packet solicited;assert(p.rover.next(solicited,p.now));
  assert(solicited.bytes[17]==1&&!correction::u32(solicited.bytes+40));
  assert(correction::u32(solicited.bytes+28)==correction::u32(discovery.bytes+24));
  assert(correction::u32(solicited.bytes+36)==correction::u32(discovery.bytes+32));
  // The live Base retired that token when it established, so it refuses the
  // solicitation instead of restarting negotiation.
  const auto generation=p.base.snapshot(p.now).generation;
  assert(!p.base.receive(solicited,p.now));
  assert(p.base.snapshot(p.now).generation==generation);
  // Replaying the retired Hello, and an older boot's Hello, cannot rotate or
  // invalidate the proven session while ordinary traffic still flows.
  Packet foreign=discovery;correction::put32(foreign.bytes+24,555);correction::seal(foreign);
  for(unsigned i=0;i<60;++i){
    p.rover.receive(discovery,p.now);p.rover.receive(foreign,p.now);
    p.step();
    assert(p.base.snapshot(p.now).session==initial.session&&p.rover.snapshot(p.now).session==initial.session);
    assert(p.base.snapshot(p.now).generation==initial.generation);
    assert(p.rover.snapshot(p.now).generation==initial.generation);
  }
  p.connected();
  // A genuine same-boot restart publishes a fresh token and recovers normally.
  p.base.begin(1,false,Transport::Radio,101,p.now,random_word);
  p.run(12000);p.connected();
  assert(p.base.snapshot(p.now).session!=initial.session);
}
void wraparound(){
  Pair p;p.now=UINT32_MAX-2500;p.begin();p.run(10000);p.connected();
  const auto generation=p.base.snapshot(p.now).generation;
  p.drop_base=p.drop_rover=true;p.run(5000);
  assert(!p.base.snapshot(p.now).connected&&!p.rover.snapshot(p.now).connected);
  p.drop_base=p.drop_rover=false;p.run(6000);p.connected();
  assert(p.base.snapshot(p.now).generation==generation);
}
}
int main(){
  startup_orders();loss_and_backpressure();codec_boundaries();explicit_errors();
  outage_and_replay();restarts_and_old_candidates();replayed_hello_cannot_rotate_session();wraparound();
  std::puts("PASS: production pair startup, lossy proof, strict codec, replay, reboot, role, outage and clock-wrap invariants");
}
