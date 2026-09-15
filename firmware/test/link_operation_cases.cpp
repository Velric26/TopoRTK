// Production link-operation core: two engines wired like the real service, with
// the wire codec exercised for every delivered message.
#include "link_operation.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using link_operation::Actions;
using link_operation::Engine;
using link_operation::Kind;
using link_operation::Message;
using link_operation::Reason;
using link_operation::State;
using link_operation::Transport;

namespace {
uint32_t now_ms = 1000;

struct Sent { bool from_rover; Message message; };

struct Node {
  Engine engine;
  link_operation::Confirmed confirmed{};
  link_operation::Pending pending{};
  bool rover = false, storage_ok = true, persist_ok = true;
  bool staging_active = false, staging_ready = false, production = false;
  bool target_usable = true, restore_usable = true;   // fault injection for cutover failures
  uint32_t staged_at = 0;
  Transport selected = Transport::WiFi;
  std::vector<Sent> history;

  void begin(bool is_rover, const link_operation::Pending *durable = nullptr) {
    rover = is_rover;
    confirmed = link_operation::Confirmed{};
    engine.begin(rover, storage_ok, &confirmed, durable);
  }
  Transport expected() const {
    const auto op = engine.snapshot(now_ms);
    return op.state == State::Restoring ? op.previous : op.transport;
  }
  bool on_expected() const { return production && selected == expected(); }
  void pump(uint32_t now) {
    link_operation::Inputs in;
    in.now = now; in.rover = rover; in.busy = false; in.storage_ok = storage_ok;
    in.staging_ready = staging_active && staging_ready;
    in.production_on_target = on_expected();
    const auto op = engine.snapshot(now);
    in.already_selected = op.active && op.transport == selected && production;
    engine.tick(in);
    apply(now);
    if (staging_active && !staging_ready && now - staged_at >= 200) staging_ready = true;
  }
  void apply(uint32_t now) {
    auto actions = engine.take();
    if (actions.persist_pending) {
      pending = engine.pending();
      engine.persisted(persist_ok);
      if (!persist_ok) storage_ok = false;
    }
    if (actions.persist_confirmed) {
      confirmed.transport = uint8_t(engine.selected());
      confirmed.revision = engine.revision();
      link_operation::seal(confirmed);
      engine.persisted(persist_ok);
      if (!persist_ok) storage_ok = false;
    }
    if (actions.stage) {
      if (engine.snapshot(now).transport != selected) {
        staging_active = true; staging_ready = false; staged_at = now;
      }
    }
    if (actions.commit) { selected = engine.snapshot(now).transport; production = target_usable; }
    if (actions.restore) {
      const auto op = engine.snapshot(now);
      selected = op.previous;
      confirmed.transport = uint8_t(op.previous);
      production = restore_usable;
      engine.restore_result(restore_usable);
    }
    if (actions.unstage) { staging_active = false; staging_ready = false; }
    if (actions.clear_pending) pending = link_operation::Pending{};
    if (actions.send) {
      correction::Packet packet;
      Message outbound = actions.message;
      outbound.boot = rover ? 0x2222u : 0x1111u;
      link_operation::encode(packet, uint8_t(rover ? 2 : 1), rover, outbound);
      Message decoded;
      if (!link_operation::decode(packet, decoded)) {
        std::printf("ENCODE-FAIL kind=%u code=%u transport=%u tag=%08x rev=%u boot=%08x from=%u\n",
                    outbound.kind, outbound.code, outbound.transport, outbound.tag, outbound.revision,
                    outbound.boot, unsigned(rover ? 2 : 1));
        std::fflush(stdout);
      }
      assert(link_operation::decode(packet, decoded));
      history.push_back({rover, decoded});
      out.push_back(decoded);
    }
  }
  std::vector<Message> out;
};

struct Pair {
  Node base, rover;
  bool drop_base = false, drop_rover = false;
  Pair() { base.begin(false); rover.begin(true); }
  void step() {
    const std::vector<Message> from_base = base.out; base.out.clear();
    const std::vector<Message> from_rover = rover.out; rover.out.clear();
    for (const auto &m : from_base) if (!drop_base) { rover.engine.receive(m, now_ms); rover.apply(now_ms); }
    for (const auto &m : from_rover) if (!drop_rover) { base.engine.receive(m, now_ms); base.apply(now_ms); }
    base.pump(now_ms); rover.pump(now_ms);
    now_ms += 50;
  }
  void run(uint32_t duration) { for (uint32_t i = 0; i < duration; i += 50) step(); }
};

// A well-formed lowercase 32-hex command id for a given seed.
uint32_t tag_of(unsigned seed) {
  char id[33];
  for (unsigned i = 0; i < 16; ++i) std::snprintf(id + 2 * i, 3, "%02x", seed & 0xff);
  id[32] = 0;
  const uint32_t tag = link_operation::tag_from_hex(id);
  assert(tag);
  return tag;
}

void selection_succeeds() {
  Pair p;
  Reason reason;
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x11), 0, now_ms, reason));
  assert(p.rover.engine.revision() == 0);   // In flight: the durable revision only moves on commit.
  p.run(1200);
  const auto coordinator = p.rover.engine.snapshot(now_ms);
  const auto delegate = p.base.engine.snapshot(now_ms);
  assert(coordinator.state == State::Succeeded && coordinator.reason == Reason::Applied);
  assert(delegate.state == State::Succeeded);
  assert(p.rover.selected == Transport::Radio && p.base.selected == Transport::Radio);
  assert(p.rover.confirmed.revision == 1 && p.base.confirmed.revision == 1);
  assert(p.rover.confirmed.transport == uint8_t(Transport::Radio));
  assert(p.base.pending.tag == 0);   // committed work releases the durable marker
}

