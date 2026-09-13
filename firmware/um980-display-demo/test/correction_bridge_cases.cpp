#include "correction_bridge.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace correction;
// Header fixtures only. Never transmitted to physical GNSS hardware.
std::vector<uint8_t> frame(unsigned type,uint32_t epoch=1000,size_t n=32,unsigned station=7){
  std::vector<uint8_t> p(n);p[0]=0xd3;p[1]=(n-6)>>8;p[2]=uint8_t(n-6);
  p[3]=type>>4;p[4]=((type&15)<<4)|(station>>8);p[5]=station;
  p[6]=epoch>>22;p[7]=epoch>>14;p[8]=epoch>>6;p[9]=epoch<<2;
  const auto crc=crc24(p.data(),n-3);p[n-3]=crc>>16;p[n-2]=crc>>8;p[n-1]=crc;return p;
}
int main(){
  BurstQueue q;auto ref=frame(1006),desc=frame(1033);auto obs=frame(1074,1000,1029);
  assert(q.enqueue(ref.data(),ref.size(),0,0));assert(q.enqueue(desc.data(),desc.size(),0,0));
  for(unsigned i=0;i<7;++i){auto f=frame(1071+i,1000,1029);assert(q.enqueue(f.data(),f.size(),i,i));}
  assert(q.size()==8&&q.overflow==1);assert(message_type(q.front(10)->data)==1006);q.pop(q.front(10));
  assert(message_type(q.front(10)->data)==1033);q.pop(q.front(10));
  // A repeated metadata stream cannot starve an observation already waiting.
  q.enqueue(ref.data(),ref.size(),10,10);assert(msm_type(message_type(q.front(10)->data)));q.pop(q.front(10));
  q.clear();q.enqueue(obs.data(),obs.size(),100,100);q.enqueue(obs.data(),obs.size(),101,101);assert(q.size()==2);
  obs=frame(1074,2000);q.enqueue(obs.data(),obs.size(),102,102);assert(q.size()==1&&q.replaced==2);
  q.tick(1602);assert(!q.size());assert(!q.enqueue(obs.data(),obs.size(),0,1500));
  obs.back()^=1;assert(!q.enqueue(obs.data(),obs.size(),0,0));obs.back()^=1;
  q.enqueue(obs.data(),obs.size(),0xfffffff0u,0xfffffff0u);assert(q.front(20));assert(!q.front(1600));
  StationGuard guard;assert(!guard.accept(obs.data(),obs.size()));assert(guard.accept(ref.data(),ref.size()));
  auto other=frame(1074,1000,32,8);assert(!guard.accept(other.data(),other.size()));assert(guard.accept(obs.data(),obs.size()));

  Bridge base,rover;constexpr uint32_t session=123456789;
  assert(!base.begin(913233,false));assert(base.begin(session,false)&&rover.begin(session,true));
  assert(!base.enqueue(obs.data(),obs.size(),0));assert(base.enqueue(ref.data(),ref.size(),0));
  auto move=[&](uint32_t now,int drop=-1,int corrupt=-1){unsigned index=0,got=0;Packet p;
    while(base.next(p,now)){
      auto send=p;if(int(index)==corrupt)send.bytes[100]^=1;
      if(int(index)!=drop)for(auto value:send.bytes)if(rover.byte(value,now)){
        assert(valid_rtcm(rover.data(),rover.size()));++got;
      }
      base.committed();++index;assert(index<60);
    }return got;
  };
  assert(move(0)==1&&rover.station()==7);
  // Four constellation frames in a burst survive the scheduler independently.
  for(unsigned type:{1074u,1084u,1094u,1124u}){auto f=frame(type,1000,1029);assert(base.enqueue(f.data(),f.size(),10));}
  assert(move(20)==4&&rover.complete==5);
  auto full=frame(1074,2000,1029);assert(base.enqueue(full.data(),full.size(),30));assert(move(40,1)==0);
  rover.tick(1100);assert(rover.stats().expired==1);
  full=frame(1074,3000,1029);base.enqueue(full.data(),full.size(),1200);assert(move(1210,-1,2)==0);
  full=frame(1074,4000,1029);base.enqueue(full.data(),full.size(),1400);assert(move(1410)==1);
  assert(rover.linked(1410)&&!rover.linked(4410));
  base.enqueue(full.data(),full.size(),1500);Packet old;assert(base.next(old,1600));
  assert(u32(old.bytes+24)==100);base.committed();
  // Session/source changes and diagnostic sessions cannot select the receiver.
  assert(rover.begin(session+1,true));for(auto v:old.bytes)assert(!rover.byte(v,1600));assert(rover.stats().wrong_session==1);
  auto wrong=old;put32(wrong.bytes+8,session+1);wrong.bytes[6]=1;seal(wrong);
  for(auto v:wrong.bytes)assert(!rover.byte(v,1600));
  assert(rover.stats().wrong_session==2);
  base.begin(session,false);rover.begin(session,true);base.enqueue(ref.data(),ref.size(),2000);
  Packet reference;assert(base.next(reference,2100));base.committed();unsigned delivered=0;
  for(auto v:reference.bytes)if(rover.byte(v,2110)){++delivered;assert(rover.known_age(2110)==100);}
  assert(delivered==1);for(auto v:reference.bytes)assert(!rover.byte(v,2200));assert(rover.stats().replays==1);
  // Carry measured age through a slow GNSS queue, rather than resetting it.
  q.clear();
  q.enqueue(ref.data(),ref.size(),2010,2110);assert(q.front(3509));assert(!q.front(3510));
  base.enqueue(full.data(),full.size(),3000);assert(!base.next(old,4500));assert(base.queue().expired>0);
  base.fail();assert(base.active()&&base.fault());assert(!base.enqueue(ref.data(),ref.size(),5000));assert(!base.next(old,5000));
  base.stop();assert(!base.active());
  std::printf("PASS: burst/reference retention, fairness, bounds, CRC, expiry/wrap, station/session isolation, replay, loss/corruption recovery and carried output age; queue=%zu bridge=%zu bytes\n",sizeof(q),sizeof(base));
}
