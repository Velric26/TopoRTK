#include "link_operation.h"

namespace link_operation {
namespace {
constexpr uint32_t negotiate_ms = 20000, commit_ms = 10000, restore_ms = 10000, retry_ms = 500;
// A Test runs the canonical 30 s profile; this window is the coordinator's bound
// on waiting for both verdicts, not the run length itself.
constexpr uint32_t test_run_ms = 40000;
constexpr uint32_t tombstone_ms = 60000;
bool expired(uint32_t now, uint32_t since, uint32_t limit) { return uint32_t(now - since) >= limit; }
bool kind_valid(uint8_t kind) { return kind >= Request && kind <= Run; }
}  // namespace

bool decode(const correction::Packet &packet, Message &message) {
  if (!correction::wire_valid(packet)) return false;
  const uint8_t *p = packet.bytes, *b = p + 12;
  if (p[4] != 1 || p[5] != 3 || p[6] || p[7] || correction::u32(p + 8)) return false;
  if (std::memcmp(b, "PLC1", 4) || b[4] != 1) return false;
  for (unsigned i = 52; i < 252; ++i) if (p[i]) return false;
  if (b[10] || b[11] || correction::u32(b + 28) || b[34] || b[35] || correction::u32(b + 36)) return false;
  message.kind = b[5]; message.from = b[6]; message.to = b[7]; message.role = b[8]; message.transport = b[9];
  message.code = b[32]; message.profile = b[33];
  message.boot = correction::u32(b + 12); message.peer_boot = correction::u32(b + 16);
  message.tag = correction::u32(b + 20); message.revision = correction::u32(b + 24);
  if (!kind_valid(message.kind)) return false;
  if (message.from < 1 || message.from > 2 || message.to < 1 || message.to > 2 || message.from == message.to) return false;
  if (message.role > 1 || message.transport > 1 || !message.boot || !message.tag) return false;
  if (message.peer_boot) return false;   // reserved: the operation is identified by tag/revision
  if (message.kind == Request || message.kind == Prepare) {
    if (message.code < uint8_t(Kind::Select) || message.code > uint8_t(Kind::Test)) return false;
    if (message.profile > 1) return false;   // 0 clean, 1 injected faults
    // A forwarded request carries the client's expected revision, which is 0 on
    // an instrument that has never committed a selection. Every other operation
    // message carries the coordinator-assigned revision.
    if (message.kind != Request && !message.revision) return false;
  } else if (message.kind == Done) {
    if (message.profile) return false;
    if (message.code < kApplied || message.code > kCancelled) return false;
    // A denial echoes the forwarded request, which may carry no revision yet.
    if (message.code != kDenied && !message.revision) return false;
  } else if (message.code || message.profile || !message.revision) {
    return false;
  }
  return true;
}

void encode(correction::Packet &packet, uint8_t unit, bool rover, const Message &message) {
  packet = correction::Packet{};
  uint8_t *p = packet.bytes, *b = p + 12;
  std::memcpy(p, "RTM1", 4); p[4] = 1; p[5] = 3;
  std::memcpy(b, "PLC1", 4); b[4] = 1; b[5] = message.kind; b[6] = unit; b[7] = uint8_t(3 - unit);
  b[8] = rover ? 1 : 0; b[9] = message.transport; b[32] = message.code; b[33] = message.profile;
  correction::put32(b + 12, message.boot);
  correction::put32(b + 20, message.tag);
  correction::put32(b + 24, message.revision);
  correction::seal(packet);
}

bool valid(const Confirmed &record) {
  return record.magic == 0x314d4f43u && record.version == 1 && record.transport <= 1 &&
         !record.reserved[0] && !record.reserved[1] &&
         record.crc == correction::crc32(reinterpret_cast<const uint8_t *>(&record), offsetof(Confirmed, crc));
}
bool valid(const Pending &record) {
  return record.magic == 0x31444e50u && record.version == 1 && record.kind >= uint8_t(Kind::Select) &&
         record.kind <= uint8_t(Kind::Test) && record.target <= 1 && record.previous <= 1 &&
         record.committed <= 1 && !record.reserved[0] && !record.reserved[1] && !record.reserved[2] &&
         record.tag && record.revision &&
         record.crc == correction::crc32(reinterpret_cast<const uint8_t *>(&record), offsetof(Pending, crc));
}
void seal(Confirmed &record) {
  record.crc = correction::crc32(reinterpret_cast<const uint8_t *>(&record), offsetof(Confirmed, crc));
}
void seal(Pending &record) {
  record.crc = correction::crc32(reinterpret_cast<const uint8_t *>(&record), offsetof(Pending, crc));
}

const char *state_text(State state) {
  switch (state) {
    case State::Negotiating: return "negotiating";
    case State::Applying: return "running";
    case State::Restoring: return "restoring";
    case State::Succeeded: return "succeeded";
    case State::Failed: return "failed";
    case State::Cancelled: return "cancelled";
    case State::Interrupted: return "interrupted";
    case State::RecoveryRequired: return "recovery_required";
    default: return "idle";
  }
}
const char *reason_text(Reason reason) {
  switch (reason) {
    case Reason::PeerUnreachable: return "peer_unreachable";
    case Reason::Busy: return "busy";
    case Reason::StaleRevision: return "stale_revision";
    case Reason::ConflictingId: return "conflicting_id";
    case Reason::StorageFailure: return "storage_failure";
    case Reason::Cancelled: return "cancelled";
    case Reason::Interrupted: return "interrupted";
    case Reason::RestoreFailed: return "restore_failed";
    case Reason::Unsupported: return "unsupported";
    case Reason::Applied: return "applied";
    case Reason::Restored: return "restored";
    case Reason::Conflict: return "conflict";
    case Reason::TestFailed: return "test_failed";
    case Reason::TestUnavailable: return "test_unavailable";
    default: return "";
  }
}
const char *kind_text(Kind kind) {
  switch (kind) {
    case Kind::Select: return "select";
    case Kind::Test: return "test";
    default: return "none";
  }
}
uint32_t tag_from_hex(const char *hex) {
  if (!hex) return 0;
  uint32_t hash = 2166136261UL;
  size_t length = 0;
  for (const char *p = hex; *p; ++p) {
    const char c = *p;
    const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (digit < 0 || ++length > 32) return 0;
    hash ^= uint8_t(digit);
    hash *= 16777619UL;
    if (!hash) hash = 1;
  }
  return length == 32 ? hash : 0;
}

void Engine::begin(bool rover, bool storage_ok, const Confirmed *confirmed, const Pending *pending) {
  rover_ = rover; storage_ok_ = storage_ok;
  if (confirmed && valid(*confirmed)) confirmed_ = *confirmed;
  if (confirmed_.transport > 1) confirmed_.transport = 0;
  previous_ = target_ = static_cast<Transport>(confirmed_.transport);
  if (pending && valid(*pending)) {
    // Work in flight at reboot is never resumed: the link returns to the
    // durable confirmed selection and the outcome is reported as interrupted.
    pending_ = *pending;
    kind_ = static_cast<Kind>(pending_.kind);
    tag_ = pending_.tag; revision_ = pending_.revision;
    target_ = static_cast<Transport>(pending_.target);
    previous_ = static_cast<Transport>(pending_.previous);
    settled_tag_ = tag_; settled_kind_ = kind_; settled_target_ = target_;
    state_ = State::Interrupted; reason_ = Reason::Interrupted;
  }
}

void Engine::reserve(Kind kind, Transport transport, uint32_t tag, uint32_t revision, uint32_t now, uint8_t profile) {
  kind_ = kind; target_ = transport; previous_ = static_cast<Transport>(confirmed_.transport);
  tag_ = tag; revision_ = revision; profile_ = profile;
  test_passed_ = test_result_known_ = peer_test_passed_ = false;
  pending_ = Pending{};
  pending_.kind = uint8_t(kind); pending_.target = uint8_t(transport);
  pending_.previous = uint8_t(previous_); pending_.tag = tag; pending_.revision = revision;
  state_ = State::Negotiating; reason_ = Reason::None; phase_ = Phase::Prepare;
  started_ = now; sent_ = 0;   // 0 = nothing transmitted yet: the first message goes out at once
  peer_ready_ = commit_received_ = false;
  committed_ = committed_commit_pending_ = false;
  actions_.persist_pending = true; actions_.stage = true;
}

bool Engine::request(Kind kind, Transport transport, uint32_t tag, uint32_t revision, uint32_t now, Reason &reason,
                     uint8_t profile) {
  now_ = now;
  if (!tag || kind == Kind::None) { reason = Reason::Conflict; return false; }
  if (kind == Kind::Test && profile > 1) { reason = Reason::Unsupported; return false; }
  if (!storage_ok_) { reason = Reason::StorageFailure; return false; }
  if (tombstone_tag_ && tag == tombstone_tag_ && uint32_t(now - tombstone_at_) < tombstone_ms) { reason = Reason::Cancelled; return false; }
  if (settled_tag_ && tag == settled_tag_ && !(state_ == State::Idle)) {
    if (kind == settled_kind_ && transport == settled_target_) return true;   // idempotent repeat
    reason = Reason::ConflictingId; return false;
  }
  if (busy()) { reason = Reason::Busy; return false; }
  if (revision != confirmed_.revision) { reason = Reason::StaleRevision; return false; }
  reserve(kind, transport, tag, revision + 1, now, profile);
  return true;
}

bool Engine::forward(Kind kind, Transport transport, uint32_t tag, uint32_t revision, uint32_t now, Reason &reason,
                     uint8_t profile) {
  now_ = now;
  if (rover_) return request(kind, transport, tag, revision, now, reason);
  if (!tag || kind == Kind::None) { reason = Reason::Conflict; return false; }
  if (!storage_ok_) { reason = Reason::StorageFailure; return false; }
  if (tombstone_tag_ && tag == tombstone_tag_ && uint32_t(now - tombstone_at_) < tombstone_ms) { reason = Reason::Cancelled; return false; }
  if (settled_tag_ && tag == settled_tag_ && state_ != State::Idle) {
    if (kind == settled_kind_ && transport == settled_target_) return true;
    reason = Reason::ConflictingId; return false;
  }
  if (busy()) { reason = Reason::Busy; return false; }
  if (revision != confirmed_.revision) { reason = Reason::StaleRevision; return false; }
  kind_ = kind; target_ = transport; previous_ = static_cast<Transport>(confirmed_.transport);
  tag_ = tag; revision_ = revision; profile_ = profile;   // revision is expected only; the answer assigns the real one
  state_ = State::Negotiating; reason_ = Reason::None; phase_ = Phase::Prepare;
  started_ = now; sent_ = 0; forwarding_ = true; commit_received_ = false;
  committed_ = committed_commit_pending_ = false;
  actions_.stage = true;
  return true;
}

bool Engine::receive(const Message &message, uint32_t now) {
  now_ = now;
  if (tombstone_tag_ && message.tag == tombstone_tag_ && uint32_t(now - tombstone_at_) < tombstone_ms) return false;
  if (rover_) {
    switch (message.kind) {
      case Request: {
        // A forwarded client request: the coordinator admits it under its own
        // pair-wide revision and answers with a denial when it refuses.
        Reason reason = Reason::None;
        const auto kind = static_cast<Kind>(message.code);
        const auto transport = static_cast<Transport>(message.transport);
        if (request(kind, transport, message.tag, message.revision, now, reason)) return true;
        refuse(message.tag, kind, transport, reason, now);
        actions_.send = true;
        actions_.message = Message{}; actions_.message.kind = Done; actions_.message.code = kDenied;
        actions_.message.transport = message.transport; actions_.message.tag = message.tag; actions_.message.revision = message.revision;
        return false;
      }
      case Ready: if (message.tag != tag_ || !busy()) return false; peer_ready_ = true; return true;
      case Done:
        if (message.tag != tag_) return false;
        if (message.code == kDenied) { settle(State::Failed, Reason::Conflict, now); return true; }
        if (message.code == kCancelled) { settle(State::Cancelled, Reason::Cancelled, now); return true; }
        if (kind_ == Kind::Test) {
          // A test never adopts the tested medium: the delegate reports its own
          // verdict and both sides must pass for the operation to succeed.
          peer_test_passed_ = message.code == kApplied; peer_test_known_ = true;
          if (test_result_known_) {
            const bool both = test_passed_ && peer_test_passed_;
            settle(both ? State::Succeeded : State::Failed, both ? Reason::Applied : Reason::TestFailed, now);
          }
          return true;
        }
        if (message.code == kFailed) { settle(State::Failed, Reason::RestoreFailed, now); return true; }
        if (committed_ && state_ == State::Succeeded) return true;   // repeated acknowledgement
        if (state_ == State::Negotiating) { state_ = State::Applying; phase_ = Phase::Commit; advance_ = now; }
        committed_ = true; pending_.committed = 1;
        committed_commit_pending_ = true;
        confirmed_ = Confirmed{};
        confirmed_.transport = uint8_t(target_); confirmed_.revision = revision_;
        // The confirmed selection is written and read back before the switch.
        actions_.persist_confirmed = true; actions_.persist_pending = true;
        return true;
      default: return false;
    }
  }
  switch (message.kind) {
    case Request:
      // Only the Rover forwards requests; a Base-side coordinator does not exist.
      actions_.send = true;
      actions_.message = Message{}; actions_.message.kind = Done; actions_.message.code = kDenied;
      actions_.message.transport = message.transport; actions_.message.tag = message.tag; actions_.message.revision = message.revision;
      return false;
    case Prepare: {
      const auto kind = static_cast<Kind>(message.code);
      const auto transport = static_cast<Transport>(message.transport);
      auto deny = [&]() {
        actions_.send = true;
        actions_.message = Message{}; actions_.message.kind = Done; actions_.message.code = kDenied;
        actions_.message.transport = message.transport; actions_.message.tag = message.tag; actions_.message.revision = message.revision;
        return false;
      };
      // A retried prepare must never restart work this instrument already has:
      // before commit it just re-announces readiness, afterwards it stays done.
      // While a request is being forwarded this message is its answer, not a
      // duplicate, so it is adopted below.
      if (!forwarding_ && tag_ && message.tag == tag_) {
        if (committed_ || state_ == State::Succeeded || state_ == State::Applying) {
          if (!committed_ && (!sent_ || expired(now, sent_, retry_ms))) send(Ready, 0, now);
          return true;
        }
        if (!sent_ || expired(now, sent_, retry_ms)) send(Ready, 0, now);
        return true;
      }
      // One operation at a time: only an *unrelated* active operation (or an
      // unrecoverable storage state) refuses; a settled record, an interrupted
      // one, or the request this instrument is itself forwarding does not.
      if ((busy() && !forwarding_) || state_ == State::RecoveryRequired) return deny();
      if (message.revision <= confirmed_.revision) return deny();
      // Adopt the coordinator's assignment. This also completes a forwarded
      // request: the delegate stops asking and follows the coordinator.
      forwarding_ = false;
      tag_ = message.tag; revision_ = message.revision; kind_ = kind; target_ = transport;
      profile_ = message.profile;
      test_passed_ = test_result_known_ = peer_test_passed_ = peer_test_known_ = false;
      previous_ = static_cast<Transport>(confirmed_.transport);
      state_ = State::Negotiating; reason_ = Reason::None; phase_ = Phase::Prepare;
      peer_ready_ = commit_received_ = false;
      committed_ = committed_commit_pending_ = false;   // never inherit a previous operation's commit
      pending_ = Pending{};
      pending_.kind = uint8_t(kind); pending_.target = uint8_t(transport);
      pending_.previous = uint8_t(previous_); pending_.tag = tag_; pending_.revision = revision_;
      actions_.persist_pending = true;
      // The negotiation window starts when this operation is adopted, never
      // from a stale engine clock.
      started_ = now;
      sent_ = 0;
      actions_.stage = true;
      return true;
    }
    case Commit:
      if (message.tag != tag_) return false;
      if (committed_) {
        // A retransmitted commit acknowledges the same adopted selection; it
        // must not restart the apply timer or disturb the confirmed record.
        if (!rover_ && state_ == State::Succeeded) {
          actions_.send = true;
          actions_.message = Message{}; actions_.message.kind = Done; actions_.message.code = kApplied;
          actions_.message.transport = uint8_t(target_); actions_.message.tag = tag_; actions_.message.revision = revision_;
        }
        return true;
      }
      if (!busy()) return false;
      if (kind_ == Kind::Test) {
        // Run phase: the tested medium is staged and proven, never selected, so
        // the confirmed record and the durable revision stay untouched.
        state_ = State::Applying; phase_ = Phase::Run; advance_ = now;
        actions_.run_test = true; actions_.profile = profile_;
        return true;
      }
      phase_ = Phase::Commit; state_ = State::Applying; advance_ = now;
      committed_ = true; committed_commit_pending_ = true;
      pending_.committed = 1;
      confirmed_ = Confirmed{};
      confirmed_.transport = uint8_t(target_); confirmed_.revision = revision_;
      // The confirmed selection is written and read back before the switch.
      actions_.persist_confirmed = true; actions_.persist_pending = true;
      return true;
    case Done:
      if (message.tag != tag_) return false;
      if (message.code == kCancelled) { settle(State::Cancelled, Reason::Cancelled, now); return true; }
      if (message.code == kFailed) { settle(State::Failed, Reason::PeerUnreachable, now); return true; }
      return false;
    default: return false;
  }
}

bool Engine::cancel(uint32_t tag, uint32_t now) {
  now_ = now;
  if (!tag || tag != tag_ || !busy()) return false;
  tombstone_tag_ = tag; tombstone_at_ = now;
  // Whoever cancels tells the other side; a cancel after commit restores.
  actions_.send = true;
  actions_.message = Message{}; actions_.message.kind = Done; actions_.message.code = kCancelled;
  actions_.message.transport = uint8_t(target_); actions_.message.tag = tag_; actions_.message.revision = revision_;
  if (committed_ || committed_commit_pending_) { state_ = State::Restoring; phase_ = Phase::Restore; advance_ = now; actions_.restore = true; }
  else settle(State::Cancelled, Reason::Cancelled, now);
  return true;
}

void Engine::adopted(Transport transport, uint32_t revision) {
  // A local recovery selection is authoritative too: without this the engine
  // would keep reporting a stale previous medium and stale revision baseline.
  confirmed_.transport = uint8_t(transport);
  confirmed_.revision = revision;
  previous_ = target_ = transport;
  settled_tag_ = tag_ = revision_ = 0;
  kind_ = Kind::None; reason_ = Reason::None;
  if (!busy()) state_ = State::Idle;
}

void Engine::clear_interrupted() {
  if (state_ != State::Interrupted) return;
  state_ = State::Idle; reason_ = Reason::None; kind_ = Kind::None;
  settled_tag_ = tag_ = revision_ = 0;
  pending_ = Pending{};
}

void Engine::send(uint8_t kind, uint8_t code, uint32_t now) {
  actions_.send = true;
  actions_.message = Message{};
  actions_.message.kind = kind; actions_.message.code = code;
  actions_.message.transport = uint8_t(target_);
  actions_.message.tag = tag_; actions_.message.revision = revision_;
  sent_ = now;
}

void Engine::settle(State state, Reason reason, uint32_t now) {
  state_ = state; reason_ = reason; phase_ = Phase::None; advance_ = now;
  settled_tag_ = tag_; settled_kind_ = kind_; settled_target_ = target_;
  if (state == State::RecoveryRequired) return;   // durable pending record stays authoritative
  actions_.clear_pending = true; actions_.unstage = true;
}

void Engine::tick(const Inputs &inputs) {
  now_ = inputs.now;
  storage_ok_ = inputs.storage_ok;
  if (state_ == State::RecoveryRequired) return;
  if (inputs.cancel && busy()) cancel(tag_, inputs.now);
  switch (state_) {
    case State::Negotiating:
      if (expired(inputs.now, started_, negotiate_ms)) { settle(State::Failed, Reason::PeerUnreachable, inputs.now); return; }
      if (inputs.already_selected) {
        if (!rover_) {
          // The candidate medium already carries this instrument's production
          // session, so there is nothing to prove: report the candidate ready
          // and let the coordinator decide the cutover.
          if (!sent_ || expired(inputs.now, sent_, retry_ms)) send(Ready, 0, inputs.now);
          return;
        }
        // Coordinator: the requested medium is already proven, so the accepted
        // operation completes without a cutover. The durable revision advances
        // only when a selection actually commits. A Test still has to run.
        if (kind_ != Kind::Test) { settle(State::Succeeded, Reason::Applied, inputs.now); return; }
      }
      if (forwarding_) {
        if (!sent_ || expired(inputs.now, sent_, retry_ms)) send(Request, uint8_t(kind_), inputs.now);
        return;
      }
      if (!rover_) {
        if (inputs.staging_ready && (!sent_ || expired(inputs.now, sent_, retry_ms))) send(Ready, 0, inputs.now);
        return;
      }
      if (inputs.staging_ready && peer_ready_) {
        state_ = State::Applying;
        phase_ = kind_ == Kind::Test ? Phase::Run : Phase::Commit;
        advance_ = inputs.now;
        if (kind_ == Kind::Test) { actions_.run_test = true; actions_.profile = profile_; }
        send(Commit, 0, inputs.now);
        return;
      }
      if (!sent_ || expired(inputs.now, sent_, retry_ms)) send(Prepare, uint8_t(kind_), inputs.now);
      return;
    case State::Applying:
      if (kind_ == Kind::Test) {
        // The engines report their own verdicts; this window only bounds the wait.
        if (expired(inputs.now, advance_, test_run_ms)) {
          if (rover_) settle(State::Failed, Reason::PeerUnreachable, inputs.now);
          else {
            actions_.send = true;
            actions_.message = Message{}; actions_.message.kind = Done; actions_.message.code = kFailed;
            actions_.message.transport = uint8_t(target_); actions_.message.tag = tag_; actions_.message.revision = revision_;
            settle(State::Failed, Reason::TestFailed, inputs.now);
          }
        }
        return;
      }
      if (committed_commit_pending_) { actions_.commit = true; committed_commit_pending_ = false; }
      if (inputs.production_on_target) {
        if (!rover_) {
          actions_.send = true;
          actions_.message = Message{}; actions_.message.kind = Done; actions_.message.code = kApplied;
          actions_.message.transport = uint8_t(target_); actions_.message.tag = tag_; actions_.message.revision = revision_;
          settle(State::Succeeded, Reason::Applied, inputs.now);
        } else if (committed_) {
          settle(State::Succeeded, Reason::Applied, inputs.now);
        }
        return;
      }
      if (expired(inputs.now, advance_, commit_ms)) {
        state_ = State::Restoring; phase_ = Phase::Restore; advance_ = inputs.now; actions_.restore = true;
        return;
      }
      if (rover_ && (!sent_ || expired(inputs.now, sent_, retry_ms))) send(Commit, 0, inputs.now);
      return;
    case State::Restoring:
      if (inputs.production_on_target) { settle(State::Failed, Reason::Restored, inputs.now); return; }
      if (expired(inputs.now, advance_, restore_ms)) { settle(State::RecoveryRequired, Reason::RestoreFailed, inputs.now); }
      return;
    default:
      return;
  }
}

Snapshot Engine::snapshot(uint32_t now) const {
  Snapshot out;
  out.state = state_; out.kind = kind_; out.tag = tag_; out.revision = revision_;
  out.transport = target_; out.previous = previous_; out.reason = reason_;
  out.coordinator = rover_; out.committed = committed_;
  out.profile = profile_; out.test_passed = test_result_known_ && test_passed_;
  out.active = state_ == State::Negotiating || state_ == State::Applying || state_ == State::Restoring;
  if (out.active) {
    const uint32_t limit = phase_ == Phase::Run ? test_run_ms
        : state_ == State::Negotiating ? negotiate_ms : state_ == State::Applying ? commit_ms : restore_ms;
    const uint32_t elapsed = uint32_t(now - started_);
    out.phase_remaining_ms = elapsed >= limit ? 0 : limit - elapsed;
  }
  return out;
}

Actions Engine::take() {
  Actions out = actions_;
  actions_ = Actions{};
  return out;
}

void Engine::refuse(uint32_t tag, Kind kind, Transport transport, Reason reason, uint32_t now) {
  tag_ = tag; kind_ = kind; target_ = transport;
  if (tombstone_tag_ == tag) { tombstone_tag_ = 0; }
  settle(State::Failed, reason, now);
}

void Engine::test_result(bool pass, uint32_t now) {
  now_ = now;
  if (kind_ != Kind::Test || phase_ != Phase::Run || state_ != State::Applying) return;
  test_passed_ = pass; test_result_known_ = true;
  if (!rover_) {
    // The delegate reports its verdict; the coordinator decides the pair outcome.
    actions_.send = true;
    actions_.message = Message{};
    actions_.message.kind = Done; actions_.message.code = pass ? kApplied : kFailed;
    actions_.message.transport = uint8_t(target_); actions_.message.tag = tag_; actions_.message.revision = revision_;
    settle(pass ? State::Succeeded : State::Failed, pass ? Reason::Applied : Reason::TestFailed, now);
    return;
  }
  if (peer_test_known_) {
    const bool both = test_passed_ && peer_test_passed_;
    settle(both ? State::Succeeded : State::Failed, both ? Reason::Applied : Reason::TestFailed, now);
  }
}

void Engine::test_unavailable(uint32_t now) {
  now_ = now;
  if (kind_ != Kind::Test || phase_ != Phase::Run || state_ != State::Applying) return;
  if (!rover_) {
    actions_.send = true;
    actions_.message = Message{};
    actions_.message.kind = Done; actions_.message.code = kFailed;
    actions_.message.transport = uint8_t(target_); actions_.message.tag = tag_; actions_.message.revision = revision_;
  }
  settle(State::Failed, Reason::TestUnavailable, now);
}

void Engine::persisted(bool ok) {
  if (ok) return;
  settle(State::RecoveryRequired, Reason::StorageFailure, now_);
}
void Engine::commit_result(bool ok) {
  if (ok) return;
  settle(State::RecoveryRequired, Reason::StorageFailure, now_);
}
void Engine::restore_result(bool ok) {
  if (!ok) { settle(State::RecoveryRequired, Reason::RestoreFailed, now_); return; }
  // Restoration adopted the previous confirmed selection again.
  confirmed_.transport = uint8_t(previous_);
}
}  // namespace link_operation
