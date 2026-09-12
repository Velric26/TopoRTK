#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

// Portable transport core. No UART, GNSS side effects, heap allocation or clock sync.
namespace correction {
constexpr size_t packet_size=256, header_size=32, payload_size=220, max_rtcm=1029;
constexpr uint32_t assembly_ms=1000, age_limit_ms=1500;
struct Packet { uint8_t bytes[packet_size]{}; };
inline uint16_t u16(const uint8_t *p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline uint32_t u32(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline void put16(uint8_t *p,uint16_t n){p[0]=uint8_t(n);p[1]=uint8_t(n>>8);}
inline void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
inline uint32_t crc32(const uint8_t *p,size_t size){uint32_t v=~0u;while(size--){v^=*p++;for(unsigned i=0;i<8;++i)v=(v>>1)^(0xedb88320u&(0u-(v&1)));}return ~v;}
inline uint32_t crc24(const uint8_t *p,size_t size){uint32_t v=0;while(size--){v^=uint32_t(*p++)<<16;for(unsigned i=0;i<8;++i){v<<=1;if(v&0x1000000u)v^=0x1864cfbu;}}return v&0xffffffu;}
inline bool valid_rtcm(const uint8_t *p,size_t size){
  if(!p||size<8||size>max_rtcm||p[0]!=0xd3||(p[1]&0xfc)||size!=size_t(((p[1]&3)<<8)|p[2])+6)return false;
  return crc24(p,size-3)==((uint32_t(p[size-3])<<16)|(uint32_t(p[size-2])<<8)|p[size-1]);
}
inline void seal(Packet &p){put32(p.bytes+252,crc32(p.bytes,252));}
inline bool wire_valid(const Packet &p){return std::memcmp(p.bytes,"RTM1",4)==0&&u32(p.bytes+252)==crc32(p.bytes,252);}

class Sender {
  uint8_t data_[max_rtcm]{};uint32_t session_=0,sequence_=0,queued_=0;uint16_t size_=0,offset_=0;uint8_t source_=0;
 public:
  uint32_t superseded=0,expired=0;
  bool begin(uint32_t session,uint8_t source){if(!session||source>1)return false;session_=session;source_=source;sequence_=queued_=0;size_=offset_=0;superseded=expired=0;return true;}
  bool enqueue(uint32_t sequence,const uint8_t *data,size_t size,uint32_t now){
    if(!session_||!sequence||sequence<=sequence_||!valid_rtcm(data,size))return false;
    if(size_)++superseded;
    std::memcpy(data_,data,size);size_=uint16_t(size);offset_=0;queued_=now;sequence_=sequence;return true;
  }
  bool next(Packet &p,uint32_t now){
    if(!size_)return false;
    if(now-queued_>=age_limit_ms){size_=0;++expired;return false;}
    std::memset(p.bytes,0,sizeof(p.bytes));std::memcpy(p.bytes,"RTM1",4);
    p.bytes[4]=1;p.bytes[5]=1;p.bytes[6]=source_;put32(p.bytes+8,session_);put32(p.bytes+12,sequence_);
    const uint16_t count=uint16_t(std::min(payload_size,size_t(size_-offset_)));
    put16(p.bytes+16,size_);put16(p.bytes+18,offset_);put16(p.bytes+20,count);put32(p.bytes+24,now-queued_);
    std::memcpy(p.bytes+header_size,data_+offset_,count);seal(p);return true;
  }
  // Call only after the complete envelope is accepted by the output adapter.
  void committed(){if(size_){offset_+=uint16_t(std::min(payload_size,size_t(size_-offset_)));if(offset_==size_)size_=0;}}
  bool pending()const{return size_!=0;}
};
struct Counters {uint32_t delivered=0,wire_errors=0,malformed=0,wrong_session=0,duplicates=0,replays=0,preempted=0,expired=0,rtcm_errors=0;};
class Receiver {
  uint8_t data_[max_rtcm]{};uint32_t session_=0,highest_=0,started_=0,sender_age_=0;uint16_t total_=0,ready_=0;uint8_t source_=0,seen_=0;bool active_=false;
  void close(){active_=false;seen_=0;ready_=0;}
 public:
  Counters stats{};
  bool select(uint32_t session,uint8_t source){
    if(!session||source>1)return false;
    session_=session;source_=source;highest_=started_=sender_age_=0;total_=ready_=0;seen_=0;active_=false;stats=Counters{};return true;
  }
  void tick(uint32_t now){if(active_&&(now-started_>=assembly_ms||uint64_t(sender_age_)+(now-started_)>=age_limit_ms)){close();++stats.expired;}}
  // True exposes exactly one complete CRC24Q-valid message until the next input.
  bool accept(const Packet &p,uint32_t now){
    ready_=0;tick(now);const uint8_t *b=p.bytes;
    if(!wire_valid(p)){++stats.wire_errors;return false;}
    const uint16_t total=u16(b+16),offset=u16(b+18),count=u16(b+20);const uint32_t sequence=u32(b+12),age=u32(b+24);
    if(b[4]!=1||b[5]!=1||b[6]>1||b[7]||u16(b+22)||u32(b+28)||!sequence||total<8||total>max_rtcm||offset>=total||offset%payload_size||count!=std::min(payload_size,size_t(total-offset))){++stats.malformed;return false;}
    for(size_t i=header_size+count;i<252;++i)if(b[i]){++stats.malformed;return false;}
    if(!session_||u32(b+8)!=session_||b[6]!=source_){++stats.wrong_session;return false;}
    if(sequence<highest_||(sequence==highest_&&!active_)){++stats.replays;return false;}
    if(sequence>highest_){if(active_)++stats.preempted;close();highest_=sequence;total_=total;started_=now;sender_age_=age;active_=true;}
    if(total!=total_){close();++stats.malformed;return false;}
    // Conservative sender queue + residence bound; excludes unknown modem flight time.
    sender_age_=std::max(sender_age_,age);
    tick(now);if(!active_)return false;
    const uint8_t bit=uint8_t(1u<<(offset/payload_size));
    if(seen_&bit){++stats.duplicates;return false;}
    std::memcpy(data_+offset,b+header_size,count);seen_|=bit;
    const uint8_t all=uint8_t((1u<<((total_+payload_size-1)/payload_size))-1);
    if(seen_!=all)return false;
    active_=false;
    if(!valid_rtcm(data_,total_)){++stats.rtcm_errors;return false;}
    ready_=total_;++stats.delivered;return true;
  }
  const uint8_t *data()const{return data_;}
  size_t size()const{return ready_;}
  bool assembling()const{return active_;}
};
class Stream {
  Packet pending_{};size_t used_=0;
 public:
  Receiver receiver;uint32_t discarded_bytes=0;
  bool select(uint32_t session,uint8_t source){if(!receiver.select(session,source))return false;used_=0;discarded_bytes=0;return true;}
  bool push(uint8_t byte,uint32_t now){
    receiver.tick(now);pending_.bytes[used_++]=byte;
    if(used_>=4&&std::memcmp(pending_.bytes,"RTM1",4)){std::memmove(pending_.bytes,pending_.bytes+1,--used_);++discarded_bytes;}
    if(used_!=packet_size)return false;
    if(!wire_valid(pending_)){++receiver.stats.wire_errors;std::memmove(pending_.bytes,pending_.bytes+1,--used_);++discarded_bytes;return false;}
    used_=0;return receiver.accept(pending_,now);
  }
};
static_assert(sizeof(Sender)<1152,"Sender memory budget");
static_assert(sizeof(Stream)<1536,"Parser/reassembler memory budget");
} // namespace correction
