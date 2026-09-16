#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include "network_service.h"
#include "wifi_transport.h"

#ifdef TOPORTK_ROLLBACK_TEST_HANG
#include <esp_ota_ops.h>
#endif
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "board_hardware.h"
#include "device_config.h"
#include "device_settings.h"
#include "diagnostic_log.h"
#include "gnss_parser.h"
#include "gnss_service.h"
#include "ui_theme.h"
#include "ui_display.h"
#include "ui_screens.h"
#include "touch_input.h"
#include "receiver_reply.h"
#include "touch_layout.h"
#include "web_http.h"
#include "rover_ap.h"
#include "survey_service.h"
#include "correction_service.h"
#include "correction_transport.h"
#include "instrument_status.h"
#include "link_diagnostic.h"
#include "link_service.h"
#include "debug_service.h"
#include "ota_service.h"
#include "peer_update.h"
#include "usb_console.h"

// The board pin map, the display/touch construction and the LCD reset sequence
// moved to board_hardware.cpp (R10a slice 5); this root composes their results.

namespace network {
constexpr uint32_t kMagic = 0x5452544B;
constexpr uint8_t kVersion = 3;
constexpr uint8_t kHelloPacket = 1;
constexpr uint8_t kTestPacket = 2;
constexpr uint8_t kRtcmPacket = 3;
constexpr char kMarker[] = "TOPO-RTK-WIFI";
constexpr uint32_t kHelloIntervalMs = 1000;
constexpr uint32_t kTestIntervalMs = 250;
constexpr uint32_t kStatusIntervalMs = 2000;
}  // namespace network


struct __attribute__((packed)) WiFiTestPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t packet_size;
  uint32_t sequence;
  uint32_t sender_ms;
  int16_t link_rssi_dbm;
  uint16_t reserved;
  char marker[16];
  uint32_t sender_boot, receiver_boot, session;
  uint8_t sender_unit, sender_role;
  uint16_t identity_reserved;
  uint32_t checksum;
};

struct __attribute__((packed)) WiFiRtcmHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t packet_size;
  uint32_t sequence;
  uint32_t sender_ms;
  uint16_t rtcm_length;
  uint16_t rtcm_message;
  uint32_t sender_boot, receiver_boot, session;
  uint8_t sender_unit, sender_role;
  uint16_t identity_reserved;
  uint32_t checksum;
};

constexpr size_t kMaxRtcmFrameSize = 1029;
constexpr size_t kMaxWiFiRtcmPacketSize =
    sizeof(WiFiRtcmHeader) + kMaxRtcmFrameSize;

// The two NVS stores the settings owner writes through. They stay separate
// objects: Preferences::begin rebinds the namespace of the object it is called
// on, and the device config and the base record live in different namespaces.
Preferences preferences;
Preferences base_preferences;

bool is_base() { return device_settings::config().role == DeviceRole::kBase; }
uint32_t last_screen_ms = 0;
uint32_t last_web_status_ms = 0;
bool wifi_peer_known = false;
IPAddress wifi_peer;
int16_t wifi_peer_rssi_dbm = 0;
uint32_t wifi_last_peer_ms = 0;
uint32_t wifi_tx_sequence = 0;
uint32_t wifi_tx_packets = 0;
uint32_t wifi_rx_packets = 0;
uint32_t wifi_invalid_packets = 0;
uint32_t wifi_sequence_gaps = 0;
uint32_t wifi_last_sequence = 0;
bool wifi_have_sequence = false;
uint32_t last_wifi_send_ms = 0;
uint32_t last_wifi_status_ms = 0;
uint32_t last_wifi_connect_attempt_ms = 0;
uint32_t rtcm_wifi_sequence = 0;
uint32_t wifi_session_id = 0, wifi_session_peer = 0, wifi_rtcm_highest = 0;
uint32_t rtcm_wifi_tx_frames = 0;
uint32_t rtcm_wifi_rx_frames = 0;
// Correction output boundary (R10a). The bounded queue, the station guard, the
// observation health, the counters and the admission policy moved into
// correction_service.cpp behind typed requests; what stays here is the gate it
// cannot read for itself (the selected role), the loop's call, and the three
// functions link_service.h declares for the link owner, unchanged in meaning.
const CorrectionGate kCorrectionGate{is_base};

void correction_service_begin() { correction_service::begin(kCorrectionGate); }

bool correction_link_input(const uint8_t *frame,size_t size,uint32_t at){
  return link_service::radio_active()&&correction_service::admit(frame,size,at,millis());
}
void correction_output_reset(){ correction_service::reset(); }
CorrectionOutputStats correction_output_stats(){
  const CorrectionSnapshot s=correction_service::snapshot();CorrectionOutputStats out;
  out.forwarded=s.forwarded;out.expired=s.expired;out.overflow=s.overflow;
  out.waiting=s.waiting;out.faults=s.faults;out.queued=s.queued;return out;
}
void sd_log_event(const char *event, const char *detail);

bool using_local_router() { return network_service::local_router(); }

const char *wifi_transport_label() { return network_service::label(); }

const char *active_wifi_ssid() { return network_service::ssid(); }

bool station_connected() { return network_service::station_connected(); }

bool on_station_subnet(IPAddress ip) { return network_service::on_station_subnet(ip); }

IPAddress station_broadcast() { return network_service::station_broadcast(); }


// The settings boundary (R10a slice 4) owns the records, the applied
// configuration and the reset consequences; these two are the root's boot
// steps. The console line belongs to the root's UART0, and the touchscreen's
// pending selection is screen state, not settings state.
void load_config() {
  device_settings::load();
  ui_set_pending_role(device_settings::config().role);
  const SettingsSnapshot settings = device_settings::snapshot();
  Serial.printf("CONFIG LOAD: %s role=%s brightness=%u rtcm=%s wifi=%s\n",
                settings.error ? "ERROR / DEFAULTS" : settings.saved ? "SAVED" : "DEFAULTS",
                settings.config.role == DeviceRole::kBase ? "BASE" : "ROVER",
                static_cast<unsigned>(settings.config.brightness),
                settings.config.base_rtcm ? "ON" : "OFF", wifi_transport_label());
}

void load_base_settings() { device_settings::load_base(); }


uint32_t fnv1a(const uint8_t *bytes, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t packet_checksum(WiFiTestPacket packet) {
  packet.checksum = 0;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&packet);
  return fnv1a(bytes, sizeof(packet));
}

uint32_t crc24q(const uint8_t *bytes, size_t length) {
  uint32_t crc = 0;
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint32_t>(bytes[index]) << 16;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc <<= 1;
      if ((crc & 0x1000000UL) != 0) crc ^= 0x1864CFBUL;
    }
  }
  return crc & 0xFFFFFFUL;
}

bool valid_rtcm_frame(const uint8_t *frame, size_t length) {
  if (frame == nullptr || length < 8 || length > kMaxRtcmFrameSize ||
      frame[0] != 0xD3 || (frame[1]&0xfc)) {
    return false;
  }
  const size_t payload_length =
      (static_cast<size_t>(frame[1] & 0x03) << 8) | frame[2];
  if (length != payload_length + 6) return false;
  const uint32_t expected_crc =
      (static_cast<uint32_t>(frame[length - 3]) << 16) |
      (static_cast<uint32_t>(frame[length - 2]) << 8) |
      frame[length - 1];
  return expected_crc == crc24q(frame, length - 3);
}

uint16_t rtcm_message_type(const uint8_t *frame, size_t length) {
  if (length < 8) return 0;
  return (static_cast<uint16_t>(frame[3]) << 4) | (frame[4] >> 4);
}

bool valid_wifi_packet(WiFiTestPacket packet) {
  if (packet.magic != network::kMagic ||
      packet.version != network::kVersion ||
      packet.packet_size != sizeof(packet) ||
      std::strncmp(packet.marker, network::kMarker, sizeof(packet.marker)) != 0) {
    return false;
  }
  const uint32_t received_checksum = packet.checksum;
  return received_checksum == packet_checksum(packet);
}

