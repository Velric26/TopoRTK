#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include "network_service.h"

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
#include "ui_display.h"
#include "ui_screens.h"
#include "touch_input.h"
#include "receiver_reply.h"
#include "touch_layout.h"
#include "web_http.h"
#include "rover_ap.h"
#include "survey_service.h"
#include "correction_service.h"
#include "correction_wifi.h"
#include "instrument_status.h"
#include "link_diagnostic.h"
#include "link_service.h"
#include "debug_service.h"
#include "ota_service.h"
#include "peer_update.h"
#include "status_surface.h"
#include "ui_presenter.h"
#include "usb_console.h"

// The board pin map, the display/touch construction and the LCD reset sequence
// moved to board_hardware.cpp (R10a slice 5); the Wi-Fi correction envelope and
// its whole state moved to correction_wifi.cpp, and the presentation mapping
// and the web status JSON to ui_presenter.cpp and status_surface.cpp (R10a
// slice 6). This root composes their results and keeps the loop order.

// The two NVS stores the settings owner writes through. They stay separate
// objects: Preferences::begin rebinds the namespace of the object it is called
// on, and the device config and the base record live in different namespaces.
Preferences preferences;
Preferences base_preferences;

bool is_base() { return device_settings::config().role == DeviceRole::kBase; }
uint32_t last_screen_ms = 0;
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
                settings.config.base_rtcm ? "ON" : "OFF", network_service::label());
}

void load_base_settings() { device_settings::load_base(); }


