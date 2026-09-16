// Presentation mapping (R10a slice 6). The frame builder moved here verbatim
// from main.cpp: the page header, the dashboard cards, the GPS, Link, Setup and
// Link-mode rows keep their strings, colors and order. What changed is only
// where the facts come from: the status facets arrive in the one Inputs value
// the composition root composes, and the network labels are read from the
// network owner instead of from the root's one-line wrappers.

#include "ui_presenter.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>
#include "board_hardware.h"
#include "correction_service.h"
#include "correction_wifi.h"
#include "debug_service.h"
#include "device_settings.h"
#include "gnss_service.h"
#include "link_diagnostic.h"
#include "link_service.h"
#include "network_service.h"
#include "ota_service.h"
#include "rover_ap.h"
#include "ui_display.h"
#include "ui_theme.h"

// The Link-mode page's refusal line: the gesture dispatch sets it, the page
// renders it, and navigation clears it. It belongs to the visit, not the link.
static char link_hint[96] = {};

static void to_upper_ascii(char *text) {
  for (char *p = text; *p; ++p) {
    if (*p >= 'a' && *p <= 'z') *p = static_cast<char>(*p - 32);
  }
}

void ui_presenter::set_link_hint(const char *text) {
  std::snprintf(link_hint, sizeof(link_hint), "%s", text ? text : "");
  to_upper_ascii(link_hint);
}

void ui_presenter::clear_link_hint() { link_hint[0] = '\0'; }

