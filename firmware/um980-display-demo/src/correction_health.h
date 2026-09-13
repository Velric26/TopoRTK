#pragma once
#include "correction_transport.h"

namespace correction {
// Observation arrival and receiver-reported age are distinct. Neither metadata
// nor repeated MSM epochs may keep correction readiness alive.
class Health {
  uint32_t epochs_[7]{},last_=0;uint8_t seen_=0;bool observed_=false;uint16_t station_=0;
 public:
  uint32_t observations=0,repeated_or_old=0,invalid_epoch=0;
  void reset(){*this=Health{};}
  uint32_t arrival_age(uint32_t now)const{return observed_?now-last_:UINT32_MAX;}
  uint16_t station()const{return station_;}
  bool observe(const uint8_t *frame,size_t size,uint32_t now){
    if(!valid_rtcm(frame,size)||size<28)return false;
    const unsigned type=(unsigned(frame[3])<<4)|(frame[4]>>4),family=type/10;
    if(family<107||family>113||type%10<1||type%10>7)return false;
    const uint16_t station=uint16_t(((frame[4]&15)<<8)|frame[5]);
    uint32_t epoch=(uint32_t(frame[6])<<22)|(uint32_t(frame[7])<<14)|(uint32_t(frame[8])<<6)|(frame[9]>>2);
    constexpr uint32_t week=604800000;
    if(family==108){const uint32_t day=epoch>>27,tod=epoch&0x7ffffffu;if(day>6||tod>=86400000){++invalid_epoch;return false;}epoch=day*86400000+tod;}
    else if(epoch>=week){++invalid_epoch;return false;}
    if(!observed_||station!=station_){seen_=0;station_=station;}
    const unsigned index=family-107;const uint8_t bit=uint8_t(1u<<index);
    if(seen_&bit){const uint32_t advance=(epoch+week-epochs_[index])%week;if(!advance||advance>week/2){++repeated_or_old;return false;}}
    epochs_[index]=epoch;seen_|=bit;observed_=true;last_=now;++observations;return true;
  }
  uint32_t effective_age(uint32_t now,bool receiver_valid,uint32_t receiver_at,uint32_t reported_age,uint16_t receiver_station)const{
    const uint32_t elapsed=now-receiver_at;
    if(!observed_||!receiver_valid||elapsed>1500||reported_age==UINT32_MAX||receiver_station!=station_)return UINT32_MAX;
    const uint32_t advanced=uint32_t(std::min(uint64_t(UINT32_MAX),uint64_t(reported_age)+elapsed));
    return std::max(arrival_age(now),advanced);
  }
  const char *state(uint32_t now,bool valid,uint32_t at,uint32_t age,uint16_t station,uint32_t limit=3000)const{
    if(!observed_)return "waiting_observations";
    if(arrival_age(now)>limit)return "stale_observations";
    if(!valid||now-at>1500||age==UINT32_MAX)return "receiver_unconfirmed";
    if(station!=station_)return "station_mismatch";
    return effective_age(now,valid,at,age,station)<=limit?"fresh":"receiver_corrections_stale";
  }
};
} // namespace correction