void sync_wifi_session(const pair_session::Snapshot &state) {
  if (state.session == wifi_session_id && state.peer_boot == wifi_session_peer) return;
  // Only transport-derived counters reset here; the legacy peer address/age
  // stay with the hello traffic that owns them, and admission reads the pair.
  wifi_session_id = state.session; wifi_session_peer = state.peer_boot;
  wifi_rtcm_highest = rtcm_wifi_sequence = wifi_tx_sequence = wifi_last_sequence = 0;
  wifi_have_sequence = false;
}
template<class Packet> bool current_wifi_identity(const Packet &packet, uint32_t now) {
  const auto state = link_service::snapshot(now);
  sync_wifi_session(state);
  return state.transport == pair_session::Transport::WiFi && state.connected &&
         packet.session == state.session && packet.session >= 1000000 &&
         packet.sender_boot == state.peer_boot && packet.receiver_boot == state.local_boot &&
         packet.sender_unit == 3 - TOPORTK_UNIT_ID && packet.sender_role == (is_base() ? 1 : 0) &&
         !packet.identity_reserved && packet.sequence;
}
template<class Packet> void set_wifi_identity(Packet &packet, const pair_session::Snapshot &state) {
  packet.sender_boot = state.local_boot; packet.receiver_boot = state.peer_boot;
  packet.session = state.session; packet.sender_unit = TOPORTK_UNIT_ID; packet.sender_role = !is_base();
}

bool legacy_wifi_packet(uint8_t *bytes, size_t size) {
  if (size < 24 || correction::u32(bytes) != network::kMagic || bytes[4] != 2 ||
      correction::u16(bytes + 6) != size) return false;
  const bool control = bytes[5] == network::kHelloPacket || bytes[5] == network::kTestPacket;
  const size_t checksum_at = control ? 36 : 20;
  if (control ? (size != 40 || std::memcmp(bytes + 20, network::kMarker, sizeof(network::kMarker)))
              : (bytes[5] != network::kRtcmPacket || correction::u16(bytes + 16) + 24 != size)) return false;
  const uint32_t checksum = correction::u32(bytes + checksum_at);
  correction::put32(bytes + checksum_at, 0);
  const bool valid = checksum == fnv1a(bytes, size);
  correction::put32(bytes + checksum_at, checksum);
  return valid;
}

bool send_wifi_packet(const IPAddress &destination, uint8_t type,
                      uint32_t sequence) {
  const auto state = link_service::snapshot(millis());
  if (state.transport != pair_session::Transport::WiFi || !state.connected) return false;

  WiFiTestPacket packet = {};
  packet.magic = network::kMagic;
  packet.version = network::kVersion;
  packet.type = type;
  packet.packet_size = sizeof(packet);
  packet.sequence = sequence;
  packet.sender_ms = millis();
  packet.link_rssi_dbm = !is_base() ? network_service::rssi() : 0;
  set_wifi_identity(packet, state);
  std::strncpy(packet.marker, network::kMarker, sizeof(packet.marker) - 1);
  packet.checksum = packet_checksum(packet);

  return wifi_transport::send(wifi_transport::Channel::Corrections, destination,
                              reinterpret_cast<const uint8_t *>(&packet),
                              sizeof(packet));
}

bool send_rtcm_packet(const uint8_t *frame, size_t frame_length,
                      uint16_t message_type) {
  const auto state = link_service::snapshot(millis());
  sync_wifi_session(state);
  if (state.transport != pair_session::Transport::WiFi || !state.connected ||
      !wifi_transport::started(wifi_transport::Channel::Corrections) ||
      !uint32_t(link_service::wifi_peer()) || frame == nullptr || frame_length > kMaxRtcmFrameSize ||
      rtcm_wifi_sequence == UINT32_MAX) {
    return false;
  }

  uint8_t packet[kMaxWiFiRtcmPacketSize] = {};
  WiFiRtcmHeader header = {};
  header.magic = network::kMagic;
  header.version = network::kVersion;
  header.type = network::kRtcmPacket;
  header.packet_size = sizeof(header) + frame_length;
  header.sequence = ++rtcm_wifi_sequence;
  header.sender_ms = millis();
  header.rtcm_length = frame_length;
  header.rtcm_message = message_type;
  set_wifi_identity(header, state);
  header.checksum = 0;
  std::memcpy(packet, &header, sizeof(header));
  std::memcpy(packet + sizeof(header), frame, frame_length);
  header.checksum = fnv1a(packet, header.packet_size);
  std::memcpy(packet, &header, sizeof(header));

  debug_frame(debugmode::Channel::WifiTx, frame, frame_length);
  return wifi_transport::send(wifi_transport::Channel::Corrections, link_service::wifi_peer(),
                              packet, header.packet_size);
}


bool handle_wifi_rtcm_packet(uint8_t *packet, size_t packet_length) {
  if (link_service::radio_active() || is_base() ||
      !gnss_service::snapshot().profile_applied || packet == nullptr ||
      packet_length < sizeof(WiFiRtcmHeader)) {
    return false;
  }

  WiFiRtcmHeader header = {};
  std::memcpy(&header, packet, sizeof(header));
  if (header.magic != network::kMagic ||
      header.version != network::kVersion ||
      header.type != network::kRtcmPacket ||
      header.packet_size != packet_length ||
      header.rtcm_length + sizeof(header) != packet_length ||
      header.rtcm_length > kMaxRtcmFrameSize) {
    return false;
  }

  const uint32_t received_checksum = header.checksum;
  header.checksum = 0;
  std::memcpy(packet, &header, sizeof(header));
  if (received_checksum != fnv1a(packet, packet_length)) return false;

  const uint8_t *frame = packet + sizeof(header);
  if (!valid_rtcm_frame(frame, header.rtcm_length) ||
      header.rtcm_message != rtcm_message_type(frame, header.rtcm_length)) {
    return false;
  }
  if (!current_wifi_identity(header, millis()) || header.sequence <= wifi_rtcm_highest) return false;
  wifi_rtcm_highest = header.sequence; // Valid envelopes consume sequence even if COM2 admission is busy.

  debug_frame(debugmode::Channel::WifiRx,frame,header.rtcm_length);
  const uint32_t admitted_at=millis();
  if(!correction_service::admit(frame,header.rtcm_length,admitted_at,admitted_at))return false;
  ++rtcm_wifi_rx_frames;wifi_last_peer_ms=millis();return true;
}

void start_wifi() {
  survey_revoke_control();
  // A network restart is not a radio/session/reference reset.
  phone_key_shown_ms = phone_key_confirm_ms = 0;
  wifi_peer_known = false;
  wifi_peer = IPAddress();
  wifi_last_peer_ms = 0;
  wifi_peer_rssi_dbm = 0;
  wifi_tx_packets = wifi_rx_packets = 0;
  wifi_sequence_gaps = wifi_invalid_packets = 0;
  last_wifi_send_ms = 0;
  last_wifi_connect_attempt_ms = 0;
  network_service::restart(device_settings::config(), board::kUnitLabel, millis());
}

// Settings boundary (R10a slice 4). The records, the applied configuration and
// the reset consequences live in device_settings.cpp; what stays here is the
// two NVS objects it stores through and the facts and actions owned by the
// receiver, the link and the UI that this root installs. The console, the
// touchscreen and the survey glue keep their calls through these wrappers.
bool settings_profile_running() { return gnss_service::snapshot().profile_running; }
bool settings_profile_failed() { return gnss_service::snapshot().profile_failed; }
void settings_receiver_config(const DeviceConfig &config) { gnss_service::set_config(config); }
void settings_receiver_base_coordinate(bool fixed, double latitude, double longitude, double height) {
  gnss_service::set_base_coordinate(fixed, latitude, longitude, height);
}
void settings_receiver_apply_profile() { gnss_service::apply_unit_profile(); }
void settings_receiver_base_failure(bool blocks_any_role) {
  gnss_service::report_base_failure(blocks_any_role);
}
void settings_link_restart() { start_wifi(); }
void settings_brightness_step_reset(uint32_t now_ms) { ui_reset_brightness_step(now_ms); }
void settings_correction_reset() { correction_service::reset(); }
void settings_receiver_profile_reset() { gnss_service::reset_for_config_change(); }
void settings_log_event(const char *event, const char *detail) { sd_log_event(event, detail); }

const SettingsHooks kSettingsHooks{
    diagnostic_busy, ota_locked, settings_profile_running, settings_profile_failed,
    using_local_router, settings_receiver_config, settings_receiver_base_coordinate,
    settings_receiver_apply_profile, settings_receiver_base_failure,
    settings_link_restart, settings_brightness_step_reset, settings_correction_reset,
    settings_receiver_profile_reset, settings_log_event};

void device_settings_begin() {
  device_settings::begin(preferences, base_preferences, kSettingsHooks);
}

bool select_config(const DeviceConfig &requested) {
  return device_settings::apply(requested, millis());
}

void select_role(DeviceRole role) { device_settings::set_role(role, millis()); }

void select_brightness(BrightnessMode mode) { device_settings::set_brightness(mode, millis()); }

void select_wifi_mode(WiFiMode mode) { device_settings::set_wifi_mode(mode, millis()); }

