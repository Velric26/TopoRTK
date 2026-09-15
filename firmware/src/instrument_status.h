#pragma once
// Central transport-aware status interpretation (R4). One fixed-size snapshot
// with four separate facets — network/peer, corrections, GNSS, readiness —
// consumed by the LCD frame, the web JSON and the CSV logger so the surfaces
// can never disagree on transport, freshness or readiness.
//
// Policy moved here verbatim from main.cpp (thresholds unchanged):
//   - Wi-Fi peer freshness: peer contact within 3000 ms; Base direct link
//     additionally requires an associated client; otherwise a station link.
//   - Radio link: Bridge::linked semantics (Rover-side reception within its
//     window; a Base's transmit-only bridge never reports receiver freshness).
//   - Correction freshness: Rover age <= 3000 ms; Base = its RTCM output flag.
//   - GNSS required fix: receiver profile applied, role-matched GGA fresh
//     within 3000 ms, quality 7 for Base / 4 for Rover.
//   - System ready: device available, UART online, version handshake, profile
//     applied, link connected, required fix, fresh corrections.
// Naming (e.g. "SiK RADIO", "LOCAL ROUTER") stays with the network/link
// owners; this module classifies and composes, it does not label.
//
// All inputs are passed explicitly by the composition root; no globals here.
#include <cstdint>

namespace instrument_status {

enum class Transport : uint8_t { None, WiFi, Radio };

struct Peer {
  bool connected = false;
  bool age_valid = false;      // false = unavailable for this transport
  uint32_t age_ms = 0;
};

struct Signal {
  bool valid = false;          // false = unknown for this transport (never a fabricated value)
  int16_t rssi_dbm = 0;
};

struct Corrections {
  bool age_valid = false;
  uint32_t age_ms = 0;
  bool fresh = false;          // Rover: age within limit; Base: its RTCM output flag
};

struct Gnss {
  bool online = false;         // UART active and version handshake complete
  int fix_quality = -1;        // -1 = no GGA
  bool gga_stale = false;
  bool required_fix = false;
  bool time_valid = false;
};

struct Readiness {
  bool system_ready = false;
  bool profile_applied = false;
};

struct Inputs {
  Transport transport = Transport::None;

  // Wi-Fi peer facet (transport WiFi).
  bool wifi_peer_known = false;
  bool wifi_peer_age_valid = false;
  uint32_t wifi_peer_age_ms = 0;
  bool base_direct_ap = false;        // Base in Direct Link mode
  bool base_direct_client = false;    // an associated client exists
  bool wifi_station_up = false;

  // Radio facet.
  bool radio_active = false;
  bool radio_linked = false;

  // Signal inputs.
  bool wifi_station_rssi_valid = false;  // Rover with an associated station
  int16_t wifi_station_rssi_dbm = 0;
  bool wifi_peer_report_valid = false;   // Base: RSSI reported by the Rover hello
  int16_t wifi_peer_report_dbm = 0;

  // Corrections freshness (receiver-side, transport-agnostic).
  bool correction_age_valid = false;
  uint32_t correction_age_ms = 0;
  bool base_rtcm_output = true;

  // GNSS facet.
  bool gga_received = false;
  bool gga_age_ok = false;            // GGA younger than the 3000 ms limit
  int fix_quality = -1;
  bool receiver_role_matches = false; // acknowledged receiver role == selected role
  bool base_role = false;             // selected role is Base
  bool time_valid = false;
  bool uart_online = false;
  bool version_ok = false;

  // Readiness inputs.
  bool profile_applied = false;
  bool device_available = true;       // not locked/paused by OTA or diagnostics
};

struct Status {
  Transport transport = Transport::None;
  Peer peer;
  Signal signal;
  Corrections corrections;
  Gnss gnss;
  Readiness readiness;
  bool link_connected = false;
};

inline Status evaluate(const Inputs &in) {
  Status s;
  s.transport = in.transport;

  // Corrections freshness (receiver-side; identical for both transports).
  s.corrections.age_valid = in.correction_age_valid;
  s.corrections.age_ms = in.correction_age_ms;
  s.corrections.fresh = in.base_role ? in.base_rtcm_output
                                     : (in.correction_age_valid && in.correction_age_ms <= 3000);

  // GNSS facet.
  s.gnss.online = in.uart_online && in.version_ok;
  s.gnss.fix_quality = in.fix_quality;
  s.gnss.gga_stale = in.gga_received && !in.gga_age_ok;
  s.gnss.required_fix = in.profile_applied && in.gga_age_ok &&
                        in.receiver_role_matches &&
                        (in.base_role ? in.fix_quality == 7 : in.fix_quality == 4);
  s.gnss.time_valid = in.time_valid;

  // Transport-specific peer and signal facets.
  if (in.transport == Transport::Radio) {
    s.peer.connected = in.radio_linked;      // Rover-side reception; Base stays disconnected
    s.peer.age_valid = false;
    s.signal.valid = false;                  // radio reports no RSSI: unknown, never Wi-Fi dBm
  } else {
    s.peer.age_valid = in.wifi_peer_age_valid;
    s.peer.age_ms = in.wifi_peer_age_ms;
    s.peer.connected = in.wifi_peer_age_valid && in.wifi_peer_age_ms <= 3000 &&
                       (in.base_direct_ap ? (in.wifi_peer_known && in.base_direct_client)
                                          : in.wifi_station_up);
    if (!in.base_role && in.wifi_station_up) {
      s.signal.valid = true;
      s.signal.rssi_dbm = in.wifi_station_rssi_dbm;
    } else if (in.base_direct_ap && in.wifi_peer_known) {
      s.signal.valid = true;                 // peer-reported RSSI from the Rover hello
      s.signal.rssi_dbm = in.wifi_peer_report_dbm;
    }
  }

  // Transport-aware link state (the correction_link_connected semantics).
  s.link_connected = in.transport == Transport::Radio ? s.peer.connected : s.peer.connected;

  // Readiness (thresholds identical to the previous system_ready).
  s.readiness.profile_applied = in.profile_applied;
  s.readiness.system_ready = in.device_available && in.uart_online && in.version_ok &&
                             in.profile_applied && s.link_connected &&
                             s.gnss.required_fix && s.corrections.fresh;
  return s;
}

}  // namespace instrument_status
