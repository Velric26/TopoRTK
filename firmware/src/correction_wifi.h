#pragma once
// Wi-Fi correction channel (R10a slice 6), extracted from main.cpp. Sole owner
// of the Wi-Fi correction envelope policy end to end: the control and RTCM
// packet identity fields with their checksums, session/boot/unit/role
// admission, the session-scoped sequence replay window of each direction,
// legacy version-2 detection, the Base transmit path, the Rover receive path,
// and the counters, peer record and 2 s status line the surfaces publish.
//
// Every wire byte, magic, version, marker, CRC24Q frame check, interval and
// admission rule is the value main.cpp used. The datagrams stay with
// wifi_transport, the pair session with link_service and the choice of medium
// with the composition root; this module only decides what may enter or leave
// the Wi-Fi correction medium, and asks network_service for the radio facts it
// reports.

#include <cstddef>
#include <cstdint>
#if __has_include(<IPAddress.h>)
#include <IPAddress.h>
#else
#include "host_hardware.h"
#endif

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

// Wire helpers. The host suite builds its own envelopes with the same two the
// send and receive paths use, so a test envelope is the firmware's own bytes.
uint32_t fnv1a(const uint8_t *bytes, size_t length);
uint32_t crc24q(const uint8_t *bytes, size_t length);

// The learned peer record, the packet counters and the admitted RTCM frame
// counters: the module's own state, declared where its owner is. The status
// surfaces, the console, the CSV row, the loop's diagnostic report and the host
// suite all read them here.
extern bool wifi_peer_known;
extern IPAddress wifi_peer;
extern int16_t wifi_peer_rssi_dbm;
extern uint32_t wifi_last_peer_ms;
extern uint32_t wifi_tx_packets;
extern uint32_t wifi_rx_packets;
extern uint32_t wifi_invalid_packets;
extern uint32_t wifi_sequence_gaps;
extern uint32_t rtcm_wifi_tx_frames;
extern uint32_t rtcm_wifi_rx_frames;

namespace correction_wifi {

// The loop step: drains the correction channel, keeps the Base test-packet and
// Rover hello cadence, and prints the 2 s Wi-Fi status line.
void service();

// A network restart forgets the learned peer, the cadence and the packet
// counters. It is not a radio/session/reference reset: nothing here touches the
// pair, the receiver or the correction queue.
void reset();

// Rover receive path for one complete datagram. True only when an admitted RTCM
// observation reached the correction owner; every refusal is counted by the
// caller exactly as before.
bool handle_rtcm_packet(uint8_t *packet, size_t packet_length);

// Base transmit path for one receiver frame: binds the pair identity, seals the
// envelope and sends it on the correction channel, counting the frame only when
// it left. False = not sent.
bool send_rtcm_packet(const uint8_t *frame, size_t frame_length,
                      uint16_t message_type);

}  // namespace correction_wifi
