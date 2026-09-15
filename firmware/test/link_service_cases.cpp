// Focused cases for the production link service: stored-selection loading and
// migration, physical-ingress control admission, version classification and
// radio-fault scoping. The service, the pair core and the operation core are the
// unmodified production sources; only hardware adapters are doubled.
#include "link_service_hardware.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using pair_session::Transport;

namespace {
const char *kOperationId = "0123456789abcdef0123456789abcdef";
const char *kStorageError = "Link preference unavailable; select matching links locally to recover.";

// R5 stored the whole preference under one key, magic/version/transport/CRC.
// Reproduced byte for byte so the upgrade path is exercised from real stored
// bytes rather than from the loader's own view of them.
struct LegacySelection {
  uint32_t magic;
  uint8_t version, transport, reserved[2];
  uint32_t crc;
};
LegacySelection legacy_selection(uint8_t transport) {
  LegacySelection record{};
  record.magic = 0x314b4e4c;
  record.version = 1;
  record.transport = transport;
  record.crc = correction::crc32(reinterpret_cast<const uint8_t *>(&record), offsetof(LegacySelection, crc));
  return record;
}
void store(const char *key, const void *data, size_t size) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  host_nvs[key] = std::vector<uint8_t>(bytes, bytes + size);
}
enum class Shape { Corrupt, Truncated, Oversized };
const char *shape_name(Shape shape) {
  return shape == Shape::Corrupt ? "corrupt" : shape == Shape::Truncated ? "truncated" : "oversized";
}
std::vector<uint8_t> damaged(const void *data, size_t size, Shape shape) {
  const auto *raw = static_cast<const uint8_t *>(data);
  std::vector<uint8_t> bytes(raw, raw + size);
  if (shape == Shape::Truncated) bytes.resize(size - 1);
  if (shape == Shape::Oversized) bytes.resize(size + 8, 0xab);
  // The trailing field of every record is its CRC.
  if (shape == Shape::Corrupt) bytes.back() ^= 0xff;
  return bytes;
}

// Published status JSON: the surface a browser and the hardware acceptance step
// read, so every assertion below is made on observable state.
StaticJsonDocument<4096> host_status;
JsonObject status(uint32_t now) {
  host_status.clear();
  JsonObject out = host_status.to<JsonObject>();
  link_service::write_json(out, now);
  return out;
}
const char *text(const JsonObject &object, const char *key) { return object[key] | ""; }

// The other instrument, speaking the production pair engine. `peer_medium` is
// the medium it really transmits on; `ingress` is where its frames arrive, which
// a relay can make differ.
struct Harness {
  pair_session::Engine peer;
  uint32_t now = 2000;
  Transport peer_medium = Transport::Radio;
  Transport ingress = Transport::Radio;
  std::vector<correction::Packet> peer_tx;

  void begin_peer(Transport medium, uint32_t boot) {
    peer_medium = medium;
    peer.begin(2, true, medium, boot, now, esp_random);
  }
  void deliver(const correction::Packet &packet, Transport medium) {
    if (medium == Transport::Radio) {
      radio_transport::Frame frame;
      frame.packet = packet;
      frame.kind = radio_transport::FrameKind::Rtcm;
      host_radio_rx.push_back(frame);
    } else {
      host_wifi_rx.push_back({packet, IPAddress(192, 168, 4, 2)});
    }
  }
  void step() {
    peer.tick(now);
    link_service::service(now, false, false);
    // What the service transmitted reaches the peer only on the peer's medium.
    const auto &sent = peer_medium == Transport::Radio ? host_radio_tx : host_wifi_tx;
    for (const auto &packet : sent) peer.receive(packet, now);
    host_radio_tx.clear();
    host_wifi_tx.clear();
    correction::Packet packet;
    for (unsigned guard = 0; guard < 4 && peer.next(packet, now); ++guard) {
      peer_tx.push_back(packet);
      deliver(packet, ingress);
      peer.committed(packet, now);
    }
    now += 100;
  }
  void run(uint32_t duration) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 100) step();
  }
};