void stale_busy_and_conflicts() {
  Pair p;
  Reason reason;
  assert(!p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x22), 7, now_ms, reason));
  assert(reason == Reason::StaleRevision);
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x22), 0, now_ms, reason));
  assert(!p.rover.engine.request(Kind::Select, Transport::WiFi, tag_of(0x33), 0, now_ms, reason));
  assert(reason == Reason::Busy);
  p.run(1200);
  // A repeated id with the same body reports the existing outcome; a new body
  // for that id is rejected outright.
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x22), 0, now_ms, reason));
  assert(p.rover.engine.revision() == 1);
  assert(!p.rover.engine.request(Kind::Select, Transport::WiFi, tag_of(0x22), 0, now_ms, reason));
  assert(reason == Reason::ConflictingId);
  // Repeating the accepted id/body after completion returns its existing
  // outcome; the recorded revision is not re-checked for that same operation.
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x22), 1, now_ms, reason));
  assert(p.rover.engine.revision() == 1);
}

void already_selected_is_immediate() {
  Pair p;
  p.rover.production = p.base.production = true;
  Reason reason;
  assert(p.rover.engine.request(Kind::Select, Transport::WiFi, tag_of(0x44), 0, now_ms, reason));
  p.run(200);
  assert(p.rover.engine.snapshot(now_ms).state == State::Succeeded);
  assert(p.rover.engine.revision() == 0);   // No commit happened, so no revision moved.
  assert(p.rover.out.empty());   // no peer traffic for a no-op selection
}

void unreachable_peer_fails_without_change() {
  Pair p;
  p.drop_rover = p.drop_base = true;
  Reason reason;
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x55), 0, now_ms, reason));
  p.run(21000);
  const auto state = p.rover.engine.snapshot(now_ms);
  assert(state.state == State::Failed && state.reason == Reason::PeerUnreachable);
  assert(p.rover.selected == Transport::WiFi && p.rover.confirmed.transport == uint8_t(Transport::WiFi));
  assert(p.rover.pending.tag == 0);   // cleared: nothing was committed
  assert(p.base.selected == Transport::WiFi);
}

void cancel_paths() {
  Pair p;
  Reason reason;
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x66), 0, now_ms, reason));
  p.run(100);   // The peer admitted it; the candidate is not proven yet.
  assert(p.base.engine.busy());
  assert(p.rover.engine.cancel(tag_of(0x66), now_ms));
  p.run(400);
  assert(p.rover.engine.snapshot(now_ms).state == State::Cancelled);
  assert(p.base.engine.snapshot(now_ms).state == State::Cancelled);
  assert(p.rover.selected == Transport::WiFi && p.base.selected == Transport::WiFi);
  // The cancelled id is tombstoned: a delayed retry cannot restart it.
  assert(!p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x66), 0, now_ms, reason));
  assert(reason == Reason::Cancelled);
}

void denied_delegate_keeps_selection() {
  Pair p;
  Reason reason;
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x77), 0, now_ms, reason));
  // The delegate answers the preparation with a denial (busy elsewhere).
  Message denied;
  denied.kind = link_operation::Done; denied.code = link_operation::kDenied;
  denied.transport = uint8_t(Transport::Radio); denied.tag = tag_of(0x77); denied.revision = 1;
  p.rover.engine.receive(denied, now_ms);
  p.run(200);
  assert(p.rover.engine.snapshot(now_ms).state == State::Failed);
  assert(p.rover.engine.snapshot(now_ms).reason == Reason::Conflict);
  assert(p.rover.selected == Transport::WiFi);
}

