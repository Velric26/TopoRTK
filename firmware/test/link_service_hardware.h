#pragma once
// Hardware doubles for the production link service. Everything here is inline
// so the service, the portable cores and the cases share one store and one set
// of adapters: NVS keeps one record per key across Preferences instances, and
// the network/UART/DNS-faced modules exist only as far as the service uses them.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#ifndef TOPORTK_UNIT_ID
#define TOPORTK_UNIT_ID 1
#endif

class IPAddress {
 public:
  uint8_t bytes[4];
  IPAddress(uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0) : bytes{a, b, c, d} {}
  IPAddress(const IPAddress &) = default;
  IPAddress &operator=(const IPAddress &) = default;
  explicit operator uint32_t() const {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
  }
  uint8_t operator[](size_t i) const { return bytes[i]; }
  uint8_t &operator[](size_t i) { return bytes[i]; }
  bool operator==(const IPAddress &other) const { return !std::memcmp(bytes, other.bytes, 4); }
  std::string toString() const {
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    return text;
  }
};

// NVS double: a persistent store per key, exactly as the service uses it. A
// second Preferences instance on the namespace sees what the first wrote, an
// absent key has length 0, and a wrong requested length reads nothing.
inline std::map<std::string, std::vector<uint8_t>> host_nvs;
inline bool host_nvs_write_fail = false;

class Preferences {
 public:
  bool begin(const char *, bool = false) { return true; }
  void end() {}
  size_t getBytesLength(const char *key) {
    const auto it = host_nvs.find(key);
    return it == host_nvs.end() ? 0 : it->second.size();
  }
  size_t getBytes(const char *key, void *out, size_t size) {
    const auto it = host_nvs.find(key);
    if (it == host_nvs.end() || it->second.size() != size) return 0;
    std::memcpy(out, it->second.data(), size);
    return size;
  }
  size_t putBytes(const char *key, const void *data, size_t size) {
    if (host_nvs_write_fail) return 0;
    const auto *bytes = static_cast<const uint8_t *>(data);
    host_nvs[key] = std::vector<uint8_t>(bytes, bytes + size);
    return size;
  }
  bool remove(const char *key) {
    host_nvs.erase(key);
    return true;
  }
};

inline uint32_t host_ms = 1000;
inline uint32_t host_boot_id = 0x51ab77c3u;
inline uint32_t &host_esp_random() {
  static uint32_t state = 0x9e3779b9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}
inline uint32_t millis() { return host_ms; }
inline uint32_t esp_random() {
  const uint32_t value = host_esp_random();
  return value ? value : 0x1234567u;
}
inline uint32_t web_boot_id() { return host_boot_id; }

#include "link_service.h"
#include "link_operation.h"
#include "radio_transport.h"
#include "wifi_transport.h"
#include "network_service.h"
#include "peer_update.h"
#include "debug_service.h"
#include "ota_service.h"
#include "survey_service.h"
#include "link_diagnostic.h"

// --- Wi-Fi peer socket ------------------------------------------------------
struct HostDatagram {
  correction::Packet packet;
  IPAddress from;
};
inline std::deque<HostDatagram> host_wifi_rx;
inline std::vector<correction::Packet> host_wifi_tx;

namespace wifi_transport {
inline bool start(Channel) { return true; }
inline int receive(Channel, uint8_t *out, size_t capacity, IPAddress &sender) {
  if (host_wifi_rx.empty()) return 0;
  const auto entry = host_wifi_rx.front();
  host_wifi_rx.pop_front();
  if (sizeof(entry.packet) > capacity) return -1;
  std::memcpy(out, entry.packet.bytes, sizeof(entry.packet));
  sender = entry.from;
  return int(sizeof(entry.packet));
}
inline bool send(Channel, IPAddress, const uint8_t *data, size_t size) {
  correction::Packet packet{};
  std::memcpy(packet.bytes, data, size < sizeof(packet) ? size : sizeof(packet));
  host_wifi_tx.push_back(packet);
  return true;
}
}  // namespace wifi_transport

// --- SiK UART2 --------------------------------------------------------------
inline std::deque<radio_transport::Frame> host_radio_rx;
inline std::vector<correction::Packet> host_radio_tx;
inline size_t host_radio_write_limit = correction::packet_size;
inline bool host_radio_backpressure = false;

namespace radio_transport {
inline void begin() {}
inline void discard_input() {}
inline DecodeStats decode_stats() { return DecodeStats{}; }
inline int send(const uint8_t *data, size_t size) {
  if (host_radio_backpressure) return -1;
  const size_t written = size < host_radio_write_limit ? size : host_radio_write_limit;
  correction::Packet packet{};
  std::memcpy(packet.bytes, data, written);
  host_radio_tx.push_back(packet);
  return int(written);
}
inline bool receive(Frame &out, size_t &byte_budget) {
  if (host_radio_rx.empty() || byte_budget < correction::packet_size) return false;
  out = host_radio_rx.front();
  host_radio_rx.pop_front();
  byte_budget -= correction::packet_size;
  return true;
}
}  // namespace radio_transport

// --- Wi-Fi topology ---------------------------------------------------------
inline IPAddress host_local_address(192, 168, 4, 1);
inline bool host_station_connected = false;

namespace network_service {
inline bool station_connected() { return host_station_connected; }
inline bool local_router() { return false; }
inline IPAddress address() { return host_local_address; }
inline IPAddress station_broadcast() { return IPAddress(192, 168, 4, 255); }
inline bool on_station_subnet(IPAddress ip) { return ip[0] == 192 && ip[1] == 168 && ip[2] == 4; }
}  // namespace network_service

// --- Composition-root hooks and neighbouring services -----------------------
inline void peer_update_service(uint32_t, const pair_session::Snapshot &, bool) {}
inline void peer_update_receive(const correction::Packet &, uint32_t) {}
inline bool peer_update_next(correction::Packet &, uint32_t) { return false; }
inline void peer_update_committed(const correction::Packet &, uint32_t) {}
inline bool peer_update_quality_ready() { return false; }
inline void debug_frame(debugmode::Channel, const uint8_t *, size_t) {}
inline bool ota_locked() { return false; }
inline bool ota_paused() { return false; }
inline bool survey_diagnostic_acquire() { return true; }
inline void survey_diagnostic_release() {}
inline bool diagnostic_busy() { return false; }
inline bool diagnostic_quick_test_start(uint8_t, uint8_t, uint32_t) { return true; }
// The operation's own start carries the admitted shape; the double accepts the
// production signature so the unmodified service compiles against it.
inline bool diagnostic_quick_test_start(uint8_t, uint8_t, uint32_t, uint16_t, uint16_t, uint8_t, uint32_t) { return true; }
inline bool diagnostic_quick_test_start(uint8_t, uint8_t, uint32_t, uint32_t) { return true; }
inline bool diagnostic_quick_test_ready(uint32_t) { return false; }
inline bool diagnostic_quick_test_pass(uint32_t) { return false; }
inline void diagnostic_quick_test_cancel(const char *, uint32_t) {}
inline void diagnostic_tests_json(JsonObject) {}
inline bool correction_link_input(const uint8_t *, size_t, uint32_t) { return true; }
inline void correction_output_reset() {}
inline CorrectionOutputStats correction_output_stats() { return CorrectionOutputStats{}; }
