// Touchscreen screens (R2a extraction, R2b light high-contrast restyle).
// Every value that depends on receiver, link or storage state arrives
// pre-formatted in UiFrame; this module renders and owns only page/form
// interaction state. All operational text is size 2 or larger; green is
// reserved for the required ready/connected/fixed state.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "ui_screens.h"
#include "ui_display.h"
#include "ui_theme.h"
#include "rover_ap.h"
#include "debug_service.h"
#include <cstring>

ScreenPage current_page = ScreenPage::kMain;
DeviceRole pending_role = DeviceRole::kRover;
uint32_t phone_key_shown_ms = 0;
uint32_t phone_key_confirm_ms = 0;

namespace {
uint8_t detail_page_index = 0;
// Link-mode confirmation (R6b): the armed button and its ten-second window.
TouchAction link_confirm_action_ = TouchAction::kNone;
uint32_t link_confirm_ms_ = 0;
constexpr uint32_t kLinkConfirmWindowMs = 10000;
}

uint8_t ui_detail_page() { return detail_page_index; }

void ui_cycle_detail_page(int8_t delta) {
  // Link has four pages (rows, counters, phone, link mode); GPS flips between two.
  const int pages = current_page == ScreenPage::kWifiDetails ? 4 : 2;
  int next = static_cast<int>(detail_page_index) + delta;
  next %= pages;
  if (next < 0) next += pages;
  detail_page_index = static_cast<uint8_t>(next);
  ui_clear_key_state();
}

bool ui_change_page(ScreenPage page, DeviceRole active_role) {
  if (page == current_page) {
    // Tapping the active tab returns to its first detail page.
    if (detail_page_index) {
      detail_page_index = 0;
      ui_clear_key_state();
      return true;
    }
    return false;
  }
  current_page = page;
  detail_page_index = 0;
  phone_key_shown_ms = phone_key_confirm_ms = 0;
  pending_role = active_role;
  ui_reset_region_cache();
  const char *name = page == ScreenPage::kGpsDetails
                         ? "GPS DETAILS"
                         : page == ScreenPage::kWifiDetails ? "WIFI DETAILS"
                         : page == ScreenPage::kSettings ? "SETUP"
                         : page == ScreenPage::kDebug ? "DEBUG" : "MAIN";
  Serial.printf("UI PAGE: %s\n", name);
  return true;
}

void ui_set_pending_role(DeviceRole role) { pending_role = role; }

void ui_toggle_key_reveal(uint32_t now) {
  phone_key_shown_ms = phone_key_shown_ms ? 0 : now;
  phone_key_confirm_ms = 0;
}

bool ui_key_confirm_active(uint32_t now) {
  return phone_key_confirm_ms && now - phone_key_confirm_ms < 10000;
}

void ui_arm_key_confirm(uint32_t now) { phone_key_confirm_ms = now; }

bool ui_link_confirm_active(uint32_t now) {
  return link_confirm_ms_ && now - link_confirm_ms_ < kLinkConfirmWindowMs;
}

TouchAction ui_link_confirm_action() { return link_confirm_action_; }

void ui_arm_link_confirm(TouchAction action, uint32_t now) {
  link_confirm_action_ = action;
  link_confirm_ms_ = now;
}

void ui_clear_link_confirm() {
  link_confirm_action_ = TouchAction::kNone;
  link_confirm_ms_ = 0;
}

void ui_clear_key_state() {
  phone_key_shown_ms = phone_key_confirm_ms = 0;
  ui_clear_link_confirm();
  ui_reset_region_cache();
}

namespace {

void draw_navigation() {
  if (!ui_region_changed(current_page, 4, layout::kNavY, "tabs")) return;
  static const char *const labels[] = {"HOME", "GPS", "LINK", "SETUP"};
  display->fillRect(0, layout::kNavY, 320, 48, colors::kBackground);
  for (uint8_t index = 0; index < 4; ++index) {
    const bool selected = index == static_cast<uint8_t>(current_page) ||
                          (current_page == ScreenPage::kDebug && index == 3);
    if (selected) {
      display->fillRect(index * 80, layout::kNavY, 80, 48, colors::kSelected);
      display->fillRect(index * 80, layout::kNavY, 80, 3, RGB565_WHITE);
    }
    draw_fitted_text(index * 80 + (80 - std::strlen(labels[index]) * 12) / 2,
                     layout::kNavY + 19, 72, labels[index], 2,
                     selected ? RGB565_WHITE : colors::kPrimary);
  }
}

void draw_header(const UiFrame &f) {
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%u|%s|%s|%c", f.header_color,
                f.header_title, f.header_subtitle, f.unit);
  if (!ui_region_changed(current_page, 0, 0, signature)) return;

