// UM980 receiver service (R10a slice 2): the receiver state, UART1 ownership and
// the profile/startup sequencers moved out of main.cpp. Bodies are unchanged
// except that `millis()` became the caller's `now_ms`, the removed globals
// became file-local state, and the observations the receiver cannot own became
// registered calls (CSV lines, the correction owner's reset, frame admission,
// and the fix-label text that instrument_status owns).
// RTCM 3 framing reuses the correction core's portable `valid_rtcm` /
// `message_type` so the wire format has one implementation.
#include "gnss_service.h"
#include <Arduino.h>
#include "correction_queue.h"
#include "debug_service.h"
#include "receiver_reply.h"

#include <cstdio>
#include <cstring>

// Sole UART1 owner. Lives here so no other translation unit can drive COM2;
// the host double observes the same object as the port the firmware writes to.
HardwareSerial gnss(1);

namespace gnss_service {
namespace {

const GnssObservers *observers = nullptr;

DeviceConfig device_config;
bool base_fixed = false;
double base_latitude = 0, base_longitude = 0, base_height = 0;
bool base_settings_failed = false;

bool profile_running = false;
bool profile_failed = false;
bool profile_waiting = false;
bool profile_ack = false;
bool profile_mode_verified = false;
uint8_t profile_step = 0;
uint8_t profile_attempts = 0;
uint32_t profile_sent_ms = 0;
uint32_t profile_complete_ms = 0;

bool is_base() { return device_config.role == DeviceRole::kBase; }
bool unit_profile_applied = false;
bool version_ok = false;
char rx_line[512] = {};
size_t rx_length = 0;
char receiver_role[8] = "UNKNOWN";
char last_displayed_fix_label[20] = {};
uint32_t byte_count = 0;
uint32_t line_count = 0;
uint32_t gga_count = 0;
uint32_t bestnav_count = 0;
uint32_t rmc_count = 0;
GnssParseStats gnss_parse_stats;
uint32_t last_rx_ms = 0;
uint32_t last_gga_ms = 0;
uint32_t next_gnss_handshake_ms = 0;
uint32_t gnss_handshake_attempts = 0;
uint8_t gnss_handshake_step = 0;
uint32_t gnss_next_command_ms = 0;
bool gnss_startup_complete = false;

// Receiver-side reference/station: captured from an RTCM 1005/1006 frame the
// receiver sent, or from one forwarded to it.
survey::Cartesian survey_reference;
uint16_t survey_station = 0;
uint32_t survey_reference_ms = 0;

uint8_t rtcm_command_acks = 0;
uint8_t rtcm_frame[correction::max_rtcm] = {};
size_t rtcm_frame_length = 0;
size_t rtcm_expected_length = 0;
uint32_t rtcm_last_byte_ms = 0;
uint32_t rtcm_uart_frames = 0;
uint32_t rtcm_uart_bad = 0;
uint16_t rtcm_last_message = 0;
uint32_t rtcm_last_rx_ms = 0;

GgaData latest_gga;
HorizontalAccuracyData latest_horizontal_accuracy;
GnssTimeData latest_gnss_time;

void capture_reference(const uint8_t *frame, size_t length, uint32_t now_ms) {
  if (survey::reference_station(frame, length, survey_reference, survey_station)) {
    survey_reference_ms = now_ms;
  }
}

void clear_partial_input() { rtcm_frame_length = rtcm_expected_length = rx_length = 0; }

void send_command(const char *command) {
  debug_observe(debugmode::Channel::GnssTx, command);
  Serial.print("ESP32> ");
  Serial.println(command);
  gnss.print(command);
  gnss.print("\r\n");
}

void handle_complete_rtcm(const uint8_t *frame, size_t frame_length, uint32_t now_ms) {
  debug_frame(debugmode::Channel::GnssRx, frame, frame_length);
  if (is_base()) capture_reference(frame, frame_length, now_ms);
  ++rtcm_uart_frames;
  rtcm_last_message = correction::message_type(frame);
  if (is_base()) rtcm_last_rx_ms = now_ms;

  // Admission and transport selection belong to the link and correction owners.
  observers->forward_frame(frame, frame_length, rtcm_last_message);
}

bool consume_rtcm_byte(uint8_t incoming, uint32_t now_ms) {
  if (rtcm_frame_length > 0 && now_ms - rtcm_last_byte_ms > 250) {
    rtcm_frame_length = 0;
    rtcm_expected_length = 0;
    ++rtcm_uart_bad;
  }

  if (rtcm_frame_length == 0) {
    if (incoming != 0xD3) return false;
    rtcm_frame[0] = incoming;
    rtcm_frame_length = 1;
    rtcm_last_byte_ms = now_ms;
    return true;
  }

  rtcm_last_byte_ms = now_ms;
  if (rtcm_frame_length >= sizeof(rtcm_frame)) {
    rtcm_frame_length = 0;
    rtcm_expected_length = 0;
    ++rtcm_uart_bad;
    return true;
  }

  rtcm_frame[rtcm_frame_length++] = incoming;
  if (rtcm_frame_length == 3) {
    const size_t payload_length =
        (static_cast<size_t>(rtcm_frame[1] & 0x03) << 8) | rtcm_frame[2];
    rtcm_expected_length = payload_length + 6;
    if (payload_length < 2 || rtcm_expected_length > sizeof(rtcm_frame)) {
      rtcm_frame_length = 0;
      rtcm_expected_length = 0;
      ++rtcm_uart_bad;
    }
    return true;
  }

  if (rtcm_expected_length > 0 && rtcm_frame_length == rtcm_expected_length) {
    if (correction::valid_rtcm(rtcm_frame, rtcm_frame_length)) {
      handle_complete_rtcm(rtcm_frame, rtcm_frame_length, now_ms);
    } else {
      ++rtcm_uart_bad;
    }
    rtcm_frame_length = 0;
    rtcm_expected_length = 0;
  }
  return true;
}

void handle_line(char *line, uint32_t now_ms) {
  debug_observe(debugmode::Channel::GnssRx, line);
  ++line_count;
  Serial.print("UM980> ");
  Serial.println(line);

  if (std::strstr(line, ";UM980,") != nullptr ||
      std::strstr(line, "$command,VERSION,response: OK") != nullptr) {
    version_ok = true;
  }

  bool valid_mode = false;
  if (std::strncmp(line, "#MODE,", 6) == 0) {
    valid_mode = valid_receiver_reply(line) || valid_unicore_ascii_crc(line);
  }
  if (valid_mode && std::strstr(line, ";MODE ROVER") != nullptr) {
    std::strcpy(receiver_role, "ROVER");
    last_displayed_fix_label[0] = '\0';
  } else if (valid_mode && std::strstr(line, ";MODE BASE") != nullptr) {
    std::strcpy(receiver_role, "BASE");
    last_displayed_fix_label[0] = '\0';
  }

  if (profile_running && profile_waiting) {
    const char *command = active_profile_command(profile_step);
    char prefix[80] = {};
    std::snprintf(prefix, sizeof(prefix), "$command,%s,response: ", command);
    if (valid_receiver_reply(line) && std::strncmp(line, prefix, std::strlen(prefix)) == 0) {
      if (std::strncmp(line + std::strlen(prefix), "OK*", 3) == 0) {
        profile_ack = true;
      } else {
        profile_running = false;
        profile_failed = true;
        Serial.printf("PROFILE FAILED: receiver rejected %s\n", command);
        observers->log_event("PROFILE_FAILED", command);
      }
    }
    if (valid_mode && std::strcmp(command, "MODE") == 0) {
      profile_mode_verified = std::strcmp(receiver_role, is_base() ? "BASE" : "ROVER") == 0;
    }
  }

  if (std::strstr(line, "$command,RTCM") != nullptr &&
      std::strstr(line, "response: OK") != nullptr && rtcm_command_acks < 6) {
    ++rtcm_command_acks;
  }

  GgaData parsed = latest_gga;
  if (parse_gga(line, parsed, gnss_parse_stats)) {
    latest_gga = parsed;
    ++gga_count;
    last_gga_ms = now_ms;
    const char *label = observers->fix_label(latest_gga.quality);
    if (std::strcmp(last_displayed_fix_label, label) != 0) {
      std::strncpy(last_displayed_fix_label, label,
                   sizeof(last_displayed_fix_label) - 1);
      last_displayed_fix_label[sizeof(last_displayed_fix_label) - 1] = '\0';
      Serial.printf("DISPLAY FIX STATE: %s (GGA quality %d)\n", label,
                    latest_gga.quality);
    }
  }

  HorizontalAccuracyData parsed_accuracy = latest_horizontal_accuracy;
  if (parse_bestnav_accuracy(line, now_ms, parsed_accuracy, gnss_parse_stats)) {
    latest_horizontal_accuracy = parsed_accuracy;
    ++bestnav_count;
    if (bestnav_count == 1) {
      Serial.printf("BESTNAV ACCURACY: PASS lat_sigma=%.4f m lon_sigma=%.4f m H-ACC(1DRMS)=%.4f m\n",
                    latest_horizontal_accuracy.latitude_sigma_m,
                    latest_horizontal_accuracy.longitude_sigma_m,
                    latest_horizontal_accuracy.horizontal_1drms_m);
    }
  }

  GnssTimeData parsed_time = latest_gnss_time;
  if (parse_rmc_time(line, now_ms, parsed_time, gnss_parse_stats)) {
    latest_gnss_time = parsed_time;
    ++rmc_count;
    if (rmc_count == 1) {
      Serial.printf("GNSS TIME: %s UTC=%02u:%02u:%02u DATE=%04u-%02u-%02u NAV=%s\n",
                    latest_gnss_time.valid ? "VALID" : "INVALID",
                    latest_gnss_time.hour, latest_gnss_time.minute,
                    latest_gnss_time.second, latest_gnss_time.year,
                    latest_gnss_time.month, latest_gnss_time.day,
                    latest_gnss_time.navigation_valid ? "VALID" : "INVALID");
    }
  }
}

}  // namespace

void begin(const GnssPort &port, const GnssObservers &observer_set, uint32_t now_ms) {
  observers = &observer_set;
  gnss.setRxBufferSize(port.rx_buffer);
  gnss.setTxBufferSize(port.tx_buffer);
  gnss.begin(port.baud, SERIAL_8N1, port.rx_pin, port.tx_pin);
  next_gnss_handshake_ms = now_ms + 1500;
}

void set_config(const DeviceConfig &config) { device_config = config; }

void set_base_coordinate(bool fixed, double latitude, double longitude, double height) {
  base_fixed = fixed;
  base_latitude = latitude;
  base_longitude = longitude;
  base_height = height;
  base_settings_failed = false;
}

void report_base_failure(bool blocks_any_role) {
  base_settings_failed = true;
  if (blocks_any_role) unit_profile_applied = false;
  if (blocks_any_role || is_base()) profile_failed = true;
}

void service_input(uint32_t now_ms) {
  while (gnss.available() > 0) {
    const uint8_t raw = static_cast<uint8_t>(gnss.read());
    ++byte_count;
    last_rx_ms = now_ms;

    if (consume_rtcm_byte(raw, now_ms)) continue;

    const char incoming = static_cast<char>(raw);

    if (incoming == '\r') continue;
    if (incoming == '\n') {
      if (rx_length > 0) {
        rx_line[rx_length] = '\0';
        handle_line(rx_line, now_ms);
        rx_length = 0;
      }
      continue;
    }

    if (rx_length < sizeof(rx_line) - 1) {
      rx_line[rx_length++] = incoming;
    } else {
      rx_length = 0;
      ++gnss_parse_stats.checksum_errors;
      Serial.println("UM980> RX LINE OVERFLOW");
    }
  }
}

void discard_input(uint32_t budget) {
  for (uint32_t index = 0; index < budget && gnss.available() > 0; ++index) {
    gnss.read();
  }
}

const char *active_profile_command(uint8_t step) {
  static char fixed_command[96];
  if (device_config.role == DeviceRole::kBase && base_fixed && step == 1) {
    std::snprintf(fixed_command, sizeof(fixed_command), "MODE BASE %.11f %.11f %.4f",
                  base_latitude, base_longitude, base_height);
    return fixed_command;
  }
  return profile_command(device_config, step);
}

const char *pending_profile_command() {
  const char *command = active_profile_command(profile_step);
  return command == nullptr ? "" : command;
}

void apply_unit_profile() {
  observers->receiver_reset();
  gnss_handshake_step = 0;
  survey_reference_ms = 0;
  if (is_base() && base_settings_failed) {
    unit_profile_applied = false;
    profile_failed = true;
    profile_running = false;
    return;
  }
  unit_profile_applied = false;
  profile_running = true;
  profile_failed = profile_waiting = profile_ack = profile_mode_verified = false;
  profile_step = profile_attempts = 0;
  rtcm_command_acks = 0;
  latest_gga = GgaData{};
  latest_horizontal_accuracy = HorizontalAccuracyData{};
  Serial.printf("PROFILE START: %s\n", is_base() ? "BASE_TEST" : "ROVER_SURVEY");
}

void service_startup(uint32_t now_ms) {
  if (profile_running) {
    gnss_handshake_step = 0;
    return;
  }
  const bool fresh_gga = last_gga_ms > 0 && now_ms - last_gga_ms < 5000;
  // UNLOG/profile changes can finish before the next 1 Hz GGA arrives.
  if (unit_profile_applied && !fresh_gga && now_ms - profile_complete_ms < 2000) return;

  if (version_ok && fresh_gga) {
    gnss_handshake_step = 0;
    if (!gnss_startup_complete) {
      gnss_startup_complete = true;
      Serial.printf("GNSS STARTUP: PASS after %lu attempt(s)\n",
                    static_cast<unsigned long>(gnss_handshake_attempts));
      if (!unit_profile_applied && !profile_failed) apply_unit_profile();
    }
    return;
  }

  if (gnss_startup_complete) {
    Serial.println("GNSS STARTUP: link lost; automatic profile will be reapplied");
    version_ok = false;
    unit_profile_applied = false;
    profile_failed = false;
    latest_gga = GgaData{};
    latest_horizontal_accuracy = HorizontalAccuracyData{};
    rtcm_last_rx_ms = 0;
    observers->receiver_reset();
    std::strcpy(receiver_role, "UNKNOWN");
    rtcm_command_acks = 0;
  }

  gnss_startup_complete = false;
  if (!gnss_handshake_step) {
    if (int32_t(now_ms - next_gnss_handshake_ms) < 0) return;
    ++gnss_handshake_attempts;
    gnss_handshake_step = 1;
    gnss_next_command_ms = now_ms;
    Serial.printf("GNSS STARTUP: attempt %lu\n",
                  static_cast<unsigned long>(gnss_handshake_attempts));
  }
  if (int32_t(now_ms - gnss_next_command_ms) < 0) return;
  static const char *const commands[] = {"VERSION", "GPGGA COM2 1", "GPRMC COM2 1",
                                         "BESTNAVA COM2 1", "MODE"};
  send_command(commands[gnss_handshake_step - 1]);
  if (++gnss_handshake_step > 5) {
    gnss_handshake_step = 0;
    next_gnss_handshake_ms = now_ms + 3000;
  } else {
    gnss_next_command_ms = now_ms + 100;
  }
}

void service_profile(uint32_t now_ms) {
  if (!profile_running) return;
  const char *command = active_profile_command(profile_step);
  if (profile_waiting) {
    const bool verified = std::strcmp(command, "MODE") != 0 || profile_mode_verified;
    if (profile_ack && verified && now_ms - profile_sent_ms >= 100) {
      ++profile_step;
      profile_attempts = 0;
      profile_waiting = false;
      command = active_profile_command(profile_step);
      if (command == nullptr) {
        profile_running = false;
        unit_profile_applied = true;
        profile_complete_ms = now_ms;
        Serial.printf("PROFILE VERIFIED: %s\n", receiver_role);
        observers->log_config(is_base() ? "BASE_TEST" : "ROVER_SURVEY",
                              device_config.base_rtcm ? "VERIFIED|RTCM_ENABLED_IF_BASE"
                                                      : "VERIFIED|RTCM_OFF");
        return;
      }
    } else if (now_ms - profile_sent_ms < 2000) {
      return;
    } else if (profile_attempts >= 3) {
      profile_running = false;
      profile_failed = true;
      Serial.printf("PROFILE FAILED: timeout on %s\n", command);
      observers->log_event("PROFILE_FAILED", command);
      return;
    } else {
      profile_waiting = false;
    }
  }
  if (!profile_waiting) {
    profile_ack = profile_mode_verified = false;
    profile_waiting = true;
    profile_sent_ms = now_ms;
    ++profile_attempts;
    send_command(command);
  }
}

void reset_for_config_change() {
  unit_profile_applied = false;
  profile_failed = false;
  latest_gga = GgaData{};
  latest_horizontal_accuracy = HorizontalAccuracyData{};
  last_gga_ms = 0;
  clear_partial_input();
  rtcm_last_rx_ms = 0;
  std::strcpy(receiver_role, "UNKNOWN");
  while (gnss.available()) gnss.read();
  if (version_ok) apply_unit_profile();
  else gnss_startup_complete = false;
}

void reset_input_state() {
  clear_partial_input();
  latest_horizontal_accuracy = HorizontalAccuracyData{};
}

void reset_reference() { survey_reference_ms = 0; }

void query_role() { send_command("MODE"); }

size_t write_frame(const uint8_t *frame, size_t length, uint32_t now_ms) {
  if (gnss.availableForWrite() < int(length)) return 0;
  const size_t written = gnss.write(frame, length);
  if (written != length) return written;
  rtcm_last_message = correction::message_type(frame);
  rtcm_last_rx_ms = now_ms;
  capture_reference(frame, length, now_ms);
  return written;
}

GnssSnapshot snapshot() {
  GnssSnapshot out;
  out.gga = latest_gga;
  out.accuracy = latest_horizontal_accuracy;
  out.time = latest_gnss_time;
  out.gga_ms = last_gga_ms;
  out.role = receiver_role;
  out.version_ok = version_ok;
  out.startup_complete = gnss_startup_complete;
  out.profile_applied = unit_profile_applied;
  out.profile_running = profile_running;
  out.profile_failed = profile_failed;
  out.uart_seen = byte_count > 0;
  out.last_rx_ms = last_rx_ms;
  out.bytes = byte_count;
  out.lines = line_count;
  out.gga_count = gga_count;
  out.bestnav_count = bestnav_count;
  out.rmc_count = rmc_count;
  out.checksum_errors = gnss_parse_stats.checksum_errors;
  out.rtcm_frames = rtcm_uart_frames;
  out.rtcm_bad = rtcm_uart_bad;
  out.rtcm_last_message = rtcm_last_message;
  out.rtcm_last_rx_ms = rtcm_last_rx_ms;
  out.reference = survey_reference;
  out.station = survey_station;
  out.reference_ms = survey_reference_ms;
  return out;
}

}  // namespace gnss_service