void receive_wifi_packets() {
  while (true) {
    IPAddress sender;
    uint8_t packet_bytes[kMaxWiFiRtcmPacketSize] = {};
    const int packet_length = wifi_transport::receive(
        wifi_transport::Channel::Corrections, packet_bytes,
        sizeof(packet_bytes), sender);
    if (packet_length <= 0) {
      if (packet_length < 0) ++wifi_invalid_packets;
      return;
    }
    // Phone AP clients are not correction peers. Accept Rover input only from
    // the upstream station subnet, never the independent phone subnet.
    if (!is_base() && !on_station_subnet(sender)) continue;
    if (legacy_wifi_packet(packet_bytes, size_t(packet_length))) {
      link_service::note_incompatible(millis()); ++wifi_invalid_packets; continue;
    }
    if (packet_length >= static_cast<int>(sizeof(WiFiRtcmHeader)) &&
        packet_bytes[5] == network::kRtcmPacket) {
      if(link_service::radio_active())continue; // Never feed parallel Wi-Fi copies.
      if(!(sender==link_service::wifi_peer())){++wifi_invalid_packets;continue;}
      if (!handle_wifi_rtcm_packet(packet_bytes, packet_length)) {
        ++wifi_invalid_packets;
      }
      continue;
    }

    if (packet_length != static_cast<int>(sizeof(WiFiTestPacket))) {
      ++wifi_invalid_packets;
      continue;
    }

    WiFiTestPacket packet = {};
    std::memcpy(&packet, packet_bytes, sizeof(packet));
    if (!valid_wifi_packet(packet) || !current_wifi_identity(packet, millis()) ||
        !(sender == link_service::wifi_peer()) || packet.reserved ||
        (wifi_have_sequence && packet.sequence <= wifi_last_sequence)) {
      ++wifi_invalid_packets;
      continue;
    }

    if (is_base() && packet.type == network::kHelloPacket) {
      wifi_peer = sender;
      wifi_peer_known = true;
      wifi_peer_rssi_dbm = packet.link_rssi_dbm;
      wifi_last_peer_ms = millis();
      ++wifi_rx_packets;
    } else if (!is_base() && packet.type == network::kTestPacket) {
      wifi_peer = sender;
      wifi_peer_known = true;
      wifi_peer_rssi_dbm = network_service::rssi();
      wifi_last_peer_ms = millis();
      ++wifi_rx_packets;
      if (wifi_have_sequence && packet.sequence > wifi_last_sequence + 1)
        wifi_sequence_gaps += packet.sequence - wifi_last_sequence - 1;
    } else {
      ++wifi_invalid_packets;
    }
    wifi_last_sequence = packet.sequence; wifi_have_sequence = true;
  }
}

void service_wifi() {
  const uint32_t now = millis();
  network_service::service(millis());
  const auto state = link_service::snapshot(now);
  sync_wifi_session(state);
  receive_wifi_packets();

  if (state.connected && state.transport == pair_session::Transport::WiFi &&
      wifi_tx_sequence != UINT32_MAX && is_base() && uint32_t(link_service::wifi_peer()) &&
      now - last_wifi_send_ms >= network::kTestIntervalMs) {
    last_wifi_send_ms = now;
    if (send_wifi_packet(link_service::wifi_peer(), network::kTestPacket, ++wifi_tx_sequence)) {
      ++wifi_tx_packets;
    }
  } else if (state.connected && state.transport == pair_session::Transport::WiFi &&
             wifi_tx_sequence != UINT32_MAX && !is_base() && station_connected() &&
             wifi_transport::started(wifi_transport::Channel::Corrections) &&
             now - last_wifi_send_ms >= network::kHelloIntervalMs) {
    last_wifi_send_ms = now;
    const IPAddress destination = link_service::wifi_peer();
    if (send_wifi_packet(destination, network::kHelloPacket, ++wifi_tx_sequence)) {
      ++wifi_tx_packets;
    }
  }

  if (now - last_wifi_status_ms >= network::kStatusIntervalMs) {
    last_wifi_status_ms = now;
    const GnssSnapshot gnss_state = gnss_service::snapshot();
    if (is_base()) {
      Serial.printf("WIFI BASE: transport=%s station=%s clients=%u peer=%s TX=%lu hello_RX=%lu bad=%lu RTCM_UART=%lu RTCM_TX=%lu RTCM_BAD=%lu last=%u\n",
                    wifi_transport_label(), station_connected() ? "UP" : "DOWN",
                    using_local_router() ? 0 : network_service::clients(),
                    wifi_peer_known ? "YES" : "NO",
                    static_cast<unsigned long>(wifi_tx_packets),
                    static_cast<unsigned long>(wifi_rx_packets),
                    static_cast<unsigned long>(wifi_invalid_packets),
                    static_cast<unsigned long>(gnss_state.rtcm_frames),
                    static_cast<unsigned long>(rtcm_wifi_tx_frames),
                    static_cast<unsigned long>(gnss_state.rtcm_bad),
                    gnss_state.rtcm_last_message);
    } else {
      Serial.printf("WIFI ROVER: transport=%s link=%s RSSI=%d TX_hello=%lu RX=%lu gap=%lu bad=%lu last=%lu RTCM_RX=%lu bytes=%lu type=%u\n",
                    wifi_transport_label(), station_connected() ? "UP" : "DOWN",
                    station_connected() ? network_service::rssi() : 0,
                    static_cast<unsigned long>(wifi_tx_packets),
                    static_cast<unsigned long>(wifi_rx_packets),
                    static_cast<unsigned long>(wifi_sequence_gaps),
                    static_cast<unsigned long>(wifi_invalid_packets),
                    static_cast<unsigned long>(wifi_last_sequence),
                    static_cast<unsigned long>(rtcm_wifi_rx_frames),
                    static_cast<unsigned long>(correction_service::snapshot().forwarded_bytes),
                    gnss_state.rtcm_last_message);
    }
  }
}

void format_horizontal_accuracy(char *output, size_t output_size,
                                uint32_t now) {
  const GnssSnapshot gnss_state = gnss_service::snapshot();
  if (!gnss_state.profile_applied || !gnss_state.gga.received ||
      gnss_state.gga.quality <= 0 || now - gnss_state.gga_ms > 3000) {
    std::snprintf(output, output_size, "---");
    return;
  }
  if (std::strcmp(gnss_state.role, "BASE") == 0 && gnss_state.gga.quality == 7) {
    std::snprintf(output, output_size, "N/A (BASE)");
    return;
  }
  if (!gnss_state.accuracy.received ||
      !std::isfinite(gnss_state.accuracy.horizontal_1drms_m) ||
      gnss_state.accuracy.horizontal_1drms_m < 0 ||
      now - gnss_state.accuracy.received_ms > 3000) {
    std::snprintf(output, output_size, "---");
    return;
  }

  const double meters = gnss_state.accuracy.horizontal_1drms_m;
  if (meters < 0.01) {
    std::snprintf(output, output_size, "%.1f mm", meters * 1000.0);
  } else if (meters < 1.0) {
    std::snprintf(output, output_size, "%.1f cm", meters * 100.0);
  } else {
    std::snprintf(output, output_size, "%.2f m", meters);
  }
}

// Composition-root inputs for instrument_status (R10a): receiver facts, the
// link snapshot, the correction owner's state and the OTA/diagnostic flags.
// The policy itself lives in instrument_status.cpp.
instrument_status::Solution solution_inputs() {
  const HorizontalAccuracyData f = gnss_service::snapshot().accuracy;
  instrument_status::Solution s;
  s.received = f.received;
  s.position_valid = f.position_valid;
  s.received_ms = f.received_ms;
  s.differential_age_ms = f.differential_age_ms;
  s.station = f.solution_station;
  return s;
}

instrument_status::Inputs status_inputs(uint32_t now) {
  const auto pair = link_service::snapshot(now);
  const GnssSnapshot receiver = gnss_service::snapshot();
  instrument_status::Inputs in;
  in.now_ms = now;
  in.transport = pair.transport == pair_session::Transport::Radio ? instrument_status::Transport::Radio
                                                                 : instrument_status::Transport::WiFi;
  in.peer_connected = pair.connected;
  in.peer_age_ms = pair.peer_age_ms;
  in.peer_reason = pair.reason;
  in.wifi_station_rssi_valid = station_connected();
  in.wifi_station_rssi_dbm = network_service::rssi();
  in.wifi_peer_report_valid = wifi_peer_known;
  in.wifi_peer_report_dbm = wifi_peer_rssi_dbm;
  in.solution = solution_inputs();
  in.base_rtcm_output = device_settings::config().base_rtcm;
  in.uart_seen = receiver.uart_seen;
  in.last_rx_ms = receiver.last_rx_ms;
  in.version_ok = receiver.version_ok;
  in.profile_applied = receiver.profile_applied;
  in.role_base = is_base();
  in.receiver_role = receiver.role;
  in.gga_received = receiver.gga.received;
  in.gga_quality = receiver.gga.quality;
  in.gga_ms = receiver.gga_ms;
  in.time_valid = receiver.time.valid;
  in.time_received_ms = receiver.time.received_ms;
  in.device_available = !diagnostic_busy() && !ota_locked();
  in.ota_paused = ota_paused();
  in.config_error = device_settings::snapshot().error;
  in.profile_failed = receiver.profile_failed;
  return in;
}