  display->fillRect(0, 0, ui_width(), 52, f.header_color);
  draw_fitted_text(10, 8, 264, f.header_title, 2, RGB565_BLACK);

  display->setTextSize(2);
  display->setTextColor(RGB565_BLACK);
  display->setCursor(10, 32);
  display->print(f.header_subtitle);

  display->fillRoundRect(282, 7, 31, 38, 5, RGB565_YELLOW);
  display->setTextColor(RGB565_BLACK);
  display->setTextSize(2);
  display->setCursor(292, 18);
  display->print(f.unit);
}

void draw_main_dashboard(const UiFrame &f) {
  draw_status_card(current_page, 60, 90, "CORRECTION LINK", f.link_value, f.link_color);
  draw_status_card(current_page, 158, 90, "GNSS SOLUTION", f.fix_value,
                   f.fix_card_color);
  draw_status_card(current_page, 256, 90, "H-UNCERTAINTY (1DRMS)", f.hacc_value,
                   f.hacc_color);
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%s|%u", f.warning_title, f.warning_detail, f.warning_color);
  if (ui_region_changed(current_page, 3, 356, signature)) {
    display->fillRoundRect(8, 356, ui_width() - 16, 68, 6, colors::kWarningBg);
    display->drawRoundRect(8, 356, 304, 68, 6, colors::kOutline);
    display->drawRoundRect(9, 357, 302, 66, 6, colors::kOutline);
    draw_fitted_text(18, 364, 284, f.warning_title, 2, colors::kPrimary);
    draw_wrapped(18, 388, 284, f.warning_detail, 2, colors::kPrimary);
  }
}

void draw_gps_details(const UiFrame &f) {
  static const char *const labels[12] = {"UART", "FIX",   "LOCAL", "UTC",
                                         "DATE", "LAT",   "LON",   "SATS",
                                         "ALT",  "H-ACC", "SIGMA", "RTCM"};
  const char *const values[12] = {f.gps_uart, f.gps_fix,  f.gps_local, f.gps_utc,
                                  f.gps_date, f.gps_lat,  f.gps_lon,   f.gps_sats,
                                  f.gps_alt,  f.gps_hacc, f.gps_sigma, f.gps_rtcm};
  for (uint8_t row = 0; row < 6; ++row) {
    const uint8_t index = static_cast<uint8_t>(detail_page_index * 6 + row);
    draw_detail_row(current_page, row, 58 + row * 52, labels[index], values[index]);
  }
  draw_button(current_page, 12, layout::kDetailToggle,
              detail_page_index ? "PAGE 1" : "PAGE 2", "", false);
}

void draw_phone_connection(const UiFrame &f);

// Fourth Link page (R6b): the medium selector that calls the same pair-operation
// service as the web Settings page. One tap arms, a second confirms; the recovery
// button exists only when the pair cannot confirm a normal selection.
void draw_link_mode(const UiFrame &f) {
  draw_detail_row(current_page, 0, 58, "SELECTED", f.link_selected_line);
  draw_detail_row(current_page, 1, 110, "PAIR", f.link_peer_line);
  const bool confirm = ui_link_confirm_active(f.now);
  const bool radio_confirm = confirm && ui_link_confirm_action() == TouchAction::kLinkRadio;
  const bool wifi_confirm = confirm && ui_link_confirm_action() == TouchAction::kLinkWifi;
  const bool recover_confirm = confirm && ui_link_confirm_action() == TouchAction::kLinkRecover;
  char signature[192] = {};
  std::snprintf(signature, sizeof(signature), "%u|%s|%u|%u|%u", confirm,
                f.link_operation_line, f.link_recover_available, f.link_switch_busy,
                f.link_radio_selected);
  if (ui_region_changed(current_page, 16, 166, signature)) {
    display->fillRect(8, 166, 304, 44, colors::kBackground);
    draw_wrapped(12, 168, 296, f.link_operation_line, 2, colors::kPrimary);
  }
  draw_button(current_page, 13, layout::kLinkRadio,
              radio_confirm ? "CONFIRM RADIO" : "USE RADIO", "",
              f.link_radio_selected && !radio_confirm,
              radio_confirm || !f.link_switch_busy);
  draw_button(current_page, 14, layout::kLinkWifi,
              wifi_confirm ? "CONFIRM WI-FI" : "USE WI-FI", "",
              f.link_wifi_selected && !wifi_confirm,
              wifi_confirm || !f.link_switch_busy);
  draw_button(current_page, 15, layout::kLinkRecover,
              recover_confirm ? "CONFIRM APPLY" : "APPLY LOCAL", "FOR RECOVERY", false,
              f.link_recover_available && (recover_confirm || !f.link_switch_busy));
  if (ui_region_changed(current_page, 17, 344, f.link_mode_hint)) {
    display->fillRect(8, 344, 304, 80, colors::kBackground);
    draw_wrapped(12, 348, 296, f.link_mode_hint, 3, colors::kSecondary);
  }
}

