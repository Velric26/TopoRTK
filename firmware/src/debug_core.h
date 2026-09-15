#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace debugmode {
// Debug is enabled by default whenever the instrument starts; a disable
// persists only until the next boot. No idle timer, no stored preference.
class Session {
  bool enabled_=true;
 public:
  void enable(uint32_t){enabled_=true;}
  void disable(){enabled_=false;}
  bool active(uint32_t)const{return enabled_;}
};
enum class Channel:uint8_t {Event,GnssRx,GnssTx,RadioRx,RadioTx,WifiRx,WifiTx};
inline const char *channel_name(Channel c){switch(c){case Channel::GnssRx:return "GNSS RX";case Channel::GnssTx:return "GNSS TX";case Channel::RadioRx:return "SiK RX";case Channel::RadioTx:return "SiK TX";case Channel::WifiRx:return "Wi-Fi RX";case Channel::WifiTx:return "Wi-Fi TX";default:return "Debug";}}
struct Entry {uint32_t sequence=0,at=0;Channel channel=Channel::Event;char text[121]{};};
class Log {
  Entry entries_[32]{};unsigned head_=0,count_=0,budget_=8;uint32_t sequence_=0,window_=0;
 public:
  uint32_t throttled=0,overwritten=0,truncated=0;
  void clear(uint32_t now){head_=count_=0;budget_=8;window_=now;throttled=overwritten=truncated=0;}
  unsigned size()const{return count_;}
  const Entry &at(unsigned i)const{return entries_[(head_+32-count_+i)%32];}
  bool push(Channel channel,const char *text,uint32_t now){
    if(!text)return false;
    if(now-window_>=1000){window_=now;budget_=8;}
    if(!budget_){++throttled;return false;}--budget_;
    auto &entry=entries_[head_];entry.sequence=++sequence_;entry.at=now;entry.channel=channel;
    size_t i=0;for(;i<120&&text[i];++i){const unsigned char c=text[i];entry.text[i]=c>=32&&c<127?char(c):' ';}entry.text[i]=0;
    if(i==120&&text[i])++truncated;
    head_=(head_+1)%32;if(count_<32)++count_;else ++overwritten;return true;
  }
};
static_assert(sizeof(Log)<4500,"Bounded passive Debug capture");
} // namespace debugmode
