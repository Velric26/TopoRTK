#include "peer_update.h"
#include "pair_session.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
namespace {
pair_session::Snapshot link_{};uint32_t sampled_at=0;
uint32_t resume_attempt=0,resume_target=0,resume_started=0,resume_last=0;
bool resume_sent=false,resume_rover=false;uint32_t recovery_boot=0;
update_notice::Peer peer;update_notice::Sender sender;
update_notice::Message acknowledgement;bool ack_pending=false;uint32_t ack_at=0,last_ack=0;
portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;char label[80]={};
bool connected(uint32_t now){
  const uint32_t elapsed=now-sampled_at;
  return link_.established&&link_.connected&&link_.local_boot&&link_.peer_boot&&link_.session>999999&&
      elapsed<4000&&link_.peer_age_ms<4000-elapsed;
}
void envelope(correction::Packet &p){p=correction::Packet{};std::memcpy(p.bytes,"RTM1",4);p.bytes[4]=1;p.bytes[5]=3;correction::put32(p.bytes+8,link_.session);}
void publish(){portENTER_CRITICAL(&guard);std::snprintf(label,sizeof(label),"%s",peer.label());portEXIT_CRITICAL(&guard);}
}
void peer_update_resume(uint32_t attempt,uint32_t target,bool rover){resume_attempt=attempt;resume_target=target;resume_rover=rover;resume_started=millis();resume_sent=false;}
uint32_t peer_update_target(){return link_.established?link_.peer_boot:0;}
void peer_update_label(char *out,size_t capacity){portENTER_CRITICAL(&guard);std::snprintf(out,capacity,"%s",label);portEXIT_CRITICAL(&guard);}
bool peer_update_prepare(uint32_t attempt,uint32_t now){return connected(now)&&sender.begin(TOPORTK_UNIT_ID,link_.rover,link_.peer_boot,attempt,180000,now);}
bool peer_update_phase(update_notice::Kind kind,uint32_t now){return sender.transition(kind,now);}
bool peer_update_acknowledged(){return sender.acknowledged();}
void peer_update_close(){sender.close();}
void peer_update_receive(const correction::Packet &p,uint32_t now){
  if(!connected(now)||!peer_wire::control(p)||correction::u32(p.bytes+8)!=link_.session)return;
  update_notice::Message m;if(!update_notice::decode(p.bytes+12,40,m)||m.role!=(link_.rover?0:1))return;
  if(m.kind==update_notice::Kind::Ack){sender.receive(m,now);return;}
  update_notice::Message ack;
  const auto previous=peer.attempt();
  if(peer.receive(m,now,ack)){
    if(peer.attempt()!=previous)recovery_boot=0;
    if(m.kind==update_notice::Kind::Reconnected)recovery_boot=link_.peer_boot;
    if(!last_ack||now-last_ack>=200){acknowledgement=ack;ack_pending=true;ack_at=now;}
  }
  publish();
}
bool peer_update_next(correction::Packet &p,uint32_t now){
  if(!connected(now))return false;
  envelope(p);
  if(ack_pending&&now-ack_at<500){update_notice::Packet a;update_notice::encode(acknowledgement,a);std::memcpy(p.bytes+12,a.bytes,40);}
  else {
    ack_pending=false;update_notice::Packet notice;
    if(sender.next(notice,now))std::memcpy(p.bytes+12,notice.bytes,40);
    else if(resume_attempt&&link_.peer_boot==resume_target&&link_.rover==resume_rover&&now-resume_started<180000&&(!resume_sent||now-resume_last>=1000)){
      update_notice::Message m;m.kind=update_notice::Kind::Reconnected;m.from=TOPORTK_UNIT_ID;m.to=3-TOPORTK_UNIT_ID;
      m.role=link_.rover;m.session=resume_target;m.attempt=resume_attempt;m.sequence=3;
      update_notice::encode(m,notice);std::memcpy(p.bytes+12,notice.bytes,40);
    }else return false;
  }
  correction::seal(p);return true;
}
void peer_update_committed(const correction::Packet &p,uint32_t now){
  if(!connected(now)||!peer_wire::control(p)||correction::u32(p.bytes+8)!=link_.session)return;
  update_notice::Message m;if(!update_notice::decode(p.bytes+12,40,m))return;
  if(m.kind==update_notice::Kind::Ack){ack_pending=false;last_ack=now;}
  else if(m.kind==update_notice::Kind::Reconnected){resume_sent=true;resume_last=now;}
  else sender.committed(now);
}
void peer_update_service(uint32_t now,const pair_session::Snapshot &link,bool quality){
  if(link_.local_boot!=link.local_boot||link_.peer_boot!=link.peer_boot||link_.rover!=link.rover)sender.close();
  if(link_.generation!=link.generation||link_.session!=link.session||link_.local_boot!=link.local_boot||
      link_.peer_boot!=link.peer_boot||link_.rover!=link.rover||link_.transport!=link.transport){
    ack_pending=false;last_ack=0;recovery_boot=0;
  }
  link_=link;sampled_at=now;
  // Notice attempts are scoped to this receiver boot, not the rotating live
  // correction session. A proven peer reboot must retain its update label.
  peer.select(TOPORTK_UNIT_ID,link_.local_boot);peer.tick(now);
  peer.recovered(peer.attempt(),recovery_boot&&recovery_boot==link_.peer_boot&&connected(now),quality);publish();
}
