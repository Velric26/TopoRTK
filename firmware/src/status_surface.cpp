// Web status surface (R10a slice 6). The JSON formatting moved here verbatim
// from main.cpp: the same fields, the same order, the same nullable values.
// Only the sources of the derived text changed - the status facets and the
// accuracy/fix labels are read from their owners with the one Inputs value the
// composition root composes.

#include "status_surface.h"
#include <Arduino.h>
#include <cstring>
#include "board_hardware.h"
#include "correction_service.h"
#include "correction_wifi.h"
#include "gnss_service.h"
#include "network_service.h"
#include "rover_ap.h"
#include "ui_display.h"
#include "ui_presenter.h"
#include "ui_theme.h"
#include "web_http.h"

// The loop's publication window: the root's last_web_status_ms moved with the
// path that owns it.
static uint32_t last_web_status_ms = 0;

bool status_surface::due(uint32_t now_ms) {
  if (now_ms - last_web_status_ms < 250) return false;
  last_web_status_ms = now_ms;
  return true;
}

size_t status_surface::format(char *output, size_t capacity,
                              const instrument_status::Inputs &inputs) {
  const uint32_t now = inputs.now_ms;
  const bool base = inputs.role_base;
  const instrument_status::Status link =
      instrument_status::status_snapshot(inputs, correction_service::health());
  const GnssSnapshot gnss_state = gnss_service::snapshot();
  const bool linked = link.link_connected;
  const bool fresh_gga = gnss_state.gga.received && now - gnss_state.gga_ms <= 3000;
  const bool online = gnss_state.version_ok && gnss_state.uart_seen &&
                      now - gnss_state.last_rx_ms < 3000;
  char accuracy[32], accuracy_m[32] = "null", quality[16] = "null", satellites[16] = "null";
  char gga_age[16] = "null", peer_age[16] = "null", correction_age[16] = "null", rssi[16] = "null";
  char signal[32] = "null", local[32] = "null", utc[32] = "null";
  ui_presenter::horizontal_accuracy(accuracy, sizeof(accuracy), now);
  if (fresh_gga) {
    std::snprintf(quality, sizeof(quality), "%d", gnss_state.gga.quality);
    std::snprintf(satellites, sizeof(satellites), "%d", gnss_state.gga.satellites);
  }
  if (std::strcmp(accuracy, "---") != 0 && std::strcmp(accuracy, "N/A (BASE)") != 0)
    std::snprintf(accuracy_m, sizeof(accuracy_m), "%.6f", gnss_state.accuracy.horizontal_1drms_m);
  if (gnss_state.gga.received) std::snprintf(gga_age, sizeof(gga_age), "%lu", static_cast<unsigned long>(now-gnss_state.gga_ms));
  if (link.peer.age_valid && link.transport == instrument_status::Transport::WiFi)
    std::snprintf(peer_age, sizeof(peer_age), "%lu", static_cast<unsigned long>(link.peer.age_ms));
  const uint32_t verified_age = instrument_status::verified_correction_age(
      inputs.solution, now, correction_service::health());
  if (verified_age != UINT32_MAX) std::snprintf(correction_age, sizeof(correction_age), "%lu", static_cast<unsigned long>(verified_age));
  if (linked&&link.signal.valid) {
    std::snprintf(rssi, sizeof(rssi), "%d", link.signal.rssi_dbm);
    std::snprintf(signal, sizeof(signal), "\"%s\"", instrument_status::link_quality_label(link.signal.rssi_dbm));
  }
  uint16_t year; uint8_t month, day, hour, minute, second;
  if (local_time_utc_minus_6(now, gnss_state.time, year, month, day, hour, minute, second)) {
    std::snprintf(local, sizeof(local), "\"%04u-%02u-%02uT%02u:%02u:%02u\"", year, month, day, hour, minute, second);
    std::snprintf(utc, sizeof(utc), "\"%04u-%02u-%02uT%02u:%02u:%02uZ\"", gnss_state.time.year, gnss_state.time.month,
                  gnss_state.time.day, gnss_state.time.hour, gnss_state.time.minute, gnss_state.time.second);
  }
  const DashboardWarning warning = instrument_status::dashboard_warning(inputs, correction_service::health());
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
    base ? "BASE" : "ROVER", gnss_state.profile_applied ? "VERIFIED" : gnss_state.profile_failed ? "FAILED" : "CONFIGURING",
    instrument_status::system_ready(link) ? "true" : "false", instrument_status::gps_required_fix(link) ? "true" : "false", linked ? "true" : "false",
    online ? "true" : "false", instrument_status::current_fix_label(gnss_state.role, gnss_state.gga.received,
                                                                   gnss_state.gga_ms, gnss_state.gga.quality, now),
    quality, gga_age, satellites, accuracy_m, accuracy,
    linked ? "true" : "false", link.transport == instrument_status::Transport::Radio ? "SiK RADIO" : network_service::label(), signal, rssi, peer_age, correction_age,
    instrument_status::correction_health_state(inputs.solution, now, correction_service::health()),
    static_cast<unsigned long>(wifi_rx_packets), static_cast<unsigned long>(wifi_sequence_gaps),
    static_cast<unsigned long>(wifi_invalid_packets), static_cast<unsigned long>(correction_service::snapshot().forwarded), local, utc,
    rover_ap_ready() ? "true" : "false",rover_ap_ssid(),rover_ap_address(),rover_ap_clients(),
    warning.title, warning.detail, warning.color == colors::kError ? "error" : "warning");
  return length >= 0 && static_cast<size_t>(length) < capacity ? length : 0;
}

void status_surface::publish(const instrument_status::Inputs &inputs) {
  char json[kWebStatusCapacity];
  const size_t length = format(json, sizeof(json), inputs);
  publish_web_status(json, length, inputs.now_ms, !inputs.role_base);
}
