#include "peer_update.h"
#include "web_http.h"
#include "wifi_transport.h"
#include <Arduino.h>
#include <cstring>
namespace {
bool started=false,rover_=false;uint32_t route_=0,hello_at=0,remote_=0,remote_at=0;
uint32_t resume_attempt=0,resume_target=0,resume_started=0,resume_last=0;
bool resume_sent=false,recovery_seen=false;IPAddress address;
update_notice::Peer peer;update_notice::Sender sender;
update_notice::Message acknowledgement;bool ack_pending=false;uint32_t ack_at=0,last_ack=0;
portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;char label[80]={};
void envelope(correction::Packet &p){p=correction::Packet{};std::memcpy(p.bytes,"RTM1",4);p.bytes[4]=1;p.bytes[5]=3;correction::put32(p.bytes+8,route_);}
void publish(){portENTER_CRITICAL(&guard);std::snprintf(label,sizeof(label),"%s",peer.label());portEXIT_CRITICAL(&guard);}
}
void peer_update_resume(uint32_t attempt,uint32_t target,bool rover){resume_attempt=attempt;resume_target=target;rover_=rover;resume_started=millis();resume_sent=false;}
uint32_t peer_update_target(){return remote_;}
void peer_update_label(char *out,size_t capacity){portENTER_CRITICAL(&guard);std::snprintf(out,capacity,"%s",label);portEXIT_CRITICAL(&guard);}
bool peer_update_prepare(uint32_t attempt,uint32_t now){return remote_&&remote_at&&now-remote_at<4000&&sender.begin(TOPORTK_UNIT_ID,rover_,remote_,attempt,180000,now);}
bool peer_update_phase(update_notice::Kind kind,uint32_t now){return sender.transition(kind,now);}
bool peer_update_acknowledged(){return sender.acknowledged();}
void peer_update_close(){sender.close();}
void peer_update_receive(const correction::Packet &p,uint32_t now){
  if(!peer_wire::control(p)||correction::u32(p.bytes+8)!=route_)return;
  const auto *b=p.bytes+12;
  if(!std::memcmp(b,"TPH1",4)){
    if(b[4]!=3-TOPORTK_UNIT_ID||b[5]!=(rover_?0:1)||b[6]||b[7]||!correction::u32(b+8))return;
    for(unsigned i=20;i<40;++i)if(b[i])return;
    // Echo of this boot's random challenge proves the peer heard this boot.
    const auto candidate=correction::u32(b+8);
    if(!remote_)remote_=candidate; // Discovery; prepare still requires a fresh echo.
    if(correction::u32(b+12)==web_boot_id()){remote_=candidate;remote_at=now;if(peer.attempt()&&correction::u32(b+16)==peer.attempt())recovery_seen=true;}
    return;
  }
  update_notice::Message m;if(!update_notice::decode(b,40,m)||m.role!=(rover_?0:1))return;
  if(m.kind==update_notice::Kind::Ack){sender.receive(m,now);return;}
  update_notice::Message ack;
  const auto previous=peer.attempt();
  if(peer.receive(m,now,ack)){if(peer.attempt()!=previous)recovery_seen=false;if(!last_ack||now-last_ack>=200){acknowledgement=ack;ack_pending=true;ack_at=now;}}
  publish();
}
bool peer_update_next(correction::Packet &p,uint32_t now){
  envelope(p);
  if(ack_pending&&now-ack_at<500){update_notice::Packet a;update_notice::encode(acknowledgement,a);std::memcpy(p.bytes+12,a.bytes,40);}
  else {
    ack_pending=false;update_notice::Packet notice;
    if(sender.next(notice,now))std::memcpy(p.bytes+12,notice.bytes,40);
    else if(resume_attempt&&remote_==resume_target&&remote_at&&now-remote_at<4000&&now-resume_started<180000&&hello_at&&now-hello_at<1000&&(!resume_sent||now-resume_last>=1000)){
      update_notice::Message m;m.kind=update_notice::Kind::Reconnected;m.from=TOPORTK_UNIT_ID;m.to=3-TOPORTK_UNIT_ID;
      m.role=rover_;m.session=resume_target;m.attempt=resume_attempt;m.sequence=3;
      update_notice::encode(m,notice);std::memcpy(p.bytes+12,notice.bytes,40);
    }else if(!hello_at||now-hello_at>=1000){
      auto *b=p.bytes+12;std::memcpy(b,"TPH1",4);b[4]=TOPORTK_UNIT_ID;b[5]=rover_;
      correction::put32(b+8,web_boot_id());correction::put32(b+12,remote_);correction::put32(b+16,resume_attempt);
    }else return false;
  }
  correction::seal(p);return true;
}
void peer_update_committed(const correction::Packet &p,uint32_t now){
  const auto *b=p.bytes+12;
  if(!std::memcmp(b,"TPH1",4)){hello_at=now;return;}
  update_notice::Message m;if(!update_notice::decode(b,40,m))return;
  if(m.kind==update_notice::Kind::Ack){ack_pending=false;last_ack=now;}
  else if(m.kind==update_notice::Kind::Reconnected){resume_sent=true;resume_last=now;}
  else sender.committed(now);
}
void peer_update_service(uint32_t now,bool rover,uint32_t route,IPAddress destination,bool quality){
  if(route_!=route||rover_!=rover){route_=route;rover_=rover;remote_=remote_at=hello_at=0;ack_pending=false;sender.close();}
  peer.select(TOPORTK_UNIT_ID,web_boot_id());peer.tick(now);
  peer.recovered(peer.attempt(),recovery_seen&&remote_at&&now-remote_at<4000,quality);publish();
  address=destination;
  if(!started)started=wifi_transport::start(wifi_transport::Channel::Peer);
  if(!started)return;
  for(unsigned budget=0;budget<4;++budget){
    IPAddress from;uint8_t bytes[sizeof(correction::Packet)];
    const int size=wifi_transport::receive(wifi_transport::Channel::Peer,bytes,sizeof(bytes),from);
    if(size<=0)break;
    if(!route_&&uint32_t(address)&&from==address&&size==int(sizeof(correction::Packet))){
      correction::Packet p;std::memcpy(p.bytes,bytes,sizeof(p));peer_update_receive(p,now);
    }
  }
  if(!route_&&uint32_t(address)){correction::Packet p;
    if(peer_update_next(p,now)&&wifi_transport::send(wifi_transport::Channel::Peer,address,p.bytes,sizeof(p)))peer_update_committed(p,now);
  }
}
