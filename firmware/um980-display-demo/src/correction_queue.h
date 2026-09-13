#pragma once
#include "correction_transport.h"

namespace correction {
inline unsigned message_type(const uint8_t *p){return (unsigned(p[3])<<4)|(p[4]>>4);}
inline unsigned station_id(const uint8_t *p){return ((p[4]&15)<<8)|p[5];}
inline bool msm_type(unsigned type){return type/10>=107&&type/10<=113&&type%10>=1&&type%10<=7;}
inline uint32_t msm_epoch(const uint8_t *p){return (uint32_t(p[6])<<22)|(uint32_t(p[7])<<14)|(uint32_t(p[8])<<6)|(p[9]>>2);}
inline bool reference_type(unsigned type){return type==1005||type==1006;}
// Latch a station from a reference, never from wire session announcements.
// A station change requires an explicit local reset/route selection.
class StationGuard {
  int station_=-1;
 public:
  void reset(){station_=-1;}
  int station()const{return station_;}
  bool accept(const uint8_t *p,size_t n){
    if(!valid_rtcm(p,n))return false;
    const unsigned type=message_type(p);
    if(reference_type(type)){if(n<(type==1006?27u:25u))return false;}
    else if(type==1033){if(n<15)return false;}
    else if(!msm_type(type)||n<28)return false;
    const int station=int(station_id(p));
    if(station_<0&&reference_type(type))station_=station;
    return station_>=0&&station==station_;
  }
};

// Two reserved reference/descriptor slots plus six observation slots. Each
// slot holds a whole RTCM frame. This is not an atomic multi-message MSM epoch.
class BurstQueue {
 public:
  struct Frame {uint8_t data[max_rtcm]{};uint32_t at=0;uint16_t size=0;};
 private:
  Frame slots_[8]{};uint8_t reference_streak_=0;
  int oldest(unsigned first,unsigned end,uint32_t now)const{
    int found=-1;for(unsigned i=first;i<end;++i)if(slots_[i].size&&(found<0||now-slots_[i].at>now-slots_[found].at))found=int(i);return found;
  }
 public:
  uint32_t expired=0,replaced=0,overflow=0,rejected=0;
  void clear(){for(auto &f:slots_)f.size=0;reference_streak_=0;}
  unsigned size()const{unsigned n=0;for(const auto &f:slots_)n+=f.size!=0;return n;}
  void tick(uint32_t now){for(auto &f:slots_)if(f.size&&now-f.at>=age_limit_ms){f.size=0;++expired;}}
  bool enqueue(const uint8_t *p,size_t n,uint32_t at,uint32_t now){
    tick(now);
    if(!valid_rtcm(p,n)||now-at>=age_limit_ms){++rejected;return false;}
    const unsigned type=message_type(p);int slot=-1;
    if(reference_type(type))slot=0;
    else if(type==1033)slot=1;
    else {
      // Drop queued older epochs of the same message type, but preserve all
      // pieces of the current epoch. Already transmitting frames are separate.
      if(msm_type(type)&&n>=28)for(unsigned i=2;i<8;++i){auto &f=slots_[i];
        if(f.size>=28&&message_type(f.data)==type&&station_id(f.data)==station_id(p)){
          const uint32_t old=msm_epoch(f.data),fresh=msm_epoch(p);
          // GLONASS's day/TOD encoding is non-linear; arrival-order queue
          // replacement is deliberately disabled for that family.
          if(type/10!=108&&fresh<604800000&&old<604800000){const uint32_t advance=(fresh+604800000-old)%604800000;
            if(advance&&advance<302400000){f.size=0;++replaced;}
          }
        }
      }
      for(unsigned i=2;i<8;++i)if(!slots_[i].size){slot=int(i);break;}
      if(slot<0){slot=oldest(2,8,now);++overflow;}
    }
    auto &f=slots_[slot];if(f.size&&slot<2)++replaced;
    std::memcpy(f.data,p,n);f.size=uint16_t(n);f.at=at;return true;
  }
  const Frame *front(uint32_t now){
    tick(now);const int ref=oldest(0,2,now),obs=oldest(2,8,now);
    const int slot=ref>=0&&(reference_streak_<2||obs<0)?ref:obs;
    return slot<0?nullptr:&slots_[slot];
  }
  void pop(const Frame *f){
    if(!f)return;
    const size_t index=size_t(f-slots_);
    if(index>=8)return;
    reference_streak_=index<2?uint8_t(std::min(2,int(reference_streak_)+1)):0;slots_[index].size=0;
  }
};
static_assert(sizeof(BurstQueue)<8500,"Bounded correction queue memory");
} // namespace correction