instrument_status::Status status_snapshot(uint32_t now) {
  // Pair connectivity is bidirectional current-boot proof, not data arrival.
  // Receiver correction age/fix gates remain independent and unchanged.
  return instrument_status::status_snapshot(status_inputs(now), correction_service::health());
}

bool gps_required_fix() { return instrument_status::gps_required_fix(status_snapshot(millis())); }
bool correction_link_connected(uint32_t now) { return instrument_status::correction_link_connected(status_snapshot(now)); }
bool fresh_rover_corrections(uint32_t now) { return instrument_status::fresh_rover_corrections(status_snapshot(now)); }
bool system_ready(uint32_t now) { return instrument_status::system_ready(status_snapshot(now)); }

int16_t current_link_rssi() { return instrument_status::current_link_rssi(status_snapshot(millis())); }
bool current_link_rssi_valid() { return instrument_status::current_link_rssi_valid(status_snapshot(millis())); }

const char *link_quality_label(int16_t rssi) { return instrument_status::link_quality_label(rssi); }
uint16_t link_quality_color(int16_t rssi) { return instrument_status::link_quality_color(rssi); }

bool fresh_gnss_time(uint32_t now) {
  const GnssTimeData time = gnss_service::snapshot().time;
  return instrument_status::fresh_gnss_time(time.valid, time.received_ms, now);
}

uint32_t verified_correction_age(uint32_t now) {
  return instrument_status::verified_correction_age(solution_inputs(), now, correction_service::health());
}

const char *correction_health_state(uint32_t now) {
  return instrument_status::correction_health_state(solution_inputs(), now, correction_service::health());
}

const char *fix_label(int quality) {
  return instrument_status::fix_label(gnss_service::snapshot().role, quality);
}
uint16_t fix_color(int quality) {
  return instrument_status::fix_color(gnss_service::snapshot().role, quality);
}


uint16_t page_header_color(uint32_t now) {
  bool ready = false;
  if (current_page == ScreenPage::kMain) {
    ready = system_ready(now);
  } else if (current_page == ScreenPage::kGpsDetails) {
    ready = gps_required_fix();
  } else if (current_page == ScreenPage::kWifiDetails) {
    ready = correction_link_connected(now);
  }
  return ready ? colors::kHeaderReady : colors::kHeaderNotReady;
}

const char *device_role_title() {
  return is_base() ? "Base" : "Rover";
}

const char *brightness_mode_label() {
  const BrightnessMode mode = device_settings::config().brightness;
  if (mode == BrightnessMode::kDay) return "DAY";
  if (mode == BrightnessMode::kNight) return "NIGHT";
  return fresh_gnss_time(millis()) ? "AUTO" : "AUTO NO GPS";
}


// Diagnostic logging boundary (R10a slice 4). The CSV session, the mount
// readback test, the session directory and the bounded best-effort appends live
// in diagnostic_log.cpp; what stays here is the one snapshot per call and the
// accuracy text the root owns. The loop keeps the same gated call in the same
// position, and the root is the only reader of receiver and link state.
LogTime log_time(uint32_t now_ms) {
  LogTime stamp;
  stamp.now_ms = now_ms;
  stamp.utc = gnss_service::snapshot().time;
  return stamp;
}

DiagnosticInputs sd_log_inputs(uint32_t now) {
  const GnssSnapshot gnss_state = gnss_service::snapshot();
  const auto link = status_snapshot(now);
  DiagnosticInputs in;
  in.time.now_ms = now;
  in.time.utc = gnss_state.time;
  in.base = is_base();
  in.uart_active = gnss_state.uart_seen && now - gnss_state.last_rx_ms < 3000;
  in.fix_quality = gnss_state.gga.received ? gnss_state.gga.quality : -1;
  in.fix_text = fix_label(in.fix_quality);
  in.role = gnss_state.role;
  in.link_connected = link.link_connected;  // Transport-aware: radio or Wi-Fi.
  in.rtcm_active = in.base
                       ? gnss_state.rtcm_frames > 0
                       : gnss_state.rtcm_last_rx_ms > 0 && now - gnss_state.rtcm_last_rx_ms <= 3000;
  in.solution_fresh = gnss_state.gga.received && now - gnss_state.gga_ms <= 3000;
  std::strncpy(in.gga_utc, gnss_state.gga.utc, sizeof(in.gga_utc) - 1);
  in.latitude = gnss_state.gga.latitude;
  in.longitude = gnss_state.gga.longitude;
  in.altitude = gnss_state.gga.altitude;
  in.satellites = gnss_state.gga.satellites;
  in.hdop = gnss_state.gga.hdop;
  in.rtcm_age_ms = gnss_state.rtcm_last_rx_ms == 0
                       ? -1L
                       : static_cast<long>(now - gnss_state.rtcm_last_rx_ms);
  in.rssi_valid = link.signal.valid;
  in.rssi_dbm = link.signal.rssi_dbm;
  in.transport = link.transport == instrument_status::Transport::Radio ? "SIK"
                 : link.transport == instrument_status::Transport::WiFi ? "WIFI"
                                                                        : "-";
  in.rtcm_uart_frames = gnss_state.rtcm_frames;
  in.rtcm_wifi_tx_frames = rtcm_wifi_tx_frames;
  in.rtcm_wifi_rx_frames = rtcm_wifi_rx_frames;
  in.forwarded_bytes = correction_service::snapshot().forwarded_bytes;
  in.sequence_gaps = wifi_sequence_gaps;
  in.invalid_packets = wifi_invalid_packets;
  return in;
}

const DiagnosticHooks kDiagnosticHooks{format_horizontal_accuracy};

void setup_sd_logging() {
  const SdPort port{board::kSdClock, board::kSdCommand, board::kSdData0,
                    board::kUnitLabel};
  diagnostic_log::begin(port, kDiagnosticHooks, log_time(millis()));
}

void service_sd_logging() { diagnostic_log::service(sd_log_inputs(millis())); }

void sd_log_event(const char *event, const char *detail) {
  diagnostic_log::event(event, detail, log_time(millis()));
}

// The warning policy lives in instrument_status; the peer_update notice text is
// the only input the composition root adds beyond the status inputs.
const char *warning_peer_notice() {
  static char notice[80];
  peer_update_label(notice, sizeof(notice));
  return notice;
}

DashboardWarning dashboard_warning(uint32_t now) {
  instrument_status::Inputs in = status_inputs(now);
  in.peer_notice = warning_peer_notice();
  return instrument_status::dashboard_warning(in, correction_service::health());
}

const char *current_fix_label(uint32_t now) {
  const GnssSnapshot gnss_state = gnss_service::snapshot();
  return instrument_status::current_fix_label(gnss_state.role, gnss_state.gga.received,
                                              gnss_state.gga_ms, gnss_state.gga.quality, now);
}

UiFrame ui_frame;

// Touchscreen Link-mode helpers (R6b): the page issues the same pair operation
// the web Settings page does, so it needs the same 32-hex request id shape and a
// place to keep the last refusal. The counter guarantees a fresh tag over a boot.
char link_ui_error[96] = {};
pair_session::Transport link_recovery_target = pair_session::Transport::Radio;

void to_upper_ascii(char *text) {
  for (char *p = text; *p; ++p) {
    if (*p >= 'a' && *p <= 'z') *p = static_cast<char>(*p - 32);
  }
}

void make_link_request_id(char out[33]) {
  static uint32_t sequence = 0;
  const uint32_t a = esp_random(), b = esp_random(), c = esp_random();
  const uint32_t d = esp_random() ^ ++sequence ^ web_boot_id();
  std::snprintf(out, 33, "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(a),
                static_cast<unsigned long>(b), static_cast<unsigned long>(c),
                static_cast<unsigned long>(d));
}