void draw_wifi_details(const UiFrame &f) {
  static const char *const labels[10] = {"MODE", "SSID", "IP",   "LINK",
                                         "RSSI", "PEER", "PKTS", "NET",
                                         "RTCM", "DATA"};
  const char *const values[10] = {f.wifi_mode, f.wifi_ssid, f.wifi_ip,   f.wifi_link,
                                  f.wifi_rssi, f.wifi_peer, f.wifi_pkts, f.wifi_net,
                                  f.wifi_rtcm, f.wifi_data};
  if (detail_page_index == 3) {
    draw_link_mode(f);  // Link mode is the fourth Link page.
  } else if (detail_page_index == 2) {
    draw_phone_connection(f);  // Phone content is the third Link page.
  } else if (detail_page_index == 0) {
    for (uint8_t row = 0; row < 6; ++row) {
      draw_detail_row(current_page, row, 58 + row * 52, labels[row], values[row]);
    }
  } else {
    for (uint8_t row = 0; row < 4; ++row) {
      draw_detail_row(current_page, row, 58 + row * 52, labels[6 + row], values[6 + row]);
    }
  }
  draw_button(current_page, 11, layout::kDetailPrev, "PREV", "", false);
  draw_button(current_page, 12, layout::kDetailNext, "NEXT", "", false);
}

void draw_settings(const UiFrame &f) {
  if (ui_region_changed(current_page, 5, 64, "headings")) {
    draw_fitted_text(12, 65, 296, "INSTRUMENT ROLE", 2, colors::kPrimary);
    draw_fitted_text(12, 295, 296, "SCREEN BRIGHTNESS", 2, colors::kPrimary);
  }
  const bool base_selected = pending_role == DeviceRole::kBase;
  draw_button(current_page, 0, layout::kBase, "BASE", base_selected ? "SELECTED" : "SEND RTCM", base_selected, !f.profile_running);
  draw_button(current_page, 1, layout::kRover, "ROVER", !base_selected ? "SELECTED" : "RECEIVE RTCM", !base_selected, !f.profile_running);
  if (ui_region_changed(current_page, 6, 174, base_selected ? "base" : "rover")) {
    display->fillRect(8, 172, 304, 52, colors::kBackground);
    draw_fitted_text(12, 176, 296, base_selected ? "Temporary averaged base." : "Receives corrections.", 2, colors::kSecondary);
    draw_fitted_text(12, 198, 296, base_selected ? "Control is unverified." : "Other unit stays Base.", 2, base_selected ? colors::kError : colors::kSecondary);
  }
  const bool needs_apply = pending_role != f.active_role || f.config_error || !f.config_saved || f.profile_failed;
  draw_button(current_page, 2, layout::kApply, f.apply_label, "", needs_apply, needs_apply && !f.profile_running);
  draw_button(current_page, 3, layout::kAuto, "AUTO", "", f.brightness_mode == BrightnessMode::kAutomatic, !f.profile_running);
  draw_button(current_page, 4, layout::kDay, "DAY", "", f.brightness_mode == BrightnessMode::kDay, !f.profile_running);
  draw_button(current_page, 5, layout::kNight, "NIGHT", "", f.brightness_mode == BrightnessMode::kNight, !f.profile_running);
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%s", f.settings_status, f.brightness_line);
  if (ui_region_changed(current_page, 7, 382, signature)) {
    display->fillRect(8, 380, 198, 46, colors::kBackground);
    draw_fitted_text(12, 385, 188, f.settings_status, 2, f.status_warning ? colors::kError : colors::kSecondary);
    draw_fitted_text(12, 407, 188, f.brightness_line, 2, colors::kSecondary);
  }
  draw_button(current_page, 9, layout::kDebug, "DEBUG", "", debug_enabled());
}

void draw_debug_settings() {
  const bool enabled = debug_enabled();
  draw_label_value(current_page, 66, "DEBUG", enabled ? "ON" : "OFF");
  if (ui_region_changed(current_page, 14, 104, enabled ? "on" : "off")) {
    display->fillRect(8, 104, 304, 128, colors::kBackground);
    draw_fitted_text(12, 108, 296, "Passive monitor keeps", 2, colors::kSecondary);
    draw_fitted_text(12, 128, 296, "survey and corrections", 2, colors::kSecondary);
    draw_fitted_text(12, 148, 296, "running normally.", 2, colors::kSecondary);
    draw_fitted_text(12, 176, 296, "Web UI: Debug tab.", 2, colors::kPrimary);
    draw_fitted_text(12, 196, 296, "On until disabled.", 2, colors::kSecondary);
    draw_fitted_text(12, 216, 296, "On after restart.", 2, colors::kSecondary);
  }
  draw_button(current_page, 10, layout::kDebugToggle, enabled ? "DISABLE DEBUG" : "ENABLE DEBUG", "", enabled);
  if (ui_region_changed(current_page, 15, 314, "debug-info")) {
    display->fillRect(8, 314, 304, 112, colors::kBackground);
    draw_fitted_text(12, 318, 296, "Updates: web Debug tab.", 2, colors::kSecondary);
    draw_fitted_text(12, 342, 296, "Enabling does not pause.", 2, colors::kSecondary);
    draw_fitted_text(12, 366, 296, "Logs stay in RAM.", 2, colors::kSecondary);
  }
}