// The Wi-Fi correction envelope policy and its state live in
// correction_wifi.cpp; the root keeps the boot/restart step that composes the
// survey revoke, the screen's key timers, the transport's own reset and the
// network restart, in the order it always ran them.
void start_wifi() {
  survey_revoke_control();
  // A network restart is not a radio/session/reference reset.
  phone_key_shown_ms = phone_key_confirm_ms = 0;
  correction_wifi::reset();
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
    network_service::local_router, settings_receiver_config, settings_receiver_base_coordinate,
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
  in.wifi_station_rssi_valid = network_service::station_connected();
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

// The status facets the root itself still asks for: the OTA gate and the
// console read them, and the host suite exercises them by name. Everything the
// presentation surfaces and the survey bridge need they read from
// instrument_status with the evaluation the root composes for them.
bool gps_required_fix() { return instrument_status::gps_required_fix(status_snapshot(millis())); }
bool system_ready(uint32_t now) { return instrument_status::system_ready(status_snapshot(now)); }

uint32_t verified_correction_age(uint32_t now) {
  return instrument_status::verified_correction_age(solution_inputs(), now, correction_service::health());
}

const char *fix_label(int quality) {
  return instrument_status::fix_label(gnss_service::snapshot().role, quality);
}


// Diagnostic logging boundary (R10a slice 4). The CSV session, the mount
// readback test, the session directory and the bounded best-effort appends live
// in diagnostic_log.cpp; what stays here is the one snapshot per call and the
// accuracy text the receiver owner shares. The loop keeps the same gated call in
// the same position, and the root is the only reader of receiver and link state.
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

const DiagnosticHooks kDiagnosticHooks{ui_presenter::horizontal_accuracy};

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

// One evaluation for the surfaces that draw or serve it: the LCD frame, the web
// status JSON and the survey Fix all read the same Inputs value, so the warning
// card, the readiness facets and the corrected age can never disagree.
instrument_status::Inputs surface_inputs(uint32_t now) {
  instrument_status::Inputs in = status_inputs(now);
  in.peer_notice = warning_peer_notice();
  return in;
}

UiFrame ui_frame;

// Touchscreen Link-mode (R6b): the page issues the same pair operation the web
// Settings page does, so it needs the same 32-hex request id shape. The counter
// guarantees a fresh tag over a boot; the refusal copy the page shows belongs to
// ui_presenter, which owns the text.
pair_session::Transport link_recovery_target = pair_session::Transport::Radio;

void make_link_request_id(char out[33]) {
  static uint32_t sequence = 0;
  const uint32_t a = esp_random(), b = esp_random(), c = esp_random();
  const uint32_t d = esp_random() ^ ++sequence ^ web_boot_id();
  std::snprintf(out, 33, "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(a),
                static_cast<unsigned long>(b), static_cast<unsigned long>(c),
                static_cast<unsigned long>(d));
}

// The frame is built by ui_presenter; the root composes the one evaluation it
// reads and keeps the render entry points the loop and the host suite call.
void ui_build_frame(UiFrame &f, uint32_t now) {
  ui_presenter::build(f, surface_inputs(now));
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
  if (changed) ui_presenter::clear_link_hint();
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
        ui_presenter::clear_link_hint();
      } else {
        char hint[96] = {};
        std::snprintf(hint, sizeof(hint), "NOT ACCEPTED: %s",
                      link_operation::reason_text(reason));
        ui_presenter::set_link_hint(hint);
      }
      ui_clear_link_confirm();
    } else {
      ui_presenter::clear_link_hint();
      ui_arm_link_confirm(action, millis());
    }
  } else if (action == TouchAction::kLinkRecover) {
    // Recovery only: a local selection for a pair that cannot confirm itself.
    // The button renders disabled otherwise, so refuse the tap as well.
    const auto operation = link_service::operation_view(millis());
    const bool available = !operation.storage_ok ||
                           !std::strcmp(operation.state, "recovery_required");
    if (!available) {
      ui_presenter::set_link_hint("LOCAL APPLY NEEDS AN UNCONFIRMED PAIR.");
    } else if (ui_link_confirm_active(millis()) &&
               ui_link_confirm_action() == TouchAction::kLinkRecover) {
      if (link_service::select(link_recovery_target, !is_base(), millis())) {
        ui_presenter::clear_link_hint();
      } else {
        ui_presenter::set_link_hint("LOCAL APPLY REFUSED. USE THE WEB SETTINGS PAGE.");
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


// The status JSON and its publication window live in status_surface.cpp; the
// root keeps the two entries the loop and the host suite call, each composed
// with one surface evaluation.
size_t format_web_status(char *output, size_t capacity, uint32_t now) {
  return status_surface::format(output, capacity, surface_inputs(now));
}

void service_web_status() {
  const uint32_t now = millis();
  if (!status_surface::due(now)) return;
  status_surface::publish(surface_inputs(now));
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
    else correction_wifi::send_rtcm_packet(frame, length, message_type);
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
  in.brightness_label = ui_presenter::brightness_label();
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
  // Turn precedence (R10b). The receiver's input is always read first and no
  // byte is ever discarded to shorten a turn. The correction output is written
  // next, in the same turn its frames were admitted, and before every optional
  // or best-effort service (the CSV session, the web publication, the settings
  // service). The optional session runs after all of them and commits at most
  // one solution sample plus diagnostic_log::kEventRowsPerTurn rows - and the
  // receiver's own input path no longer writes to the card at all - so a slow,
  // full or failing SD card can delay COM2 admission by one bounded commit, not
  // by the rows the turn produced.
  ota_service(millis(),!is_base(),gnss_service::snapshot().profile_running,display_ready&&!device_settings::snapshot().error&&survey_service_ready()&&web_service_ready());
  if(!ota_locked())read_usb_console();
  if(!ota_paused())gnss_service::service_input(millis());
  else gnss_service::discard_input(2048);
  if(!ota_locked()){gnss_service::service_startup(millis());gnss_service::service_profile(millis());}
  network_service::service(millis());
  correction_wifi::service();
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

// The survey bridge itself lives in survey_service.cpp (R10a slice 6); the root
// supplies the one status evaluation it reads, exactly as the other surfaces.
void service_survey() {
  const uint32_t now = millis();
  survey_publish(status_inputs(now));
}

bool peer_update_quality_ready(){return is_base()||(!ota_paused()&&verified_correction_age(millis())<=3000&&gps_required_fix());}
void ota_reset_corrections(){
  correction_service::reset();gnss_service::reset_input_state();
  link_service::clear_pending();
}

