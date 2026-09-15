#include "link_service.h"
#include "wifi_transport.h"
#include "correction_bridge.h"
#include "radio_transport.h"
#include "network_service.h"
#include "peer_update.h"
#include "debug_service.h"
#include "ota_service.h"
#include "web_http.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

namespace link_service {
namespace {
pair_session::Engine pair;
correction::Bridge live;
Transport selected_ = Transport::WiFi;
bool initialized = false, rover_ = false, storage_ok = false;
uint32_t applied_session = 0, applied_peer = 0;
IPAddress address;
uint32_t tx_wait = 0, output_rejected = 0, short_writes = 0;
const char *fault = "";

// Local preference only. R6 adds a pair-wide commit/operation record; R5 never
// claims a local selection is a confirmed pair-wide change. No packet writes NVS.
constexpr uint32_t preference_magic = 0x314b4e4c;
struct Preference { uint32_t magic; uint8_t version, transport, reserved[2]; uint32_t crc; };
bool valid(const Preference &p) {
  return p.magic == preference_magic && p.version == 1 && p.transport <= 1 &&
         !p.reserved[0] && !p.reserved[1] &&
         p.crc == correction::crc32(reinterpret_cast<const uint8_t *>(&p), offsetof(Preference, crc));
}
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
}

void begin(bool rover, uint32_t now) {
  if (initialized) return;
  initialized = true;
  Preferences p;
  storage_ok = p.begin("topolink", false);
  if (storage_ok && p.isKey("selection")) {
    Preference saved{};
    storage_ok = p.getBytes("selection", &saved, sizeof(saved)) == sizeof(saved) && valid(saved);
    if (storage_ok) selected_ = static_cast<Transport>(saved.transport);
  }
  p.end();
  restart(rover, now);
  // A passive UART listener does not transmit on an unselected medium.
  radio_transport::begin();
}
bool select(Transport transport, bool rover, uint32_t now) {
  if (!initialized) begin(rover, now);
  if (transport != Transport::WiFi && transport != Transport::Radio) return false;
  if (storage_ok && selected_ == transport && rover_ == rover && !live.fault()) return true;
  Preference next{};
  next.magic = preference_magic; next.version = 1; next.transport = static_cast<uint8_t>(transport);
  next.crc = correction::crc32(reinterpret_cast<const uint8_t *>(&next), offsetof(Preference, crc));
  Preferences p; Preference check{};
  if (!p.begin("topolink", false)) return false;
  const bool saved = p.putBytes("selection", &next, sizeof(next)) == sizeof(next) &&
      p.getBytes("selection", &check, sizeof(check)) == sizeof(check) &&
      !std::memcmp(&next, &check, sizeof(next));
  p.end();
  if (!saved) {
    // Persistence is indeterminate; do not pretend the old choice will reboot.
    storage_ok = false; live.stop(); correction_output_reset(); return false;
  }
  selected_ = transport; storage_ok = true;
  tx_wait = output_rejected = short_writes = 0;
  radio_transport::discard_input();
  restart(rover, now);
  return true;
}
Transport selected() { return selected_; }
pair_session::Snapshot snapshot(uint32_t now) {
  auto state = pair.snapshot(now);
  if (!storage_ok) { state.connected = state.established = false; state.reason = "recovery_required"; }
  else if (live.fault()) { state.connected = false; state.reason = "radio_output_fault"; }
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

void service(uint32_t now, bool rover, bool radio_reserved) {
  if (!initialized) begin(rover, now);
  if (rover != rover_) restart(rover, now);
  pair.tick(now);
  synchronize(now);
  peer_update_service(now, snapshot(now), peer_update_quality_ready());
  // The peer socket is independent of old Wi-Fi HELLO/data discovery. Only
  // selected-medium PLC1 can establish a peer. Unselected controls/data drain.
  wifi_transport::start(wifi_transport::Channel::Peer);
  for (unsigned budget = 0; budget < 4; ++budget) {
    correction::Packet packet; IPAddress from;
    const int size = wifi_transport::receive(wifi_transport::Channel::Peer, packet.bytes, sizeof(packet), from);
    if (!size) break;
    if (storage_ok && selected_ == Transport::WiFi && size == int(sizeof(packet)) && upstream(from))
      control(packet, now, from);
  }
  live.tick(now);
  if (!radio_reserved) {
    radio_transport::Frame frame; size_t budget = 2048;
    while (radio_transport::receive(frame, budget)) {
      if (!storage_ok || selected_ != Transport::Radio || frame.kind != radio_transport::FrameKind::Rtcm) continue;
      const auto &packet = frame.packet;
      if (peer_wire::control(packet)) {
        if (!std::memcmp(packet.bytes + 12, "TPH1", 4)) pair.incompatible(now);
        else control(packet, now);
      } else if (connected(now) && !ota_paused() && live.packet(packet, now)) {
        debug_frame(debugmode::Channel::RadioRx, live.data(), live.size());
        if (!correction_link_input(live.data(), live.size(), now - live.known_age(now))) ++output_rejected;
      }
    }
  }
  // Receives may have established/replaced the peer since the first service.
  synchronize(now);
  peer_update_service(now, snapshot(now), peer_update_quality_ready());
  if (!storage_ok || (radio_active() && radio_reserved)) return;
  correction::Packet packet;
  const bool bootstrap = pair.next(packet, now);
  const bool notice = !bootstrap && connected(now) && peer_update_next(packet, now);
  const bool data = !bootstrap && !notice && radio_active() && connected(now) &&
                    !ota_paused() && live.next(packet, now);
  if (!bootstrap && !notice && !data) return;
  bool sent = false;
  if (radio_active()) {
    const int written = radio_transport::send(packet.bytes, sizeof(packet));
    if (written >= 0) debug_frame(debugmode::Channel::RadioTx, packet.bytes, size_t(written));
    sent = written == int(sizeof(packet));
    if (written < 0) ++tx_wait;
    else if (!sent) { ++short_writes; live.fail(); correction_output_reset(); fault = "Radio output fault; reselect the link locally."; }
  } else {
    const auto to = bootstrap ? destination() : address;
    if (uint32_t(to)) sent = wifi_transport::send(wifi_transport::Channel::Peer, to, packet.bytes, sizeof(packet));
  }
  if (sent) {
    if (bootstrap) pair.committed(packet, now);
    else if (notice) peer_update_committed(packet, now);
    else live.committed();
    synchronize(now);
  }
}

void write_json(JsonObject d, uint32_t now) {
  const auto state = snapshot(now);
  d["transport"] = radio_active() ? "sik" : "wifi";
  d["session"] = session(); d["peer_connected"] = state.connected;
  d["pair_state"] = state.reason; d["boot"] = state.local_boot; d["peer_boot"] = state.peer_boot;
  d["fault"] = live.fault(); d["station"] = live.station(); d["error"] = storage_ok ? fault : "Link preference unavailable; select matching links locally to recover.";
  d["submitted"] = live.submitted; d["envelopes"] = live.envelopes; d["received"] = live.complete;
  d["station_rejected"] = live.station_rejected; d["queue_pending"] = live.queue().size();
  d["queue_expired"] = live.queue().expired; d["queue_overflow"] = live.queue().overflow;
  d["queue_replaced"] = live.queue().replaced; d["sender_expired"] = live.sender_expired();
  d["wire_errors"] = live.stats().wire_errors; d["assembly_expired"] = live.stats().expired;
  d["wrong_session"] = live.stats().wrong_session; d["replays"] = live.stats().replays;
  d["rtcm_errors"] = live.stats().rtcm_errors; d["tx_wait"] = tx_wait;
  d["output_rejected"] = output_rejected; d["short_writes"] = short_writes;
  d["workspace_bytes"] = sizeof(live) + sizeof(pair);
  const auto output = correction_output_stats(); auto out = d.createNestedObject("output");
  out["forwarded"] = output.forwarded; out["expired"] = output.expired; out["overflow"] = output.overflow;
  out["wait_polls"] = output.waiting; out["faults"] = output.faults; out["pending"] = output.queued;
}
}  // namespace link_service
