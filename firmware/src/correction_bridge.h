#pragma once
#include "correction_queue.h"

namespace correction {
// Production radio data path, independent of UART and GNSS hardware. A local
// request selects the session; incoming packets can never select/change it.
class Bridge {
  Sender sender_;Stream stream_;BurstQueue queued_;StationGuard station_;
  uint32_t session_=0,sequence_=0,last_received_=0;bool rover_=false,received_=false,fault_=false;
 public:
  uint32_t submitted=0,envelopes=0,complete=0,station_rejected=0;
  bool begin(uint32_t session,bool rover){
    // Six-digit sessions belong to the synthetic paired diagnostic.
    if(session<1000000)return false;
    session_=session;rover_=rover;sequence_=last_received_=0;received_=fault_=false;
    submitted=envelopes=complete=station_rejected=0;
    queued_.clear();station_.reset();sender_.begin(session,0);stream_.select(session,0);return true;
  }
  // Teardown is a session boundary: a fault latched by the session being torn
  // down must never inhibit the next one. A fault inside an unchanged session
  // is not cleared here, only by fail()/begin().
  void stop(){session_=0;queued_.clear();station_.reset();received_=false;fault_=false;}
  void clear_pending(){queued_.clear();sender_.begin(session_,0);stream_.receiver.discard_assembly();received_=false;}
  void fail(){fault_=true;queued_.clear();received_=false;}
  bool fault()const{return fault_;}
  bool active()const{return session_!=0;}
  bool rover()const{return rover_;}
  uint32_t session()const{return session_;}
  int station()const{return station_.station();}
  bool linked(uint32_t now)const{return active()&&rover_&&received_&&now-last_received_<3000;}
  bool enqueue(const uint8_t *p,size_t n,uint32_t now){
    if(!active()||fault_||rover_)return false;
    if(!station_.accept(p,n)){++station_rejected;return false;}
    if(!queued_.enqueue(p,n,now,now))return false;
    ++submitted;return true;
  }
  bool next(Packet &packet,uint32_t now){
    if(!active()||fault_||rover_)return false;
    queued_.tick(now);
    if(sender_.next(packet,now))return true;
    const auto *f=queued_.front(now);if(!f)return false;
    if(sequence_==UINT32_MAX){fail();return false;} // No silent Wi-Fi fallback.
    const bool accepted=sender_.enqueue(++sequence_,f->data,f->size,f->at);queued_.pop(f);
    return accepted&&sender_.next(packet,now);
  }
  void committed(){sender_.committed();++envelopes;}
  bool byte(uint8_t value,uint32_t now){
    if(!active()||fault_||!rover_||!stream_.push(value,now))return false;
    if(!station_.accept(stream_.receiver.data(),stream_.receiver.size())){++station_rejected;return false;}
    last_received_=now;received_=true;++complete;return true;
  }
  bool packet(const Packet &p,uint32_t now){
    if(!active()||fault_||!rover_||!stream_.receiver.accept(p,now))return false;
    if(!station_.accept(stream_.receiver.data(),stream_.receiver.size())){++station_rejected;return false;}
    last_received_=now;received_=true;++complete;return true;
  }
  void tick(uint32_t now){stream_.receiver.tick(now);queued_.tick(now);}
  const uint8_t *data()const{return stream_.receiver.data();}
  size_t size()const{return stream_.receiver.size();}
  uint32_t known_age(uint32_t now)const{return stream_.receiver.known_age(now);}
  const Counters &stats()const{return stream_.receiver.stats;}
  const BurstQueue &queue()const{return queued_;}
  uint32_t sender_expired()const{return sender_.expired;}
};
static_assert(sizeof(Bridge)<11500,"Fixed live radio workspace");
} // namespace correction