// The frame carries ready-to-draw strings so the screen module holds no receiver state.
void ui_build_frame(UiFrame &f, uint32_t now) {
  const GnssSnapshot gnss_state = gnss_service::snapshot();
  f.now = now;
  f.unit = board::kUnitLabel;
  f.header_color = page_header_color(now);
  if (current_page == ScreenPage::kGpsDetails) {
    std::snprintf(f.header_title, sizeof(f.header_title), "GPS / %s", device_role_title());
    std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "REQUIRED FIX: %s",
                  gps_required_fix() ? "YES" : "NO");
  } else if (current_page == ScreenPage::kWifiDetails) {
    std::snprintf(f.header_title, sizeof(f.header_title), "Link / %s", device_role_title());
    std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "LINK %s",
                  correction_link_connected(now) ? "CONNECTED" : "DOWN");
  } else if (current_page == ScreenPage::kSettings) {
    std::snprintf(f.header_title, sizeof(f.header_title), "Setup / %s", device_role_title());
    std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "ROLE & BRIGHTNESS");
  } else if (current_page == ScreenPage::kDebug) {
    std::snprintf(f.header_title, sizeof(f.header_title), "Debug / %s", device_role_title());
    std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "PASSIVE MONITOR");
  } else {
    std::snprintf(f.header_title, sizeof(f.header_title), "TopoRTK - %s", device_role_title());
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (local_time_utc_minus_6(now, gnss_state.time, year, month, day, hour, minute, second)) {
      std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "%s %02u:%02u",
                    system_ready(now) ? "READY" : "NOT READY", hour, minute);
    } else {
      std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "%s",
                    system_ready(now) ? "READY" : "NOT READY");
    }
  }
  if (debug_enabled()) std::strncat(f.header_subtitle, " -DBG", sizeof(f.header_subtitle) - std::strlen(f.header_subtitle) - 1);
  if (current_page == ScreenPage::kMain) {
    const auto link = status_snapshot(now);
    const bool linked = link.link_connected;
    const bool radio = link.transport == instrument_status::Transport::Radio;
    if (linked) {
      if (radio) std::strcpy(f.link_value, "Radio Connected");
      else std::snprintf(f.link_value, sizeof(f.link_value), "Wi-Fi  %d dBm", link.signal.rssi_dbm);
    } else {
      std::snprintf(f.link_value, sizeof(f.link_value), "%s Disconnected", radio ? "Radio" : "Wi-Fi");
    }
    f.link_color = !linked ? colors::kFailFill : radio ? colors::kHeaderReady : link_quality_color(link.signal.rssi_dbm);
    std::snprintf(f.fix_value, sizeof(f.fix_value), "%s", current_fix_label(now));
    f.fix_card_color = now - gnss_state.gga_ms > 3000 ? colors::kFailFill : fix_color(gnss_state.gga.quality);
    format_horizontal_accuracy(f.hacc_value, sizeof(f.hacc_value), now);
    f.hacc_color = std::strcmp(f.hacc_value, "---") == 0 ? colors::kSecondary : colors::kPrimary;
    const DashboardWarning warning = dashboard_warning(now);
    std::snprintf(f.warning_title, sizeof(f.warning_title), "%s", warning.title);
    std::snprintf(f.warning_detail, sizeof(f.warning_detail), "%s", warning.detail);
    f.warning_color = warning.color;
  }

  if (current_page == ScreenPage::kGpsDetails) {
    const bool uart_active = gnss_state.uart_seen && now - gnss_state.last_rx_ms < 3000;
    std::snprintf(f.gps_uart, sizeof(f.gps_uart), "%s", uart_active ? "RECEIVING" : "WAITING");
    std::snprintf(f.gps_fix, sizeof(f.gps_fix), "%s",
                  gnss_state.gga.received ? fix_label(gnss_state.gga.quality) : "NO GGA");
    uint16_t local_year = 0;
    uint8_t local_month = 0, local_day = 0, local_hour = 0, local_minute = 0, local_second = 0;
    if (local_time_utc_minus_6(now, gnss_state.time, local_year, local_month, local_day,
                               local_hour, local_minute, local_second)) {
      std::snprintf(f.gps_local, sizeof(f.gps_local), "%02u:%02u:%02u UTC-6", local_hour,
                    local_minute, local_second);
    } else {
      std::strcpy(f.gps_local, "WAITING FOR GNSS TIME");
    }
    if (fresh_gnss_time(now)) {
      std::snprintf(f.gps_utc, sizeof(f.gps_utc), "%02u:%02u:%02u",
                    gnss_state.time.hour, gnss_state.time.minute, gnss_state.time.second);
    } else {
      std::strcpy(f.gps_utc, "---");
    }
    if (fresh_gnss_time(now)) {
      std::snprintf(f.gps_date, sizeof(f.gps_date), "%04u-%02u-%02u",
                    gnss_state.time.year, gnss_state.time.month, gnss_state.time.day);
    } else {
      std::strcpy(f.gps_date, "---");
    }
    if (gnss_state.gga.received && gnss_state.gga.quality > 0) {
      std::snprintf(f.gps_lat, sizeof(f.gps_lat), "%.8f", gnss_state.gga.latitude);
    } else {
      std::strcpy(f.gps_lat, "---");
    }
    if (gnss_state.gga.received && gnss_state.gga.quality > 0) {
      std::snprintf(f.gps_lon, sizeof(f.gps_lon), "%.8f", gnss_state.gga.longitude);
    } else {
      std::strcpy(f.gps_lon, "---");
    }
    std::snprintf(f.gps_sats, sizeof(f.gps_sats), "%d  HDOP %.2f",
                  gnss_state.gga.satellites, gnss_state.gga.hdop);
    if (gnss_state.gga.received && gnss_state.gga.quality > 0) {
      std::snprintf(f.gps_alt, sizeof(f.gps_alt), "%.3f m", gnss_state.gga.altitude);
    } else {
      std::strcpy(f.gps_alt, "---");
    }
    format_horizontal_accuracy(f.gps_hacc, sizeof(f.gps_hacc), now);
    if (gnss_state.accuracy.received) {
      std::snprintf(f.gps_sigma, sizeof(f.gps_sigma), "N %.3f E %.3f m",
                    gnss_state.accuracy.latitude_sigma_m,
                    gnss_state.accuracy.longitude_sigma_m);
    } else {
      std::strcpy(f.gps_sigma, "---");
    }
    if (is_base()) {
      std::snprintf(f.gps_rtcm, sizeof(f.gps_rtcm), "OUT %lu  TYPE %u",
                    static_cast<unsigned long>(rtcm_wifi_tx_frames), gnss_state.rtcm_last_message);
    } else if (gnss_state.rtcm_last_rx_ms > 0) {
      std::snprintf(f.gps_rtcm, sizeof(f.gps_rtcm), "%lu ms  F %lu",
                    static_cast<unsigned long>(now - gnss_state.rtcm_last_rx_ms),
                    static_cast<unsigned long>(rtcm_wifi_rx_frames));
    } else {
      std::strcpy(f.gps_rtcm, "NONE");
    }
  }

  if (current_page == ScreenPage::kWifiDetails) {
    const auto link = status_snapshot(now);
    const bool linked = link.link_connected; // Transport-aware: radio or Wi-Fi.
    std::snprintf(f.wifi_mode, sizeof(f.wifi_mode), "%s", wifi_transport_label());
    std::snprintf(f.wifi_ssid, sizeof(f.wifi_ssid), "%s", active_wifi_ssid());
    const IPAddress local_ip = network_service::address();
    std::snprintf(f.wifi_ip, sizeof(f.wifi_ip), "%u.%u.%u.%u", local_ip[0], local_ip[1],
                  local_ip[2], local_ip[3]);
    std::snprintf(f.wifi_link, sizeof(f.wifi_link), "%s", linked ? "LINKED" : "NO LINK");
    // Signal strength is only known on Wi-Fi; the radio reports none.
    if (linked && link.signal.valid) {
      std::snprintf(f.wifi_rssi, sizeof(f.wifi_rssi), "%d dBm  %s", link.signal.rssi_dbm,
                    link_quality_label(link.signal.rssi_dbm));
    } else {
      std::strcpy(f.wifi_rssi, link.transport == instrument_status::Transport::Radio ? "N/A (RADIO)" : "---");
    }
    if (wifi_peer_known) {
      std::snprintf(f.wifi_peer, sizeof(f.wifi_peer), "%u.%u.%u.%u", wifi_peer[0],
                    wifi_peer[1], wifi_peer[2], wifi_peer[3]);
    } else if (!is_base() && station_connected() && !using_local_router()) {
      std::strcpy(f.wifi_peer, "192.168.4.1");
    } else {
      std::strcpy(f.wifi_peer, "---");
    }
    std::snprintf(f.wifi_pkts, sizeof(f.wifi_pkts), "TX %lu  RX %lu",
                  static_cast<unsigned long>(wifi_tx_packets),
                  static_cast<unsigned long>(wifi_rx_packets));
    std::snprintf(f.wifi_net, sizeof(f.wifi_net), "GAP %lu  BAD %lu",
                  static_cast<unsigned long>(wifi_sequence_gaps),
                  static_cast<unsigned long>(wifi_invalid_packets));
    std::snprintf(f.wifi_rtcm, sizeof(f.wifi_rtcm), "TX %lu  RX %lu",
                  static_cast<unsigned long>(rtcm_wifi_tx_frames),
                  static_cast<unsigned long>(rtcm_wifi_rx_frames));
    std::snprintf(f.wifi_data, sizeof(f.wifi_data), "%lu bytes  type %u",
                  static_cast<unsigned long>(correction_service::snapshot().forwarded_bytes), gnss_state.rtcm_last_message);
  }

  if (current_page == ScreenPage::kSettings) {
    const SettingsSnapshot settings = device_settings::snapshot();
    f.active_role = settings.config.role;
    f.profile_running = gnss_state.profile_running;
    f.profile_failed = gnss_state.profile_failed;
    f.profile_applied = gnss_state.profile_applied;
    f.config_error = settings.error;
    f.config_saved = settings.saved;
    f.brightness_mode = settings.config.brightness;
    const bool base_selected = pending_role == DeviceRole::kBase;
    const bool needs_apply = pending_role != settings.config.role || settings.error || !settings.saved || gnss_state.profile_failed;
    if (gnss_state.profile_running) std::strcpy(f.apply_label, "APPLYING...");
    else if (gnss_state.profile_failed) std::strcpy(f.apply_label, "RETRY SETUP");
    else if (needs_apply) std::strcpy(f.apply_label, base_selected ? "USE BASE" : "USE ROVER");
    else if (gnss_state.profile_applied) std::strcpy(f.apply_label, is_base() ? "BASE ACTIVE" : "ROVER ACTIVE");
    else std::strcpy(f.apply_label, "WAITING FOR GNSS");
    const char *mode_label = brightness_mode_label();
    if (settings.error) std::strcpy(f.settings_status, "SAVE FAILED");
    else if (gnss_state.profile_failed) std::strcpy(f.settings_status, "SETUP FAILED");
    else if (gnss_state.profile_running) std::strcpy(f.settings_status, "CONFIGURING...");
    else if (pending_role != settings.config.role) std::strcpy(f.settings_status, "TAP USE TO SAVE");
    else if (settings.saved) std::strcpy(f.settings_status, "SAVED");
    else std::strcpy(f.settings_status, "DEFAULTS");
    f.status_warning = settings.error || gnss_state.profile_failed;
    if (!backlight_pwm_ready) std::strcpy(f.brightness_line, "PWM ERROR");
    else if (settings.config.brightness == BrightnessMode::kAutomatic && !fresh_gnss_time(millis()))
      std::strcpy(f.brightness_line, "AUTO NO GPS");
    else
      std::snprintf(f.brightness_line, sizeof(f.brightness_line), "%s %u%%", mode_label,
                    static_cast<unsigned>((backlight_duty * 100U + 127U) / 255U));
  }
  if (current_page == ScreenPage::kWifiDetails && ui_detail_page() == 2) {
    f.phone_is_base = is_base();
    f.phone_local_router = using_local_router();
    if (f.phone_is_base) {
      const IPAddress address = network_service::address();
      std::snprintf(f.phone_url, sizeof(f.phone_url), "http://%u.%u.%u.%u",
                    address[0], address[1], address[2], address[3]);
    } else {
      std::snprintf(f.phone_url, sizeof(f.phone_url), "http://%s", rover_ap_address());
    }
  }
  if (current_page == ScreenPage::kWifiDetails && ui_detail_page() == 3) {
    const auto pair = link_service::snapshot(now);
    const auto operation = link_service::operation_view(now);
    const bool radio_selected = pair.transport == pair_session::Transport::Radio;
    std::snprintf(f.link_selected_line, sizeof(f.link_selected_line), "%s%s",
                  radio_selected ? "RADIO" : "WI-FI",
                  operation.storage_ok ? "" : " · RECORD UNVERIFIED");
    if (pair.connected) {
      std::snprintf(f.link_peer_line, sizeof(f.link_peer_line), "CONNECTED · SESSION %lu",
                    static_cast<unsigned long>(pair.session));
    } else {
      char reason[40] = {};
      std::snprintf(reason, sizeof(reason), "%s", pair.reason);
      for (char *c = reason; *c; ++c) {
        if (*c == '_') *c = ' ';
      }
      to_upper_ascii(reason);
      std::snprintf(f.link_peer_line, sizeof(f.link_peer_line), "NO PEER · %s", reason);
    }
    if (operation.active) {
      std::snprintf(f.link_operation_line, sizeof(f.link_operation_line),
                    "%s TO %s · %lu S LEFT", operation.state, operation.transport,
                    static_cast<unsigned long>((operation.remaining_ms + 999) / 1000));
    } else if (std::strcmp(operation.state, "idle") != 0) {
      std::snprintf(f.link_operation_line, sizeof(f.link_operation_line), "LAST %s · %s",
                    operation.state, operation.reason);
    } else {
      std::strcpy(f.link_operation_line, "NONE YET");
    }
    to_upper_ascii(f.link_operation_line);
    f.link_radio_selected = radio_selected;
    f.link_wifi_selected = !radio_selected;
    f.link_switch_busy = operation.active || diagnostic_busy() || ota_locked() ||
                         ota_paused() || gnss_state.profile_running;
    // The recovery escape hatch exists only when pair confirmation is not
    // available; a normal switch goes through the coordinator on its own medium.
    f.link_recover_available = !operation.storage_ok ||
                               !std::strcmp(operation.state, "recovery_required");
    if (link_ui_error[0]) {
      std::snprintf(f.link_mode_hint, sizeof(f.link_mode_hint), "%s", link_ui_error);
    } else if (f.link_recover_available) {
      std::strcpy(f.link_mode_hint,
                  "PAIR NOT CONFIRMED. SET THE SAME LINK ON BOTH UNITS, OR APPLY LOCALLY.");
    } else if (f.link_switch_busy) {
      std::strcpy(f.link_mode_hint, "A TEST OR UPDATE OWNS THE LINK. WAIT FOR IT TO FINISH.");
    } else {
      std::strcpy(f.link_mode_hint,
                  "ONE TAP ARMS, A SECOND CONFIRMS THE PAIR-WIDE SWITCH.");
    }
  }
}

