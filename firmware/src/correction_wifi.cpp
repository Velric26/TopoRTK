// Wi-Fi correction channel (R10a slice 6). The envelope policy moved here
// verbatim from main.cpp; only the composition root's helpers it used to call
// became the owning service's published functions (network_service's label,
// station state and clients, the device settings' applied role).

#include "correction_wifi.h"
#include <Arduino.h>
#include <cstring>
#include "board_hardware.h"
#include "correction_service.h"
#include "correction_transport.h"
#include "debug_service.h"
#include "device_settings.h"
#include "gnss_service.h"
#include "link_service.h"
#include "network_service.h"
#include "wifi_transport.h"

// The applied role, the same fact the root's is_base() reads.
static bool is_base() { return device_settings::config().role == DeviceRole::kBase; }

bool wifi_peer_known = false;
IPAddress wifi_peer;
int16_t wifi_peer_rssi_dbm = 0;
uint32_t wifi_last_peer_ms = 0;
static uint32_t wifi_tx_sequence = 0;
uint32_t wifi_tx_packets = 0;
uint32_t wifi_rx_packets = 0;
uint32_t wifi_invalid_packets = 0;
uint32_t wifi_sequence_gaps = 0;
static uint32_t wifi_last_sequence = 0;
static bool wifi_have_sequence = false;
static uint32_t last_wifi_send_ms = 0;
static uint32_t last_wifi_status_ms = 0;
static uint32_t last_wifi_connect_attempt_ms = 0;
static uint32_t rtcm_wifi_sequence = 0;
static uint32_t wifi_session_id = 0, wifi_session_peer = 0, wifi_rtcm_highest = 0;
uint32_t rtcm_wifi_tx_frames = 0;
uint32_t rtcm_wifi_rx_frames = 0;

uint32_t fnv1a(const uint8_t *bytes, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

static uint32_t packet_checksum(WiFiTestPacket packet) {
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

static bool valid_rtcm_frame(const uint8_t *frame, size_t length) {
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

static uint16_t rtcm_message_type(const uint8_t *frame, size_t length) {
  if (length < 8) return 0;
  return (static_cast<uint16_t>(frame[3]) << 4) | (frame[4] >> 4);
}

static bool valid_wifi_packet(WiFiTestPacket packet) {
  if (packet.magic != network::kMagic ||
      packet.version != network::kVersion ||
      packet.packet_size != sizeof(packet) ||
      std::strncmp(packet.marker, network::kMarker, sizeof(packet.marker)) != 0) {
    return false;
  }
  const uint32_t received_checksum = packet.checksum;
  return received_checksum == packet_checksum(packet);
}

static void sync_wifi_session(const pair_session::Snapshot &state) {
  if (state.session == wifi_session_id && state.peer_boot == wifi_session_peer) return;
  // Only transport-derived counters reset here; the legacy peer address/age
  // stay with the hello traffic that owns them, and admission reads the pair.
  wifi_session_id = state.session; wifi_session_peer = state.peer_boot;
  wifi_rtcm_highest = rtcm_wifi_sequence = wifi_tx_sequence = wifi_last_sequence = 0;
  wifi_have_sequence = false;
}
template<class Packet> static bool current_wifi_identity(const Packet &packet, uint32_t now) {
  const auto state = link_service::snapshot(now);
  sync_wifi_session(state);
  return state.transport == pair_session::Transport::WiFi && state.connected &&
         packet.session == state.session && packet.session >= 1000000 &&
         packet.sender_boot == state.peer_boot && packet.receiver_boot == state.local_boot &&
         packet.sender_unit == 3 - TOPORTK_UNIT_ID && packet.sender_role == (is_base() ? 1 : 0) &&
         !packet.identity_reserved && packet.sequence;
}
template<class Packet> static void set_wifi_identity(Packet &packet, const pair_session::Snapshot &state) {
  packet.sender_boot = state.local_boot; packet.receiver_boot = state.peer_boot;
  packet.session = state.session; packet.sender_unit = TOPORTK_UNIT_ID; packet.sender_role = !is_base();
}

static bool legacy_wifi_packet(uint8_t *bytes, size_t size) {
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

static bool send_wifi_packet(const IPAddress &destination, uint8_t type,
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

bool correction_wifi::send_rtcm_packet(const uint8_t *frame, size_t frame_length,
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
  if (!wifi_transport::send(wifi_transport::Channel::Corrections, link_service::wifi_peer(),
                            packet, header.packet_size)) {
    return false;
  }
  ++rtcm_wifi_tx_frames;
  return true;
}

bool correction_wifi::handle_rtcm_packet(uint8_t *packet, size_t packet_length) {
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

void correction_wifi::reset() {
  wifi_peer_known = false;
  wifi_peer = IPAddress();
  wifi_last_peer_ms = 0;
  wifi_peer_rssi_dbm = 0;
  wifi_tx_packets = wifi_rx_packets = 0;
  wifi_sequence_gaps = wifi_invalid_packets = 0;
  last_wifi_send_ms = 0;
  last_wifi_connect_attempt_ms = 0;
}

static void receive_wifi_packets() {
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
    if (!is_base() && !network_service::on_station_subnet(sender)) continue;
    if (legacy_wifi_packet(packet_bytes, size_t(packet_length))) {
      link_service::note_incompatible(millis()); ++wifi_invalid_packets; continue;
    }
    if (packet_length >= static_cast<int>(sizeof(WiFiRtcmHeader)) &&
        packet_bytes[5] == network::kRtcmPacket) {
      if(link_service::radio_active())continue; // Never feed parallel Wi-Fi copies.
      if(!(sender==link_service::wifi_peer())){++wifi_invalid_packets;continue;}
      if (!correction_wifi::handle_rtcm_packet(packet_bytes, packet_length)) {
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

void correction_wifi::service() {
  const uint32_t now = millis();
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
             wifi_tx_sequence != UINT32_MAX && !is_base() && network_service::station_connected() &&
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
                    network_service::label(), network_service::station_connected() ? "UP" : "DOWN",
                    network_service::local_router() ? 0 : network_service::clients(),
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
                    network_service::label(), network_service::station_connected() ? "UP" : "DOWN",
                    network_service::station_connected() ? network_service::rssi() : 0,
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