void ui_presenter::horizontal_accuracy(char *output, size_t output_size,
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

const char *ui_presenter::brightness_label() {
  const BrightnessMode mode = device_settings::config().brightness;
  if (mode == BrightnessMode::kDay) return "DAY";
  if (mode == BrightnessMode::kNight) return "NIGHT";
  const GnssTimeData time = gnss_service::snapshot().time;
  return instrument_status::fresh_gnss_time(time.valid, time.received_ms, millis())
             ? "AUTO"
             : "AUTO NO GPS";
}

static const char *device_role_title(bool base) { return base ? "Base" : "Rover"; }

static uint16_t page_header_color(const instrument_status::Status &status) {
  bool ready = false;
  if (current_page == ScreenPage::kMain) {
    ready = instrument_status::system_ready(status);
  } else if (current_page == ScreenPage::kGpsDetails) {
    ready = instrument_status::gps_required_fix(status);
  } else if (current_page == ScreenPage::kWifiDetails) {
    ready = instrument_status::correction_link_connected(status);
  }
  return ready ? colors::kHeaderReady : colors::kHeaderNotReady;
}

// The frame carries ready-to-draw strings so the screen module holds no receiver state.
void ui_presenter::build(UiFrame &f, const instrument_status::Inputs &inputs) {
  const uint32_t now = inputs.now_ms;
  const bool base = inputs.role_base;
  const GnssSnapshot gnss_state = gnss_service::snapshot();
  const instrument_status::Status status =
      instrument_status::status_snapshot(inputs, correction_service::health());
  f.now = now;
  f.unit = board::kUnitLabel;
  f.header_color = page_header_color(status);
  if (current_page == ScreenPage::kGpsDetails) {
    std::snprintf(f.header_title, sizeof(f.header_title), "GPS / %s", device_role_title(base));
    std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "REQUIRED FIX: %s",
                  instrument_status::gps_required_fix(status) ? "YES" : "NO");
  } else if (current_page == ScreenPage::kWifiDetails) {
    std::snprintf(f.header_title, sizeof(f.header_title), "Link / %s", device_role_title(base));
    std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "LINK %s",
                  instrument_status::correction_link_connected(status) ? "CONNECTED" : "DOWN");
  } else if (current_page == ScreenPage::kSettings) {
    std::snprintf(f.header_title, sizeof(f.header_title), "Setup / %s", device_role_title(base));
    std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "ROLE & BRIGHTNESS");
  } else if (current_page == ScreenPage::kDebug) {
    std::snprintf(f.header_title, sizeof(f.header_title), "Debug / %s", device_role_title(base));
    std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "PASSIVE MONITOR");
  } else {
    std::snprintf(f.header_title, sizeof(f.header_title), "TopoRTK - %s", device_role_title(base));
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (local_time_utc_minus_6(now, gnss_state.time, year, month, day, hour, minute, second)) {
      std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "%s %02u:%02u",
                    instrument_status::system_ready(status) ? "READY" : "NOT READY", hour, minute);
    } else {
      std::snprintf(f.header_subtitle, sizeof(f.header_subtitle), "%s",
                    instrument_status::system_ready(status) ? "READY" : "NOT READY");
    }
  }
  if (debug_enabled()) std::strncat(f.header_subtitle, " -DBG", sizeof(f.header_subtitle) - std::strlen(f.header_subtitle) - 1);
  if (current_page == ScreenPage::kMain) {
    const auto &link = status;
    const bool linked = link.link_connected;
    const bool radio = link.transport == instrument_status::Transport::Radio;
    if (linked) {
      if (radio) std::strcpy(f.link_value, "Radio Connected");
      else std::snprintf(f.link_value, sizeof(f.link_value), "Wi-Fi  %d dBm", link.signal.rssi_dbm);
    } else {
      std::snprintf(f.link_value, sizeof(f.link_value), "%s Disconnected", radio ? "Radio" : "Wi-Fi");
    }
    f.link_color = !linked ? colors::kFailFill : radio ? colors::kHeaderReady : instrument_status::link_quality_color(link.signal.rssi_dbm);
    std::snprintf(f.fix_value, sizeof(f.fix_value), "%s",
                  instrument_status::current_fix_label(gnss_state.role, gnss_state.gga.received,
                                                       gnss_state.gga_ms, gnss_state.gga.quality, now));
    f.fix_card_color = now - gnss_state.gga_ms > 3000 ? colors::kFailFill : instrument_status::fix_color(gnss_state.role, gnss_state.gga.quality);
    horizontal_accuracy(f.hacc_value, sizeof(f.hacc_value), now);
    f.hacc_color = std::strcmp(f.hacc_value, "---") == 0 ? colors::kSecondary : colors::kPrimary;
    const DashboardWarning warning =
        instrument_status::dashboard_warning(inputs, correction_service::health());
    std::snprintf(f.warning_title, sizeof(f.warning_title), "%s", warning.title);
    std::snprintf(f.warning_detail, sizeof(f.warning_detail), "%s", warning.detail);
    f.warning_color = warning.color;
  }

  if (current_page == ScreenPage::kGpsDetails) {
    const bool uart_active = gnss_state.uart_seen && now - gnss_state.last_rx_ms < 3000;
    std::snprintf(f.gps_uart, sizeof(f.gps_uart), "%s", uart_active ? "RECEIVING" : "WAITING");
    std::snprintf(f.gps_fix, sizeof(f.gps_fix), "%s",
                  gnss_state.gga.received ? instrument_status::fix_label(gnss_state.role, gnss_state.gga.quality) : "NO GGA");
    uint16_t local_year = 0;
    uint8_t local_month = 0, local_day = 0, local_hour = 0, local_minute = 0, local_second = 0;
    if (local_time_utc_minus_6(now, gnss_state.time, local_year, local_month, local_day,
                               local_hour, local_minute, local_second)) {
      std::snprintf(f.gps_local, sizeof(f.gps_local), "%02u:%02u:%02u UTC-6", local_hour,
                    local_minute, local_second);
    } else {
      std::strcpy(f.gps_local, "WAITING FOR GNSS TIME");
    }
    if (instrument_status::fresh_gnss_time(gnss_state.time.valid, gnss_state.time.received_ms, now)) {
      std::snprintf(f.gps_utc, sizeof(f.gps_utc), "%02u:%02u:%02u",
                    gnss_state.time.hour, gnss_state.time.minute, gnss_state.time.second);
    } else {
      std::strcpy(f.gps_utc, "---");
    }
    if (instrument_status::fresh_gnss_time(gnss_state.time.valid, gnss_state.time.received_ms, now)) {
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
    horizontal_accuracy(f.gps_hacc, sizeof(f.gps_hacc), now);
    if (gnss_state.accuracy.received) {
      std::snprintf(f.gps_sigma, sizeof(f.gps_sigma), "N %.3f E %.3f m",
                    gnss_state.accuracy.latitude_sigma_m,
                    gnss_state.accuracy.longitude_sigma_m);
    } else {
      std::strcpy(f.gps_sigma, "---");
    }
    if (base) {
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
    const auto &link = status;
    const bool linked = link.link_connected; // Transport-aware: radio or Wi-Fi.
    std::snprintf(f.wifi_mode, sizeof(f.wifi_mode), "%s", network_service::label());
    std::snprintf(f.wifi_ssid, sizeof(f.wifi_ssid), "%s", network_service::ssid());
    const IPAddress local_ip = network_service::address();
    std::snprintf(f.wifi_ip, sizeof(f.wifi_ip), "%u.%u.%u.%u", local_ip[0], local_ip[1],
                  local_ip[2], local_ip[3]);
    std::snprintf(f.wifi_link, sizeof(f.wifi_link), "%s", linked ? "LINKED" : "NO LINK");
    // Signal strength is only known on Wi-Fi; the radio reports none.
    if (linked && link.signal.valid) {
      std::snprintf(f.wifi_rssi, sizeof(f.wifi_rssi), "%d dBm  %s", link.signal.rssi_dbm,
                    instrument_status::link_quality_label(link.signal.rssi_dbm));
    } else {
      std::strcpy(f.wifi_rssi, link.transport == instrument_status::Transport::Radio ? "N/A (RADIO)" : "---");
    }
    if (wifi_peer_known) {
      std::snprintf(f.wifi_peer, sizeof(f.wifi_peer), "%u.%u.%u.%u", wifi_peer[0],
                    wifi_peer[1], wifi_peer[2], wifi_peer[3]);
    } else if (!base && network_service::station_connected() && !network_service::local_router()) {
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
    else if (gnss_state.profile_applied) std::strcpy(f.apply_label, base ? "BASE ACTIVE" : "ROVER ACTIVE");
    else std::strcpy(f.apply_label, "WAITING FOR GNSS");
    const char *mode_label = brightness_label();
    if (settings.error) std::strcpy(f.settings_status, "SAVE FAILED");
    else if (gnss_state.profile_failed) std::strcpy(f.settings_status, "SETUP FAILED");
    else if (gnss_state.profile_running) std::strcpy(f.settings_status, "CONFIGURING...");
    else if (pending_role != settings.config.role) std::strcpy(f.settings_status, "TAP USE TO SAVE");
    else if (settings.saved) std::strcpy(f.settings_status, "SAVED");
    else std::strcpy(f.settings_status, "DEFAULTS");
    f.status_warning = settings.error || gnss_state.profile_failed;
    if (!backlight_pwm_ready) std::strcpy(f.brightness_line, "PWM ERROR");
    else if (settings.config.brightness == BrightnessMode::kAutomatic &&
             !instrument_status::fresh_gnss_time(gnss_state.time.valid, gnss_state.time.received_ms, millis()))
      std::strcpy(f.brightness_line, "AUTO NO GPS");
    else
      std::snprintf(f.brightness_line, sizeof(f.brightness_line), "%s %u%%", mode_label,
                    static_cast<unsigned>((backlight_duty * 100U + 127U) / 255U));
  }
  if (current_page == ScreenPage::kWifiDetails && ui_detail_page() == 2) {
    f.phone_is_base = base;
    f.phone_local_router = network_service::local_router();
    if (f.phone_is_base) {
      const IPAddress address = network_service::address();
      std::snprintf(f.phone_url, sizeof(f.phone_url), "http://%u.%u.%u.%u",
                    address[0], address[1], address[2], address[3]);
    } else {
      std::snprintf(f.phone_url, sizeof(f.phone_url), "http://%s", rover_ap_address());
    }
  }
  // Both disruptive controls share one hint line: the armed prompt and the
  // refusal the diagnostics layer returned belong where the control was tapped.
  std::snprintf(f.hint_line, sizeof(f.hint_line), "%s", link_hint);
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
    if (link_hint[0]) {
      // The refusal or armed prompt already sits in the shared hint line.
    } else if (f.link_recover_available) {
      std::strcpy(f.hint_line,
                  "PAIR NOT CONFIRMED. SET THE SAME LINK ON BOTH UNITS, OR APPLY LOCALLY.");
    } else if (f.link_switch_busy) {
      std::strcpy(f.hint_line, "A TEST OR UPDATE OWNS THE LINK. WAIT FOR IT TO FINISH.");
    } else {
      std::strcpy(f.hint_line,
                  "ONE TAP ARMS, A SECOND CONFIRMS THE PAIR-WIDE SWITCH.");
    }
  }
}
