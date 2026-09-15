#pragma once
#include "correction_transport.h"

namespace pair_session {
enum class Transport : uint8_t { WiFi=0, Radio=1 };
struct Snapshot {
  Transport transport=Transport::WiFi;
  uint32_t local_boot=0, peer_boot=0, session=0, generation=0, peer_age_ms=UINT32_MAX;
  bool rover=false, established=false, connected=false;
  const char *reason="negotiating";
};

// Main-loop owner. No received discovery packet can mutate the active session.
// Call committed only when the complete prepared envelope was accepted for TX.
class Engine {
 public:
  void begin(uint8_t unit,bool rover,Transport transport,uint32_t boot,uint32_t now,uint32_t (*random)());
  bool receive(const correction::Packet &,uint32_t now);
  bool next(correction::Packet &,uint32_t now);
  void committed(const correction::Packet &,uint32_t now);
  void tick(uint32_t now);
  Snapshot snapshot(uint32_t now) const;
  void incompatible(uint32_t now);
 private:
  enum class Stage : uint8_t { None, Hello, Challenge, Proof, Offer, Accept, Confirm };
  struct Candidate {
    Stage stage=Stage::None;
    uint32_t boot=0, local=0, peer=0, session=0, started=0, sent=0;
    bool transmitted=false;
  } candidate_{};
  uint8_t unit_=0;
  bool rover_=false, valid_=false, established_=false;
  Transport transport_=Transport::WiFi;
  uint32_t boot_=0, generation_=0, started_=0, discovery_=0;
  uint32_t peer_boot_=0, session_=0, local_nonce_=0, peer_nonce_=0;
  uint32_t serial_=0, (*random_)()=nullptr;
  uint32_t hello_sent_=0, heartbeat_sent_=0, challenge_sent_=0, proof_time_=0;
  uint32_t heartbeat_=0, peer_heartbeat_=0;
  bool hello_transmitted_=false, heartbeat_transmitted_=false;
  bool challenge_transmitted_=false, peer_heartbeat_seen_=false, proof_seen_=false;
  bool confirm_pending_=false, protocol_error_=false, role_error_=false;
  uint32_t nonce();
  void rover_candidate(uint32_t now);
  void establish(uint32_t now);
  void advance_heartbeat();
  void encode(correction::Packet &,uint8_t kind,uint32_t target,uint32_t challenge,uint32_t echo,uint32_t session) const;
};
static_assert(sizeof(Engine)<256,"Pair engine fixed memory budget");
} // namespace pair_session