void draw_static_screen() {
  ui_build_frame(ui_frame, millis());
  ui_draw_static(ui_frame);
}

void draw_dynamic_screen() {
  if (!display_ready) return;
  ui_build_frame(ui_frame, millis());
  ui_draw_dynamic(ui_frame);
}

void change_page(ScreenPage page) {
  const bool changed = ui_change_page(page, device_settings::config().role);
  // A refusal belongs to the visit that produced it, not to the next one.
  if (changed) link_ui_error[0] = 0;
  if (changed && display_ready) {
    draw_static_screen();
    draw_dynamic_screen();
  }
}

void handle_ui_gesture(const UiGesture &gesture) {
  if (gesture.kind == UiGesture::Kind::kSwipe) {
    if (current_page == ScreenPage::kMain && gesture.y <= 160 && gesture.dy > 0) {
      change_page(ScreenPage::kGpsDetails);
    } else if (current_page == ScreenPage::kMain && gesture.y >= 320 &&
               gesture.dy < 0) {
      change_page(ScreenPage::kWifiDetails);
    } else if (current_page == ScreenPage::kGpsDetails && gesture.dy < 0) {
      change_page(ScreenPage::kMain);
    } else if (current_page == ScreenPage::kWifiDetails && gesture.dy > 0) {
      change_page(ScreenPage::kMain);
    }
    return;
  }
  const TouchAction action = gesture.action;
  Serial.printf("UI TAP: x=%d y=%d action=%u\n", gesture.x, gesture.y,
                static_cast<unsigned>(action));
  if (action >= TouchAction::kHome && action <= TouchAction::kSettings) {
    change_page(static_cast<ScreenPage>(static_cast<uint8_t>(action) - 1));
    return;
  }
  if (action == TouchAction::kDebug) {
    change_page(ScreenPage::kDebug);
    return;
  }
  if (action == TouchAction::kDebugToggle) {
    debug_enable_local(!debug_enabled());
    draw_dynamic_screen();
    return;
  }
  if (action == TouchAction::kDetailPrev || action == TouchAction::kDetailNext) {
    ui_cycle_detail_page(action == TouchAction::kDetailPrev ? -1 : 1);
    // Detail pages have different regions: erase old rows and cached buttons
    // once at navigation, then retain incremental rendering between taps.
    if (display_ready) draw_static_screen();
    draw_dynamic_screen();
    return;
  }
  if (gnss_service::snapshot().profile_running || ota_locked()) return;
  if (action == TouchAction::kShowKey && !is_base()) {
    ui_toggle_key_reveal(millis());
  } else if (action == TouchAction::kNewKey && !is_base()) {
    if (ui_key_confirm_active(millis())) {
      rotate_rover_ap_password();
      survey_revoke_control();
      ui_clear_key_state();
    } else {
      ui_arm_key_confirm(millis());
    }
  }
  if (action == TouchAction::kLinkRadio || action == TouchAction::kLinkWifi) {
    // The touchscreen asks the pair service for the same switch the web page
    // does: one tap arms, the same tap confirms, and the coordinator owns the
    // cutover. No session code and no second bootstrap path exist here.
    const auto transport = action == TouchAction::kLinkRadio
                               ? pair_session::Transport::Radio
                               : pair_session::Transport::WiFi;
    link_recovery_target = transport;
    if (ui_link_confirm_active(millis()) && ui_link_confirm_action() == action) {
      char id[33] = {};
      make_link_request_id(id);
      link_operation::Reason reason = link_operation::Reason::None;
      if (link_service::request_operation(link_operation::Kind::Select, transport, id,
                                          link_service::revision(), reason)) {
        link_ui_error[0] = 0;
      } else {
        std::snprintf(link_ui_error, sizeof(link_ui_error), "NOT ACCEPTED: %s",
                      link_operation::reason_text(reason));
        to_upper_ascii(link_ui_error);
      }
      ui_clear_link_confirm();
    } else {
      link_ui_error[0] = 0;
      ui_arm_link_confirm(action, millis());
    }
  } else if (action == TouchAction::kLinkRecover) {
    // Recovery only: a local selection for a pair that cannot confirm itself.
    // The button renders disabled otherwise, so refuse the tap as well.
    const auto operation = link_service::operation_view(millis());
    const bool available = !operation.storage_ok ||
                           !std::strcmp(operation.state, "recovery_required");
    if (!available) {
      std::strcpy(link_ui_error, "LOCAL APPLY NEEDS AN UNCONFIRMED PAIR.");
    } else if (ui_link_confirm_active(millis()) &&
               ui_link_confirm_action() == TouchAction::kLinkRecover) {
      if (link_service::select(link_recovery_target, !is_base(), millis())) {
        link_ui_error[0] = 0;
      } else {
        std::strcpy(link_ui_error, "LOCAL APPLY REFUSED. USE THE WEB SETTINGS PAGE.");
      }
      ui_clear_link_confirm();
    } else {
      ui_arm_link_confirm(TouchAction::kLinkRecover, millis());
    }
  }
  if (action == TouchAction::kBase) pending_role = DeviceRole::kBase;
  else if (action == TouchAction::kRover) pending_role = DeviceRole::kRover;
  else if (action == TouchAction::kApply) {
    const DeviceConfig applied = device_settings::config();
    DeviceConfig requested = applied;
    requested.role = pending_role;
    if (requested.role != applied.role) requested.base_rtcm = true;
    select_config(requested);
  } else if (action == TouchAction::kAuto) select_brightness(BrightnessMode::kAutomatic);
  else if (action == TouchAction::kDay) select_brightness(BrightnessMode::kDay);
  else if (action == TouchAction::kNight) select_brightness(BrightnessMode::kNight);
  draw_dynamic_screen();
}

