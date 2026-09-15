// Status/readiness policy extracted from main.cpp (R10a slice 1). Pure
// classification and composition: every input arrives in Inputs plus the
// correction owner's Health, and no state is owned here.
#include "instrument_status.h"
#include "ui_theme.h"

#include <cstring>

namespace {

bool role_is_base(const char *receiver_role) {
  return std::strcmp(receiver_role, "BASE") == 0;
}

}  // namespace

namespace instrument_status {

bool fresh_gnss_time(bool time_valid, uint32_t received_ms, uint32_t now_ms) {
  return time_valid && received_ms > 0 && now_ms - received_ms < 3000;
}

uint32_t verified_correction_age(const Solution &solution, uint32_t now_ms,
                                 const correction::Health &health) {
  return health.effective_age(now_ms, solution.received && solution.position_valid,
                              solution.received_ms, solution.differential_age_ms,
                              solution.station);
}

const char *correction_health_state(const Solution &solution, uint32_t now_ms,
                                    const correction::Health &health) {
  return health.state(now_ms, solution.received && solution.position_valid,
                      solution.received_ms, solution.differential_age_ms,
                      solution.station);
}

Status status_snapshot(const Inputs &in, const correction::Health &health) {
  Status s;
  s.transport = in.transport;

  // Corrections freshness (receiver-side; identical for both transports).
  const uint32_t correction_age = verified_correction_age(in.solution, in.now_ms, health);
  s.corrections.age_valid = correction_age != UINT32_MAX;
  s.corrections.age_ms = correction_age;
  s.corrections.fresh = in.role_base ? in.base_rtcm_output
                                     : (s.corrections.age_valid && correction_age <= 3000);

  // GNSS facet.
  const bool uart_online = in.uart_seen && in.now_ms - in.last_rx_ms < 3000;
  const bool gga_fresh = in.gga_received && in.now_ms - in.gga_ms <= 3000;
  const bool receiver_role_matches =
      std::strcmp(in.receiver_role, in.role_base ? "BASE" : "ROVER") == 0;
  s.gnss.online = uart_online && in.version_ok;
  s.gnss.fix_quality = in.gga_received ? in.gga_quality : -1;
  s.gnss.gga_stale = in.gga_received && !gga_fresh;
  s.gnss.required_fix = in.profile_applied && gga_fresh && receiver_role_matches &&
                        (in.role_base ? in.gga_quality == 7 : in.gga_quality == 4);
  s.gnss.time_valid = fresh_gnss_time(in.time_valid, in.time_received_ms, in.now_ms);

  s.peer.connected = in.peer_connected;
  s.peer.age_valid = in.peer_age_ms != UINT32_MAX;
  s.peer.age_ms = in.peer_age_ms;
  if (in.transport == Transport::WiFi) {
    if (!in.role_base && in.wifi_station_rssi_valid) {
      s.signal.valid = true;
      s.signal.rssi_dbm = in.wifi_station_rssi_dbm;
    } else if (in.role_base && in.wifi_peer_report_valid) {
      s.signal.valid = true;
      s.signal.rssi_dbm = in.wifi_peer_report_dbm;
    }
  }
  s.link_connected = s.peer.connected;

  // Readiness (thresholds identical to the previous system_ready).
  s.readiness.profile_applied = in.profile_applied;
  s.readiness.system_ready = in.device_available && uart_online && in.version_ok &&
                             in.profile_applied && s.link_connected &&
                             s.gnss.required_fix && s.corrections.fresh;
  return s;
}

bool gps_required_fix(const Status &s) { return s.gnss.required_fix; }
bool correction_link_connected(const Status &s) { return s.link_connected; }
bool fresh_rover_corrections(const Status &s) { return s.corrections.fresh; }
bool system_ready(const Status &s) { return s.readiness.system_ready; }
int16_t current_link_rssi(const Status &s) { return s.signal.rssi_dbm; }
bool current_link_rssi_valid(const Status &s) { return s.signal.valid; }

const char *fix_label(const char *receiver_role, int quality) {
  if (role_is_base(receiver_role)) {
    if (quality == 0) return "BASE WAIT";
    if (quality == 1 || quality == 2) return "BASE SURVEY";
    if (quality == 7) return "BASE LOCKED";
  }

  switch (quality) {
    case 0: return "NO FIX";
    case 1: return "GPS FIX";
    case 2: return "DGPS";
    case 4: return "RTK FIXED";
    case 5: return "RTK FLOAT";
    case 6: return "ESTIMATED";
    case 7: return "MANUAL";
    case 8: return "SIMULATION";
    default: return "UNKNOWN";
  }
}

uint16_t fix_color(const char *receiver_role, int quality) {
  if (role_is_base(receiver_role) && quality == 7) {
    return colors::kReadyText;
  }

  switch (quality) {
    case 4: return colors::kReadyText;
    case 5: return colors::kSecondary;
    case 1:
    case 2: return colors::kSecondary;
    default: return colors::kError;
  }
}

const char *current_fix_label(const char *receiver_role, bool gga_received,
                              uint32_t gga_ms, int quality, uint32_t now_ms) {
  if (!gga_received) return "NO FIX";
  if (now_ms - gga_ms > 3000) return "GNSS STALE";
  return fix_label(receiver_role, quality);
}

const char *link_quality_label(int16_t rssi) {
  if (rssi >= -60) return "EXCELLENT";
  if (rssi >= -70) return "GOOD";
  if (rssi >= -80) return "FAIR";
  return "WEAK";
}

uint16_t link_quality_color(int16_t rssi) {
  if (rssi >= -70) return colors::kReadyText;
  if (rssi >= -80) return colors::kSecondary;
  return colors::kError;
}

DashboardWarning dashboard_warning(const Inputs &in, const correction::Health &health) {
  if (in.ota_paused) return {"FIRMWARE UPDATE", "Local operations paused. Keep power on.", colors::kPrimary};
  if (!std::strcmp(in.peer_reason, "recovery_required")) return {"LINK RECOVERY", "Select matching links locally.", colors::kPrimary};
  if (!std::strcmp(in.peer_reason, "same_role")) return {"PAIR ROLE ERROR", "Select one Base and one Rover.", colors::kPrimary};
  if (!std::strcmp(in.peer_reason, "protocol_incompatible")) return {"LINK VERSION", "Update both instruments.", colors::kPrimary};
  if (in.peer_notice[0]) return {"PAIRED UNIT", in.peer_notice, colors::kPrimary};

  const Status status = status_snapshot(in, health);
  const bool receiver_role_base = role_is_base(in.receiver_role);
  const char *alert = "CHECK FIX QUALITY";
  const char *detail = "Wait for a stable required fix.";
  uint16_t alert_color = colors::kPrimary;
  if (in.config_error) {
    alert = "SETTINGS NOT SAVED";
    detail = "Open Setup and retry saving.";
  } else if (in.profile_failed) {
    alert = "SETUP FAILED";
    detail = "Check GNSS cable. Retry in Setup.";
  } else if (!status.gnss.online) {
    alert = "UM980 OFFLINE";
    detail = "Check receiver power and cable.";
    alert_color = colors::kError;
  } else if (!status.readiness.profile_applied) {
    alert = "CONFIGURING UM980";
    detail = "Applying your saved role.";
  } else if (status.gnss.gga_stale) {
    alert = "GNSS DATA STALE";
    detail = "Check receiver power and cable.";
    alert_color = colors::kError;
  } else if (in.role_base && !in.base_rtcm_output) {
    alert = "CORRECTION OUTPUT OFF";
    detail = "Enable RTCM from the USB console.";
  } else if (!status.link_connected && !std::strcmp(in.peer_reason, "negotiating")) {
    alert = "PAIRING"; detail = "Waiting for the selected link.";
  } else if (!status.link_connected) {
    alert = in.role_base ? "ROVER LINK DOWN" : "BASE LINK DOWN";
    detail = in.role_base ? "Set the other unit to Rover." : "Power on the base; check range.";
    alert_color = colors::kError;
  } else if (!in.role_base && !status.corrections.fresh) {
    alert = "NO FRESH CORRECTIONS";
    detail = "Check base fix and RTCM output.";
  } else if (receiver_role_base && (in.gga_quality == 1 || in.gga_quality == 2)) {
    alert = "BASE SURVEYING";
    detail = "Keep antenna still with clear sky.";
  } else if (receiver_role_base && in.gga_quality == 7) {
    alert = "BASE LOCKED";
    detail = "Temporary base. Control unverified.";
  } else if (in.gga_quality == 4) {
    alert = "RTK FIXED";
    detail = "Verify control before surveying.";
  } else if (in.gga_quality == 5) {
    alert = "RTK FLOAT";
    detail = "Wait for RTK fixed; check sky view.";
  } else if (in.gga_quality == 0) {
    alert = "NO GNSS FIX";
    detail = "Move antenna to a clear sky view.";
  }
  return {alert, detail, alert_color};
}

}  // namespace instrument_status
