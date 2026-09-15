#pragma once
#include "correction_transport.h"
#include "update_notice.h"
namespace pair_session { struct Snapshot; }
// All mutation is on the main loop/UART owner; HTTP reads a copied label only.
void peer_update_service(uint32_t now,const pair_session::Snapshot &link,bool quality_ready);
void peer_update_receive(const correction::Packet &packet,uint32_t now);
bool peer_update_next(correction::Packet &packet,uint32_t now);
void peer_update_committed(const correction::Packet &packet,uint32_t now);
bool peer_update_prepare(uint32_t attempt,uint32_t now);
bool peer_update_phase(update_notice::Kind kind,uint32_t now);
bool peer_update_acknowledged();
void peer_update_close();
uint32_t peer_update_target();
void peer_update_resume(uint32_t attempt,uint32_t target,bool rover);
void peer_update_label(char *out,size_t capacity);
bool peer_update_quality_ready();

namespace peer_wire {
// One parser consumes entire CRC-valid envelopes before dispatching their type.
// Marker-like bytes inside correction payloads can never become control input.
class Stream {
  correction::Packet pending_{};size_t used_=0;
 public:
  uint32_t wire_errors=0;
  void reset(){used_=0;wire_errors=0;}
  bool byte(uint8_t value,correction::Packet &out){
    pending_.bytes[used_++]=value;
    if(used_>=4&&std::memcmp(pending_.bytes,"RTM1",4))std::memmove(pending_.bytes,pending_.bytes+1,--used_);
    if(used_!=sizeof(pending_))return false;
    if(!correction::wire_valid(pending_)){++wire_errors;std::memmove(pending_.bytes,pending_.bytes+1,--used_);return false;}
    out=pending_;used_=0;return true;
  }
};
inline bool control(const correction::Packet &p){
  if(!correction::wire_valid(p)||p.bytes[4]!=1||p.bytes[5]!=3)return false;
  for(unsigned i=6;i<8;++i)if(p.bytes[i])return false;
  for(unsigned i=52;i<252;++i)if(p.bytes[i])return false;
  return true;
}
} // namespace peer_wire