void restoration_and_recovery() {
  // The candidate never carries production: the committed cutover must restore
  // the previously confirmed medium and report the actual outcome.
  {
    Pair p;
    Reason reason;
    p.rover.production = p.base.production = true;
    p.rover.target_usable = false;
    assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x88), 0, now_ms, reason));
    p.run(12000);
    const auto state = p.rover.engine.snapshot(now_ms);
    assert(state.state == State::Failed && state.reason == Reason::Restored);
    assert(p.rover.selected == Transport::WiFi && p.rover.confirmed.transport == uint8_t(Transport::WiFi));
  }
  // When restoration cannot be established either, the outcome is explicit and
  // the durable pending record is kept instead of claiming success.
  {
    Pair p;
    Reason reason;
    p.rover.production = p.base.production = true;
    p.rover.target_usable = false;
    p.rover.restore_usable = false;
    assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0x99), 0, now_ms, reason));
    p.run(22000);
    const auto state = p.rover.engine.snapshot(now_ms);
    assert(state.state == State::RecoveryRequired);
    assert(state.reason == Reason::RestoreFailed);
    assert(p.rover.pending.tag == tag_of(0x99));
  }
}

void persistence_failure_is_recovery_required() {
  Pair p;
  p.rover.persist_ok = false;
  Reason reason;
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0xaa), 0, now_ms, reason));
  p.run(400);
  const auto state = p.rover.engine.snapshot(now_ms);
  assert(state.state == State::RecoveryRequired);
  assert(state.reason == Reason::StorageFailure);
}

void reboot_reports_interrupted() {
  link_operation::Pending durable{};
  durable.kind = uint8_t(Kind::Select); durable.target = uint8_t(Transport::Radio);
  durable.previous = uint8_t(Transport::WiFi); durable.tag = tag_of(0xbb); durable.revision = 4;
  link_operation::seal(durable);
  Node node;
  node.confirmed.transport = uint8_t(Transport::WiFi);
  node.confirmed.revision = 3;
  link_operation::seal(node.confirmed);
  node.engine.begin(false, true, &node.confirmed, &durable);
  const auto state = node.engine.snapshot(now_ms);
  assert(state.state == State::Interrupted && state.reason == Reason::Interrupted);
  assert(state.transport == Transport::Radio && state.previous == Transport::WiFi);
  assert(node.confirmed.transport == uint8_t(Transport::WiFi));   // confirmed selection retained
  assert(node.engine.revision() == 3);
  node.engine.clear_interrupted();
  assert(node.engine.snapshot(now_ms).state == State::Idle);
  assert(!node.engine.pending().tag);
}

void delegate_forwarding() {
  // A request arriving on the Base is forwarded and only the coordinator grants
  // it; the delegate mirrors the operation state for its own settings page.
  Pair p;
  Reason reason;
  assert(p.base.engine.forward(Kind::Select, Transport::Radio, tag_of(0xbb), 0, now_ms, reason));
  p.run(1200);
  assert(p.base.engine.snapshot(now_ms).state == State::Succeeded);
  assert(p.rover.engine.snapshot(now_ms).state == State::Succeeded);
  assert(p.base.selected == Transport::Radio && p.rover.selected == Transport::Radio);
  // A coordinator role never appears on the delegate, and a stray Request is
  // denied rather than performed.
  Message request;
  request.kind = link_operation::Request; request.code = uint8_t(Kind::Select);
  request.transport = uint8_t(Transport::Radio); request.tag = tag_of(0xcc); request.revision = 1;
  assert(!p.base.engine.receive(request, now_ms));
  p.base.pump(now_ms);   // the denial is queued as an action; the link owner applies it
  const std::vector<Message> reply = p.base.out; p.base.out.clear();
  assert(reply.size() == 1 && reply[0].kind == link_operation::Done && reply[0].code == link_operation::kDenied);
}

void local_selection_updates_the_baseline() {
  Pair p;
  Reason reason;
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0xdd), 0, now_ms, reason));
  p.run(1200);
  assert(p.rover.engine.snapshot(now_ms).state == State::Succeeded && p.rover.selected == Transport::Radio);
  // A local recovery selection moves the confirmed medium outside the service;
  // it must also become the engine's baseline for the next operation.
  const uint32_t revision = p.rover.confirmed.revision;
  // The service moves its own selected medium at the same time (that is what a
  // local recovery selection does), then the engine adopts the new baseline.
  p.rover.selected = Transport::WiFi;
  p.rover.engine.adopted(Transport::WiFi, revision);
  const auto adopted = p.rover.engine.snapshot(now_ms);
  assert(p.rover.engine.selected() == Transport::WiFi);
  assert(adopted.previous == Transport::WiFi && adopted.state == State::Idle && adopted.tag == 0);
  assert(p.rover.engine.revision() == revision);
  assert(p.rover.engine.request(Kind::Select, Transport::Radio, tag_of(0xee), revision, now_ms, reason));
  assert(p.rover.engine.snapshot(now_ms).previous == Transport::WiFi);
  p.run(1200);
  assert(p.rover.engine.snapshot(now_ms).state == State::Succeeded);
  assert(p.rover.selected == Transport::Radio && p.rover.confirmed.revision == revision + 1);
}