// A missing record is the only case that keeps the historical Wi-Fi default: it
// is absence, not corruption, and nothing is migrated on the way in.
void missing_record_keeps_wifi_default() {
  assert(host_nvs.empty());
  link_service::begin(false, 2000);
  const auto d = status(2000);
  assert(d["transport"] == "wifi");
  assert(std::strcmp(text(d, "pair_state"), "recovery_required") != 0);
  assert(std::strcmp(text(d, "error"), "") == 0);
  assert(d["radio_reserved"] == false);
  assert(host_nvs.count("confirmed") == 0);
  printf("missing_record_keeps_wifi_default: transport=%s pair_state=%s\n", text(d, "transport"), text(d, "pair_state"));
}

// A valid R5 Radio preference migrates into one record both owners accept, so
// production and the operation engine share the same baseline medium.
void legacy_radio_migration() {
  const auto record = legacy_selection(1);
  store("selection", &record, sizeof(record));
  link_service::begin(false, 2000);
  JsonObject d = status(2000);
  assert(d["transport"] == "sik");
  assert(std::strcmp(text(d, "pair_state"), "recovery_required") != 0);
  // The operation owner reads the same record: before the first operation its
  // baseline is the medium production is already running on.
  link_service::service_settings(2000, false);
  link_operation::Reason reason = link_operation::Reason::None;
  assert(link_service::request_operation(link_operation::Kind::Select, Transport::WiFi, kOperationId, 0, reason));
  link_service::service(2100, false, false);
  d = status(2100);
  assert(d["operation_active"] == true);
  assert(std::strcmp(text(d, "operation_transport"), "wifi") == 0);
  assert(std::strcmp(text(d, "operation_previous"), text(d, "transport")) == 0);
  // ...because the migration persisted one record both owners accept.
  link_operation::Confirmed migrated{};
  assert(host_nvs.count("confirmed") == 1);
  assert(host_nvs["confirmed"].size() == sizeof(migrated));
  std::memcpy(&migrated, host_nvs["confirmed"].data(), sizeof(migrated));
  assert(link_operation::valid(migrated));
  assert(migrated.transport == 1);
  printf("legacy_radio_migration: migrated_transport=%u migrated_valid=1 transport=%s operation_previous=%s\n",
         unsigned(migrated.transport), text(d, "transport"), text(d, "operation_previous"));
}

// Present but wrong-length or CRC-invalid records are corruption: storage is
// not ok and no medium is claimed, never a silent Wi-Fi selection.
void invalid_record(const char *key, Shape shape) {
  link_operation::Confirmed confirmed;
  confirmed.transport = 1;
  link_operation::seal(confirmed);
  link_operation::Pending pending;
  pending.kind = uint8_t(link_operation::Kind::Select);
  pending.target = 1; pending.previous = 0;
  pending.tag = 0x12345678u; pending.revision = 1;
  link_operation::seal(pending);
  const auto selection = legacy_selection(1);
  const void *source = &selection;
  size_t size = sizeof(selection);
  if (!std::strcmp(key, "confirmed")) { source = &confirmed; size = sizeof(confirmed); }
  if (!std::strcmp(key, "pending")) {
    source = &pending; size = sizeof(pending);
    store("selection", &selection, sizeof(selection));   // a valid Radio preference...
  }
  const auto bytes = damaged(source, size, shape);
  store(key, bytes.data(), bytes.size());
  link_service::begin(false, 2000);
  const auto d = status(2000);
  assert(std::strcmp(text(d, "pair_state"), "recovery_required") == 0);
  assert(d["peer_connected"] == false);
  assert(std::strcmp(text(d, "error"), kStorageError) == 0);
  printf("invalid_record(%s,%s): stored=%u pair_state=%s transport=%s\n", key, shape_name(shape),
         unsigned(bytes.size()), text(d, "pair_state"), text(d, "transport"));
}

// A valid pending record must never mask a corrupt confirmed one.
void valid_pending_masks_nothing() {
  link_operation::Confirmed confirmed;
  confirmed.transport = 1;
  link_operation::seal(confirmed);
  store("confirmed", &confirmed, sizeof(confirmed));
  host_nvs["confirmed"].back() ^= 0xff;
  link_operation::Pending pending;
  pending.kind = uint8_t(link_operation::Kind::Select);
  pending.target = 0; pending.previous = 1;
  pending.tag = 0x12345678u; pending.revision = 7;
  link_operation::seal(pending);
  assert(link_operation::valid(pending));
  store("pending", &pending, sizeof(pending));
  link_service::begin(false, 2000);
  const auto d = status(2000);
  assert(std::strcmp(text(d, "pair_state"), "recovery_required") == 0);
  assert(std::strcmp(text(d, "error"), kStorageError) == 0);
  assert(d["operation_active"] == false);
  printf("valid_pending_masks_nothing: pair_state=%s operation_active=0\n", text(d, "pair_state"));
}

