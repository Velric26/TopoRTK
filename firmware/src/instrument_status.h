#pragma once
// Central transport-aware status interpretation (R4). Owns the readiness,
// freshness, fix-label, link-quality and dashboard-warning policy; the
// composition root fills one Inputs per evaluation, so the LCD frame, the web
// JSON, the CSV logger and the survey service can never disagree on transport,
// freshness or readiness.
//
// Pair connectivity is current-boot bidirectional proof (R5), supplied by the
// sole link owner. It is independent of correction observations or GNSS time.
// Thresholds (unchanged since R4):
//   - Correction freshness: Rover age <= 3000 ms; Base = its RTCM output flag.
//     The receiver-side age itself comes from the correction owner's Health,
//     which keeps observation state; this module only binds and compares it.
//   - GNSS required fix: receiver profile applied, role-matched GGA fresh
//     within 3000 ms, quality 7 for Base / 4 for Rover.
//   - Receiver online: at least one byte within 3000 ms, then version handshake.
//   - System ready: device available, receiver online, profile applied, link
//     connected, required fix, fresh corrections.
// Naming (e.g. "SiK RADIO", "LOCAL ROUTER") stays with the network/link
// owners; this module classifies and composes, it does not label.
//
// Ownership (R10a): the policy lives here; the composition root still supplies
// receiver facts, the link snapshot, the correction owner's Health instance,
// the profile/config flags and the OTA/diagnostic state in one Inputs value.
// No globals, no callbacks into main.cpp and no Arduino/NVS/socket dependency,
// so the host doubles build it unchanged and the receiver/correction services
// can fill the same Inputs once they own those facts.

#include <cstdint>
#include "correction_health.h"

// Dashboard warning selection output (title/detail/color) for the LCD warning
// card and the web status JSON.
struct DashboardWarning {
  const char *title;
  const char *detail;
  uint16_t color;
};

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

struct Status {
  Transport transport = Transport::None;
  Peer peer;
  Signal signal;
  Corrections corrections;
  Gnss gnss;
  Readiness readiness;
  bool link_connected = false;
};

// Receiver-side solution facts the correction owner's age policy reads.
struct Solution {
  bool received = false;
  bool position_valid = false;
  uint32_t received_ms = 0;
  uint32_t differential_age_ms = UINT32_MAX;
  uint16_t station = 0;
};

// Everything one evaluation reads, filled by the composition root.
struct Inputs {
  uint32_t now_ms = 0;

  // Selected-medium, boot-proven peer facet (link owner).
  Transport transport = Transport::None;
  bool peer_connected = false;
  uint32_t peer_age_ms = UINT32_MAX;   // UINT32_MAX = unavailable for this transport
  const char *peer_reason = "";        // pair_session reason text, drives the link warning

  // Signal inputs.
  bool wifi_station_rssi_valid = false;  // Rover with an associated station
  int16_t wifi_station_rssi_dbm = 0;
  bool wifi_peer_report_valid = false;   // Base: RSSI reported by the Rover hello
  int16_t wifi_peer_report_dbm = 0;

  // Receiver facts (GNSS owner).
  bool uart_seen = false;                // at least one receiver byte this boot
  uint32_t last_rx_ms = 0;
  bool version_ok = false;
  bool profile_applied = false;
  bool role_base = false;                // selected role
  const char *receiver_role = "UNKNOWN"; // acknowledged receiver role text
  bool gga_received = false;
  int gga_quality = -1;                  // as received; -1 = no GGA
  uint32_t gga_ms = 0;
  bool time_valid = false;
  uint32_t time_received_ms = 0;

  // Correction facet (correction owner).
  Solution solution;
  bool base_rtcm_output = true;          // Base: its RTCM output flag

  // Composition-root state.
  bool device_available = true;          // not locked/paused by OTA or diagnostics
  bool ota_paused = false;
  bool config_error = false;
  bool profile_failed = false;
  const char *peer_notice = "";          // peer_update notice text; empty when none
};

// Production: one Status from the composition root's inputs plus the correction
// owner's observation state.
Status status_snapshot(const Inputs &in, const correction::Health &health);

// Facet accessors for consumers that already hold a Status.
bool gps_required_fix(const Status &s);
bool correction_link_connected(const Status &s);
bool fresh_rover_corrections(const Status &s);
bool system_ready(const Status &s);
int16_t current_link_rssi(const Status &s);
bool current_link_rssi_valid(const Status &s);

// Receiver bindings used without building a whole Inputs.
bool fresh_gnss_time(bool time_valid, uint32_t received_ms, uint32_t now_ms);
uint32_t verified_correction_age(const Solution &solution, uint32_t now_ms,
                                 const correction::Health &health);
const char *correction_health_state(const Solution &solution, uint32_t now_ms,
                                    const correction::Health &health);
const char *fix_label(const char *receiver_role, int quality);
uint16_t fix_color(const char *receiver_role, int quality);
const char *current_fix_label(const char *receiver_role, bool gga_received,
                              uint32_t gga_ms, int quality, uint32_t now_ms);
const char *link_quality_label(int16_t rssi);
uint16_t link_quality_color(int16_t rssi);

// Warning selection returns only fixed labels or the composition root's notice.
DashboardWarning dashboard_warning(const Inputs &in, const correction::Health &health);

}  // namespace instrument_status
