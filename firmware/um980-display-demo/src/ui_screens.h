#pragma once
// Screen renderers extracted from main.cpp (R2a). The composition root
// builds one UiFrame per render tick with pre-formatted, ready-to-draw
// content; renderers hold no receiver, link or storage state. Page and
// settings-form state (current_page, pending_role, phone-key timers) is
// screen-owned; the root mutates it only through the functions below.
#include <cstdint>
#include "device_config.h"
#include "touch_layout.h"

struct UiFrame {
  uint32_t now = 0;
  char unit = 'A';
  // Header (built by the root: page-aware readiness policy stays there).
  uint16_t header_color = 0;
  char header_title[48] = {};
  char header_subtitle[64] = {};
  // Dashboard cards.
  char link_value[64] = {};
  uint16_t link_color = 0;
  char fix_value[24] = {};
  uint16_t fix_card_color = 0;
  char hacc_value[64] = {};
  uint16_t hacc_color = 0;
  char warning_title[24] = {};
  char warning_detail[48] = {};
  uint16_t warning_color = 0;
  // GPS details rows, top to bottom.
  char gps_uart[24] = {};
  char gps_fix[24] = {};
  char gps_local[32] = {};
  char gps_utc[16] = {};
  char gps_date[16] = {};
  char gps_lat[24] = {};
  char gps_lon[24] = {};
  char gps_sats[32] = {};
  char gps_alt[24] = {};
  char gps_hacc[64] = {};
  char gps_sigma[40] = {};
  char gps_rtcm[40] = {};
  // Link details rows.
  char wifi_mode[24] = {};
  char wifi_ssid[40] = {};
  char wifi_ip[20] = {};
  char wifi_link[16] = {};
  char wifi_rssi[24] = {};
  char wifi_peer[20] = {};
  char wifi_pkts[24] = {};
  char wifi_net[24] = {};
  char wifi_rtcm[24] = {};
  char wifi_data[32] = {};
  // Setup page: role/form state owned by the root, labels pre-built.
  DeviceRole active_role = DeviceRole::kRover;
  bool profile_running = false;
  bool profile_failed = false;
  bool profile_applied = false;
  bool config_error = false;
  bool config_saved = false;
  BrightnessMode brightness_mode = BrightnessMode::kAutomatic;
  bool status_warning = false;
  char apply_label[24] = {};
  char settings_status[80] = {};
  char brightness_line[64] = {};
  // Phone content on the third Link detail page.
  bool phone_is_base = false;
  bool phone_local_router = false;
  char phone_url[48] = {};
};

extern ScreenPage current_page;
extern DeviceRole pending_role;
extern uint32_t phone_key_shown_ms;
extern uint32_t phone_key_confirm_ms;

// Cycles two GPS or three Link pages; the caller clears and redraws the page.
void ui_cycle_detail_page(int8_t delta);
uint8_t ui_detail_page();
// Returns true when the page changed; the caller re-renders.
bool ui_change_page(ScreenPage page, DeviceRole active_role);
void ui_set_pending_role(DeviceRole role);
void ui_toggle_key_reveal(uint32_t now);
bool ui_key_confirm_active(uint32_t now);
void ui_arm_key_confirm(uint32_t now);
void ui_clear_key_state();

void ui_draw_static(const UiFrame &frame);
void ui_draw_dynamic(const UiFrame &frame);