void ui_module_begin() {
  static const UiBoardConfig ui_board{
      board::kWidth, board::kBacklight, board::kDayBacklightDuty,
      board::kNightBacklightDuty, board::kBacklightPwmChannel,
      board::kBacklightPwmHz, board::kBacklightFadeMs};
  ui_display_begin(ui_board);
  touch_input_begin();
  touch_input_on_gesture(handle_ui_gesture);
}

void service_swipe_navigation() {
  if (!touch_ready) return;
  const uint32_t now = millis();
  static uint32_t last_touch_poll_ms = 0;
  if (now - last_touch_poll_ms < 20) return;
  last_touch_poll_ms = now;
  int16_t x = 0;
  int16_t y = 0;
  const TouchRead state = board_hardware::read_touch(x, y);
  touch_input_sample(state, x, y, now);
}


size_t format_web_status(char *output, size_t capacity, uint32_t now) {
  const auto link = status_snapshot(now);
  const GnssSnapshot gnss_state = gnss_service::snapshot();
  const bool linked = link.link_connected;
  const bool fresh_gga = gnss_state.gga.received && now - gnss_state.gga_ms <= 3000;
  const bool online = gnss_state.version_ok && gnss_state.uart_seen &&
                      now - gnss_state.last_rx_ms < 3000;
  char accuracy[32], accuracy_m[32] = "null", quality[16] = "null", satellites[16] = "null";
  char gga_age[16] = "null", peer_age[16] = "null", correction_age[16] = "null", rssi[16] = "null";
  char signal[32] = "null", local[32] = "null", utc[32] = "null";
  format_horizontal_accuracy(accuracy, sizeof(accuracy), now);
  if (fresh_gga) {
    std::snprintf(quality, sizeof(quality), "%d", gnss_state.gga.quality);
    std::snprintf(satellites, sizeof(satellites), "%d", gnss_state.gga.satellites);
  }
  if (std::strcmp(accuracy, "---") != 0 && std::strcmp(accuracy, "N/A (BASE)") != 0)
    std::snprintf(accuracy_m, sizeof(accuracy_m), "%.6f", gnss_state.accuracy.horizontal_1drms_m);
  if (gnss_state.gga.received) std::snprintf(gga_age, sizeof(gga_age), "%lu", static_cast<unsigned long>(now-gnss_state.gga_ms));
  if (link.peer.age_valid && link.transport == instrument_status::Transport::WiFi)
    std::snprintf(peer_age, sizeof(peer_age), "%lu", static_cast<unsigned long>(link.peer.age_ms));
  if (verified_correction_age(now)!=UINT32_MAX) std::snprintf(correction_age, sizeof(correction_age), "%lu", static_cast<unsigned long>(verified_correction_age(now)));
  if (linked&&link.signal.valid) {
    std::snprintf(rssi, sizeof(rssi), "%d", link.signal.rssi_dbm);
    std::snprintf(signal, sizeof(signal), "\"%s\"", link_quality_label(link.signal.rssi_dbm));
  }
  uint16_t year; uint8_t month, day, hour, minute, second;
  if (local_time_utc_minus_6(now, gnss_state.time, year, month, day, hour, minute, second)) {
    std::snprintf(local, sizeof(local), "\"%04u-%02u-%02uT%02u:%02u:%02u\"", year, month, day, hour, minute, second);
    std::snprintf(utc, sizeof(utc), "\"%04u-%02u-%02uT%02u:%02u:%02uZ\"", gnss_state.time.year, gnss_state.time.month,
                  gnss_state.time.day, gnss_state.time.hour, gnss_state.time.minute, gnss_state.time.second);
  }
  const DashboardWarning warning = dashboard_warning(now);
  // All string fields are internal enums/fixed labels; no credentials or receiver text enter JSON.
  const int length = std::snprintf(output, capacity,
    "{\"api_version\":1,\"ui_version\":\"%s\",\"boot_id\":\"%08lx\",\"uptime_ms\":%lu,"
    "\"device\":{\"unit\":\"%c\",\"role\":\"%s\",\"profile\":\"%s\"},"
    "\"state\":{\"ready\":%s,\"gps_fixed\":%s,\"correction_link_connected\":%s},"
    "\"gnss\":{\"online\":%s,\"fix\":\"%s\",\"gga_quality\":%s,\"gga_age_ms\":%s,\"satellites\":%s,"
    "\"horizontal_uncertainty_m\":%s,\"horizontal_uncertainty_label\":\"%s\"},"
    "\"link\":{\"connected\":%s,\"transport\":\"%s\",\"quality\":%s,\"rssi_dbm\":%s,\"peer_age_ms\":%s,"
    "\"correction_age_ms\":%s,\"correction_state\":\"%s\",\"received_packets\":%lu,\"sequence_gaps\":%lu,\"invalid_packets\":%lu,\"rtcm_received_frames\":%lu},"
    "\"time\":{\"local\":%s,\"utc\":%s,\"utc_offset\":\"-06:00\"},"
    "\"phone_wifi\":{\"available\":%s,\"ssid\":\"%s\",\"address\":\"%s\",\"clients\":%u},"
    "\"warning\":{\"title\":\"%s\",\"detail\":\"%s\",\"severity\":\"%s\"}}",
    kWebUiVersion, static_cast<unsigned long>(web_boot_id()), static_cast<unsigned long>(now), board::kUnitLabel,
    is_base() ? "BASE" : "ROVER", gnss_state.profile_applied ? "VERIFIED" : gnss_state.profile_failed ? "FAILED" : "CONFIGURING",
    system_ready(now) ? "true" : "false", gps_required_fix() ? "true" : "false", linked ? "true" : "false",
    online ? "true" : "false", current_fix_label(now), quality, gga_age, satellites, accuracy_m, accuracy,
    linked ? "true" : "false", link.transport == instrument_status::Transport::Radio ? "SiK RADIO" : wifi_transport_label(), signal, rssi, peer_age, correction_age,correction_health_state(now),
    static_cast<unsigned long>(wifi_rx_packets), static_cast<unsigned long>(wifi_sequence_gaps),
    static_cast<unsigned long>(wifi_invalid_packets), static_cast<unsigned long>(correction_service::snapshot().forwarded), local, utc,
    rover_ap_ready() ? "true" : "false",rover_ap_ssid(),rover_ap_address(),rover_ap_clients(),
    warning.title, warning.detail, warning.color == colors::kError ? "error" : "warning");
  return length >= 0 && static_cast<size_t>(length) < capacity ? length : 0;
}