void codec_boundaries() {
  Message message;
  message.kind = link_operation::Prepare; message.code = uint8_t(Kind::Select);
  message.transport = uint8_t(Transport::Radio); message.tag = 0x12345678; message.revision = 9;
  message.boot = 0x00001234;
  correction::Packet packet;
  link_operation::encode(packet, 2, true, message);
  Message decoded;
  assert(link_operation::decode(packet, decoded));
  assert(decoded.boot == 0x00001234);
  assert(decoded.kind == link_operation::Prepare && decoded.code == uint8_t(Kind::Select));
  assert(decoded.transport == uint8_t(Transport::Radio) && decoded.tag == 0x12345678 && decoded.revision == 9);
  assert(decoded.from == 2 && decoded.to == 1 && decoded.role == 1);
  auto reject = [&](correction::Packet bad) {
    correction::seal(bad);
    Message ignored;
    assert(!link_operation::decode(bad, ignored));
  };
  { correction::Packet bad = packet; bad.bytes[0] = 'X'; reject(bad); }
  { correction::Packet bad = packet; bad.bytes[5] = 1; reject(bad); }
  { correction::Packet bad = packet; bad.bytes[6] = 1; reject(bad); }
  { correction::Packet bad = packet; correction::put32(bad.bytes + 8, 5); reject(bad); }
  { correction::Packet bad = packet; bad.bytes[17] = 12; reject(bad); }        // unknown kind
  { correction::Packet bad = packet; bad.bytes[17] = 0; reject(bad); }
  { correction::Packet bad = packet; bad.bytes[18] = 2; bad.bytes[19] = 2; reject(bad); }   // from == to
  { correction::Packet bad = packet; bad.bytes[20] = 2; reject(bad); }         // role
  { correction::Packet bad = packet; bad.bytes[21] = 3; reject(bad); }         // transport
  { correction::Packet bad = packet; bad.bytes[22] = 1; reject(bad); }         // body reserved
  { correction::Packet bad = packet; bad.bytes[44] = 9; reject(bad); }         // code out of range
  { correction::Packet bad = packet; correction::put32(bad.bytes + 40, 5); reject(bad); }   // session
  { correction::Packet bad = packet; correction::put32(bad.bytes + 28, 1); reject(bad); }   // peer boot
  { correction::Packet bad = packet; correction::put32(bad.bytes + 32, 0); reject(bad); }   // tag
  { correction::Packet bad = packet; correction::put32(bad.bytes + 36, 0); reject(bad); }   // revision
  { correction::Packet bad = packet; bad.bytes[45] = 1; reject(bad); }                     // attempt
  { correction::Packet bad = packet; correction::put32(bad.bytes + 48, 1); reject(bad); }   // revision copy
  { correction::Packet bad = packet; bad.bytes[60] = 1; reject(bad); }        // padding
  { correction::Packet bad = packet; bad.bytes[36] ^= 1; Message ignored; assert(!link_operation::decode(bad, ignored)); }   // CRC (not resealed)
  // A negotiation envelope is never an operation envelope.
  correction::Packet hello;
  std::memset(hello.bytes, 0, sizeof(hello.bytes));
  std::memcpy(hello.bytes, "RTM1", 4); hello.bytes[4] = 1; hello.bytes[5] = 3;
  std::memcpy(hello.bytes + 12, "PLC1", 4); hello.bytes[16] = 1; hello.bytes[17] = 1;
  correction::put32(hello.bytes + 24, 1); correction::put32(hello.bytes + 32, 1);
  correction::seal(hello);
  assert(!link_operation::decode(hello, decoded));
}
}  // namespace

int main() {
  selection_succeeds();
  stale_busy_and_conflicts();
  already_selected_is_immediate();
  unreachable_peer_fails_without_change();
  cancel_paths();
  denied_delegate_keeps_selection();
  restoration_and_recovery();
  persistence_failure_is_recovery_required();
  local_selection_updates_the_baseline();
  reboot_reports_interrupted();
  delegate_forwarding();
  codec_boundaries();
  std::puts("PASS: production link-operation admission, staleness, cancellation, restoration, recovery, reboot and wire boundaries");
}
