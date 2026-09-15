#pragma once
#include "correction_transport.h"

// Update intent/status only. This core cannot write flash, change a receiver,
// select a correction session, or make a survey observation usable.
namespace update_notice {
constexpr size_t packet_size = 40;
constexpr uint32_t max_notice_ms = 180000, retry_ms = 500, retry_window_ms = 3000;
enum class Kind : uint8_t { Prepare = 1, Updating = 2, Cancel = 3, Reconnected = 4, Ack = 5 };
enum class State : uint8_t { None, Preparing, Updating, Overdue, Reconnecting };
struct Packet { uint8_t bytes[packet_size]{}; };
struct Message {
  Kind kind = Kind::Prepare;
  uint8_t from = 0, to = 0, role = 0; // Instrument IDs 1/2; role 0 Base, 1 Rover.
  uint32_t session = 0, attempt = 0, sequence = 0, duration = 0;
  Kind acknowledged = Kind::Prepare;
};
inline bool valid(const Message &m) {
  const auto k = uint8_t(m.kind), a = uint8_t(m.acknowledged);
  if (k < 1 || k > 5 || m.from < 1 || m.from > 2 || m.to != 3-m.from ||
      m.role > 1 || !m.session || !m.attempt || !m.sequence) return false;
  if (m.kind == Kind::Prepare || m.kind == Kind::Updating)
    return m.duration >= retry_window_ms && m.duration <= max_notice_ms && a == 1;
  if (m.duration) return false;
  return m.kind == Kind::Ack ? a >= 1 && a <= 4 : a == 1;
}
inline bool encode(const Message &m, Packet &p) {
  if (!valid(m)) return false;
  p = Packet{}; auto *b = p.bytes;
  std::memcpy(b,"TUP1",4); b[4]=1; b[5]=uint8_t(m.kind); b[6]=m.from; b[7]=m.to;
  correction::put32(b+8,m.session); correction::put32(b+12,m.attempt);
  correction::put32(b+16,m.sequence); correction::put32(b+20,m.duration);
  b[24]=m.role; b[25]=uint8_t(m.acknowledged);
  correction::put32(b+36,correction::crc32(b,36)); return true;
}
inline bool decode(const uint8_t *b, size_t size, Message &m) {
  if (!b || size != packet_size || std::memcmp(b,"TUP1",4) || b[4]!=1 ||
      correction::u32(b+36)!=correction::crc32(b,36)) return false;
  for (unsigned i=26;i<36;++i) if (b[i]) return false;
  Message candidate;
  candidate.kind=Kind(b[5]); candidate.from=b[6]; candidate.to=b[7];
  candidate.session=correction::u32(b+8); candidate.attempt=correction::u32(b+12);
  candidate.sequence=correction::u32(b+16); candidate.duration=correction::u32(b+20);
  candidate.role=b[24]; candidate.acknowledged=Kind(b[25]);
  if (!valid(candidate)) return false;
  m=candidate; return true;
}

class Peer {
  uint8_t local_=0, peer_=0, role_=0;
  uint32_t session_=0, highest_=0, sequence_=0, started_=0, duration_=0, last_duration_=0;
  Kind last_kind_=Kind::Prepare;
  State state_=State::None;
 public:
  // Only a trusted adapter may select a NEW session. Do not call for duplicate
  // join requests: clearing highest_ would erase the replay boundary.
  bool select(uint8_t local, uint32_t session) {
    if (local<1 || local>2 || !session) return false;
    if (local==local_ && session==session_) return true;
    local_=local; peer_=3-local; session_=session; highest_=sequence_=0;
    state_=State::None; return true;
  }
  void tick(uint32_t now) {
    if ((state_==State::Preparing || state_==State::Updating || state_==State::Reconnecting) &&
        now-started_>=duration_) state_=State::Overdue;
  }
  bool receive(const Message &m, uint32_t now, Message &ack) {
    tick(now);
    if (!valid(m) || m.kind==Kind::Ack || m.from!=peer_ || m.to!=local_ ||
        m.session!=session_ || m.attempt<highest_) return false;
    if (m.attempt>highest_) {
      // A reordered cancellation is a tombstone: later buffered prepare/update
      // packets for that attempt must not reopen it. Reconnect needs an update.
      if (m.kind==Kind::Reconnected) return false;
      highest_=m.attempt; sequence_=0; started_=now; duration_=m.duration; role_=m.role;
      state_=State::Preparing;
    }
    if (m.role!=role_ || m.sequence<sequence_) return false;
    if (m.sequence==sequence_) {
      // Exact phase retries may be acknowledged, but never refresh the deadline.
      if (m.kind!=last_kind_ || m.duration!=last_duration_ || state_==State::Overdue ||
          (state_==State::None && m.kind!=Kind::Cancel)) return false;
    } else {
      if (state_==State::None || state_==State::Overdue) return false;
      if (m.kind==Kind::Prepare && sequence_) return false;
      if (m.kind==Kind::Updating && state_!=State::Preparing) return false;
      if (m.kind==Kind::Reconnected && state_!=State::Updating) return false;
      if (m.kind==Kind::Cancel) state_=State::None;
      else if (m.kind==Kind::Updating) state_=State::Updating;
      else if (m.kind==Kind::Reconnected) state_=State::Reconnecting;
      // A later phase may shorten, but cannot extend, the original deadline.
      if (m.duration) duration_=std::min(duration_,uint32_t(std::min(uint64_t(max_notice_ms),uint64_t(now-started_)+m.duration)));
      sequence_=m.sequence; last_kind_=m.kind; last_duration_=m.duration;
    }
    ack=Message{}; ack.kind=Kind::Ack; ack.from=local_; ack.to=peer_;
    ack.role=1-role_; ack.session=session_; ack.attempt=m.attempt;
    ack.sequence=m.sequence; ack.acknowledged=m.kind; return true;
  }
  // Adapter must establish fresh peer boot/attempt correlation and local quality.
  // A network notice alone never calls this or bypasses correction-age gates.
  bool recovered(uint32_t attempt, bool fresh_identity, bool quality_ready) {
    if (attempt!=highest_ || !fresh_identity || !quality_ready ||
        (state_!=State::Reconnecting && state_!=State::Overdue)) return false;
    state_=State::None; return true;
  }
  State state() const { return state_; }
  uint32_t attempt() const { return highest_; }
  const char *label() const {
    switch(state_) {
      case State::Preparing: return role_ ? "Rover preparing update" : "Base preparing update";
      case State::Updating: return role_ ? "Rover updating - reception paused" : "Base updating - corrections paused";
      case State::Overdue: return "Update overdue - link unavailable";
      case State::Reconnecting: return "Peer reconnected - checking corrections";
      default: return "";
    }
  }
};

class Sender {
  Message current_{};
  uint32_t phase_at_=0, last_sent_=0, attempt_at_=0, budget_=0;
  unsigned sent_=0;
  bool active_=false, acknowledged_=false;
 public:
  bool begin(uint8_t local, bool rover, uint32_t session, uint32_t attempt,
             uint32_t duration, uint32_t now) {
    if (active_ || !session || !attempt || duration<retry_window_ms || duration>max_notice_ms) return false;
    // Attempt numbers must increase within a selected session, including cancels.
    if (current_.session==session && attempt<=current_.attempt) return false;
    Message m; m.from=local; m.to=3-local; m.role=rover; m.session=session;
    m.attempt=attempt; m.sequence=1; m.duration=duration;
    if (!valid(m)) return false;
    current_=m; phase_at_=attempt_at_=now; budget_=duration;
    sent_=0; acknowledged_=false; active_=true; return true;
  }
  bool transition(Kind kind, uint32_t now) {
    if (!active_ || current_.sequence==UINT32_MAX || now-attempt_at_>=budget_) return false;
    if (kind==Kind::Updating && current_.kind!=Kind::Prepare) return false;
    if (kind==Kind::Reconnected && current_.kind!=Kind::Updating) return false;
    if (kind!=Kind::Updating && kind!=Kind::Cancel && kind!=Kind::Reconnected) return false;
    if (current_.kind==Kind::Cancel || current_.kind==Kind::Reconnected) return false;
    const auto remaining=budget_-(now-attempt_at_);
    if (kind==Kind::Updating && remaining<retry_window_ms) return false;
    current_.kind=kind; ++current_.sequence;
    current_.duration=kind==Kind::Updating ? remaining : 0;
    phase_at_=now; sent_=0; acknowledged_=false; return true;
  }
  bool next(Packet &p, uint32_t now) const {
    if (!active_ || acknowledged_ || now-attempt_at_>=budget_ || now-phase_at_>=retry_window_ms ||
        sent_>=6 || (sent_ && now-last_sent_<retry_ms)) return false;
    return encode(current_,p);
  }
  void committed(uint32_t now) { ++sent_; last_sent_=now; }
  bool receive(const Message &m, uint32_t now) {
    if (!active_ || now-attempt_at_>=budget_ || !valid(m) || m.kind!=Kind::Ack || m.from!=current_.to || m.to!=current_.from ||
        m.role==current_.role || m.session!=current_.session || m.attempt!=current_.attempt ||
        m.sequence!=current_.sequence || m.acknowledged!=current_.kind) return false;
    acknowledged_=true; return true;
  }
  bool acknowledged() const { return acknowledged_; }
  // Adapter closes after abort/success or a bounded timeout, retaining high water.
  void close() { active_=false; }
};

// Demultiplex at an envelope boundary in the UART owner. Feeding this parser
// independently with arbitrary RTCM payload bytes is NOT a safe multiplexer.
static_assert(sizeof(Peer)+sizeof(Sender)<256,"Bounded update notice state");
} // namespace update_notice
