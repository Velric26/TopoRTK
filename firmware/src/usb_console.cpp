// USB (UART0) console owner (R10a slice 5), extracted from main.cpp. The
// allowlist, the help text, every verb's wording and every refusal are the ones
// main.cpp printed; what moved out is the composition-root facts the verbs
// report, which arrive as one ConsoleInputs copy per line.
#include <Arduino.h>
#include <cstring>

#include "usb_console.h"

#include "board_hardware.h"
#include "correction_service.h"
#include "device_settings.h"
#include "gnss_service.h"
#include "link_diagnostic.h"
#include "network_service.h"
#include "rover_ap.h"
#include "ui_display.h"
#include "web_http.h"
#include "wifi_transport.h"

namespace {

char usb_line[256] = {};
size_t usb_length = 0;

}  // namespace

namespace usb_console {

void print_help() {
  Serial.println("Safe console commands:");
  Serial.println("  help           - show this list");
  Serial.println("  role?          - query current UM980 mode");
  Serial.println("  config?        - show saved settings and profile status");
  Serial.println("  phone?         - show phone Wi-Fi status (no password)");
  Serial.println("  role rover     - save rover role and verify UM980");
  Serial.println("  role base-test - save temporary base role and verify UM980");
  Serial.println("  wifi?          - show selected transport, SSID, IP, and connection state");
  Serial.println("  wifi direct    - use the validated Base-AP/Rover-client test link");
  Serial.println("  wifi local     - make both units join the configured local router");
  Serial.println("  rtcm?          - show RTCM bridge counters");
  Serial.println("  accuracy?      - show BESTNAV horizontal-accuracy parser status");
  Serial.println("  time?          - show GNSS UTC and fixed UTC-6 local time");
  Serial.println("  brightness auto|day|night - select display brightness mode");
  Serial.println("  brightness?    - report GNSS clock and PWM duty/frequency");
  Serial.println("  rtcm base-test - base role: save and enable COM2 MSM4 test output");
  Serial.println("  rtcm off       - base role: save and disable RTCM output");
  Serial.println("No arbitrary passthrough and no SAVECONFIG are provided.");
}

void handle(const char *command, const ConsoleInputs &in) {
  if(std::strcmp(command,"diag?")==0){static char data[kDiagnosticCapacity];if(diagnostic_snapshot(data,sizeof(data)))Serial.println(data);return;}
  if(std::strncmp(command,"diag ",5)==0){Serial.println(diagnostic_request(command+5)?"DIAG QUEUED":"DIAG REJECTED");return;}
  Serial.print("CONSOLE> ");
  Serial.println(command);

  const GnssSnapshot gnss_state = gnss_service::snapshot();
  if (gnss_state.profile_running) {
    Serial.println("CONSOLE> profile in progress; retry after verification");
    return;
  }

  if (std::strcmp(command, "help") == 0) {
    print_help();
  } else if (std::strcmp(command, "role?") == 0) {
    gnss_service::query_role();
  } else if (std::strcmp(command, "config?") == 0) {
    const SettingsSnapshot settings = device_settings::snapshot();
    Serial.printf("CONFIG: role=%s brightness=%u rtcm=%s wifi=%s storage=%s profile=%s\n",
                  in.base ? "BASE" : "ROVER", static_cast<unsigned>(settings.config.brightness),
                  settings.config.base_rtcm ? "ON" : "OFF", network_service::label(),
                  settings.error ? "ERROR" : settings.saved ? "SAVED" : "DEFAULTS",
                  gnss_state.profile_applied ? "VERIFIED" : gnss_state.profile_failed ? "FAILED" : "WAITING");
  } else if (std::strcmp(command,"phone?") == 0) {
    Serial.printf("PHONE WIFI: %s SSID=%s IP=%s clients=%u UI=%s error=%s\n",
                  rover_ap_ready() ? "READY" : "OFF",rover_ap_ssid(),rover_ap_address(),rover_ap_clients(),kWebUiVersion,rover_ap_error());
  } else if (std::strcmp(command, "role rover") == 0) {
    device_settings::set_role(DeviceRole::kRover, millis());
  } else if (std::strcmp(command, "role base-test") == 0) {
    Serial.println("WARNING: temporary averaged base for functional testing only.");
    device_settings::set_role(DeviceRole::kBase, millis());
  } else if (std::strcmp(command, "wifi?") == 0) {
    const IPAddress ip = network_service::address();
    Serial.printf("WIFI CONFIG: transport=%s role=%s ssid=%s station=%s udp=%s ip=%s peer=%s\n",
                  network_service::label(), in.base ? "BASE" : "ROVER",
                  network_service::ssid(), network_service::station_connected() ? "UP" : "DOWN",
                  wifi_transport::started(wifi_transport::Channel::Corrections) ? "UP" : "DOWN", ip.toString().c_str(),
                  in.peer_known ? in.peer.toString().c_str() : "NONE");
  } else if (std::strcmp(command, "wifi direct") == 0) {
    device_settings::set_wifi_mode(WiFiMode::kDirect, millis());
  } else if (std::strcmp(command, "wifi local") == 0) {
    device_settings::set_wifi_mode(WiFiMode::kLocalRouter, millis());
  } else if (std::strcmp(command, "rtcm?") == 0) {
    Serial.printf("RTCM STATUS: UART=%lu TX=%lu RX=%lu bytes=%lu UART_BAD=%lu NET_BAD=%lu last=%u age_ms=%lu\n",
                  static_cast<unsigned long>(gnss_state.rtcm_frames),
                  static_cast<unsigned long>(in.rtcm_wifi_tx_frames),
                  static_cast<unsigned long>(in.rtcm_wifi_rx_frames),
                  static_cast<unsigned long>(correction_service::snapshot().forwarded_bytes),
                  static_cast<unsigned long>(gnss_state.rtcm_bad),
                  static_cast<unsigned long>(in.invalid_packets),
                  gnss_state.rtcm_last_message,
                  gnss_state.rtcm_last_rx_ms == 0
                      ? 0UL
                      : static_cast<unsigned long>(millis() - gnss_state.rtcm_last_rx_ms));
  } else if (std::strcmp(command, "accuracy?") == 0) {
    Serial.printf("ACCURACY STATUS: BESTNAV=%lu lat_sigma=%.4f m lon_sigma=%.4f m H-ACC(1DRMS)=%.4f m age_ms=%lu usable=%s\n",
                  static_cast<unsigned long>(gnss_state.bestnav_count),
                  gnss_state.accuracy.latitude_sigma_m,
                  gnss_state.accuracy.longitude_sigma_m,
                  gnss_state.accuracy.horizontal_1drms_m,
                  gnss_state.accuracy.received
                      ? static_cast<unsigned long>(millis() -
                                                   gnss_state.accuracy.received_ms)
                      : 0UL,
                  gnss_state.gga.received && gnss_state.gga.quality > 0 &&
                          !(std::strcmp(gnss_state.role, "BASE") == 0 &&
                            gnss_state.gga.quality == 7)
                      ? "YES"
                      : "NO");
  } else if (std::strcmp(command, "time?") == 0) {
    const uint32_t now = millis();
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (local_time_utc_minus_6(now, gnss_state.time, year, month, day, hour, minute, second)) {
      Serial.printf("GNSS TIME: VALID UTC=%04u-%02u-%02u %02u:%02u:%02u LOCAL(UTC-6)=%04u-%02u-%02u %02u:%02u:%02u NAV=%s\n",
                    gnss_state.time.year, gnss_state.time.month,
                    gnss_state.time.day, gnss_state.time.hour,
                    gnss_state.time.minute, gnss_state.time.second, year,
                    month, day, hour, minute, second,
                    gnss_state.time.navigation_valid ? "VALID" : "INVALID");
    } else {
      Serial.println("GNSS TIME: INVALID OR STALE; AUTO BRIGHTNESS IS FULL");
    }
  } else if (std::strcmp(command, "brightness?") == 0) {
    Serial.printf("BRIGHTNESS: %s current=%u target=%u PWM=%s hardware_duty=%lu frequency=%lu Hz\n",
                  in.brightness_label, static_cast<unsigned>(backlight_duty),
                  static_cast<unsigned>(ui_backlight_target()), backlight_pwm_ready ? "PASS" : "FAIL",
                  static_cast<unsigned long>(ledcRead(board::kBacklightPwmChannel)),
                  static_cast<unsigned long>(ledcReadFreq(board::kBacklightPwmChannel)));
  } else if (std::strcmp(command, "brightness auto") == 0) {
    device_settings::set_brightness(BrightnessMode::kAutomatic, millis());
  } else if (std::strcmp(command, "brightness day") == 0) {
    device_settings::set_brightness(BrightnessMode::kDay, millis());
  } else if (std::strcmp(command, "brightness night") == 0) {
    device_settings::set_brightness(BrightnessMode::kNight, millis());
  } else if (std::strcmp(command, "rtcm base-test") == 0) {
    if (!in.base) {
      Serial.println("CONSOLE> rejected; select base role first");
    } else {
      DeviceConfig requested = device_settings::config();
      requested.base_rtcm = true;
      device_settings::apply(requested, millis());
    }
  } else if (std::strcmp(command, "rtcm off") == 0) {
    if (!in.base) {
      Serial.println("CONSOLE> rejected; select base role first");
    } else {
      DeviceConfig requested = device_settings::config();
      requested.base_rtcm = false;
      device_settings::apply(requested, millis());
    }
  } else if (command[0] != '\0') {
    Serial.println("CONSOLE> rejected; type help for the safe command list");
  }
}

void service(ConsoleFacts facts) {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\r') continue;
    if (incoming == '\n') {
      if (usb_length > 0) {
        usb_line[usb_length] = '\0';
        handle(usb_line, facts());
        usb_length = 0;
      }
      continue;
    }

    if (usb_length < sizeof(usb_line) - 1) {
      usb_line[usb_length++] = incoming;
    } else {
      usb_length = 0;
      Serial.println("CONSOLE> input too long; rejected");
    }
  }
}

}  // namespace usb_console
