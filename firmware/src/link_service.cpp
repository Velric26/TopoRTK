#include "link_service.h"
#include "wifi_transport.h"
#include "correction_bridge.h"
#include "radio_transport.h"
#include "network_service.h"
#include "peer_update.h"
#include "debug_service.h"
#include "ota_service.h"
#include "survey_service.h"
#include "link_diagnostic.h"
#include "web_http.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>

namespace link_service {
namespace {
pair_session::Engine pair;
pair_session::Engine staging;
correction::Bridge live;
link_operation::Engine operation;
Transport selected_ = Transport::WiFi;
bool initialized = false, rover_ = false, storage_ok = false;
bool staging_active = false, reserved = false, radio_reserved_ = false;
Transport staging_transport = Transport::WiFi;
uint32_t applied_session = 0, applied_peer = 0;
link_operation::Confirmed confirmed{};
link_operation::Pending pending{};
IPAddress address;
uint32_t tx_wait = 0, output_rejected = 0, short_writes = 0;
uint32_t control_rx = 0, operation_rx = 0, staging_rx = 0, radio_rx = 0, wifi_rx = 0;
const char *fault = "";

// Cross-task handoff: the HTTP task fills one bounded request; the main loop
// drains it and performs the authoritative admission.
struct QueuedRequest {
  bool valid = false, cancel = false;
  link_operation::Kind kind = link_operation::Kind::None;
  Transport transport = Transport::WiFi;
  uint32_t tag = 0, revision = 0;
  char id[33] = {};
};
QueuedRequest queued;
char operation_id[33] = {};
bool operation_id_peer = false;

struct View {
  uint32_t revision = 0, tag = 0;
  uint8_t state = 0, kind = 0, transport = 0;
  bool busy = false, storage_ok = true;
};
View view;
portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
constexpr size_t kSettingsCapacity = 1536;
char settings[kSettingsCapacity] = {};
size_t settings_length = 0;
uint32_t settings_ms = 0;

// Legacy R5 single-key preference, read once for upgrade compatibility.
constexpr uint32_t preference_magic = 0x314b4e4c;
struct Preference { uint32_t magic; uint8_t version, transport, reserved[2]; uint32_t crc; };
bool valid(const Preference &p) {
  return p.magic == preference_magic && p.version == 1 && p.transport <= 1 &&
         !p.reserved[0] && !p.reserved[1] &&
         p.crc == correction::crc32(reinterpret_cast<const uint8_t *>(&p), offsetof(Preference, crc));
}
template <class T> bool write_record(const char *key, const T &record) {
  Preferences p;
  if (!p.begin("topolink", false)) return false;
  T check{};
  const bool saved = p.putBytes(key, &record, sizeof(record)) == sizeof(record) &&
                     p.getBytes(key, &check, sizeof(check)) == sizeof(check) &&
                     !std::memcmp(&record, &check, sizeof(check));
  p.end();
  return saved;
}
bool clear_record(const char *key) {
  Preferences p;
  if (!p.begin("topolink", false)) return false;
  const bool removed = p.remove(key);
  p.end();
  return removed;
}
bool load_records() {
  Preferences p;
  if (!p.begin("topolink", false)) return false;
  bool ok = true;
  size_t size = sizeof(confirmed);
  if (p.getBytes("confirmed", &confirmed, size) == size) {
    ok = link_operation::valid(confirmed);
    if (!ok) confirmed = link_operation::Confirmed{};
  } else {
    confirmed = link_operation::Confirmed{};
    size = sizeof(Preference);
    Preference legacy{};
    if (p.getBytes("selection", &legacy, size) == size && valid(legacy)) confirmed.transport = legacy.transport;
  }
  size = sizeof(pending);
  if (p.getBytes("pending", &pending, size) == size) {
    ok = link_operation::valid(pending);
    if (!ok) pending = link_operation::Pending{};
  } else {
    pending = link_operation::Pending{};
  }
  p.end();
  return ok;
}
bool persist_confirmed() {
  confirmed.magic = 0x314d4f43u; confirmed.version = 1;
  confirmed.reserved[0] = confirmed.reserved[1] = 0;
  link_operation::seal(confirmed);
  return write_record("confirmed", confirmed);
}
bool persist_pending(bool clear) {
  if (clear) return clear_record("pending");
  pending.magic = 0x31444e50u; pending.version = 1;
  pending.reserved[0] = pending.reserved[1] = pending.reserved[2] = 0;
  link_operation::seal(pending);
  return write_record("pending", pending);
}

const char *transport_text(Transport transport) { return transport == Transport::Radio ? "sik" : "wifi"; }

void synchronize(uint32_t now) {
  const auto state = pair.snapshot(now);
  const uint32_t next = storage_ok && state.established ? state.session : 0;
  if (next == applied_session && state.peer_boot == applied_peer) return;
  // Reset only on a proved identity transition, never on a missed heartbeat.
  // This preserves both radio and Wi-Fi replay high-water marks over outages.
  live.stop();
  correction_output_reset();
  applied_session = next;
  applied_peer = state.peer_boot;
  if (next && selected_ == Transport::Radio) live.begin(next, rover_);
}
void restart(bool rover, uint32_t now) {
  rover_ = rover;
  applied_session = applied_peer = 0;
  live.stop();
  address = IPAddress();
  fault = "";
  correction_output_reset();
  pair.begin(TOPORTK_UNIT_ID, rover, selected_, web_boot_id(), now, esp_random);
}
bool upstream(IPAddress from) {
  if (rover_ || network_service::local_router()) return network_service::on_station_subnet(from);
  // Direct Base AP: never accept a bootstrap from another interface/subnet.
  const auto local = network_service::address();
  return uint32_t(local) && from[0] == local[0] && from[1] == local[1] && from[2] == local[2];
}
IPAddress destination() {
  // Broadcast negotiation also discovers a changed DHCP address after an outage.
  if (network_service::station_connected()) return network_service::station_broadcast();
  if (!rover_ && !network_service::local_router()) {
    auto ip = network_service::address();
    if (uint32_t(ip)) ip[3] = 255;
    return ip;
  }
  return IPAddress();
}
void control(const correction::Packet &packet, uint32_t now, IPAddress from = IPAddress()) {
  if (!peer_wire::control(packet)) return;
  if (!std::memcmp(packet.bytes + 12, "PLC1", 4)) {
    const auto before = pair.snapshot(now);
    const bool accepted = pair.receive(packet, now);
    synchronize(now);
    const auto state = pair.snapshot(now);
    // Only a fresh heartbeat that actually advanced peer freshness may move the
    // learned address, so a replayed envelope cannot redirect correction output.
    if (accepted && uint32_t(from) && state.connected &&
        packet.bytes[17] == 6 && state.peer_age_ms < before.peer_age_ms &&
        state.peer_boot == correction::u32(packet.bytes + 24)) address = from;
  } else if (selected_ == Transport::WiFi && !std::memcmp(packet.bytes + 12, "TPH1", 4)) {
    // A complete recognized legacy envelope is evidence, silence is not.
    pair.incompatible(now);
  } else if (snapshot(now).connected && correction::u32(packet.bytes + 8) == session() &&
             (!uint32_t(from) || from == address)) {
    peer_update_receive(packet, now);
  }
}
// Routes one control envelope: pair operations first, then the candidate
// staging engine, then the selected production link.
void route_control(const correction::Packet &packet, uint32_t now, IPAddress from = IPAddress()) {
  ++control_rx;
  link_operation::Message message;
  if (link_operation::decode(packet, message)) {
    ++operation_rx;
    if (message.from == 3 - TOPORTK_UNIT_ID && message.to == TOPORTK_UNIT_ID) operation.receive(message, now);
    return;
  }
  if (staging_active && staging.receive(packet, now)) { ++staging_rx; return; }
  control(packet, now, from);
}
bool transmit(const correction::Packet &packet, Transport transport, IPAddress from = IPAddress()) {
  if (transport == Transport::Radio) {
    if (radio_reserved_) return false;
    const int written = radio_transport::send(packet.bytes, sizeof(packet));
    if (written >= 0) debug_frame(debugmode::Channel::RadioTx, packet.bytes, size_t(written));
    if (written < 0) { ++tx_wait; return false; }
    if (written != int(sizeof(packet))) {
      ++short_writes; live.fail(); correction_output_reset(); fault = "Radio output fault; reselect the link locally.";
      return false;
    }
    return true;
  }
  IPAddress to = from;
  if (!uint32_t(to)) to = address;
  if (!uint32_t(to)) to = destination();
  if (!uint32_t(to)) return false;
  return wifi_transport::send(wifi_transport::Channel::Peer, to, packet.bytes, sizeof(packet));
}

bool operation_expected_connected(uint32_t now) {
  const auto op = operation.snapshot(now);
  if (!op.active) return false;
  const Transport expected = op.state == link_operation::State::Restoring ? op.previous : op.transport;
  return selected_ == expected && connected(now);
}
void commit_local(uint32_t now) {
  // The proven candidate becomes the production session: no second handshake,
  // and both sides cleared replay/assembly/queue state when they staged.
  correction_output_reset();
  selected_ = staging_transport;
  pair = staging;
  staging = pair_session::Engine{};
  staging_active = false;
  radio_transport::discard_input();
  synchronize(now);
}
void restore_local(uint32_t now) {
  const auto op = operation.snapshot(now);
  correction_output_reset();
  selected_ = op.previous;
  confirmed.transport = uint8_t(op.previous);
  const bool stored = persist_confirmed();
  staging_active = false;
  staging = pair_session::Engine{};
  radio_transport::discard_input();
  restart(rover_, now);
  operation.restore_result(stored);
}
void start_staging(uint32_t now) {
  const auto op = operation.snapshot(now);
  if (op.transport == selected_) {
    // Nothing to negotiate: the requested medium already carries production.
    staging_active = false;
    return;
  }
  staging_transport = op.transport;
  staging.begin(TOPORTK_UNIT_ID, rover_, staging_transport, web_boot_id(), now, esp_random);
  staging_active = true;
}
void apply_actions(uint32_t now) {
  const auto actions = operation.take();
  bool stored = true;
  if (actions.persist_pending) {
    pending = operation.pending();
    stored = pending.tag ? persist_pending(false) : persist_pending(true);
    if (!stored) operation.persisted(false);
  }
  if (actions.persist_confirmed) {
    confirmed.transport = uint8_t(operation.selected());
    if (operation.revision()) confirmed.revision = operation.revision();
    stored = persist_confirmed() && stored;
    if (!stored) operation.persisted(false);
  }
  if (actions.stage && stored) {
    if (!reserved && survey_diagnostic_acquire()) reserved = true;
    start_staging(now);
  }
  if (actions.commit && stored) commit_local(now);
  if (actions.restore) restore_local(now);
  if (actions.unstage) {
    staging_active = false;
    staging = pair_session::Engine{};
    if (reserved) { survey_diagnostic_release(); reserved = false; }
  }
  if (actions.clear_pending) {
    pending = link_operation::Pending{};
    persist_pending(true);
  }
  if (actions.send) {
    // Operation control rides the operation's own medium, never a relay.
    correction::Packet packet;
    link_operation::Message message = actions.message;
    message.boot = web_boot_id();
    link_operation::encode(packet, TOPORTK_UNIT_ID, rover_, message);
    transmit(packet, static_cast<Transport>(message.transport));
  }
}
void drain_queue(uint32_t now) {
  QueuedRequest request;
  portENTER_CRITICAL(&guard);
  request = queued;
  queued = QueuedRequest{};
  portEXIT_CRITICAL(&guard);
  if (!request.valid) return;
  std::memcpy(operation_id, request.id, sizeof(operation_id));
  operation_id_peer = false;
  link_operation::Reason reason = link_operation::Reason::None;
  bool accepted = false;
  if (request.cancel) {
    accepted = operation.cancel(request.tag, now);
    if (!accepted) reason = link_operation::Reason::Conflict;
  } else if (rover_) {
    accepted = operation.request(request.kind, request.transport, request.tag, request.revision, now, reason);
  } else {
    accepted = operation.forward(request.kind, request.transport, request.tag, request.revision, now, reason);
  }
  if (!accepted) operation.refuse(request.tag, request.kind, request.transport, reason, now);
}
}  // namespace

void begin(bool rover, uint32_t now) {
  if (initialized) return;
  initialized = true;
  storage_ok = load_records();
  selected_ = static_cast<Transport>(confirmed.transport <= 1 ? confirmed.transport : 0);
  operation.begin(rover, storage_ok, &confirmed, pending.tag ? &pending : nullptr);
  restart(rover, now);
  // A passive UART listener does not transmit on an unselected medium.
  radio_transport::begin();
}

pair_session::Snapshot snapshot(uint32_t now) {
  auto state = pair.snapshot(now);
  const auto op = operation.snapshot(now);
  if (!storage_ok || op.state == link_operation::State::RecoveryRequired) {
    state.connected = state.established = false;
    state.reason = "recovery_required";
  } else if (live.fault()) {
    state.connected = false;
    state.reason = "radio_output_fault";
  }
  return state;
}
bool connected(uint32_t now) { return snapshot(now).connected; }
uint32_t session() { return applied_session; }
uint32_t peer_boot() { return applied_peer; }
bool radio_active() { return selected_ == Transport::Radio; }
bool radio_submit(const uint8_t *frame, size_t size, uint32_t now) {
  return radio_active() && connected(now) && !ota_paused() && live.enqueue(frame, size, now);
}
void clear_pending() { live.clear_pending(); }
IPAddress wifi_peer() { return address; }
void note_incompatible(uint32_t now) { if (selected_ == Transport::WiFi) pair.incompatible(now); }

bool select(Transport transport, bool rover, uint32_t now) {
  if (!initialized) begin(rover, now);
  if (transport != Transport::WiFi && transport != Transport::Radio) return false;
  // A pair-wide operation owns disruptive work; local recovery waits for it.
  if (operation.busy()) return false;
  if (storage_ok && selected_ == transport && rover_ == rover && !live.fault()) {
    operation.clear_interrupted();
    return true;
  }
  confirmed.transport = uint8_t(transport);
  if (!persist_confirmed()) {
    // Persistence is indeterminate; do not pretend the old choice will reboot.
    storage_ok = false; live.stop(); correction_output_reset(); return false;
  }
  clear_record("pending");
  operation.clear_interrupted();
  operation.adopted(transport, confirmed.revision);
  selected_ = transport; storage_ok = true;
  pending = link_operation::Pending{};
  tx_wait = output_rejected = short_writes = 0;
  radio_transport::discard_input();
  restart(rover, now);
  return true;
}

void service(uint32_t now, bool rover, bool radio_reserved) {
  if (!initialized) begin(rover, now);
  radio_reserved_ = radio_reserved;
  if (rover != rover_) restart(rover, now);
  drain_queue(now);
  pair.tick(now);
  if (staging_active) staging.tick(now);
  synchronize(now);
  peer_update_service(now, snapshot(now), peer_update_quality_ready());

  // Peer socket: independent of the selected medium so a candidate on Wi-Fi can
  // still be negotiated while production runs on radio.
  wifi_transport::start(wifi_transport::Channel::Peer);
  for (unsigned budget = 0; budget < 4; ++budget) {
    correction::Packet packet; IPAddress from;
    const int size = wifi_transport::receive(wifi_transport::Channel::Peer, packet.bytes, sizeof(packet), from);
    if (!size) break;
    if (storage_ok && size == int(sizeof(packet)) && upstream(from)) route_control(packet, now, from);
  }
  live.tick(now);
  if (!radio_reserved) {
    radio_transport::Frame frame; size_t budget = 2048;
    while (radio_transport::receive(frame, budget)) {
      if (!storage_ok || frame.kind != radio_transport::FrameKind::Rtcm) continue;
      const auto &packet = frame.packet;
      if (peer_wire::control(packet)) {
        ++radio_rx;
        if (!std::memcmp(packet.bytes + 12, "TPH1", 4)) pair.incompatible(now);
        else route_control(packet, now);
      } else if (selected_ == Transport::Radio && connected(now) && !ota_paused() && live.packet(packet, now)) {
        debug_frame(debugmode::Channel::RadioRx, live.data(), live.size());
        if (!correction_link_input(live.data(), live.size(), now - live.known_age(now))) ++output_rejected;
      }
    }
  }
  // Receives may have established/replaced a peer since the first service.
  synchronize(now);
  peer_update_service(now, snapshot(now), peer_update_quality_ready());

  link_operation::Inputs inputs;
  inputs.now = now;
  inputs.rover = rover;
  inputs.busy = diagnostic_busy() || ota_locked() || ota_paused() || reserved;
  inputs.storage_ok = storage_ok;
  inputs.staging_ready = staging_active && staging.snapshot(now).connected;
  inputs.production_on_target = operation_expected_connected(now);
  {
    const auto op = operation.snapshot(now);
    inputs.already_selected = op.active && op.transport == selected_ && connected(now);
  }
  operation.tick(inputs);
  apply_actions(now);

  if (!storage_ok) return;
  // One packet per turn: operation control, candidate proof, production
  // bootstrap, peer notice, then production data on the selected medium.
  correction::Packet packet;
  if (staging_active && staging.next(packet, now)) {
    if (transmit(packet, staging_transport)) staging.committed(packet, now);
    return;
  }
  const bool bootstrap = pair.next(packet, now);
  if (bootstrap) {
    if (transmit(packet, selected_)) { pair.committed(packet, now); synchronize(now); }
    return;
  }
  if (!connected(now)) return;
  if (peer_update_next(packet, now)) {
    if (transmit(packet, selected_)) peer_update_committed(packet, now);
    return;
  }
  if (selected_ == Transport::Radio && !ota_paused() && live.next(packet, now)) {
    if (transmit(packet, Transport::Radio)) live.committed();
  }
}

bool request_operation(link_operation::Kind kind, Transport transport, const char *id,
                       uint32_t revision, link_operation::Reason &reason) {
  const uint32_t tag = link_operation::tag_from_hex(id);
  if (!tag) { reason = link_operation::Reason::Conflict; return false; }
  if (kind == link_operation::Kind::Test) {
    // Automated quick tests are the R9 checkpoint; never accept silently.
    reason = link_operation::Reason::Unsupported;
    return false;
  }
  View current;
  portENTER_CRITICAL(&guard);
  current = view;
  const bool pending_request = queued.valid;
  portEXIT_CRITICAL(&guard);
  if (!current.storage_ok) { reason = link_operation::Reason::StorageFailure; return false; }
  if (current.busy || pending_request) { reason = link_operation::Reason::Busy; return false; }
  if (current.tag == tag && current.state != uint8_t(link_operation::State::Idle)) {
    if (current.kind == uint8_t(kind) && current.transport == uint8_t(transport)) return true;  // idempotent repeat
    reason = link_operation::Reason::ConflictingId;
    return false;
  }
  if (revision != current.revision) { reason = link_operation::Reason::StaleRevision; return false; }
  portENTER_CRITICAL(&guard);
  queued = QueuedRequest{};
  queued.valid = true; queued.kind = kind; queued.transport = transport; queued.tag = tag; queued.revision = revision;
  std::memcpy(queued.id, id, 32);
  portEXIT_CRITICAL(&guard);
  return true;
}

bool cancel_operation(const char *id, link_operation::Reason &reason) {
  const uint32_t tag = link_operation::tag_from_hex(id);
  if (!tag) { reason = link_operation::Reason::Conflict; return false; }
  View current;
  bool pending_request;
  portENTER_CRITICAL(&guard);
  current = view;
  pending_request = queued.valid;
  portEXIT_CRITICAL(&guard);
  if (pending_request) { reason = link_operation::Reason::Busy; return false; }
  if (!current.busy || current.tag != tag) { reason = link_operation::Reason::Conflict; return false; }
  portENTER_CRITICAL(&guard);
  queued = QueuedRequest{};
  queued.valid = true; queued.cancel = true; queued.tag = tag; queued.revision = current.revision;
  std::memcpy(queued.id, id, 32);
  portEXIT_CRITICAL(&guard);
  return true;
}

void service_settings(uint32_t now, bool corrections_fresh) {
  if (now - settings_ms < 200) return;
  settings_ms = now;
  const auto link = snapshot(now);
  const auto op = operation.snapshot(now);
  StaticJsonDocument<1536> document;
  document["version"] = 1;
  char boot[16];
  std::snprintf(boot, sizeof(boot), "%08lx", static_cast<unsigned long>(web_boot_id()));
  document["boot_id"] = boot;
  document["uptime_ms"] = now;
  document["revision"] = operation.revision();
  document["selected_transport"] = transport_text(selected_);
  if (op.active) document["candidate_transport"] = transport_text(op.transport);
  else document["candidate_transport"] = nullptr;
  document["peer_connected"] = link.connected;
  document["corrections_fresh"] = corrections_fresh;
  if (op.state == link_operation::State::Idle) document["operation"] = nullptr;
  else {
    auto entry = document.createNestedObject("operation");
    entry["id"] = operation_id[0] ? operation_id : "";
    entry["tag"] = op.tag;
    entry["kind"] = link_operation::kind_text(op.kind);
    entry["transport"] = transport_text(op.transport);
    entry["previous_transport"] = transport_text(op.previous);
    entry["state"] = link_operation::state_text(op.state);
    entry["committed"] = op.committed;
    entry["coordinator"] = op.coordinator;
    entry["phase_remaining_ms"] = op.active ? op.phase_remaining_ms : static_cast<uint32_t>(0);
    entry["reason"] = link_operation::reason_text(op.reason);
  }
  diagnostic_tests_json(document.createNestedObject("last_tests"));
  if (document.overflowed()) return;
  char buffer[kSettingsCapacity];
  const size_t length = serializeJson(document, buffer, sizeof(buffer));
  if (!length || length >= sizeof(buffer)) return;
  portENTER_CRITICAL(&guard);
  std::memcpy(settings, buffer, length + 1);
  settings_length = length;
  view.revision = operation.revision();
  view.tag = op.tag;
  view.state = uint8_t(op.state);
  view.kind = uint8_t(op.kind);
  view.transport = uint8_t(op.transport);
  view.busy = operation.busy();
  view.storage_ok = storage_ok && op.state != link_operation::State::RecoveryRequired;
  portEXIT_CRITICAL(&guard);
}

bool settings_snapshot(char *out, size_t capacity) {
  portENTER_CRITICAL(&guard);
  const size_t length = settings_length;
  const bool ok = length && length < capacity;
  if (ok) std::memcpy(out, settings, length + 1);
  portEXIT_CRITICAL(&guard);
  return ok;
}

void write_json(JsonObject d, uint32_t now) {
  const auto state = snapshot(now);
  const auto op = operation.snapshot(now);
  d["transport"] = radio_active() ? "sik" : "wifi";
  d["session"] = session(); d["peer_connected"] = state.connected;
  d["pair_state"] = state.reason; d["boot"] = state.local_boot; d["peer_boot"] = state.peer_boot;
  // Candidate staging and the operation record: the pieces a failed cutover is
  // diagnosed from, without a serial console.
  const auto candidate = staging.snapshot(now);
  d["staging_active"] = staging_active;
  d["staging_transport"] = staging_transport == Transport::Radio ? "sik" : "wifi";
  d["staging_session"] = candidate.session;
  d["staging_connected"] = candidate.connected;
  d["staging_state"] = candidate.reason;
  d["staging_peer_boot"] = candidate.peer_boot;
  d["operation_active"] = op.active;
  d["operation_tag"] = op.tag;
  d["operation_transport"] = op.transport == Transport::Radio ? "sik" : "wifi";
  d["operation_previous"] = op.previous == Transport::Radio ? "sik" : "wifi";
  d["operation_coordinator"] = op.coordinator;
  d["peer_address"] = address.toString();
  // Radio/Wi-Fi control reception counters: the evidence a failed candidate is
  // diagnosed from when the two media behave differently.
  d["control_rx"] = control_rx;
  d["operation_rx"] = operation_rx;
  d["staging_rx"] = staging_rx;
  d["radio_control_rx"] = radio_rx;
  d["radio_decode_errors"] = radio_transport::decode_stats().rtcm_errors + radio_transport::decode_stats().pair_errors;
  d["radio_discarded_bytes"] = radio_transport::decode_stats().discarded_bytes;
  d["fault"] = live.fault(); d["station"] = live.station();
  d["operation_state"] = link_operation::state_text(op.state);
  d["operation_reason"] = link_operation::reason_text(op.reason);
  d["revision"] = operation.revision();
  d["error"] = storage_ok ? fault : "Link preference unavailable; select matching links locally to recover.";
  d["submitted"] = live.submitted; d["envelopes"] = live.envelopes; d["received"] = live.complete;
  d["station_rejected"] = live.station_rejected; d["queue_pending"] = live.queue().size();
  d["queue_expired"] = live.queue().expired; d["queue_overflow"] = live.queue().overflow;
  d["queue_replaced"] = live.queue().replaced; d["sender_expired"] = live.sender_expired();
  d["wire_errors"] = live.stats().wire_errors; d["assembly_expired"] = live.stats().expired;
  d["wrong_session"] = live.stats().wrong_session; d["replays"] = live.stats().replays;
  d["rtcm_errors"] = live.stats().rtcm_errors; d["tx_wait"] = tx_wait;
  d["output_rejected"] = output_rejected; d["short_writes"] = short_writes;
  d["workspace_bytes"] = sizeof(live) + sizeof(pair) + sizeof(staging) + sizeof(operation);
  const auto output = correction_output_stats(); auto out = d.createNestedObject("output");
  out["forwarded"] = output.forwarded; out["expired"] = output.expired; out["overflow"] = output.overflow;
  out["wait_polls"] = output.waiting; out["faults"] = output.faults; out["pending"] = output.queued;
}
}  // namespace link_service
