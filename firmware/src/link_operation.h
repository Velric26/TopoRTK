#pragma once
#include "correction_transport.h"
#include "pair_session.h"

// R6 pair-wide link operations: one durable selection/operation record per
// instrument, a coordinator (always the Rover unit) that admits at most one
// operation, and a bounded negotiate/apply/restore cutover. Portable: no
// Arduino, NVS, sockets or logging. The link owner supplies observed facts and
// performs the returned actions; this module decides.
namespace link_operation {
// One canonical medium type: the pair engine owns the wire meaning.
using Transport = pair_session::Transport;

enum class Kind : uint8_t { None = 0, Select = 1, Test = 2 };
enum class State : uint8_t {
  Idle = 0, Negotiating = 1, Applying = 2, Restoring = 3,
  Succeeded = 4, Failed = 5, Cancelled = 6, Interrupted = 7, RecoveryRequired = 8,
};
enum class Reason : uint8_t {
  None = 0, PeerUnreachable = 1, Busy = 2, StaleRevision = 3, ConflictingId = 4,
  StorageFailure = 5, Cancelled = 6, Interrupted = 7, RestoreFailed = 8,
  Unsupported = 9, Applied = 10, Restored = 11, Conflict = 12,
};

// PLC1 operation kinds, above the negotiation kinds pair_session owns (1..6).
enum Msg : uint8_t { Request = 7, Prepare = 8, Ready = 9, Commit = 10, Done = 11 };
// Body byte for Request/Prepare: the requested operation kind.
// Body byte for Done: the outcome code below.
enum Done : uint8_t { kApplied = 1, kFailed = 2, kDenied = 3, kCancelled = 4 };

struct Message {
  uint8_t kind = 0, from = 0, to = 0, role = 0, transport = 0, code = 0;
  uint32_t boot = 0, peer_boot = 0, tag = 0, revision = 0;
};
// Validates one complete PLC1 envelope carrying an operation kind.
bool decode(const correction::Packet &packet, Message &message);
void encode(correction::Packet &packet, uint8_t unit, bool rover, const Message &message);

// Durable records (NVS namespace topolink). Magic/version/CRC checked.
struct Confirmed { uint32_t magic = 0x314d4f43u; uint8_t version = 1, transport = 0, reserved[2] = {}; uint32_t revision = 0, crc = 0; };
struct Pending { uint32_t magic = 0x31444e50u; uint8_t version = 1, kind = 0, target = 0, previous = 0, committed = 0, reserved[3] = {}; uint32_t tag = 0, revision = 0, crc = 0; };
bool valid(const Confirmed &record);
bool valid(const Pending &record);
void seal(Confirmed &record);
void seal(Pending &record);

const char *state_text(State state);
const char *reason_text(Reason reason);
const char *kind_text(Kind kind);
// 32 lowercase hex characters to a nonzero tag; 0 for any malformed id.
uint32_t tag_from_hex(const char *hex);

struct Snapshot {
  State state = State::Idle;
  Kind kind = Kind::None;
  Transport transport = Transport::WiFi, previous = Transport::WiFi;
  uint32_t tag = 0, revision = 0, phase_remaining_ms = 0;
  Reason reason = Reason::None;
  bool coordinator = false, active = false, committed = false;
};

// Observed facts supplied by the link owner each service turn.
struct Inputs {
  uint32_t now = 0;
  bool rover = false;              // coordinator role (always the Rover unit)
  bool busy = false;               // occupation/OTA/probe/profile/reservation
  bool storage_ok = true;
  bool staging_ready = false;      // local candidate-medium proof complete
  bool production_on_target = false;  // production session up on the expected medium
  bool already_selected = false;   // requested medium is the confirmed, connected one
  bool cancel = false;
};

struct Actions {
  bool persist_pending = false, clear_pending = false, persist_confirmed = false;
  bool commit = false;    // adopt target: pause corrections, switch, restart on target
  bool restore = false;   // return to the previous confirmed selection
  bool stage = false;     // start/adopt candidate-medium staging
  bool unstage = false;   // release staging, candidate abandoned
  bool send = false;
  Message message{};
};

class Engine {
 public:
  // Restores durable state. A pending record that never committed is reported
  // interrupted and the confirmed selection stays authoritative.
  void begin(bool rover, bool storage_ok, const Confirmed *confirmed, const Pending *pending);
  // Coordinator admission for a request (local or forwarded by a delegate).
  bool request(Kind kind, Transport transport, uint32_t tag, uint32_t revision, uint32_t now, Reason &reason);
  // Delegate initiation: forward a client request to the coordinator.
  bool forward(Kind kind, Transport transport, uint32_t tag, uint32_t revision, uint32_t now, Reason &reason);
  bool receive(const Message &message, uint32_t now);
  bool cancel(uint32_t tag, uint32_t now);
  // After a local (recovery) selection the interrupted record is settled.
  void clear_interrupted();
  void tick(const Inputs &inputs);
  Snapshot snapshot(uint32_t now) const;
  bool busy() const { return state_ == State::Negotiating || state_ == State::Applying || state_ == State::Restoring; }
  const Pending &pending() const { return pending_; }
  uint32_t revision() const { return confirmed_.revision; }
  Transport selected() const { return static_cast<Transport>(confirmed_.transport); }
  Actions take();
  // The main loop refused a request that HTTP already accepted: the outcome is
  // published against that operation id instead of a retroactive HTTP status.
  void refuse(uint32_t tag, Kind kind, Transport transport, Reason reason, uint32_t now);
  // Durable-write outcomes reported by the link owner.
  void persisted(bool ok);
  void commit_result(bool ok);
  void restore_result(bool ok);

 private:
  enum class Phase : uint8_t { None = 0, Prepare, Commit, Restore };
  void send(uint8_t kind, uint8_t code, uint32_t now);
  void settle(State state, Reason reason, uint32_t now);
  void reserve(Kind kind, Transport transport, uint32_t tag, uint32_t revision, uint32_t now);

  State state_ = State::Idle;
  Reason reason_ = Reason::None;
  Kind kind_ = Kind::None;
  Phase phase_ = Phase::None;
  bool rover_ = false, storage_ok_ = true, forwarding_ = false;
  bool peer_ready_ = false, commit_received_ = false, committed_ = false, committed_commit_pending_ = false;
  uint32_t tag_ = 0, revision_ = 0, now_ = 0, started_ = 0, sent_ = 0, advance_ = 0;
  uint32_t settled_tag_ = 0, tombstone_tag_ = 0, tombstone_at_ = 0;
  Kind settled_kind_ = Kind::None;
  Transport settled_target_ = Transport::WiFi;
  Transport target_ = Transport::WiFi, previous_ = Transport::WiFi;
  Confirmed confirmed_{};
  Pending pending_{};
  Actions actions_{};
};

static_assert(sizeof(Engine) < 256, "Link operation fixed memory budget");
}  // namespace link_operation