void draw_phone_connection(const UiFrame &f) {
  const bool reveal = phone_key_shown_ms && f.now - phone_key_shown_ms < 30000;
  const bool confirm = phone_key_confirm_ms && f.now - phone_key_confirm_ms < 10000;
  if (!reveal) phone_key_shown_ms = 0;
  if (!confirm) phone_key_confirm_ms = 0;
  // The cache never stores the credential itself.
  char signature[128];
  std::snprintf(signature, sizeof(signature), "%u|%u|%u|%u|%u|%s|%s|%lu", f.phone_is_base, f.phone_local_router, rover_ap_ready(), reveal,
                rover_ap_clients(), rover_ap_error(), rover_ap_address(), static_cast<unsigned long>(phone_key_shown_ms));
  if (ui_region_changed(current_page, 8, 60, signature)) {
    display->fillRect(8, 58, 304, 150, colors::kBackground);
    draw_fitted_text(12, 60, 296, "1. JOIN WI-FI", 2, colors::kSecondary);
    draw_fitted_text(12, 80, 296, f.phone_is_base ? (f.phone_local_router ? "JOIN THE LOCAL ROUTER" : "JOIN BASE WI-FI") : rover_ap_ssid(), 2, colors::kPrimary);
    char state[64];
    std::snprintf(state, sizeof(state), "READY, PHONES: %u", rover_ap_clients());
    draw_fitted_text(12, 100, 296, f.phone_is_base ? "OPEN BASE WEB FOR SETUP" : (rover_ap_ready() ? state : "AP UNAVAILABLE"), 2, f.phone_is_base || rover_ap_ready() ? colors::kAccent : colors::kError);
    draw_fitted_text(12, 120, 296, f.phone_is_base ? "KEYS ON ROVER SCREEN" : "KEY (HIDES IN 30 S)", 2, colors::kSecondary);
    draw_fitted_text(12, 140, 296, reveal && !f.phone_is_base ? rover_ap_password() : "****.****", 2, colors::kPrimary);
    draw_fitted_text(12, 166, 296, rover_ap_error()[0] ? rover_ap_error() : "WEB: TAP TAKE CONTROL", 2, colors::kSecondary);
  }
  draw_button(current_page, 7, layout::kShowKey, reveal ? "HIDE KEY" : "SHOW KEY", "", reveal, !f.phone_is_base);
  draw_button(current_page, 8, layout::kNewKey, confirm ? "CONFIRM" : "NEW KEY", "", confirm, !f.phone_is_base);
  char bottom[80];
  std::snprintf(bottom, sizeof(bottom), "%u|%s", confirm, f.phone_url);
  if (ui_region_changed(current_page, 9, 272, bottom)) {
    display->fillRect(8, 266, 304, 110, colors::kBackground);
    draw_fitted_text(12, 270, 296, "2. OPEN IN CHROME", 2, colors::kSecondary);
    draw_fitted_text(12, 292, 296, f.phone_url, 2, colors::kPrimary);
    if (confirm) {
      draw_wrapped(12, 316, 296, "New key: phones drop. Tap CONFIRM in 10 s.", 2, colors::kError);
    } else {
      draw_wrapped(12, 316, 296, "Android 'No Internet' is normal. Keep it open.", 2, colors::kSecondary);
    }
  }
}

}  // namespace

void ui_draw_static(const UiFrame &frame) {
  ui_reset_region_cache();
  display->fillScreen(colors::kBackground);
  draw_header(frame);
}

void ui_draw_dynamic(const UiFrame &frame) {
  draw_header(frame);
  if (current_page == ScreenPage::kGpsDetails) {
    draw_gps_details(frame);
  } else if (current_page == ScreenPage::kWifiDetails) {
    draw_wifi_details(frame);
  } else if (current_page == ScreenPage::kSettings) {
    draw_settings(frame);
  } else if (current_page == ScreenPage::kDebug) {
    draw_debug_settings();
  } else {
    draw_main_dashboard(frame);
  }
  draw_navigation();
}