// An unwritable migration is indeterminate: it reports recovery instead of
// exposing a medium neither owner has confirmed.
void migration_write_failure() {
  const auto record = legacy_selection(1);
  store("selection", &record, sizeof(record));
  host_nvs_write_fail = true;
  link_service::begin(false, 2000);
  const auto d = status(2000);
  assert(std::strcmp(text(d, "pair_state"), "recovery_required") == 0);
  assert(host_nvs.count("confirmed") == 0);
  printf("migration_write_failure: pair_state=%s confirmed_records=%u\n", text(d, "pair_state"),
         unsigned(host_nvs.count("confirmed")));
}

// The reservation the link owner is honoring is published, and a reserved
// stream is never written.
void radio_reservation_published() {
  link_service::begin(false, 2000);
  link_service::service(2000, false, true);
  const auto reserved = status(2000);
  assert(reserved["radio_reserved"] == true);
  assert(host_radio_tx.empty());
  link_service::service(2100, false, false);
  const auto released = status(2100);
  assert(released["radio_reserved"] == false);
  printf("radio_reservation_published: reserved=1 released=0 radio_writes=%u\n", unsigned(host_radio_tx.size()));
}

// A Radio-declared exchange relayed over Wi-Fi proves nothing; the identical
// exchange on the medium it declares establishes the pair.
void cross_medium_ingress() {
  const auto record = legacy_selection(1);
  store("selection", &record, sizeof(record));
  link_service::begin(false, 2000);
  Harness h;
  h.begin_peer(Transport::Radio, 202);
  h.ingress = Transport::WiFi;
  h.run(6000);
  const uint32_t relayed = status(h.now)["control_rx"];
  const uint32_t relayed_dropped = status(h.now)["cross_medium_dropped"];
  const bool relayed_connected = link_service::connected(h.now);
  const bool peer_connected = h.peer.snapshot(h.now).connected;
  assert(relayed > 0);
  assert(!relayed_connected);
  assert(!peer_connected);
  assert(relayed_dropped > 0);
  h.ingress = Transport::Radio;
  h.run(200);   // drain anything still queued from the relayed phase
  const uint32_t relayed_drain = status(h.now)["cross_medium_dropped"];
  h.run(12000);
  const uint32_t same_medium_dropped = status(h.now)["cross_medium_dropped"];
  const bool same_medium_connected = link_service::connected(h.now);
  const bool same_medium_peer = h.peer.snapshot(h.now).connected;
  assert(same_medium_connected);
  assert(same_medium_peer);
  // Correct same-medium traffic is never counted as a drop.
  assert(same_medium_dropped == relayed_drain);
  printf("cross_medium_ingress: relayed_control_rx=%u relayed_dropped=%u relayed_connected=0 "
         "same_medium_connected=1 same_medium_dropped=%u session=%u\n",
         unsigned(relayed), unsigned(relayed_dropped), unsigned(same_medium_dropped - relayed_drain),
         link_service::session());
}

