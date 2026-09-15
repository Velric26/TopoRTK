#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace linktest {
constexpr uint32_t magic=0x47445452; // RTDG, little endian
enum Kind:uint8_t {Hello=1,Data=2,Result=3,Abort=4};
enum State:uint8_t {Idle,Armed,Running,Done,Failed};
#pragma pack(push,1)
struct Packet {
  uint32_t marker=magic; uint8_t version=1,kind=Hello,node=0,mode=0;
  uint32_t run=0; uint16_t seconds=0,rate=0;
  uint32_t sequence=0,planned=0,sent=0,received=0,errors=0,duplicates=0,reordered=0,max_gap_ms=0;
  uint8_t payload[204]={}; uint32_t checksum=0;
};
#pragma pack(pop)
static_assert(sizeof(Packet)==256,"Diagnostic wire format");
struct Config {uint32_t run=0;uint16_t seconds=60,rate=1000;uint8_t mode=0;}; // mode: Base TX, Rover TX, both
inline uint32_t crc(const void *bytes,size_t length){
  uint32_t value=~0u;const auto *p=static_cast<const uint8_t*>(bytes);
  while(length--){value^=*p++;for(int bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u & (0u-(value&1)));}return ~value;
}
inline void seal(Packet &p){p.checksum=crc(&p,sizeof(p)-4);}
inline bool valid(const Packet &p){return p.marker==magic&&p.version==1&&p.kind>=Hello&&p.kind<=Abort&&p.node<2&&p.checksum==crc(&p,sizeof(p)-4);}
inline bool valid_config(const Config &c){return c.run>=100000&&c.run<=999999&&(c.seconds==30||c.seconds==60||c.seconds==120||c.seconds==300)&&(c.rate==200||c.rate==1000||c.rate==3000)&&c.mode<3;}
inline uint32_t planned(const Config &c,uint8_t node){return (c.mode==2||c.mode==node)?uint32_t(c.seconds)*c.rate/sizeof(Packet):0;}
class Engine {
 public:
  Config config{};State state=Idle;uint8_t node=0;
  uint32_t armed_at=0,start_at=0,done_at=0,sent=0,received=0,errors=0,duplicates=0,reordered=0,max_gap_ms=0;
  bool peer_result=false;Packet peer{};
  const char *reason="idle";
  bool busy()const{return state==Armed||state==Running;}
  bool arm(const Config &c,uint8_t n,uint32_t now){
    if(busy()||!valid_config(c)||n>1||c.run==config.run)return false;
    *this=Engine{};config=c;node=n;state=Armed;armed_at=now;reason="waiting_for_other_instrument";return true;
  }
  void abort(uint32_t now,const char *why){state=Failed;done_at=now;reason=why;}
  void tick(uint32_t now){
    if(state==Armed&&now-armed_at>=120000)abort(now,"peer_timeout");
    if(state==Running&&int32_t(now-start_at)>=int32_t(config.seconds*1000u+5000u)){state=Done;done_at=now;reason="complete";}
  }
  bool next(Packet &p,uint32_t now){
    tick(now);
    uint8_t kind=0;
    if(state==Armed||(state==Running&&int32_t(now-start_at)<0)){
      if(now-last_control_<250)return false;
      kind=Hello;
    }else if(state==Running&&now-start_at<uint32_t(config.seconds)*1000&&sent<planned(config,node)){
      if(now-start_at<uint64_t(sent)*sizeof(Packet)*1000/config.rate)return false;
      kind=Data;
    }else if((state==Done||state==Failed)&&now-done_at<60000){
      if(now-last_control_<500)return false;
      kind=state==Done?Result:Abort;
    }else return false;
    p=Packet{};p.kind=kind;p.node=node;p.mode=config.mode;p.run=config.run;p.seconds=config.seconds;p.rate=config.rate;
    p.sequence=sent;p.planned=planned(config,node);p.sent=sent;p.received=received;p.errors=errors;p.duplicates=duplicates;p.reordered=reordered;p.max_gap_ms=max_gap_ms;
    if(kind==Data)for(size_t i=0;i<sizeof(p.payload);++i)p.payload[i]=uint8_t(i+sent+node);
    seal(p);return true;
  }
  void transmitted(const Packet &p,uint32_t now){if(p.kind==Data)++sent;else last_control_=now;}
  void receive(const Packet &p,uint32_t now){
    if(!valid(p)){if(busy())++errors;return;}
    if(p.run!=config.run||p.node==node||p.mode!=config.mode||p.seconds!=config.seconds||p.rate!=config.rate)return;
    if(p.planned!=planned(config,p.node)){if(busy())++errors;return;}
    if(p.kind==Hello&&state==Armed){state=Running;start_at=now+2000;reason="running";return;}
    if(p.kind==Abort){if(busy())abort(now,"peer_aborted");return;}
    if(p.kind==Result){if(state==Running||state==Done){peer=p;peer_result=true;}return;}
    if(p.kind!=Data||state!=Running)return;
    if(p.sequence>=planned(config,p.node)||p.sequence>=4096){++errors;return;}
    for(size_t i=0;i<sizeof(p.payload);++i)if(p.payload[i]!=uint8_t(i+p.sequence+p.node)){++errors;return;}
    auto &byte=seen_[p.sequence/8];const uint8_t bit=1u<<(p.sequence%8);
    if(byte&bit){++duplicates;return;}byte|=bit;
    if(received&&p.sequence<highest_)++reordered;
    highest_=std::max(highest_,p.sequence);
    if(last_rx_)max_gap_ms=std::max(max_gap_ms,now-last_rx_);
    last_rx_=now;++received;
  }
  bool local_pass()const{return state==Done&&sent==planned(config,node)&&received==planned(config,1-node)&&!errors&&!duplicates&&!reordered;}
  bool pair_pass()const{return local_pass()&&peer_result&&peer.sent==planned(config,1-node)&&peer.received==sent&&!peer.errors&&!peer.duplicates&&!peer.reordered;}
 private:
  uint8_t seen_[512]={};uint32_t highest_=0,last_rx_=0,last_control_=0;
};
}
