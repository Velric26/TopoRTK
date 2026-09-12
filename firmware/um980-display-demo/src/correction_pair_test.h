#pragma once
#include "correction_transport.h"
#include <new>

// Finite synthetic Base -> Rover diagnostic. Never writes to a GNSS receiver.
namespace correctiontest {
constexpr size_t message_size=600;
inline unsigned fault(uint32_t sequence){return (sequence-1)%6;}
inline bool deliverable(uint32_t sequence){return fault(sequence)!=2&&fault(sequence)!=3;}
inline void source(uint8_t *b,uint32_t run,uint32_t sequence){
  for(size_t i=0;i<message_size;++i)b[i]=uint8_t(i*37+sequence*13+run);
  b[0]=0xd3;b[1]=uint8_t((message_size-6)>>8);b[2]=uint8_t(message_size-6);
  correction::put32(b+3,run);correction::put32(b+7,sequence);
  uint32_t crc=correction::crc24(b,message_size-3);
  b[message_size-3]=uint8_t(crc>>16);b[message_size-2]=uint8_t(crc>>8);b[message_size-1]=uint8_t(crc);
}
enum State {Idle,Armed,Running,Done,Failed};
class Engine {
  correction::Sender sender_;correction::Packet fragments_[3]{},control_{};
  uint8_t data_[message_size]{},seen_[20]{};size_t control_used_=0;
  uint32_t armed_=0,last_control_=0,finished_=0,queued_=0;unsigned output_=0,outputs_=0;
  bool control_sent_=false;
  void complete(uint32_t now){state=Done;finished_=now;reason="complete";control_sent_=false;}
  void consume(){
    const auto &r=stream.receiver;uint32_t seq=r.size()==message_size?correction::u32(r.data()+7):0;
    if(!seq||seq>planned()||!deliverable(seq)){++invalid;return;}
    source(data_,run,seq);
    if(r.size()!=message_size||std::memcmp(data_,r.data(),message_size)){++invalid;return;}
    const unsigned bit=seq-1;if(seen_[bit/8]&(1u<<(bit%8))){++invalid;return;}
    seen_[bit/8]|=uint8_t(1u<<(bit%8));++accepted;
  }
  void receive_control(uint32_t now){
    const uint8_t *b=control_.bytes;
    if(correction::u32(b+252)!=correction::crc32(b,252)){++control_errors;return;}
    if(b[4]!=1||b[5]<1||b[5]>3||b[6]!=(1-node)||b[7]!=1||correction::u32(b+8)!=run||correction::u16(b+12)!=seconds)return;
    for(size_t i=14;i<16;++i)if(b[i])return;
    for(size_t i=32;i<252;++i)if(b[i])return;
    if(b[5]==1&&state==Armed){state=Running;start_at=now+2000;reason="synthetic_rtcm_running";}
    if(b[5]==3&&busy())abort(now,"peer_aborted");
    if(b[5]==2&&(state==Running||state==Done)){
      peer_received=true;peer_sent=correction::u32(b+16);peer_accepted=correction::u32(b+20);
      peer_invalid=correction::u32(b+24);peer_tx_expired=correction::u32(b+28);
    }
  }
 public:
  correction::Stream stream;
  State state=Idle;uint32_t run=0,start_at=0,sent=0,accepted=0,invalid=0,tx_expired=0,wire_sent=0,control_errors=0;
  uint32_t dropped=0,corrupted=0,duplicated=0,peer_sent=0,peer_accepted=0,peer_invalid=0,peer_tx_expired=0;
  uint16_t seconds=0;uint8_t node=0;bool peer_received=false;const char *reason="";
  unsigned planned()const{return seconds/2;}
  unsigned expected()const{unsigned n=0;for(unsigned i=1;i<=planned();++i)if(deliverable(i))++n;return n;}
  bool busy()const{return state==Armed||state==Running;}
  bool local_pass()const{return state==Done&&!invalid&&!tx_expired&&(node?accepted==expected():sent==planned());}
  bool pair_pass()const{return local_pass()&&peer_received&&!peer_invalid&&!peer_tx_expired&&(node?peer_sent==planned():peer_accepted==expected());}
  bool arm(uint32_t id,uint16_t duration,uint8_t role,uint32_t now){
    if(busy()||id<100000||id>999999||id==run||role>1||(duration!=30&&duration!=60&&duration!=120&&duration!=300))return false;
    this->~Engine();new(this) Engine;run=id;seconds=duration;node=role;armed_=now;state=Armed;reason="waiting_for_other_instrument";
    sender_.begin(run,0);stream.select(run,0);return true;
  }
  void abort(uint32_t now,const char *why){state=Failed;reason=why;finished_=now;outputs_=0;control_sent_=false;}
  void tick(uint32_t now){
    stream.receiver.tick(now);
    if(state==Armed&&now-armed_>=120000)abort(now,"peer_timeout");
    if(state==Running&&int32_t(now-start_at)>=int32_t(seconds*1000u+5000u))complete(now);
  }
  void byte(uint8_t value,uint32_t now){
    // Independent bounded scanners: RTC1 control bytes are harmless stream noise.
    if(node&&state==Running&&stream.push(value,now))consume();
    control_.bytes[control_used_++]=value;
    if(control_used_>=4&&std::memcmp(control_.bytes,"RTC1",4))std::memmove(control_.bytes,control_.bytes+1,--control_used_);
    if(control_used_==256){receive_control(now);std::memmove(control_.bytes,control_.bytes+1,--control_used_);}
  }
  bool next(correction::Packet &p,uint32_t now){
    tick(now);
    const bool hello=state==Armed||(state==Running&&int32_t(now-start_at)<0);
    const bool final=(state==Done||state==Failed)&&now-finished_<60000;
    if((hello||final)&&(!control_sent_||now-last_control_>=(hello?250u:500u))){
      p=correction::Packet{};auto b=p.bytes;std::memcpy(b,"RTC1",4);b[4]=1;b[5]=hello?1:state==Done?2:3;b[6]=node;b[7]=1;
      correction::put32(b+8,run);correction::put16(b+12,seconds);correction::put32(b+16,sent);
      correction::put32(b+20,accepted);correction::put32(b+24,invalid);correction::put32(b+28,tx_expired);correction::seal(p);return true;
    }
    if(node||state!=Running||int32_t(now-start_at)<0)return false;
    if(output_==outputs_){
      if(sent>=planned()||now-start_at<sent*2000)return false;
      ++sent;queued_=now;source(data_,run,sent);sender_.enqueue(sent,data_,sizeof(data_),now);
      for(auto &fragment:fragments_){sender_.next(fragment,now);sender_.committed();}
      output_=0;outputs_=fault(sent)==1?4:fault(sent)==3?2:3;
      if(fault(sent)==3)++dropped;
    }
    if(now-queued_>=correction::age_limit_ms){++tx_expired;output_=outputs_;return false;}
    unsigned index=output_;
    if(fault(sent)==1)index=output_?output_-1:0; // repeat first fragment before completing
    if(fault(sent)==3&&output_==1)index=2;
    if(fault(sent)==4)index=2-output_;
    p=fragments_[index];correction::put32(p.bytes+24,now-queued_);correction::seal(p);
    if(fault(sent)==2&&index==1)p.bytes[252]^=1;
    return true;
  }
  void committed(const correction::Packet &p,uint32_t now){
    if(!std::memcmp(p.bytes,"RTC1",4)){last_control_=now;control_sent_=true;return;}
    if(fault(sent)==1&&output_==1)++duplicated;
    if(fault(sent)==2&&output_==1)++corrupted;
    ++output_;++wire_sent;
  }
};
static_assert(sizeof(Engine)<4608,"Paired diagnostic fixed memory budget");
} // namespace correctiontest