// A recognized unsupported outer version is a companion mismatch, not silence,
// and only the selected medium can evidence it.
void incompatible_version() {
  const auto record = legacy_selection(1);
  store("selection", &record, sizeof(record));
  link_service::begin(false, 2000);
  Harness h;
  h.begin_peer(Transport::Radio, 202);
  h.run(10000);
  assert(link_service::connected(h.now));
  correction::Packet version_two;
  bool found = false;
  for (const auto &packet : h.peer_tx) {
    if (packet.bytes[17] == 1) { version_two = packet; found = true; break; }   // a pair Hello
  }
  assert(found);
  version_two.bytes[4] = 2;
  correction::seal(version_two);
  assert(!peer_wire::control(version_two));
  assert(peer_wire::incompatible(version_two));
  // Wi-Fi is only a listener here: its traffic cannot misreport the Radio link.
  h.deliver(version_two, Transport::WiFi);
  h.run(500);
  const bool listener_connected = link_service::connected(h.now);
  assert(listener_connected);
  assert(std::strcmp(text(status(h.now), "pair_state"), "protocol_incompatible") != 0);
  // On the selected medium the same envelope is actionable.
  assert(link_service::select(Transport::WiFi, false, h.now));
  h.peer_medium = Transport::WiFi;
  h.deliver(version_two, Transport::WiFi);
  h.run(300);
  const bool incompatible = std::strcmp(text(status(h.now), "pair_state"), "protocol_incompatible") == 0;
  assert(incompatible);
  assert(!link_service::connected(h.now));
  // A proven session is not torn down by a replayed incompatible frame.
  assert(link_service::select(Transport::Radio, false, h.now));
  assert(link_service::select(Transport::WiFi, false, h.now));
  h.peer_medium = Transport::WiFi;
  h.ingress = Transport::WiFi;
  h.begin_peer(Transport::WiFi, 404);
  h.run(10000);
  assert(link_service::connected(h.now));
  const uint32_t session = link_service::session();
  h.deliver(version_two, Transport::WiFi);
  h.run(500);
  const bool survived = link_service::connected(h.now);
  assert(survived);
  assert(link_service::session() == session);
  printf("incompatible_version: listener_connected=1 selected_reason=protocol_incompatible replay_kept_session=1\n");
}

// A radio short write latches for the radio session only: it survives later
// heartbeats of that session, never inhibits Wi-Fi, and clears on teardown.
void radio_fault_isolation() {
  const auto record = legacy_selection(1);
  store("selection", &record, sizeof(record));
  link_service::begin(false, 2000);
  Harness h;
  h.begin_peer(Transport::Radio, 202);
  host_radio_write_limit = 100;
  h.run(1500);
  JsonObject d = status(h.now);
  assert(d["transport"] == "sik");
  assert(std::strcmp(text(d, "pair_state"), "radio_output_fault") == 0);
  assert(d["peer_connected"] == false);
  assert(uint32_t(d["short_writes"]) > 0);
  assert(d["fault"] == true);   // the published latch, not only the reason text
  const uint32_t short_writes = d["short_writes"];
  h.run(2000);
  assert(std::strcmp(text(status(h.now), "pair_state"), "radio_output_fault") == 0);
  // Explicit recovery to Wi-Fi: the radio latch never inhibits another medium
  // and is cleared by the teardown that switches to it.
  host_radio_write_limit = correction::packet_size;
  assert(link_service::select(Transport::WiFi, false, h.now));
  h.peer_medium = Transport::WiFi;
  h.ingress = Transport::WiFi;
  h.begin_peer(Transport::WiFi, 404);
  h.run(12000);
  const bool wifi_connected = link_service::connected(h.now);
  const bool wifi_peer_connected = status(h.now)["peer_connected"] == true;
  const bool fault_cleared = status(h.now)["fault"] == false;
  const char *recovered_reason = text(status(h.now), "pair_state");
  assert(wifi_connected);
  assert(wifi_peer_connected);
  assert(fault_cleared);
  assert(std::strcmp(recovered_reason, "radio_output_fault") != 0);
  printf("radio_fault_isolation: short_writes=%u radio_pair_state=radio_output_fault "
         "wifi_connected=1 wifi_peer_connected=1 fault_cleared=1\n", unsigned(short_writes));
}
}  // namespace

int main(int argc, char **argv) {
  assert(argc >= 2);
  const std::string name = argv[1];
  if (name == "missing_record_keeps_wifi_default") missing_record_keeps_wifi_default();
  else if (name == "legacy_radio_migration") legacy_radio_migration();
  else if (name == "invalid_record") {
    assert(argc == 4);
    const std::string kind = argv[3];
    const Shape shape = kind == "corrupt" ? Shape::Corrupt : kind == "truncated" ? Shape::Truncated : Shape::Oversized;
    invalid_record(argv[2], shape);
  } else if (name == "valid_pending_masks_nothing") valid_pending_masks_nothing();
  else if (name == "migration_write_failure") migration_write_failure();
  else if (name == "radio_reservation_published") radio_reservation_published();
  else if (name == "cross_medium_ingress") cross_medium_ingress();
  else if (name == "incompatible_version") incompatible_version();
  else if (name == "radio_fault_isolation") radio_fault_isolation();
  else {
    std::fprintf(stderr, "unknown case %s\n", argv[1]);
    return 2;
  }
  std::printf("PASS %s\n", argv[1]);
  return 0;
}
