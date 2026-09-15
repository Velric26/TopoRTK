#include "pair_session.h"

namespace pair_session {
namespace {
constexpr uint32_t freshness_ms=4000, negotiation_ms=20000;
enum Kind : uint8_t { Hello=1, Proof=2, Offer=3, Accept=4, Confirm=5, Heartbeat=6 };
struct Message {
  uint8_t kind=0, from=0, to=0, role=0, transport=0;
  uint32_t boot=0, target=0, challenge=0, echo=0, session=0;
};
// Return -1 only for a recognized, otherwise structurally valid old version.
int decode(const correction::Packet &packet,Message &m){
  const uint8_t *p=packet.bytes,*b=p+12;
  if(!correction::wire_valid(packet)||p[5]!=3||p[6]||p[7]||correction::u32(p+8)||std::memcmp(b,"PLC1",4))return 0;
  for(unsigned i=52;i<252;++i)if(p[i])return 0;
  if(b[10]||b[11]||correction::u32(b+32)||correction::u32(b+36))return 0;
  m.kind=b[5];m.from=b[6];m.to=b[7];m.role=b[8];m.transport=b[9];
  m.boot=correction::u32(b+12);m.target=correction::u32(b+16);
  m.challenge=correction::u32(b+20);m.echo=correction::u32(b+24);m.session=correction::u32(b+28);
  if(m.kind<Hello||m.kind>Heartbeat||m.from<1||m.from>2||m.to<1||m.to>2||m.from==m.to||m.role>1||m.transport>1||!m.boot||!m.challenge)return 0;
  // An unsolicited Hello is untargeted; a solicited reply names the observed
  // boot and echoes its discovery token. Sessions are never carried by Hello.
  if(m.kind==Hello){if(m.session||(m.target!=0)!=(m.echo!=0))return 0;}
  else if(!m.target||!m.echo||(m.kind==Proof ? m.session!=0 : m.session<=999999))return 0;
  if(p[4]!=1||b[4]!=1)return (p[4]==2||b[4]==2)?-1:0;
  return 1;
}
bool newer(uint32_t value,uint32_t previous){return value!=previous&&uint32_t(value-previous)<0x80000000u;}
}

uint32_t Engine::nonce(){
  // Randomness provides reboot isolation; the serial prevents a stuck RNG from
  // repeating challenges within one owner lifetime. No clock/receiver dependency.
  ++serial_;
  uint32_t value=(random_?random_():0)^uint32_t(serial_*0x9e3779b9u);
  if(!value)value=serial_?serial_:1;
  return value;
}
void Engine::begin(uint8_t unit,bool rover,Transport transport,uint32_t boot,uint32_t now,uint32_t (*random)()){
  const uint32_t generation=generation_+1,serial=serial_;
  *this=Engine{};generation_=generation;serial_=serial;
  unit_=unit;rover_=rover;transport_=transport;boot_=boot;random_=random;started_=now;
  valid_=(unit==1||unit==2)&&boot&&uint8_t(transport)<=1;
  discovery_=nonce();
  if(valid_&&rover_)rover_candidate(now);
}
void Engine::rover_candidate(uint32_t now){
  candidate_=Candidate{};candidate_.stage=Stage::Hello;candidate_.local=nonce();candidate_.started=now;
}
void Engine::advance_heartbeat(){
  if(!++heartbeat_)++heartbeat_;
  challenge_transmitted_=false;
}
void Engine::establish(uint32_t now){
  peer_boot_=candidate_.boot;session_=candidate_.session;
  local_nonce_=candidate_.local;peer_nonce_=candidate_.peer;
  established_=true;++generation_;
  heartbeat_=nonce();peer_heartbeat_=0;peer_heartbeat_seen_=false;
  heartbeat_transmitted_=challenge_transmitted_=proof_seen_=false;
  heartbeat_sent_=challenge_sent_=proof_time_=now;
  protocol_error_=role_error_=false;
  // A completed challenge is never reused to prove another boot/session.
  discovery_=nonce();
}
void Engine::encode(correction::Packet &packet,uint8_t kind,uint32_t target,uint32_t challenge,uint32_t echo,uint32_t session) const{
  packet=correction::Packet{};uint8_t *p=packet.bytes,*b=p+12;
  std::memcpy(p,"RTM1",4);p[4]=1;p[5]=3;
  std::memcpy(b,"PLC1",4);b[4]=1;b[5]=kind;b[6]=unit_;b[7]=uint8_t(3-unit_);b[8]=rover_?1:0;b[9]=uint8_t(transport_);
  correction::put32(b+12,boot_);correction::put32(b+16,target);correction::put32(b+20,challenge);
  correction::put32(b+24,echo);correction::put32(b+28,session);correction::seal(packet);
}
void Engine::tick(uint32_t now){
  if(candidate_.stage!=Stage::None&&uint32_t(now-candidate_.started)>=negotiation_ms){
    candidate_=Candidate{};
    if(!established_&&rover_)rover_candidate(now);
  }
  // Expire the outstanding proof token, not the established production session.
  if(established_&&challenge_transmitted_&&uint32_t(now-challenge_sent_)>=freshness_ms)advance_heartbeat();
  if(proof_seen_&&uint32_t(now-proof_time_)>=freshness_ms)proof_seen_=false;
}
void Engine::incompatible(uint32_t){protocol_error_=true;}

bool Engine::receive(const correction::Packet &packet,uint32_t now){
  Message m;const int decoded=decode(packet,m);
  if(!valid_||!decoded)return false;
  if(m.from!=3-unit_||m.to!=unit_||m.transport!=uint8_t(transport_))return false;
  if(decoded<0){incompatible(now);return false;}
  if(m.role==uint8_t(rover_)){role_error_=true;return false;}
  if(m.kind!=Hello&&m.target!=boot_)return false;
  tick(now);
  if(m.kind==Heartbeat){
    if(!established_||m.boot!=peer_boot_||m.session!=session_)return false;
    if(!peer_heartbeat_seen_||newer(m.challenge,peer_heartbeat_)){
      peer_heartbeat_=m.challenge;peer_heartbeat_seen_=true;
    }else if(m.challenge!=peer_heartbeat_)return false;
    if(challenge_transmitted_&&m.echo==heartbeat_&&uint32_t(now-challenge_sent_)<freshness_ms){
      proof_seen_=true;proof_time_=challenge_sent_;advance_heartbeat();
      protocol_error_=role_error_=false;
    }
    if(!rover_&&candidate_.stage==Stage::Confirm)candidate_=Candidate{};
    return true;
  }
  if(m.kind==Hello){
    if(rover_){
      // A Base Hello solicits fresh proof. Binding the reply to the observed
      // boot and discovery token keeps a replayed Hello from enlisting the
      // live Base, whose token is retired on establishment. An established
      // session is never invalidated here; heartbeats keep TX priority.
      if(candidate_.stage==Stage::None||candidate_.stage==Stage::Hello){
        if(candidate_.stage==Stage::None)rover_candidate(now);
        if(m.boot!=candidate_.boot||m.challenge!=candidate_.peer){
          candidate_.boot=m.boot;candidate_.peer=m.challenge;candidate_.transmitted=false;
        }
      }
      return true;
    }
    if(established_&&m.boot==peer_boot_&&m.challenge==peer_nonce_)return true;
    // Only this boot's current token is answerable: establish() retires it, so
    // a delayed solicitation cannot restart a proven session.
    if(m.target||m.echo){
      if(m.target!=boot_||m.echo!=discovery_)return false;
    }
    if(candidate_.stage!=Stage::None&&candidate_.stage!=Stage::Confirm){
      // An unproven candidate may follow the newest solicitation of that boot.
      if(m.boot==candidate_.boot&&candidate_.stage==Stage::Challenge&&m.challenge!=candidate_.peer){
        candidate_.peer=m.challenge;candidate_.local=nonce();candidate_.started=now;candidate_.transmitted=false;
        return true;
      }
      return m.boot==candidate_.boot&&m.challenge==candidate_.peer;
    }
    candidate_=Candidate{};candidate_.stage=Stage::Challenge;candidate_.boot=m.boot;
    candidate_.peer=m.challenge;candidate_.local=nonce();candidate_.started=now;
    return true;
  }
  if(rover_){
    if(m.kind==Confirm&&established_&&m.boot==peer_boot_&&m.session==session_&&m.challenge==peer_nonce_&&m.echo==local_nonce_)return true;
    if(candidate_.stage==Stage::None||m.echo!=candidate_.local)return false;
    if(m.kind==Proof){
      if(candidate_.stage!=Stage::Hello&&(m.boot!=candidate_.boot||m.challenge!=candidate_.peer))return false;
      if(candidate_.stage==Stage::Hello){
        candidate_.boot=m.boot;candidate_.peer=m.challenge;candidate_.stage=Stage::Proof;candidate_.transmitted=false;
      }
      return true;
    }
    if(m.boot!=candidate_.boot||m.challenge!=candidate_.peer)return false;
    if(m.kind==Offer&&(candidate_.stage==Stage::Proof||candidate_.stage==Stage::Accept)){
      if(candidate_.session&&candidate_.session!=m.session)return false;
      if(candidate_.stage!=Stage::Accept){candidate_.session=m.session;candidate_.stage=Stage::Accept;candidate_.transmitted=false;}
      return true;
    }
    if(m.kind==Confirm&&candidate_.stage==Stage::Accept&&m.session==candidate_.session){
      establish(now);candidate_=Candidate{};return true;
    }
    return false;
  }
  if(m.kind==Accept&&established_&&m.boot==peer_boot_&&m.session==session_&&m.challenge==peer_nonce_&&m.echo==local_nonce_){
    confirm_pending_=true;return true;
  }
  if(candidate_.stage==Stage::None||m.boot!=candidate_.boot||m.echo!=candidate_.local||m.challenge!=candidate_.peer)return false;
  if(m.kind==Proof&&(candidate_.stage==Stage::Challenge||candidate_.stage==Stage::Offer)){
    if(candidate_.stage==Stage::Challenge){
      const uint32_t range=UINT32_MAX-999999u;
      candidate_.session=1000000u+nonce()%range;
      if(candidate_.session==session_)candidate_.session=(session_==UINT32_MAX)?1000000u:session_+1;
      candidate_.stage=Stage::Offer;candidate_.transmitted=false;
    }
    return true;
  }
  if(m.kind==Accept&&candidate_.stage==Stage::Offer&&m.session==candidate_.session){
    establish(now);candidate_.stage=Stage::Confirm;candidate_.transmitted=false;return true;
  }
  return false;
}

bool Engine::next(correction::Packet &packet,uint32_t now){
  if(!valid_)return false;
  if(established_&&(!heartbeat_transmitted_||uint32_t(now-heartbeat_sent_)>=1000)){
    // Before the first peer heartbeat, use the completed peer handshake nonce
    // as the nonzero echo. It cannot match the peer's fresh heartbeat token.
    encode(packet,Heartbeat,peer_boot_,heartbeat_,peer_heartbeat_seen_?peer_heartbeat_:peer_nonce_,session_);return true;
  }
  if(confirm_pending_){encode(packet,Confirm,peer_boot_,local_nonce_,peer_nonce_,session_);return true;}
  if(candidate_.stage!=Stage::None){
    const uint32_t retry=candidate_.stage==Stage::Hello?1000:500;
    if(candidate_.transmitted&&uint32_t(now-candidate_.sent)<retry)return false;
    uint8_t kind=Proof;
    switch(candidate_.stage){
      case Stage::Hello:kind=Hello;break;
      case Stage::Offer:kind=Offer;break;
      case Stage::Accept:kind=Accept;break;
      case Stage::Confirm:kind=Confirm;break;
      default:break;
    }
    // A solicited Hello carries the observed boot/token; an untargeted one is
    // used only when this engine opened the negotiation itself.
    encode(packet,kind,candidate_.boot,candidate_.local,candidate_.peer,candidate_.session);return true;
  }
  if(!established_&&!rover_&&(!hello_transmitted_||uint32_t(now-hello_sent_)>=1000)){
    encode(packet,Hello,0,discovery_,0,0);return true;
  }
  return false;
}
void Engine::committed(const correction::Packet &packet,uint32_t now){
  // A delayed adapter completion may refer to a superseded candidate. Only the
  // exact current preparation advances clocks; failed writes consume nothing.
  correction::Packet current;
  if(!next(current,now)||std::memcmp(current.bytes,packet.bytes,sizeof(packet.bytes)))return;
  const uint8_t kind=packet.bytes[17];
  if(kind==Heartbeat){
    heartbeat_sent_=now;heartbeat_transmitted_=true;
    if(!challenge_transmitted_){challenge_sent_=now;challenge_transmitted_=true;}
  }else if(confirm_pending_){confirm_pending_=false;}
  else if(candidate_.stage!=Stage::None){candidate_.sent=now;candidate_.transmitted=true;}
  else {hello_sent_=now;hello_transmitted_=true;}
}
Snapshot Engine::snapshot(uint32_t now) const{
  Snapshot out;out.transport=transport_;out.local_boot=boot_;out.peer_boot=peer_boot_;out.session=session_;
  out.generation=generation_;out.rover=rover_;out.established=established_;
  if(proof_seen_)out.peer_age_ms=uint32_t(now-proof_time_);
  out.connected=established_&&proof_seen_&&out.peer_age_ms<freshness_ms;
  if(!valid_)out.reason="invalid_identity";
  else if(out.connected)out.reason="connected";
  else if(protocol_error_)out.reason="protocol_incompatible";
  else if(role_error_)out.reason="same_role";
  else if(established_||uint32_t(now-started_)>=negotiation_ms)out.reason="peer_unreachable";
  return out;
}
} // namespace pair_session