void service_web_status() {
  const uint32_t now = millis();
  if (now - last_web_status_ms < 250) return;
  last_web_status_ms = now;
  char json[kWebStatusCapacity];
  const size_t length = format_web_status(json, sizeof(json), now);
  publish_web_status(json, length, now, !is_base());
}





// Receiver service observations (R10a). The receiver owner calls these at the
// point in its own input handling where the fact became true, so the CSV
// session lines, the correction owner's reset and the link admission keep the
// order they had when the receiver state lived here.
void receiver_log_event(const char *event, const char *detail) {
  sd_log_event(event, detail);
}

void receiver_log_config(const char *profile, const char *notes) {
  diagnostic_log::config(profile, notes, log_time(millis()));
}

void receiver_reset() { correction_service::reset(); }

void receiver_forward_frame(const uint8_t *frame, size_t length,
                            uint16_t message_type) {
  const bool base = is_base();
  const bool admitted = base && gnss_service::snapshot().profile_applied &&
                        device_settings::config().base_rtcm && !diagnostic_busy() && !ota_paused();
  if (admitted) {
    if (link_service::radio_active()) link_service::radio_submit(frame, length, millis());
    else if (send_rtcm_packet(frame, length, message_type)) ++rtcm_wifi_tx_frames;
  }
}

// The fix-label text belongs to instrument_status; the receiver owner only
// decides when a changed label is announced.
const char *receiver_fix_label(int quality) { return fix_label(quality); }

const GnssObservers kGnssObservers{receiver_log_event, receiver_log_config,
                                   receiver_reset, receiver_forward_frame,
                                   receiver_fix_label};

// The receiver service's UART1 wiring: the pins and the baud are the board
// layer's (board_hardware.h), the buffered port is the receiver owner's.
void receiver_service_begin() {
  gnss_service::begin(GnssPort{board::kGnssRx, board::kGnssTx, board::kGnssBaud, 2048, 2048},
                      kGnssObservers, millis());
}

// The USB console owner (usb_console.cpp) owns the verb list, the wording and
// the refusals. What stays here is the one value copy of the facts it prints
// but cannot read - the applied role, the Wi-Fi direct peer and its counters,
// the brightness label the LCD also shows - plus the three names the host suite
// and setup()/loop() call.
ConsoleInputs console_inputs() {
  ConsoleInputs in;
  in.base = is_base();
  in.peer_known = wifi_peer_known;
  in.peer = wifi_peer;
  in.rtcm_wifi_tx_frames = rtcm_wifi_tx_frames;
  in.rtcm_wifi_rx_frames = rtcm_wifi_rx_frames;
  in.invalid_packets = wifi_invalid_packets;
  in.brightness_label = brightness_mode_label();
  return in;
}

void print_console_help() { usb_console::print_help(); }

void handle_usb_command(const char *command) {
  usb_console::handle(command, console_inputs());
}

void read_usb_console() { usb_console::service(console_inputs); }

void setup() {
  ota_boot_begin();
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("TopoRTK UM980 display demo");
  Serial.printf("Instrument unit: %c\n", board::kUnitLabel);
  load_config();
  load_base_settings();
  ui_module_begin();

  board_hardware::begin();
  setup_backlight(device_settings::config().brightness, millis());

  if (display_ready) {
    draw_static_screen();
    draw_dynamic_screen();
  }

  receiver_service_begin();
  correction_service_begin();
  print_console_help();
  start_wifi();
  setup_sd_logging();
  const DiagnosticSnapshot log = diagnostic_log::snapshot();
  survey_begin(log.ready && log.verified);
  diagnostic_begin();
  link_service::begin(!is_base(),millis());
  Serial.println("BOOT COMPLETE");
}

void service_survey();
void loop() {
  ota_service(millis(),!is_base(),gnss_service::snapshot().profile_running,display_ready&&!device_settings::snapshot().error&&survey_service_ready()&&web_service_ready());
  if(!ota_locked())read_usb_console();
  if(!ota_paused())gnss_service::service_input(millis());
  else gnss_service::discard_input(2048);
  if(!ota_locked()){gnss_service::service_startup(millis());gnss_service::service_profile(millis());}
  service_wifi();
  diagnostic_service(millis(),!is_base(),wifi_peer_known?wifi_peer:IPAddress(),gnss_service::snapshot().profile_running);
  correction_service::service_output(millis());
  service_survey();
  service_swipe_navigation();
  service_brightness(millis(), device_settings::config().brightness, gnss_service::snapshot().time);
  if(!ota_paused())service_sd_logging();
  service_web_status();
  {
    const uint32_t settings_now = millis();
    link_service::service_settings(settings_now, status_snapshot(settings_now).corrections.fresh);
  }

  const uint32_t render=millis();
  if (render - last_screen_ms >= 250) {
    last_screen_ms = render;
    draw_dynamic_screen();
  }
  delay(2);
}

void service_survey() {
  survey::BaseRequest request;
  if(survey_take_base(request)) {
    // The settings owner records the coordinate and tells the receiver owner
    // either way; the survey glue only reports what the receiver published.
    device_settings::apply_base(request.position,request.fixed,request.revision);
  }
  const uint32_t now=millis();survey::Fix f;f.now=now;f.rover=!is_base();
  const GnssSnapshot gnss_state=gnss_service::snapshot();
  const SettingsSnapshot settings=device_settings::snapshot();
  f.unit=board::kUnitLabel;f.boot_id=web_boot_id();f.reset_reason=esp_reset_reason();f.free_heap=ESP.getFreeHeap();f.min_heap=ESP.getMinFreeHeap();f.free_psram=ESP.getFreePsram();
  f.profile_ok=gnss_state.profile_applied&&!gnss_state.profile_failed&&!diagnostic_busy()&&!ota_paused();f.base_apply_pending=gnss_state.profile_running;f.base_apply_failed=settings.base_failed||gnss_state.profile_failed;f.base_revision=settings.base_revision;
  f.base_attempt_revision=settings.base_attempt_revision;f.base_fixed=settings.base_fixed;f.base_setting={settings.base_latitude,settings.base_longitude,settings.base_height};
  f.position=gnss_state.accuracy.position;f.position_valid=gnss_state.accuracy.position_valid;
  f.received=gnss_state.accuracy.received_ms;f.epoch=gnss_state.accuracy.epoch;
  f.fixed=gnss_state.accuracy.rtk_fixed && gnss_state.gga.received && gnss_state.gga.quality==4 && now-gnss_state.gga_ms<1500;
  f.hacc=gnss_state.accuracy.horizontal_1drms_m;f.vacc=gnss_state.accuracy.vertical_sigma_m;
  f.linked=correction_link_connected(now);f.correction_age=verified_correction_age(now);
  f.position_valid=f.position_valid && gnss_state.accuracy.solution_station==gnss_state.station;
  f.reference_valid=gnss_state.reference_ms!=0;f.reference=gnss_state.reference;f.station=gnss_state.station;
  f.reference_age=gnss_state.reference_ms?now-gnss_state.reference_ms:UINT32_MAX;f.satellites=gnss_state.gga.satellites;
  if(fresh_gnss_time(now))std::snprintf(f.utc,sizeof(f.utc),"%04u-%02u-%02uT%02u:%02u:%02uZ",gnss_state.time.year,gnss_state.time.month,gnss_state.time.day,gnss_state.time.hour,gnss_state.time.minute,gnss_state.time.second);
  survey_update(f);
}

bool peer_update_quality_ready(){return is_base()||(!ota_paused()&&verified_correction_age(millis())<=3000&&gps_required_fix());}
void ota_reset_corrections(){
  correction_service::reset();gnss_service::reset_input_state();
  link_service::clear_pending();
}

