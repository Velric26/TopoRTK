#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <TCA9554.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "device_config.h"
#include "receiver_reply.h"
#include "touch_layout.h"
#include "web_http.h"
#include "rover_ap.h"
#include "survey_service.h"
#include "wifi_credentials.h"

#ifndef TOPORTK_DISPLAY_ROTATION
#define TOPORTK_DISPLAY_ROTATION 2
#endif

#ifndef TOPORTK_UNIT_ID
#define TOPORTK_UNIT_ID 1
#endif

static_assert(TOPORTK_DISPLAY_ROTATION == 0 || TOPORTK_DISPLAY_ROTATION == 2,
              "The 320x480 touchscreen layout requires portrait rotation 0 or 2");
static_assert(TOPORTK_UNIT_ID >= 1 && TOPORTK_UNIT_ID <= 26,
              "TOPORTK_UNIT_ID must be between 1 (A) and 26 (Z)");

namespace board {
constexpr int kBacklight = 6;
constexpr uint8_t kBacklightPwmChannel = 0;
constexpr uint32_t kBacklightPwmHz = 5000;
constexpr uint8_t kNightBacklightDuty = 26;  // About 10%; visibly different from Day.
constexpr uint8_t kDayBacklightDuty = 255;
constexpr uint32_t kBacklightFadeMs = 1200;
constexpr int kSdData0 = 9;
constexpr int kSdCommand = 10;
constexpr int kSdClock = 11;
constexpr int kSpiMiso = 2;
constexpr int kSpiMosi = 1;
constexpr int kSpiClock = 5;
constexpr int kLcdChipSelect = -1;
constexpr int kLcdDataCommand = 3;
constexpr int kLcdReset = -1;
constexpr uint8_t kDisplayRotation = TOPORTK_DISPLAY_ROTATION;
constexpr char kUnitLabel = 'A' + TOPORTK_UNIT_ID - 1;
constexpr int kWidth = 320;
constexpr int kHeight = 480;
constexpr int kI2cSda = 8;
constexpr int kI2cScl = 7;
constexpr uint8_t kIoExpanderAddress = 0x20;
constexpr uint8_t kTouchAddress = 0x38;
constexpr uint8_t kLcdResetExpanderPin = 1;
constexpr int kGnssRx = 44;
constexpr int kGnssTx = 43;
constexpr uint32_t kGnssBaud = 115200;
}  // namespace board

namespace network {
constexpr char kDirectSsid[] = "TopoRTK-Link-Test";
constexpr char kDirectPassword[] = "TopoRTK-test-2026";
constexpr const char *kLocalSsid = toportk::wifi_credentials::kLocalSsid;
constexpr const char *kLocalPassword = toportk::wifi_credentials::kLocalPassword;
constexpr uint16_t kUdpPort = 22345;
constexpr uint32_t kMagic = 0x5452544B;
constexpr uint8_t kVersion = 2;
constexpr uint8_t kHelloPacket = 1;
constexpr uint8_t kTestPacket = 2;
constexpr uint8_t kRtcmPacket = 3;
constexpr char kMarker[] = "TOPO-RTK-WIFI";
constexpr uint32_t kHelloIntervalMs = 1000;
constexpr uint32_t kTestIntervalMs = 250;
constexpr uint32_t kStatusIntervalMs = 2000;
}  // namespace network

namespace colors {
constexpr uint16_t kBackground = RGB565_BLACK;
constexpr uint16_t kHeaderReady = 0x0400;
constexpr uint16_t kHeaderNotReady = 0x4208;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kMuted = 0xD69A;
constexpr uint16_t kWarning = 0xFD20;
constexpr uint16_t kAccent = 0x4E9F;
constexpr uint16_t kBorder = 0x528A;
}  // namespace colors

struct GgaData {
  bool received = false;
  int quality = 0;
  int satellites = 0;
  double latitude = 0.0;
  double longitude = 0.0;
  double hdop = 0.0;
  double altitude = 0.0;
  char utc[16] = "---";
};

struct HorizontalAccuracyData {
  bool received = false;
  double latitude_sigma_m = 0.0;
  double longitude_sigma_m = 0.0;
  double horizontal_1drms_m = 0.0;
  uint32_t received_ms = 0;
  double vertical_sigma_m = 0;
  uint32_t differential_age_ms=UINT32_MAX;
  uint16_t solution_station=65535;
  survey::Position position;
  uint64_t epoch = 0;
  bool position_valid = false;
  bool rtk_fixed = false;
};

struct GnssTimeData {
  bool received = false;
  bool valid = false;
  bool navigation_valid = false;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t day = 0;
  uint8_t month = 0;
  uint16_t year = 0;
  uint32_t received_ms = 0;
};

struct __attribute__((packed)) WiFiTestPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t packet_size;
  uint32_t sequence;
  uint32_t sender_ms;
  int16_t link_rssi_dbm;
  uint16_t reserved;
  char marker[16];
  uint32_t checksum;
};

struct __attribute__((packed)) WiFiRtcmHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t packet_size;
  uint32_t sequence;
  uint32_t sender_ms;
  uint16_t rtcm_length;
  uint16_t rtcm_message;
  uint32_t checksum;
};

constexpr size_t kMaxRtcmFrameSize = 1029;
constexpr size_t kMaxWiFiRtcmPacketSize =
    sizeof(WiFiRtcmHeader) + kMaxRtcmFrameSize;

TCA9554 io_expander(board::kIoExpanderAddress);
Arduino_DataBus *display_bus = new Arduino_ESP32SPI(
    board::kLcdDataCommand, board::kLcdChipSelect, board::kSpiClock,
    board::kSpiMosi, board::kSpiMiso);
Arduino_GFX *display = new Arduino_ST7796(
    display_bus, board::kLcdReset, board::kDisplayRotation, true,
    board::kWidth, board::kHeight);
HardwareSerial gnss(1);
WiFiUDP wifi_udp;

bool display_ready = false;
bool touch_ready = false;
bool touch_active = false;
bool touch_cancelled = false;
bool touch_moved = false;
uint32_t touch_start_ms = 0;
int16_t touch_start_x = 0;
int16_t touch_start_y = 0;
int16_t touch_last_x = 0;
int16_t touch_last_y = 0;
uint32_t touch_last_seen_ms = 0;
uint32_t last_touch_poll_ms = 0;
ScreenPage current_page = ScreenPage::kMain;
uint32_t phone_key_shown_ms = 0;
uint32_t phone_key_confirm_ms = 0;
struct UiRegionCache {
  bool valid = false;
  ScreenPage page = ScreenPage::kMain;
  uint8_t type = 0;
  int16_t y = 0;
  char signature[128] = {};
};
constexpr size_t kUiRegionCacheCount = 24;
UiRegionCache ui_region_cache[kUiRegionCacheCount];
BrightnessMode brightness_mode = BrightnessMode::kAutomatic;
DeviceConfig device_config;
DeviceRole pending_role = DeviceRole::kRover;
Preferences preferences;
bool preferences_ready = false;
bool config_saved = false;
bool config_error = false;
bool profile_running = false;
bool profile_failed = false;
bool profile_waiting = false;
bool profile_ack = false;
bool profile_mode_verified = false;
uint8_t profile_step = 0;
uint8_t profile_attempts = 0;
uint32_t profile_sent_ms = 0;
uint32_t profile_complete_ms = 0;
struct BaseSettings {
  uint32_t magic=0x54425331U,fixed=0,revision=0;
  double latitude=0,longitude=0,height=0;
  uint32_t checksum=0;
};
BaseSettings base_settings;
bool base_settings_failed=false;
uint32_t base_attempt_revision=0;
survey::Cartesian survey_reference;
uint16_t survey_station=0;
uint32_t survey_reference_ms=0;

void capture_reference(const uint8_t *frame,size_t length) {
  if(survey::reference_station(frame,length,survey_reference,survey_station))survey_reference_ms=millis();
}

const char *active_profile_command(uint8_t step) {
  static char fixed_command[96];
  if(device_config.role==DeviceRole::kBase && base_settings.fixed && step==1) {
    std::snprintf(fixed_command,sizeof(fixed_command),"MODE BASE %.11f %.11f %.4f",base_settings.latitude,base_settings.longitude,base_settings.height);
    return fixed_command;
  }
  return profile_command(device_config,step);
}

bool is_base() { return device_config.role == DeviceRole::kBase; }
uint8_t backlight_duty = 255;
uint8_t target_backlight_duty = 255;
uint32_t last_brightness_step_ms = 0;
bool backlight_pwm_ready = false;
bool unit_profile_applied = false;
bool version_ok = false;
char rx_line[512] = {};
size_t rx_length = 0;
char usb_line[64] = {};
size_t usb_length = 0;
char receiver_role[8] = "UNKNOWN";
char last_displayed_fix_label[20] = {};
uint32_t byte_count = 0;
uint32_t line_count = 0;
uint32_t gga_count = 0;
uint32_t bestnav_count = 0;
uint32_t rmc_count = 0;
uint32_t checksum_errors = 0;
uint32_t last_rx_ms = 0;
uint32_t last_screen_ms = 0;
uint32_t last_web_status_ms = 0;
uint32_t last_gga_ms = 0;
uint32_t next_gnss_handshake_ms = 0;
uint32_t gnss_handshake_attempts = 0;
bool gnss_startup_complete = false;
bool wifi_udp_started = false;
bool wifi_peer_known = false;
IPAddress wifi_peer;
int16_t wifi_peer_rssi_dbm = 0;
uint32_t wifi_last_peer_ms = 0;
uint32_t wifi_tx_sequence = 0;
uint32_t wifi_tx_packets = 0;
uint32_t wifi_rx_packets = 0;
uint32_t wifi_invalid_packets = 0;
uint32_t wifi_sequence_gaps = 0;
uint32_t wifi_last_sequence = 0;
bool wifi_have_sequence = false;
uint32_t last_wifi_send_ms = 0;
uint32_t last_wifi_status_ms = 0;
uint32_t last_wifi_connect_attempt_ms = 0;
uint8_t rtcm_command_acks = 0;
uint8_t rtcm_frame[kMaxRtcmFrameSize] = {};
size_t rtcm_frame_length = 0;
size_t rtcm_expected_length = 0;
uint32_t rtcm_last_byte_ms = 0;
uint32_t rtcm_uart_frames = 0;
uint32_t rtcm_uart_bad = 0;
uint32_t rtcm_wifi_sequence = 0;
uint32_t rtcm_wifi_tx_frames = 0;
uint32_t rtcm_wifi_rx_frames = 0;
uint32_t rtcm_forwarded_bytes = 0;
uint16_t rtcm_last_message = 0;
uint32_t rtcm_last_rx_ms = 0;
GgaData latest_gga;
HorizontalAccuracyData latest_horizontal_accuracy;
GnssTimeData latest_gnss_time;
bool sd_ready = false;
bool sd_test_passed = false;
char sd_session_path[128] = {};
bool sd_state_initialized = false;
bool sd_last_uart_active = false;
bool sd_last_linked = false;
bool sd_last_rtcm_active = false;
int sd_last_fix_quality = -1;
char sd_last_role[8] = {};
uint32_t last_sd_solution_ms = 0;

void sd_log_event(const char *event, const char *detail);
void apply_unit_profile();

bool using_local_router() {
  return device_config.wifi_mode == WiFiMode::kLocalRouter;
}

const char *wifi_transport_label() {
  return using_local_router() ? "LOCAL ROUTER" : "DIRECT LINK";
}

const char *active_wifi_ssid() {
  return using_local_router() ? network::kLocalSsid : network::kDirectSsid;
}

const char *active_wifi_password() {
  return using_local_router() ? network::kLocalPassword : network::kDirectPassword;
}

bool station_connected() {
  return WiFi.status() == WL_CONNECTED;
}

bool on_station_subnet(IPAddress ip) {
  if (!station_connected()) return false;
  const IPAddress local = WiFi.localIP(), mask = WiFi.subnetMask();
  for (int i=0; i<4; ++i) if ((ip[i] & mask[i]) != (local[i] & mask[i])) return false;
  return true;
}

IPAddress station_broadcast() {
  const IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
  return IPAddress(ip[0] | uint8_t(~mask[0]),ip[1] | uint8_t(~mask[1]),
                   ip[2] | uint8_t(~mask[2]),ip[3] | uint8_t(~mask[3]));
}

void load_config() {
  device_config.role = TOPORTK_UNIT_ID == 1 ? DeviceRole::kBase : DeviceRole::kRover;
  preferences_ready = preferences.begin("toportk", false);
  config_error = !preferences_ready;
  if (preferences_ready && preferences.isKey("config")) {
    config_saved = decode_config(preferences.getUInt("config", 0), device_config);
    config_error = !config_saved;
  }
  brightness_mode = device_config.brightness;
  pending_role = device_config.role;
  Serial.printf("CONFIG LOAD: %s role=%s brightness=%u rtcm=%s wifi=%s\n",
                config_error ? "ERROR / DEFAULTS" : config_saved ? "SAVED" : "DEFAULTS",
                is_base() ? "BASE" : "ROVER", static_cast<unsigned>(brightness_mode),
                device_config.base_rtcm ? "ON" : "OFF", wifi_transport_label());
}

void load_base_settings() {
  Preferences p;
  if(!p.begin("topobase",false)){base_settings_failed=true;if(is_base())profile_failed=true;return;}
  if(p.isKey("settings")){
    BaseSettings saved;
    const bool ok=p.getBytes("settings",&saved,sizeof(saved))==sizeof(saved) && saved.magic==0x54425331U && saved.fixed<=1 &&
      saved.checksum==survey::crc32(std::string(reinterpret_cast<const char *>(&saved),offsetof(BaseSettings,checksum))) &&
      std::isfinite(saved.latitude)&&std::isfinite(saved.longitude)&&std::isfinite(saved.height)&&
      saved.latitude>=-80 && saved.latitude<=84 && std::abs(saved.longitude)<=180 && saved.height>=-1000 && saved.height<=10000;
    if(ok)base_settings=saved;else base_settings_failed=true;
  }
  p.end();
  if(base_settings_failed && is_base())profile_failed=true;
}

bool save_config(const DeviceConfig &requested) {
  const uint32_t word = encode_config(requested);
  if (config_saved && !config_error && word == encode_config(device_config)) return true;
  if (!preferences_ready) preferences_ready = preferences.begin("toportk", false);
  if (!preferences_ready || preferences.putUInt("config", word) != sizeof(word) ||
      preferences.getUInt("config", 0) != word) {
    config_error = true;
    Serial.println("CONFIG SAVE: FAILED; selection was not applied");
    sd_log_event("CONFIG_ERROR", "NVS_WRITE_FAILED");
    return false;
  }
  config_saved = true;
  config_error = false;
  Serial.println("CONFIG SAVE: PASS");
  return true;
}

void reset_ui_region_cache() {
  for (UiRegionCache &region : ui_region_cache) region.valid = false;
}

bool ui_region_changed(uint8_t type, int16_t y, const char *signature) {
  UiRegionCache *empty = nullptr;
  for (UiRegionCache &region : ui_region_cache) {
    if (!region.valid) {
      if (empty == nullptr) empty = &region;
      continue;
    }
    if (region.page != current_page || region.type != type || region.y != y) {
      continue;
    }
    if (std::strncmp(region.signature, signature,
                     sizeof(region.signature)) == 0) {
      return false;
    }
    std::strncpy(region.signature, signature, sizeof(region.signature) - 1);
    region.signature[sizeof(region.signature) - 1] = '\0';
    return true;
  }

  if (empty != nullptr) {
    empty->valid = true;
    empty->page = current_page;
    empty->type = type;
    empty->y = y;
    std::strncpy(empty->signature, signature, sizeof(empty->signature) - 1);
    empty->signature[sizeof(empty->signature) - 1] = '\0';
  }
  return true;
}

uint32_t fnv1a(const uint8_t *bytes, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t packet_checksum(WiFiTestPacket packet) {
  packet.checksum = 0;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&packet);
  return fnv1a(bytes, sizeof(packet));
}

uint32_t crc24q(const uint8_t *bytes, size_t length) {
  uint32_t crc = 0;
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint32_t>(bytes[index]) << 16;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc <<= 1;
      if ((crc & 0x1000000UL) != 0) crc ^= 0x1864CFBUL;
    }
  }
  return crc & 0xFFFFFFUL;
}

bool valid_rtcm_frame(const uint8_t *frame, size_t length) {
  if (frame == nullptr || length < 8 || length > kMaxRtcmFrameSize ||
      frame[0] != 0xD3) {
    return false;
  }
  const size_t payload_length =
      (static_cast<size_t>(frame[1] & 0x03) << 8) | frame[2];
  if (length != payload_length + 6) return false;
  const uint32_t expected_crc =
      (static_cast<uint32_t>(frame[length - 3]) << 16) |
      (static_cast<uint32_t>(frame[length - 2]) << 8) |
      frame[length - 1];
  return expected_crc == crc24q(frame, length - 3);
}

uint16_t rtcm_message_type(const uint8_t *frame, size_t length) {
  if (length < 8) return 0;
  return (static_cast<uint16_t>(frame[3]) << 4) | (frame[4] >> 4);
}

bool valid_wifi_packet(WiFiTestPacket packet) {
  if (packet.magic != network::kMagic ||
      packet.version != network::kVersion ||
      packet.packet_size != sizeof(packet) ||
      std::strncmp(packet.marker, network::kMarker, sizeof(packet.marker)) != 0) {
    return false;
  }
  const uint32_t received_checksum = packet.checksum;
  return received_checksum == packet_checksum(packet);
}

bool send_wifi_packet(const IPAddress &destination, uint8_t type,
                      uint32_t sequence) {
  if (!wifi_udp_started) return false;

  WiFiTestPacket packet = {};
  packet.magic = network::kMagic;
  packet.version = network::kVersion;
  packet.type = type;
  packet.packet_size = sizeof(packet);
  packet.sequence = sequence;
  packet.sender_ms = millis();
  packet.link_rssi_dbm =
      !is_base() && WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  std::strncpy(packet.marker, network::kMarker, sizeof(packet.marker) - 1);
  packet.checksum = packet_checksum(packet);

  if (!wifi_udp.beginPacket(destination, network::kUdpPort)) return false;
  const size_t written = wifi_udp.write(
      reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  return written == sizeof(packet) && wifi_udp.endPacket() == 1;
}

bool send_rtcm_packet(const uint8_t *frame, size_t frame_length,
                      uint16_t message_type) {
  if (!wifi_udp_started || !wifi_peer_known || frame == nullptr ||
      frame_length > kMaxRtcmFrameSize) {
    return false;
  }

  uint8_t packet[kMaxWiFiRtcmPacketSize] = {};
  WiFiRtcmHeader header = {};
  header.magic = network::kMagic;
  header.version = network::kVersion;
  header.type = network::kRtcmPacket;
  header.packet_size = sizeof(header) + frame_length;
  header.sequence = ++rtcm_wifi_sequence;
  header.sender_ms = millis();
  header.rtcm_length = frame_length;
  header.rtcm_message = message_type;
  header.checksum = 0;
  std::memcpy(packet, &header, sizeof(header));
  std::memcpy(packet + sizeof(header), frame, frame_length);
  header.checksum = fnv1a(packet, header.packet_size);
  std::memcpy(packet, &header, sizeof(header));

  if (!wifi_udp.beginPacket(wifi_peer, network::kUdpPort)) return false;
  const size_t written = wifi_udp.write(packet, header.packet_size);
  return written == header.packet_size && wifi_udp.endPacket() == 1;
}

bool handle_wifi_rtcm_packet(uint8_t *packet, size_t packet_length) {
  if (is_base() || !unit_profile_applied || packet == nullptr ||
      packet_length < sizeof(WiFiRtcmHeader)) {
    return false;
  }

  WiFiRtcmHeader header = {};
  std::memcpy(&header, packet, sizeof(header));
  if (header.magic != network::kMagic ||
      header.version != network::kVersion ||
      header.type != network::kRtcmPacket ||
      header.packet_size != packet_length ||
      header.rtcm_length + sizeof(header) != packet_length ||
      header.rtcm_length > kMaxRtcmFrameSize) {
    return false;
  }

  const uint32_t received_checksum = header.checksum;
  header.checksum = 0;
  std::memcpy(packet, &header, sizeof(header));
  if (received_checksum != fnv1a(packet, packet_length)) return false;

  const uint8_t *frame = packet + sizeof(header);
  if (!valid_rtcm_frame(frame, header.rtcm_length) ||
      header.rtcm_message != rtcm_message_type(frame, header.rtcm_length)) {
    return false;
  }

  const size_t written = gnss.write(frame, header.rtcm_length);
  if (written != header.rtcm_length) return false;
  ++rtcm_wifi_rx_frames;
  rtcm_forwarded_bytes += written;
  rtcm_last_message = header.rtcm_message;
  rtcm_last_rx_ms = millis();
  wifi_last_peer_ms = millis();
  capture_reference(frame,header.rtcm_length);
  return true;
}

void start_wifi() {
  survey_revoke_control();
  survey_reference_ms=0;
  stop_rover_ap();
  phone_key_shown_ms = phone_key_confirm_ms = 0;
  wifi_udp.stop();
  wifi_udp_started = false;
  WiFi.mode(WIFI_OFF);
  wifi_peer_known = false;
  wifi_peer = IPAddress();
  wifi_last_peer_ms = 0;
  wifi_peer_rssi_dbm = 0;
  wifi_have_sequence = false;
  wifi_last_sequence = 0;
  wifi_tx_sequence = wifi_tx_packets = wifi_rx_packets = 0;
  wifi_sequence_gaps = wifi_invalid_packets = 0;
  rtcm_wifi_sequence = rtcm_wifi_tx_frames = rtcm_wifi_rx_frames = 0;
  rtcm_forwarded_bytes = rtcm_uart_frames = rtcm_uart_bad = 0;
  rtcm_last_rx_ms = rtcm_last_message = 0;
  last_wifi_send_ms = 0;
  WiFi.persistent(false);
  WiFi.setSleep(false);

  if (!using_local_router() && is_base()) {
    WiFi.mode(WIFI_AP);
    const IPAddress address(192, 168, 4, 1);
    const IPAddress gateway(192, 168, 4, 1);
    const IPAddress subnet(255, 255, 255, 0);
    const bool configured = WiFi.softAPConfig(address, gateway, subnet);
    const bool started = WiFi.softAP(network::kDirectSsid, network::kDirectPassword,
                                     6, false, 1);
    wifi_udp_started = started && wifi_udp.begin(network::kUdpPort);
    Serial.printf("WIFI BASE: AP config=%s start=%s UDP=%s IP=%s\n",
                  configured ? "PASS" : "FAIL", started ? "PASS" : "FAIL",
                  wifi_udp_started ? "PASS" : "FAIL",
                  WiFi.softAPIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(active_wifi_ssid(), active_wifi_password());
    if (!is_base()) start_rover_ap(board::kUnitLabel);
    last_wifi_connect_attempt_ms = millis();
    Serial.printf("WIFI %s: %s connecting to %s\n", is_base() ? "BASE" : "ROVER",
                  wifi_transport_label(), active_wifi_ssid());
  }
}

bool select_config(const DeviceConfig &requested) {
  if (profile_running) return false;
  if (!save_config(requested)) return false;
  const bool role_changed = requested.role != device_config.role;
  const bool profile_changed = role_changed || requested.base_rtcm != device_config.base_rtcm;
  const bool wifi_changed = requested.wifi_mode != device_config.wifi_mode;
  device_config = requested;
  if (brightness_mode != requested.brightness) last_brightness_step_ms = millis();
  brightness_mode = requested.brightness;
  if (role_changed || wifi_changed) start_wifi();
  if (wifi_changed) {
    sd_log_event("WIFI_MODE", using_local_router() ? "LOCAL_ROUTER" : "DIRECT_LINK");
  }
  if (profile_changed || profile_failed) {
    unit_profile_applied = false;
    profile_failed = false;
    latest_gga = GgaData{};
    latest_horizontal_accuracy = HorizontalAccuracyData{};
    last_gga_ms = 0;
    rtcm_frame_length = rtcm_expected_length = rx_length = 0;
    rtcm_last_rx_ms = 0;
    std::strcpy(receiver_role, "UNKNOWN");
    while (gnss.available()) gnss.read();
    if (version_ok) apply_unit_profile();
    else gnss_startup_complete = false;
    sd_log_event("CONFIG_SELECTED", is_base() ? "BASE_TEST" : "ROVER_SURVEY");
  }
  return true;
}

void select_role(DeviceRole role) {
  DeviceConfig requested = device_config;
  requested.role = role;
  requested.base_rtcm = true;
  select_config(requested);
}

void select_brightness(BrightnessMode mode) {
  DeviceConfig requested = device_config;
  requested.brightness = mode;
  select_config(requested);
}

void select_wifi_mode(WiFiMode mode) {
  DeviceConfig requested = device_config;
  requested.wifi_mode = mode;
  select_config(requested);
}

void receive_wifi_packets() {
  if (!wifi_udp_started) return;

  int packet_length = 0;
  while ((packet_length = wifi_udp.parsePacket()) > 0) {
    // Phone AP clients are not correction peers. Accept Rover input only from
    // the upstream station subnet, never the independent phone subnet.
    if (!is_base() && !on_station_subnet(wifi_udp.remoteIP())) {
      while (wifi_udp.available() > 0) wifi_udp.read();
      continue;
    }
    if (packet_length > static_cast<int>(kMaxWiFiRtcmPacketSize)) {
      while (wifi_udp.available() > 0) wifi_udp.read();
      ++wifi_invalid_packets;
      continue;
    }

    uint8_t packet_bytes[kMaxWiFiRtcmPacketSize] = {};
    const int bytes_read = wifi_udp.read(packet_bytes, packet_length);
    if (bytes_read != packet_length) {
      ++wifi_invalid_packets;
      continue;
    }

    if (packet_length >= static_cast<int>(sizeof(WiFiRtcmHeader)) &&
        packet_bytes[5] == network::kRtcmPacket) {
      if (!handle_wifi_rtcm_packet(packet_bytes, packet_length)) {
        ++wifi_invalid_packets;
      }
      continue;
    }

    if (packet_length != static_cast<int>(sizeof(WiFiTestPacket))) {
      ++wifi_invalid_packets;
      continue;
    }

    WiFiTestPacket packet = {};
    std::memcpy(&packet, packet_bytes, sizeof(packet));
    if (!valid_wifi_packet(packet)) {
      ++wifi_invalid_packets;
      continue;
    }

    if (is_base() && packet.type == network::kHelloPacket) {
      wifi_peer = wifi_udp.remoteIP();
      wifi_peer_known = true;
      wifi_peer_rssi_dbm = packet.link_rssi_dbm;
      wifi_last_peer_ms = millis();
      ++wifi_rx_packets;
    } else if (!is_base() && packet.type == network::kTestPacket) {
      wifi_peer_rssi_dbm = WiFi.RSSI();
      wifi_last_peer_ms = millis();
      ++wifi_rx_packets;
      if (wifi_have_sequence) {
        if (packet.sequence > wifi_last_sequence + 1) {
          wifi_sequence_gaps += packet.sequence - wifi_last_sequence - 1;
        } else if (packet.sequence <= wifi_last_sequence) {
          ++wifi_invalid_packets;
        }
      }
      wifi_last_sequence = packet.sequence;
      wifi_have_sequence = true;
    } else {
      ++wifi_invalid_packets;
    }
  }
}

void service_wifi() {
  const uint32_t now = millis();
  service_rover_ap();

  if (station_connected() && !wifi_udp_started) {
    wifi_udp_started = wifi_udp.begin(network::kUdpPort);
    Serial.printf("WIFI %s: %s connected UDP=%s IP=%s RSSI=%d dBm\n",
                  is_base() ? "BASE" : "ROVER", wifi_transport_label(),
                  wifi_udp_started ? "PASS" : "FAIL",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }

  const bool needs_station = using_local_router() || !is_base();
  if (needs_station && !station_connected() &&
      now - last_wifi_connect_attempt_ms >= 10000) {
    last_wifi_connect_attempt_ms = now;
    WiFi.disconnect();
    WiFi.begin(active_wifi_ssid(), active_wifi_password());
    Serial.printf("WIFI %s: %s retrying connection\n",
                  is_base() ? "BASE" : "ROVER", wifi_transport_label());
  }

  receive_wifi_packets();

  if (is_base() && wifi_peer_known &&
      now - last_wifi_send_ms >= network::kTestIntervalMs) {
    last_wifi_send_ms = now;
    if (send_wifi_packet(wifi_peer, network::kTestPacket, ++wifi_tx_sequence)) {
      ++wifi_tx_packets;
    }
  } else if (!is_base() && station_connected() &&
             wifi_udp_started &&
             now - last_wifi_send_ms >= network::kHelloIntervalMs) {
    last_wifi_send_ms = now;
    const IPAddress destination = using_local_router()
                                    ? station_broadcast()
                                    : IPAddress(192, 168, 4, 1);
    if (send_wifi_packet(destination, network::kHelloPacket, ++wifi_tx_sequence)) {
      ++wifi_tx_packets;
    }
  }

  if (now - last_wifi_status_ms >= network::kStatusIntervalMs) {
    last_wifi_status_ms = now;
    if (is_base()) {
      Serial.printf("WIFI BASE: transport=%s station=%s clients=%u peer=%s TX=%lu hello_RX=%lu bad=%lu RTCM_UART=%lu RTCM_TX=%lu RTCM_BAD=%lu last=%u\n",
                    wifi_transport_label(), station_connected() ? "UP" : "DOWN",
                    using_local_router() ? 0 : WiFi.softAPgetStationNum(),
                    wifi_peer_known ? "YES" : "NO",
                    static_cast<unsigned long>(wifi_tx_packets),
                    static_cast<unsigned long>(wifi_rx_packets),
                    static_cast<unsigned long>(wifi_invalid_packets),
                    static_cast<unsigned long>(rtcm_uart_frames),
                    static_cast<unsigned long>(rtcm_wifi_tx_frames),
                    static_cast<unsigned long>(rtcm_uart_bad), rtcm_last_message);
    } else {
      Serial.printf("WIFI ROVER: transport=%s link=%s RSSI=%d TX_hello=%lu RX=%lu gap=%lu bad=%lu last=%lu RTCM_RX=%lu bytes=%lu type=%u\n",
                    wifi_transport_label(), station_connected() ? "UP" : "DOWN",
                    station_connected() ? WiFi.RSSI() : 0,
                    static_cast<unsigned long>(wifi_tx_packets),
                    static_cast<unsigned long>(wifi_rx_packets),
                    static_cast<unsigned long>(wifi_sequence_gaps),
                    static_cast<unsigned long>(wifi_invalid_packets),
                    static_cast<unsigned long>(wifi_last_sequence),
                    static_cast<unsigned long>(rtcm_wifi_rx_frames),
                    static_cast<unsigned long>(rtcm_forwarded_bytes),
                    rtcm_last_message);
    }
  }
}

void reset_lcd() {
  io_expander.write1(board::kLcdResetExpanderPin, HIGH);
  delay(10);
  io_expander.write1(board::kLcdResetExpanderPin, LOW);
  delay(10);
  io_expander.write1(board::kLcdResetExpanderPin, HIGH);
  delay(200);
}

bool i2c_device_present(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

enum class TouchRead { kError, kReleased, kContact, kMultiple };

TouchRead read_touch(int16_t &x, int16_t &y) {
  constexpr uint8_t kFirstRegister = 0x00;
  constexpr size_t kReadLength = 7;
  uint8_t data[kReadLength] = {};

  Wire.beginTransmission(board::kTouchAddress);
  Wire.write(kFirstRegister);
  if (Wire.endTransmission(false) != 0) return TouchRead::kError;

  const size_t received = Wire.requestFrom(
      static_cast<int>(board::kTouchAddress), static_cast<int>(kReadLength));
  if (received != kReadLength) {
    while (Wire.available()) Wire.read();
    return TouchRead::kError;
  }
  for (size_t index = 0; index < kReadLength; ++index) {
    data[index] = Wire.read();
  }

  const uint8_t points = data[2] & 0x0F;
  if (points == 0) return TouchRead::kReleased;
  if (points != 1) return TouchRead::kMultiple;
  x = static_cast<int16_t>(((data[3] & 0x0F) << 8) | data[4]);
  y = static_cast<int16_t>(((data[5] & 0x0F) << 8) | data[6]);
  if (x < 0 || x >= board::kWidth || y < 0 || y >= board::kHeight) return TouchRead::kError;
  if (board::kDisplayRotation == 2) {
    x = board::kWidth - 1 - x;
    y = board::kHeight - 1 - y;
  }
  return TouchRead::kContact;
}

int hex_value(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

uint32_t ascii_crc32(const char *begin, const char *end) {
  uint32_t crc = 0;
  for (const char *cursor = begin; cursor < end; ++cursor) {
    crc ^= static_cast<uint8_t>(*cursor);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1U) != 0 ? 0xEDB88320UL : 0);
    }
  }
  return crc;
}

bool valid_unicore_ascii_crc(const char *line) {
  if (line == nullptr || line[0] != '#') return false;
  const char *star = std::strrchr(line, '*');
  if (star == nullptr || std::strlen(star + 1) != 8) return false;

  char *end = nullptr;
  const unsigned long expected = std::strtoul(star + 1, &end, 16);
  if (end == nullptr || *end != '\0') return false;
  return static_cast<uint32_t>(expected) == ascii_crc32(line + 1, star);
}

bool valid_nmea_checksum(const char *line) {
  if (line == nullptr || line[0] != '$') return false;
  const char *star = std::strchr(line, '*');
  if (star == nullptr || star[1] == '\0' || star[2] == '\0') return false;

  uint8_t calculated = 0;
  for (const char *cursor = line + 1; cursor < star; ++cursor) {
    calculated ^= static_cast<uint8_t>(*cursor);
  }

  const int high = hex_value(star[1]);
  const int low = hex_value(star[2]);
  return high >= 0 && low >= 0 && calculated == ((high << 4) | low);
}

double nmea_coordinate(const char *value, char hemisphere) {
  if (value == nullptr || value[0] == '\0') return 0.0;
  const double raw = std::strtod(value, nullptr);
  const double degrees = std::floor(raw / 100.0);
  double decimal = degrees + (raw - degrees * 100.0) / 60.0;
  if (hemisphere == 'S' || hemisphere == 'W') decimal = -decimal;
  return decimal;
}

bool parse_gga(const char *line, GgaData &result) {
  if (line == nullptr ||
      (std::strncmp(line, "$GNGGA,", 7) != 0 &&
       std::strncmp(line, "$GPGGA,", 7) != 0)) {
    return false;
  }
  if (!valid_nmea_checksum(line)) {
    ++checksum_errors;
    return false;
  }

  char copy[256] = {};
  std::strncpy(copy, line, sizeof(copy) - 1);
  char *star = std::strchr(copy, '*');
  if (star != nullptr) *star = '\0';

  char *fields[16] = {};
  size_t field_count = 0;
  fields[field_count++] = copy;
  for (char *cursor = copy; *cursor != '\0' && field_count < 16; ++cursor) {
    if (*cursor == ',') {
      *cursor = '\0';
      fields[field_count++] = cursor + 1;
    }
  }
  if (field_count < 11) return false;

  result.received = true;
  result.quality = std::atoi(fields[6]);
  result.satellites = std::atoi(fields[7]);
  result.hdop = std::strtod(fields[8], nullptr);
  result.altitude = std::strtod(fields[9], nullptr);
  result.latitude = nmea_coordinate(fields[2], fields[3][0]);
  result.longitude = nmea_coordinate(fields[4], fields[5][0]);

  if (fields[1][0] == '\0') {
    std::strcpy(result.utc, "---");
  } else if (std::strlen(fields[1]) >= 6) {
    std::snprintf(result.utc, sizeof(result.utc), "%.2s:%.2s:%.2s",
                  fields[1], fields[1] + 2, fields[1] + 4);
  } else {
    std::strncpy(result.utc, fields[1], sizeof(result.utc) - 1);
  }
  return true;
}

bool parse_rmc_time(const char *line, GnssTimeData &result) {
  if (line == nullptr ||
      (std::strncmp(line, "$GNRMC,", 7) != 0 &&
       std::strncmp(line, "$GPRMC,", 7) != 0)) {
    return false;
  }
  if (!valid_nmea_checksum(line)) {
    ++checksum_errors;
    return false;
  }

  char copy[256] = {};
  std::strncpy(copy, line, sizeof(copy) - 1);
  char *star = std::strchr(copy, '*');
  if (star != nullptr) *star = '\0';

  char *fields[16] = {};
  size_t field_count = 0;
  fields[field_count++] = copy;
  for (char *cursor = copy; *cursor != '\0' && field_count < 16; ++cursor) {
    if (*cursor == ',') {
      *cursor = '\0';
      fields[field_count++] = cursor + 1;
    }
  }
  if (field_count < 10 || std::strlen(fields[1]) < 6 ||
      std::strlen(fields[9]) != 6) {
    return false;
  }
  for (uint8_t index = 0; index < 6; ++index) {
    if (fields[1][index] < '0' || fields[1][index] > '9' ||
        fields[9][index] < '0' || fields[9][index] > '9') {
      return false;
    }
  }

  const int hour = (fields[1][0] - '0') * 10 + fields[1][1] - '0';
  const int minute = (fields[1][2] - '0') * 10 + fields[1][3] - '0';
  const int second = (fields[1][4] - '0') * 10 + fields[1][5] - '0';
  const int day = (fields[9][0] - '0') * 10 + fields[9][1] - '0';
  const int month = (fields[9][2] - '0') * 10 + fields[9][3] - '0';
  const int year = 2000 + (fields[9][4] - '0') * 10 + fields[9][5] - '0';
  const bool digits_valid =
      hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 &&
      second >= 0 && second <= 60 && day >= 1 && day <= 31 &&
      month >= 1 && month <= 12 && year >= 2020 && year <= 2099;

  result.received = true;
  // RMC status V means the navigation solution is invalid; the receiver can
  // still provide a valid GNSS-disciplined clock and date while indoors.
  result.valid = digits_valid;
  result.navigation_valid = fields[2][0] == 'A';
  result.hour = static_cast<uint8_t>(hour);
  result.minute = static_cast<uint8_t>(minute);
  result.second = static_cast<uint8_t>(second);
  result.day = static_cast<uint8_t>(day);
  result.month = static_cast<uint8_t>(month);
  result.year = static_cast<uint16_t>(year);
  result.received_ms = millis();
  return true;
}

bool parse_bestnav_accuracy(const char *line, HorizontalAccuracyData &result) {
  if (line == nullptr || std::strncmp(line, "#BESTNAVA,", 10) != 0) {
    return false;
  }
  if (!valid_unicore_ascii_crc(line)) {
    ++checksum_errors;
    return false;
  }

  char copy[512] = {};
  std::strncpy(copy, line, sizeof(copy) - 1);
  char *star = std::strrchr(copy, '*');
  if (star != nullptr) *star = '\0';
  char *body = std::strchr(copy, ';');
  if (body == nullptr) return false;
  *body='\0';
  ++body;

  char *header[10]={copy};size_t header_count=1;
  for(char *cursor=copy;*cursor && header_count<10;++cursor)if(*cursor==','){*cursor='\0';header[header_count++]=cursor+1;}

  char *fields[16] = {};
  size_t field_count = 0;
  fields[field_count++] = body;
  for (char *cursor = body; *cursor != '\0' && field_count < 16; ++cursor) {
    if (*cursor == ',') {
      *cursor = '\0';
      fields[field_count++] = cursor + 1;
    }
  }
  if (field_count < 10) return false;

  const double latitude_sigma = std::strtod(fields[7], nullptr);
  const double longitude_sigma = std::strtod(fields[8], nullptr);
  if (!std::isfinite(latitude_sigma) || !std::isfinite(longitude_sigma) ||
      latitude_sigma < 0.0 || longitude_sigma < 0.0 ||
      latitude_sigma > 10000.0 || longitude_sigma > 10000.0) {
    return false;
  }

  result.received = true;
  result.latitude_sigma_m = latitude_sigma;
  result.longitude_sigma_m = longitude_sigma;
  result.horizontal_1drms_m =
      std::sqrt(latitude_sigma * latitude_sigma +
                longitude_sigma * longitude_sigma);
  result.received_ms = millis();
  auto numeric=[](const char *field,double &value){char *end=nullptr;if(!field||!field[0])return false;
    value=std::strtod(field,&end);return end && *end=='\0' && std::isfinite(value);};
  double latitude=0,longitude=0,msl=0,undulation=0,vertical=0,week=0,tow=0,sigma_check=0,diff_age=0,solution_age=0,station_id=0;
  if(field_count>10 && fields[10][0]=='"'){++fields[10];char *quote=std::strchr(fields[10],'"');if(quote)*quote='\0';}
  result.position_valid=std::strcmp(fields[0],"SOL_COMPUTED")==0 && std::strcmp(fields[6],"WGS84")==0 &&
    numeric(fields[2],latitude)&&numeric(fields[3],longitude)&&numeric(fields[4],msl)&&numeric(fields[5],undulation)&&numeric(fields[9],vertical)&&
    numeric(fields[7],sigma_check)&&numeric(fields[8],sigma_check)&&latitude>=-80&&latitude<=84&&std::abs(longitude)<=180&&msl>=-1200&&msl<=15000&&std::abs(undulation)<=150&&vertical>=0&&vertical<=10000&&
    header_count>5&&std::strcmp(header[3],"FINE")==0&&numeric(header[4],week)&&numeric(header[5],tow)&&week>=2000&&week<10000&&tow>=0&&tow<604800000&&
    field_count>12&&numeric(fields[10],station_id)&&station_id>=0&&station_id<=4095&&std::floor(station_id)==station_id&&
    numeric(fields[11],diff_age)&&diff_age>0&&diff_age<=3600&&numeric(fields[12],solution_age)&&solution_age>=0&&solution_age<=1.5;
  result.position={latitude,longitude,msl+undulation};result.vertical_sigma_m=vertical;
  result.differential_age_ms=result.position_valid?uint32_t(std::ceil(diff_age*1000)):UINT32_MAX;result.solution_station=uint16_t(station_id);
  result.epoch=result.position_valid?uint64_t(week)*604800000ULL+uint64_t(tow):0;
  result.rtk_fixed=std::strcmp(fields[1],"NARROW_INT")==0 || std::strcmp(fields[1],"L1_INT")==0 || std::strcmp(fields[1],"WIDE_INT")==0;
  return true;
}

void format_horizontal_accuracy(char *output, size_t output_size,
                                uint32_t now) {
  if (!unit_profile_applied || !latest_gga.received || latest_gga.quality <= 0 ||
      now - last_gga_ms > 3000) {
    std::snprintf(output, output_size, "---");
    return;
  }
  if (std::strcmp(receiver_role, "BASE") == 0 && latest_gga.quality == 7) {
    std::snprintf(output, output_size, "N/A (BASE)");
    return;
  }
  if (!latest_horizontal_accuracy.received ||
      !std::isfinite(latest_horizontal_accuracy.horizontal_1drms_m) ||
      latest_horizontal_accuracy.horizontal_1drms_m < 0 ||
      now - latest_horizontal_accuracy.received_ms > 3000) {
    std::snprintf(output, output_size, "---");
    return;
  }

  const double meters = latest_horizontal_accuracy.horizontal_1drms_m;
  if (meters < 0.01) {
    std::snprintf(output, output_size, "%.1f mm", meters * 1000.0);
  } else if (meters < 1.0) {
    std::snprintf(output, output_size, "%.1f cm", meters * 100.0);
  } else {
    std::snprintf(output, output_size, "%.2f m", meters);
  }
}

const char *fix_label(int quality) {
  if (std::strcmp(receiver_role, "BASE") == 0) {
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

uint16_t fix_color(int quality) {
  if (std::strcmp(receiver_role, "BASE") == 0 && quality == 7) {
    return RGB565_GREEN;
  }

  switch (quality) {
    case 4: return RGB565_GREEN;
    case 5: return RGB565_YELLOW;
    case 1:
    case 2: return RGB565_CYAN;
    default: return RGB565_RED;
  }
}

void draw_fitted_text(int16_t x, int16_t y, int16_t width, const char *text,
                      uint8_t size, uint16_t color) {
  while (size > 1 && std::strlen(text) * 6 * size > static_cast<size_t>(width)) --size;
  char fitted[96] = {};
  const size_t capacity = std::min(static_cast<size_t>(width / (6 * size)), sizeof(fitted) - 1);
  std::strncpy(fitted, text, capacity);
  if (std::strlen(text) > capacity && capacity >= 3) std::memcpy(fitted + capacity - 3, "...", 3);
  display->setTextWrap(false);
  display->setTextSize(size);
  display->setTextColor(color);
  display->setCursor(x, y);
  display->print(fitted);
}

void draw_label_value(int16_t y, const char *label, const char *value) {
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%s", label, value);
  if (!ui_region_changed(2, y, signature)) return;
  display->fillRect(0, y, board::kWidth, 30, colors::kBackground);
  display->drawFastHLine(10, y + 29, 300, colors::kPanel);
  display->setTextSize(2);
  display->setTextColor(colors::kMuted);
  display->setCursor(10, y + 7);
  display->print(label);
  draw_fitted_text(92, y + 7, 218, value, 2, RGB565_WHITE);
}

bool peer_linked(uint32_t now) {
  if (wifi_last_peer_ms == 0 || now - wifi_last_peer_ms > 3000) return false;
  if (is_base() && !using_local_router()) {
    return wifi_peer_known && WiFi.softAPgetStationNum() > 0;
  }
  return station_connected();
}

int16_t current_link_rssi() {
  if (!is_base() && station_connected()) {
    return WiFi.RSSI();
  }
  return wifi_peer_rssi_dbm;
}

const char *link_quality_label(int16_t rssi) {
  if (rssi >= -60) return "EXCELLENT";
  if (rssi >= -70) return "GOOD";
  if (rssi >= -80) return "FAIR";
  return "WEAK";
}

uint16_t link_quality_color(int16_t rssi) {
  if (rssi >= -70) return RGB565_GREEN;
  if (rssi >= -80) return RGB565_YELLOW;
  return RGB565_RED;
}

bool fresh_gnss_time(uint32_t now) {
  return latest_gnss_time.valid && latest_gnss_time.received_ms > 0 &&
         now - latest_gnss_time.received_ms < 3000;
}

bool leap_year(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t days_in_month(uint8_t month, uint16_t year) {
  constexpr uint8_t kDays[] = {31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
  if (month == 2 && leap_year(year)) return 29;
  return month >= 1 && month <= 12 ? kDays[month - 1] : 31;
}

bool local_time_utc_minus_6(uint32_t now, uint16_t &year, uint8_t &month,
                            uint8_t &day, uint8_t &hour, uint8_t &minute,
                            uint8_t &second) {
  if (!fresh_gnss_time(now)) return false;
  year = latest_gnss_time.year;
  month = latest_gnss_time.month;
  day = latest_gnss_time.day;
  minute = latest_gnss_time.minute;
  second = latest_gnss_time.second;
  int local_hour = static_cast<int>(latest_gnss_time.hour) - 6;
  if (local_hour < 0) {
    local_hour += 24;
    if (day > 1) {
      --day;
    } else if (month > 1) {
      --month;
      day = days_in_month(month, year);
    } else {
      --year;
      month = 12;
      day = 31;
    }
  }
  hour = static_cast<uint8_t>(local_hour);
  return true;
}

bool gps_required_fix() {
  if (!unit_profile_applied || !latest_gga.received || millis() - last_gga_ms > 3000) return false;
  if (std::strcmp(receiver_role, is_base() ? "BASE" : "ROVER") != 0) return false;
  if (std::strcmp(receiver_role, "BASE") == 0) return latest_gga.quality == 7;
  if (std::strcmp(receiver_role, "ROVER") == 0) return latest_gga.quality == 4;
  return false;
}

bool correction_link_connected(uint32_t now) {
  return peer_linked(now);
}

bool fresh_rover_corrections(uint32_t now) {
  return is_base() ? device_config.base_rtcm :
         (rtcm_last_rx_ms > 0 && now - rtcm_last_rx_ms <= 3000);
}

bool system_ready(uint32_t now) {
  const bool uart_active = byte_count > 0 && now - last_rx_ms < 3000;
  return uart_active && version_ok && unit_profile_applied &&
         correction_link_connected(now) && gps_required_fix() &&
         fresh_rover_corrections(now);
}

uint16_t page_header_color(uint32_t now) {
  bool ready = false;
  if (current_page == ScreenPage::kMain) {
    ready = system_ready(now);
  } else if (current_page == ScreenPage::kGpsDetails) {
    ready = gps_required_fix();
  } else if (current_page == ScreenPage::kWifiDetails) {
    ready = correction_link_connected(now);
  }
  return ready ? colors::kHeaderReady : colors::kHeaderNotReady;
}

const char *device_role_title() {
  return is_base() ? "Base" : "Rover";
}

const char *brightness_mode_label() {
  if (brightness_mode == BrightnessMode::kDay) return "DAY OVERRIDE";
  if (brightness_mode == BrightnessMode::kNight) return "NIGHT OVERRIDE";
  return fresh_gnss_time(millis()) ? "AUTO: GNSS TIME" : "AUTO: NO GNSS TIME";
}

uint8_t automatic_brightness_target(uint32_t now) {
  constexpr uint8_t kNightDuty = board::kNightBacklightDuty;
  constexpr uint8_t kDayDuty = board::kDayBacklightDuty;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (!local_time_utc_minus_6(now, year, month, day, hour, minute, second)) {
    return kDayDuty;
  }

  const int local_minutes = hour * 60 + minute;
  if (local_minutes < 5 * 60 || local_minutes >= 20 * 60) return kNightDuty;
  if (local_minutes >= 7 * 60 && local_minutes < 18 * 60) return kDayDuty;

  double daylight_weight = 0.0;
  if (local_minutes < 7 * 60) {
    const double progress = (local_minutes - 5 * 60) / 120.0;
    daylight_weight = std::sqrt(progress);
  } else {
    const double progress = (local_minutes - 18 * 60) / 120.0;
    daylight_weight = std::sqrt(1.0 - progress);
  }
  return static_cast<uint8_t>(kNightDuty +
                              (kDayDuty - kNightDuty) * daylight_weight);
}

void service_brightness() {
  const uint32_t now = millis();
  if (brightness_mode == BrightnessMode::kDay) {
    target_backlight_duty = board::kDayBacklightDuty;
  } else if (brightness_mode == BrightnessMode::kNight) {
    target_backlight_duty = board::kNightBacklightDuty;
  } else {
    target_backlight_duty = automatic_brightness_target(now);
  }

  if (backlight_duty == target_backlight_duty) {
    last_brightness_step_ms = now;
    return;
  }
  const uint32_t elapsed = now - last_brightness_step_ms;
  if (elapsed < 20) return;
  last_brightness_step_ms = now;
  // Catch up by elapsed time even when SD writes or a page redraw delay the loop.
  const int step = std::max(1U, std::min(elapsed, board::kBacklightFadeMs) * 255U / board::kBacklightFadeMs);
  if (backlight_duty < target_backlight_duty) {
    backlight_duty = std::min(static_cast<int>(target_backlight_duty), backlight_duty + step);
  } else {
    backlight_duty = std::max(static_cast<int>(target_backlight_duty), backlight_duty - step);
  }
  if (backlight_pwm_ready) ledcWrite(board::kBacklightPwmChannel, backlight_duty);
}

void setup_backlight() {
  backlight_duty = brightness_mode == BrightnessMode::kNight
                       ? board::kNightBacklightDuty : board::kDayBacklightDuty;
  target_backlight_duty = backlight_duty;
  last_brightness_step_ms = millis();
  backlight_pwm_ready = ledcSetup(board::kBacklightPwmChannel, board::kBacklightPwmHz, 8) != 0;
  if (backlight_pwm_ready) {
    ledcAttachPin(board::kBacklight, board::kBacklightPwmChannel);
    ledcWrite(board::kBacklightPwmChannel, backlight_duty);
  } else {
    pinMode(board::kBacklight, OUTPUT);
    digitalWrite(board::kBacklight, HIGH);
  }
  Serial.printf("BACKLIGHT: PWM=%s GPIO=%d frequency=%lu Hz duty=%lu\n",
                backlight_pwm_ready ? "PASS" : "FAIL", board::kBacklight,
                static_cast<unsigned long>(ledcReadFreq(board::kBacklightPwmChannel)),
                static_cast<unsigned long>(ledcRead(board::kBacklightPwmChannel)));
}

bool sd_append_text(const char *path, const char *text) {
  if (!sd_ready || path == nullptr || text == nullptr) return false;
  if(!survey_sd_lock())return false;
  struct Unlock {~Unlock(){survey_sd_unlock();}} unlock;
  File file = SD_MMC.open(path, FILE_APPEND);
  if (!file) return false;
  const size_t written = file.print(text);
  file.flush();
  file.close();
  return written == std::strlen(text);
}

void sd_log_event(const char *event, const char *detail) {
  if (!sd_ready || sd_session_path[0] == '\0') return;
  char path[160] = {};
  std::snprintf(path, sizeof(path), "%s/events.csv", sd_session_path);
  char utc[24] = "---";
  if (latest_gnss_time.valid) {
    std::snprintf(utc, sizeof(utc), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  latest_gnss_time.year, latest_gnss_time.month,
                  latest_gnss_time.day, latest_gnss_time.hour,
                  latest_gnss_time.minute, latest_gnss_time.second);
  }
  char line[256] = {};
  std::snprintf(line, sizeof(line), "%lu,%c,%s,%s,%s\n",
                static_cast<unsigned long>(millis()), board::kUnitLabel, utc,
                event == nullptr ? "UNKNOWN" : event,
                detail == nullptr ? "" : detail);
  sd_append_text(path, line);
}

void sd_log_config(const char *profile, const char *notes) {
  if (!sd_ready || !sd_test_passed || sd_session_path[0] == '\0') return;
  char path[160] = {};
  std::snprintf(path, sizeof(path), "%s/config.csv", sd_session_path);
  char line[256] = {};
  std::snprintf(line, sizeof(line), "%lu,%c,%s,%s\n",
                static_cast<unsigned long>(millis()), board::kUnitLabel,
                profile == nullptr ? "UNKNOWN" : profile,
                notes == nullptr ? "" : notes);
  sd_append_text(path, line);
  sd_log_event("CONFIG_PROFILE", profile == nullptr ? "UNKNOWN" : profile);
}

void sd_log_solution(uint32_t now) {
  if (!sd_ready || !sd_test_passed || sd_session_path[0] == '\0' ||
      !latest_gga.received || now - last_gga_ms > 3000) {
    return;
  }
  char path[160] = {};
  std::snprintf(path, sizeof(path), "%s/solution.csv", sd_session_path);
  char hacc[24] = {};
  format_horizontal_accuracy(hacc, sizeof(hacc), now);
  const long rtcm_age = rtcm_last_rx_ms == 0
                            ? -1L
                            : static_cast<long>(now - rtcm_last_rx_ms);
  const int16_t rssi = current_link_rssi();
  char line[320] = {};
  std::snprintf(line, sizeof(line),
                "%lu,%s,%s,%s,%.8f,%.8f,%.3f,%d,%.2f,%s,%ld,%d,%lu,%lu,%lu,%lu,%lu,%lu\n",
                static_cast<unsigned long>(now), latest_gnss_time.valid
                                                  ? latest_gga.utc
                                                  : "---",
                latest_gnss_time.valid ? "VALID" : "TIME_WAIT",
                fix_label(latest_gga.quality), latest_gga.latitude,
                latest_gga.longitude, latest_gga.altitude,
                latest_gga.satellites, latest_gga.hdop, hacc, rtcm_age, rssi,
                static_cast<unsigned long>(rtcm_uart_frames),
                static_cast<unsigned long>(rtcm_wifi_tx_frames),
                static_cast<unsigned long>(rtcm_wifi_rx_frames),
                static_cast<unsigned long>(rtcm_forwarded_bytes),
                static_cast<unsigned long>(wifi_sequence_gaps),
                static_cast<unsigned long>(wifi_invalid_packets));
  sd_append_text(path, line);
}

void setup_sd_logging() {
  SD_MMC.setPins(board::kSdClock, board::kSdCommand, board::kSdData0);
  if (!SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("SD: MOUNT FAIL");
    return;
  }
  sd_ready = true;
  Serial.printf("SD: MOUNT PASS card=%llu MB free=%llu MB\n",
                static_cast<unsigned long long>(SD_MMC.cardSize() / (1024ULL * 1024ULL)),
                static_cast<unsigned long long>((SD_MMC.totalBytes() - SD_MMC.usedBytes()) /
                                                (1024ULL * 1024ULL)));

  SD_MMC.mkdir("/TOPO-RTK");
  char unit_path[48] = {};
  std::snprintf(unit_path, sizeof(unit_path), "/TOPO-RTK/UNIT-%c", board::kUnitLabel);
  SD_MMC.mkdir(unit_path);
  char sessions_path[80] = {};
  std::snprintf(sessions_path, sizeof(sessions_path), "%s/SESSIONS", unit_path);
  SD_MMC.mkdir(sessions_path);
  // Repeated boot timing must not collide with an existing diagnostic session.
  for(unsigned attempt=0;attempt<1000;++attempt){
    std::snprintf(sd_session_path,sizeof(sd_session_path),"%s/BOOT-%lu-%u",sessions_path,static_cast<unsigned long>(millis()),attempt);
    if(!SD_MMC.exists(sd_session_path))break;
  }
  if (!SD_MMC.mkdir(sd_session_path)) {
    Serial.printf("SD: SESSION DIR FAIL %s\n", sd_session_path);
    sd_ready = false;
    return;
  }

  const char *test_path = "/TOPO-RTK/SD-READBACK-TEST.TXT";
  File test = SD_MMC.open(test_path, FILE_WRITE);
  if (!test) {
    Serial.println("SD: TEST WRITE FAIL");
    sd_ready = false;
    return;
  }
  const char *test_text = "TopoRTK SD readback test v1\n";
  const size_t test_written = test.print(test_text);
  test.flush();
  test.close();
  File readback = SD_MMC.open(test_path, FILE_READ);
  char readback_text[64] = {};
  const size_t read_count = readback ? readback.readBytes(readback_text,
                                                           sizeof(readback_text) - 1)
                                     : 0;
  if (readback) readback.close();
  sd_test_passed = test_written == std::strlen(test_text) &&
                   std::strncmp(readback_text, test_text, std::strlen(test_text)) == 0 &&
                   read_count == std::strlen(test_text);
  Serial.printf("SD: READBACK %s\n", sd_test_passed ? "PASS" : "FAIL");
  if (!sd_test_passed) return;

  char path[160] = {};
  std::snprintf(path, sizeof(path), "%s/events.csv", sd_session_path);
  sd_append_text(path, "uptime_ms,unit,utc,event,detail\n");
  std::snprintf(path, sizeof(path), "%s/solution.csv", sd_session_path);
  sd_append_text(path,
                 "uptime_ms,utc_time,utc_status,fix,latitude,longitude,altitude_m,satellites,hdop,h_acc,rtcm_age_ms,link_rssi_dbm,rtcm_uart_frames,rtcm_wifi_tx_frames,rtcm_wifi_rx_frames,rtcm_forwarded_bytes,wifi_sequence_gaps,wifi_invalid_packets\n");
  std::snprintf(path, sizeof(path), "%s/config.csv", sd_session_path);
  sd_append_text(path, "uptime_ms,unit,profile,notes\n");
  char session_path[160] = {};
  std::snprintf(session_path, sizeof(session_path), "%s/session.json", sd_session_path);
  File session = SD_MMC.open(session_path, FILE_WRITE);
  if (session) {
    session.printf("{\"unit\":\"%c\",\"boot_uptime_ms\":%lu,\"card_bytes\":%llu}\n",
                   board::kUnitLabel, static_cast<unsigned long>(millis()),
                   static_cast<unsigned long long>(SD_MMC.cardSize()));
    session.flush();
    session.close();
  }
  sd_log_event("BOOT", "SD_READY|READBACK_PASS");
}

void service_sd_logging() {
  if (!sd_ready || !sd_test_passed) return;
  const uint32_t now = millis();
  const bool uart_active = byte_count > 0 && now - last_rx_ms < 3000;
  const bool linked = peer_linked(now);
  const bool rtcm_active = is_base()
                               ? rtcm_uart_frames > 0
                               : rtcm_last_rx_ms > 0 && now - rtcm_last_rx_ms <= 3000;
  const int quality = latest_gga.received ? latest_gga.quality : -1;
  if (!sd_state_initialized) {
    sd_state_initialized = true;
    sd_last_uart_active = uart_active;
    sd_last_linked = linked;
    sd_last_rtcm_active = rtcm_active;
    sd_last_fix_quality = quality;
    std::strncpy(sd_last_role, receiver_role, sizeof(sd_last_role) - 1);
    sd_log_event("STATE", "INITIAL");
  } else {
    if (uart_active != sd_last_uart_active) {
      sd_last_uart_active = uart_active;
      sd_log_event("UART", uart_active ? "RECEIVING" : "OFFLINE");
    }
    if (linked != sd_last_linked) {
      sd_last_linked = linked;
      sd_log_event("LINK", linked ? "CONNECTED" : "DISCONNECTED");
    }
    if (rtcm_active != sd_last_rtcm_active) {
      sd_last_rtcm_active = rtcm_active;
      sd_log_event("RTCM", rtcm_active ? "ACTIVE" : "INACTIVE");
    }
    if (quality != sd_last_fix_quality) {
      sd_last_fix_quality = quality;
      sd_log_event("GNSS_FIX", fix_label(quality));
    }
    if (std::strcmp(sd_last_role, receiver_role) != 0) {
      std::strncpy(sd_last_role, receiver_role, sizeof(sd_last_role) - 1);
      sd_log_event("ROLE", receiver_role);
    }
  }
  if (now - last_sd_solution_ms >= 1000) {
    last_sd_solution_ms = now;
    sd_log_solution(now);
  }
}

void draw_status_card(int16_t y, int16_t height, const char *title,
                      const char *value, uint16_t color,
                      uint8_t value_size = 3) {
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%d|%s|%s|%u|%u", height,
                title, value, color, value_size);
  if (!ui_region_changed(1, y, signature)) return;
  display->fillRoundRect(8, y, board::kWidth - 16, height, 6, colors::kPanel);
  display->fillRoundRect(8, y + 10, 4, height - 20, 2, color);
  display->setTextSize(1);
  display->setTextColor(colors::kMuted);
  display->setCursor(18, y + 9);
  display->print(title);
  draw_fitted_text(20, y + 28, 282, value, value_size, color);
}

void draw_button(uint8_t id, const TouchRect &rect, const char *label,
                 const char *subtitle, bool selected, bool enabled = true) {
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%s|%u|%u", label, subtitle, selected, enabled);
  if (!ui_region_changed(10 + id, rect.y, signature)) return;
  const uint16_t background = selected && enabled ? 0x0129 : colors::kPanel;
  const uint16_t foreground = enabled ? RGB565_WHITE : colors::kMuted;
  display->fillRoundRect(rect.x, rect.y, rect.width, rect.height, 8, background);
  display->drawRoundRect(rect.x, rect.y, rect.width, rect.height, 8,
                          selected && enabled ? colors::kAccent : colors::kBorder);
  const int16_t text_width = std::strlen(label) * 12;
  draw_fitted_text(rect.x + (rect.width - text_width) / 2,
                   rect.y + (subtitle[0] ? 14 : (rect.height - 16) / 2),
                   rect.width - 12, label, 2, foreground);
  if (subtitle[0]) {
    draw_fitted_text(rect.x + (rect.width - std::strlen(subtitle) * 6) / 2,
                     rect.y + 47, rect.width - 12, subtitle, 1,
                     selected ? colors::kAccent : colors::kMuted);
  }
}

void draw_navigation() {
  if (!ui_region_changed(4, layout::kNavY, "tabs")) return;
  static const char *const labels[] = {"HOME", "GPS", "LINK", "SETUP"};
  display->fillRect(0, layout::kNavY, 320, 48, colors::kPanel);
  for (uint8_t index = 0; index < 4; ++index) {
    const bool selected = index == static_cast<uint8_t>(current_page) ||
                          (current_page == ScreenPage::kPhone && index == 2);
    if (selected) display->fillRect(index * 80 + 8, layout::kNavY, 64, 3, colors::kAccent);
    draw_fitted_text(index * 80 + (80 - std::strlen(labels[index]) * 12) / 2,
                     layout::kNavY + 19, 72, labels[index], 2,
                     selected ? RGB565_WHITE : colors::kMuted);
  }
}

void draw_settings() {
  if (ui_region_changed(5, 64, "headings")) {
    draw_fitted_text(12, 65, 296, "INSTRUMENT ROLE", 2, RGB565_WHITE);
    draw_fitted_text(12, 295, 296, "SCREEN BRIGHTNESS", 2, RGB565_WHITE);
  }
  const bool base_selected = pending_role == DeviceRole::kBase;
  draw_button(0, layout::kBase, "BASE", base_selected ? "SELECTED" : "SEND RTCM", base_selected, !profile_running);
  draw_button(1, layout::kRover, "ROVER", !base_selected ? "SELECTED" : "RECEIVE RTCM", !base_selected, !profile_running);
  const char *hint1 = base_selected ? "Temporary averaged base." : "Receives base corrections.";
  const char *hint2 = base_selected ? "Control point is unverified." : "Keep the other unit in Base.";
  if (ui_region_changed(6, 174, hint1)) {
    display->fillRect(8, 172, 304, 52, colors::kBackground);
    draw_fitted_text(12, 178, 296, hint1, 1, colors::kMuted);
    draw_fitted_text(12, 199, 296, hint2, 1, base_selected ? colors::kWarning : colors::kMuted);
  }
  const bool needs_apply = pending_role != device_config.role || config_error || !config_saved || profile_failed;
  const char *action = profile_running ? "APPLYING..." : profile_failed ? "RETRY SETUP" :
      needs_apply ? (base_selected ? "USE BASE" : "USE ROVER") :
      unit_profile_applied ? (is_base() ? "BASE ACTIVE" : "ROVER ACTIVE") : "WAITING FOR GNSS";
  draw_button(2, layout::kApply, action, "", needs_apply, needs_apply && !profile_running);
  draw_button(3, layout::kAuto, "AUTO", "", brightness_mode == BrightnessMode::kAutomatic, !profile_running);
  draw_button(4, layout::kDay, "DAY", "", brightness_mode == BrightnessMode::kDay, !profile_running);
  draw_button(5, layout::kNight, "NIGHT", "", brightness_mode == BrightnessMode::kNight, !profile_running);
  const char *status = config_error ? "SAVE FAILED - TAP USE TO RETRY" : profile_failed ? "SETUP FAILED - TAP RETRY" :
      profile_running ? "Saved. Configuring receiver..." : pending_role != device_config.role ? "Role not applied. Tap Use to save." :
      config_saved ? "Saved. Restores at power-on." : "Factory defaults. Tap Use to save.";
  char brightness[64] = {};
  std::snprintf(brightness, sizeof(brightness), "%s %u%%", brightness_mode_label(),
                static_cast<unsigned>((backlight_duty * 100U + 127U) / 255U));
  if (!backlight_pwm_ready) std::strcpy(brightness, "BACKLIGHT PWM ERROR");
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%s", status, brightness);
  if (ui_region_changed(7, 382, signature)) {
    display->fillRect(8, 380, 304, 46, colors::kBackground);
    draw_fitted_text(12, 385, 296, status, 1, config_error || profile_failed ? colors::kWarning : colors::kMuted);
    draw_fitted_text(12, 407, 296, brightness, 2, colors::kMuted);
  }
}

void draw_header(uint32_t now) {
  char title[48] = {};
  char subtitle[64] = {};
  const uint16_t header_color = page_header_color(now);
  if (current_page == ScreenPage::kGpsDetails) {
    std::snprintf(title, sizeof(title), "GPS / %s", device_role_title());
    std::snprintf(subtitle, sizeof(subtitle), "REQUIRED FIX: %s",
                  gps_required_fix() ? "YES" : "NO");
  } else if (current_page == ScreenPage::kWifiDetails) {
    std::snprintf(title, sizeof(title), "Link / %s", device_role_title());
    std::snprintf(subtitle, sizeof(subtitle), "NETWORK: %s",
                  correction_link_connected(now) ? "CONNECTED" : "DISCONNECTED");
  } else if (current_page == ScreenPage::kSettings) {
    std::snprintf(title, sizeof(title), "Setup / %s", device_role_title());
    std::snprintf(subtitle, sizeof(subtitle), "ROLE AND DISPLAY PREFERENCES");
  } else if (current_page == ScreenPage::kPhone) {
    std::snprintf(title, sizeof(title), "Phone / %s", device_role_title());
    std::snprintf(subtitle, sizeof(subtitle), "LOCAL WEB UI %s",kWebUiVersion);
  } else {
    std::snprintf(title, sizeof(title), "TopoRTK - %s", device_role_title());
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (local_time_utc_minus_6(now, year, month, day, hour, minute, second)) {
      std::snprintf(subtitle, sizeof(subtitle), "%s  LOCAL %02u:%02u",
                    system_ready(now) ? "READY" : "NOT READY", hour, minute);
    } else {
      std::snprintf(subtitle, sizeof(subtitle), "%s  LOCAL TIME WAIT",
                    system_ready(now) ? "READY" : "NOT READY");
    }
  }

  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%u|%s|%s|%c", header_color,
                title, subtitle, board::kUnitLabel);
  if (!ui_region_changed(0, 0, signature)) return;

  display->fillRect(0, 0, board::kWidth, 52, header_color);
  draw_fitted_text(10, 8, 264, title, 2, RGB565_WHITE);

  display->setTextSize(1);
  display->setCursor(10, 34);
  display->print(subtitle);

  display->fillRoundRect(282, 7, 31, 38, 5, RGB565_YELLOW);
  display->setTextColor(RGB565_BLACK);
  display->setTextSize(2);
  display->setCursor(292, 18);
  display->print(board::kUnitLabel);
}

void draw_static_screen() {
  reset_ui_region_cache();
  display->fillScreen(colors::kBackground);
  draw_header(millis());
}

struct DashboardWarning {
  const char *title;
  const char *detail;
  uint16_t color;
};

DashboardWarning dashboard_warning(uint32_t now) {
  const bool uart_active = byte_count > 0 && now - last_rx_ms < 3000;
  const bool linked = peer_linked(now);
  const char *alert = "CHECK FIX QUALITY";
  const char *detail = "Wait for a stable required fix.";
  uint16_t alert_color = colors::kWarning;
  if (config_error) {
    alert = "SETTINGS NOT SAVED";
    detail = "Open Setup and retry saving.";
  } else if (profile_failed) {
    alert = "SETUP FAILED";
    detail = "Check GNSS cable. Retry in Setup.";
  } else if (!uart_active || !version_ok) {
    alert = "UM980 OFFLINE";
    detail = "Check receiver power and cable.";
    alert_color = RGB565_RED;
  } else if (!unit_profile_applied) {
    alert = "CONFIGURING UM980";
    detail = "Applying your saved role.";
  } else if (latest_gga.received && now - last_gga_ms > 3000) {
    alert = "GNSS DATA STALE";
    detail = "Check receiver power and cable.";
    alert_color = RGB565_RED;
  } else if (is_base() && !device_config.base_rtcm) {
    alert = "CORRECTION OUTPUT OFF";
    detail = "Enable RTCM from the USB console.";
  } else if (!linked) {
    alert = is_base() ? "ROVER LINK DOWN" : "BASE LINK DOWN";
    detail = is_base() ? "Set the other unit to Rover." : "Power on the base; check range.";
    alert_color = RGB565_RED;
  } else if (!is_base() &&
             (rtcm_last_rx_ms == 0 || now - rtcm_last_rx_ms > 3000)) {
    alert = "NO FRESH CORRECTIONS";
    detail = "Check base fix and RTCM output.";
  } else if (std::strcmp(receiver_role, "BASE") == 0 &&
             (latest_gga.quality == 1 || latest_gga.quality == 2)) {
    alert = "BASE SURVEYING";
    detail = "Keep antenna still with clear sky.";
  } else if (std::strcmp(receiver_role, "BASE") == 0 &&
             latest_gga.quality == 7) {
    alert = "BASE LOCKED";
    detail = "Temporary base. Control unverified.";
  } else if (latest_gga.quality == 4) {
    alert = "RTK FIXED";
    detail = "Verify control before surveying.";
  } else if (latest_gga.quality == 5) {
    alert = "RTK FLOAT";
    detail = "Wait for RTK fixed; check sky view.";
  } else if (latest_gga.quality == 0) {
    alert = "NO GNSS FIX";
    detail = "Move antenna to a clear sky view.";
  }
  return {alert, detail, alert_color};
}

const char *current_fix_label(uint32_t now) {
  if (!latest_gga.received) return "NO FIX";
  if (now - last_gga_ms > 3000) return "GNSS STALE";
  return fix_label(latest_gga.quality);
}

void draw_main_dashboard(uint32_t now) {
  const bool linked = peer_linked(now);
  const int16_t rssi = current_link_rssi();
  char value[64] = {};
  draw_status_card(60, 66, "CORRECTION LINK", linked ? "CONNECTED" : "NO LINK",
                   linked ? RGB565_GREEN : RGB565_RED);
  draw_status_card(134, 66, "GNSS SOLUTION", current_fix_label(now),
                   now - last_gga_ms > 3000 ? RGB565_RED : fix_color(latest_gga.quality));
  format_horizontal_accuracy(value, sizeof(value), now);
  draw_status_card(208, 66, "HORIZONTAL UNCERTAINTY / 1DRMS", value,
                   std::strcmp(value, "---") == 0 ? colors::kWarning : RGB565_WHITE);
  if (linked) std::snprintf(value, sizeof(value), "%s  %d dBm", link_quality_label(rssi), rssi);
  else std::strcpy(value, "---");
  draw_status_card(282, 66, "LINK SIGNAL", value, linked ? link_quality_color(rssi) : RGB565_RED);
  const DashboardWarning warning = dashboard_warning(now);
  const char *alert = warning.title;
  const char *detail = warning.detail;
  const uint16_t alert_color = warning.color;
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%s|%u", alert, detail, alert_color);
  if (ui_region_changed(3, 356, signature)) {
    display->fillRoundRect(8, 356, board::kWidth - 16, 68, 6, colors::kPanel);
    display->drawRoundRect(8, 356, 304, 68, 6, alert_color);
    draw_fitted_text(18, 368, 284, alert, 2, alert_color);
    draw_fitted_text(18, 399, 284, detail, 1, RGB565_WHITE);
  }
}

size_t format_web_status(char *output, size_t capacity, uint32_t now) {
  const bool linked = correction_link_connected(now);
  const bool fresh_gga = latest_gga.received && now - last_gga_ms <= 3000;
  const bool online = version_ok && byte_count > 0 && now - last_rx_ms < 3000;
  char accuracy[32], accuracy_m[32] = "null", quality[16] = "null", satellites[16] = "null";
  char gga_age[16] = "null", peer_age[16] = "null", correction_age[16] = "null", rssi[16] = "null";
  char signal[32] = "null", local[32] = "null", utc[32] = "null";
  format_horizontal_accuracy(accuracy, sizeof(accuracy), now);
  if (fresh_gga) {
    std::snprintf(quality, sizeof(quality), "%d", latest_gga.quality);
    std::snprintf(satellites, sizeof(satellites), "%d", latest_gga.satellites);
  }
  if (std::strcmp(accuracy, "---") != 0 && std::strcmp(accuracy, "N/A (BASE)") != 0)
    std::snprintf(accuracy_m, sizeof(accuracy_m), "%.6f", latest_horizontal_accuracy.horizontal_1drms_m);
  if (latest_gga.received) std::snprintf(gga_age, sizeof(gga_age), "%lu", static_cast<unsigned long>(now-last_gga_ms));
  if (wifi_last_peer_ms) std::snprintf(peer_age, sizeof(peer_age), "%lu", static_cast<unsigned long>(now-wifi_last_peer_ms));
  if (rtcm_last_rx_ms) std::snprintf(correction_age, sizeof(correction_age), "%lu", static_cast<unsigned long>(now-rtcm_last_rx_ms));
  if (linked) {
    std::snprintf(rssi, sizeof(rssi), "%d", current_link_rssi());
    std::snprintf(signal, sizeof(signal), "\"%s\"", link_quality_label(current_link_rssi()));
  }
  uint16_t year; uint8_t month, day, hour, minute, second;
  if (local_time_utc_minus_6(now, year, month, day, hour, minute, second)) {
    std::snprintf(local, sizeof(local), "\"%04u-%02u-%02uT%02u:%02u:%02u\"", year, month, day, hour, minute, second);
    std::snprintf(utc, sizeof(utc), "\"%04u-%02u-%02uT%02u:%02u:%02uZ\"", latest_gnss_time.year, latest_gnss_time.month,
                  latest_gnss_time.day, latest_gnss_time.hour, latest_gnss_time.minute, latest_gnss_time.second);
  }
  const DashboardWarning warning = dashboard_warning(now);
  // All string fields are internal enums/fixed labels; no credentials or receiver text enter JSON.
  const int length = std::snprintf(output, capacity,
    "{\"api_version\":1,\"ui_version\":\"%s\",\"boot_id\":\"%08lx\",\"uptime_ms\":%lu,"
    "\"device\":{\"unit\":\"%c\",\"role\":\"%s\",\"profile\":\"%s\"},"
    "\"state\":{\"ready\":%s,\"gps_fixed\":%s,\"correction_link_connected\":%s},"
    "\"gnss\":{\"online\":%s,\"fix\":\"%s\",\"gga_quality\":%s,\"gga_age_ms\":%s,\"satellites\":%s,"
    "\"horizontal_uncertainty_m\":%s,\"horizontal_uncertainty_label\":\"%s\"},"
    "\"link\":{\"connected\":%s,\"transport\":\"%s\",\"quality\":%s,\"rssi_dbm\":%s,\"peer_age_ms\":%s,"
    "\"correction_age_ms\":%s,\"received_packets\":%lu,\"sequence_gaps\":%lu,\"invalid_packets\":%lu,\"rtcm_received_frames\":%lu},"
    "\"time\":{\"local\":%s,\"utc\":%s,\"utc_offset\":\"-06:00\"},"
    "\"phone_wifi\":{\"available\":%s,\"ssid\":\"%s\",\"address\":\"%s\",\"clients\":%u},"
    "\"warning\":{\"title\":\"%s\",\"detail\":\"%s\",\"severity\":\"%s\"}}",
    kWebUiVersion, static_cast<unsigned long>(web_boot_id()), static_cast<unsigned long>(now), board::kUnitLabel,
    is_base() ? "BASE" : "ROVER", unit_profile_applied ? "VERIFIED" : profile_failed ? "FAILED" : "CONFIGURING",
    system_ready(now) ? "true" : "false", gps_required_fix() ? "true" : "false", linked ? "true" : "false",
    online ? "true" : "false", current_fix_label(now), quality, gga_age, satellites, accuracy_m, accuracy,
    linked ? "true" : "false", wifi_transport_label(), signal, rssi, peer_age, correction_age,
    static_cast<unsigned long>(wifi_rx_packets), static_cast<unsigned long>(wifi_sequence_gaps),
    static_cast<unsigned long>(wifi_invalid_packets), static_cast<unsigned long>(rtcm_wifi_rx_frames), local, utc,
    rover_ap_ready() ? "true" : "false",rover_ap_ssid(),rover_ap_address(),rover_ap_clients(),
    warning.title, warning.detail, warning.color == RGB565_RED ? "error" : "warning");
  return length >= 0 && static_cast<size_t>(length) < capacity ? length : 0;
}

void service_web_status() {
  const uint32_t now = millis();
  if (now - last_web_status_ms < 250) return;
  last_web_status_ms = now;
  char json[kWebStatusCapacity];
  const size_t length = format_web_status(json, sizeof(json), now);
  publish_web_status(json, length, now, !is_base());
}

void draw_gps_details(uint32_t now) {
  char value[80] = {};
  const bool uart_active = byte_count > 0 && now - last_rx_ms < 3000;
  draw_label_value(58, "UART", uart_active ? "RECEIVING" : "WAITING");
  draw_label_value(89, "FIX",
                   latest_gga.received ? fix_label(latest_gga.quality)
                                       : "NO GGA");

  uint16_t local_year = 0;
  uint8_t local_month = 0, local_day = 0, local_hour = 0, local_minute = 0,
          local_second = 0;
  if (local_time_utc_minus_6(now, local_year, local_month, local_day,
                             local_hour, local_minute, local_second)) {
    std::snprintf(value, sizeof(value), "%02u:%02u:%02u UTC-6", local_hour,
                  local_minute, local_second);
  } else {
    std::strcpy(value, "WAITING FOR GNSS TIME");
  }
  draw_label_value(120, "LOCAL", value);

  if (fresh_gnss_time(now)) {
    std::snprintf(value, sizeof(value), "%02u:%02u:%02u",
                  latest_gnss_time.hour, latest_gnss_time.minute,
                  latest_gnss_time.second);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(151, "UTC", value);
  if (fresh_gnss_time(now)) {
    std::snprintf(value, sizeof(value), "%04u-%02u-%02u",
                  latest_gnss_time.year, latest_gnss_time.month,
                  latest_gnss_time.day);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(182, "DATE", value);

  if (latest_gga.received && latest_gga.quality > 0) {
    std::snprintf(value, sizeof(value), "%.8f", latest_gga.latitude);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(213, "LAT", value);
  if (latest_gga.received && latest_gga.quality > 0) {
    std::snprintf(value, sizeof(value), "%.8f", latest_gga.longitude);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(244, "LON", value);

  std::snprintf(value, sizeof(value), "%d  HDOP %.2f",
                latest_gga.satellites, latest_gga.hdop);
  draw_label_value(275, "SATS", value);
  if (latest_gga.received && latest_gga.quality > 0) {
    std::snprintf(value, sizeof(value), "%.3f m", latest_gga.altitude);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(306, "ALT", value);

  format_horizontal_accuracy(value, sizeof(value), now);
  draw_label_value(337, "H-ACC", value);
  if (latest_horizontal_accuracy.received) {
    std::snprintf(value, sizeof(value), "N %.3f E %.3f m",
                  latest_horizontal_accuracy.latitude_sigma_m,
                  latest_horizontal_accuracy.longitude_sigma_m);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(368, "SIGMA", value);

  if (is_base()) {
    std::snprintf(value, sizeof(value), "OUT %lu  TYPE %u",
                  static_cast<unsigned long>(rtcm_wifi_tx_frames),
                  rtcm_last_message);
  } else if (rtcm_last_rx_ms > 0) {
    std::snprintf(value, sizeof(value), "%lu ms  F %lu",
                  static_cast<unsigned long>(now - rtcm_last_rx_ms),
                  static_cast<unsigned long>(rtcm_wifi_rx_frames));
  } else {
    std::strcpy(value, "NONE");
  }
  draw_label_value(399, "RTCM", value);
}

void draw_wifi_details(uint32_t now) {
  char value[80] = {};
  const bool linked = peer_linked(now);
  const int16_t rssi = current_link_rssi();
  draw_label_value(58, "MODE", wifi_transport_label());
  draw_label_value(89, "SSID", active_wifi_ssid());
  const IPAddress local_ip = is_base() && !using_local_router()
                               ? WiFi.softAPIP() : WiFi.localIP();
  std::snprintf(value, sizeof(value), "%u.%u.%u.%u", local_ip[0], local_ip[1],
                local_ip[2], local_ip[3]);
  draw_label_value(120, "IP", value);
  draw_label_value(151, "LINK", linked ? "LINKED" : "NO LINK");
  if (linked) {
    std::snprintf(value, sizeof(value), "%d dBm  %s", rssi,
                  link_quality_label(rssi));
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(182, "RSSI", value);

  if (wifi_peer_known) {
    std::snprintf(value, sizeof(value), "%u.%u.%u.%u", wifi_peer[0],
                  wifi_peer[1], wifi_peer[2], wifi_peer[3]);
  } else if (!is_base() && station_connected() && !using_local_router()) {
    std::strcpy(value, "192.168.4.1");
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(213, "PEER", value);
  std::snprintf(value, sizeof(value), "TX %lu  RX %lu",
                static_cast<unsigned long>(wifi_tx_packets),
                static_cast<unsigned long>(wifi_rx_packets));
  draw_label_value(244, "PKTS", value);
  std::snprintf(value, sizeof(value), "GAP %lu  BAD %lu",
                static_cast<unsigned long>(wifi_sequence_gaps),
                static_cast<unsigned long>(wifi_invalid_packets));
  draw_label_value(275, "NET", value);
  std::snprintf(value, sizeof(value), "TX %lu  RX %lu",
                static_cast<unsigned long>(rtcm_wifi_tx_frames),
                static_cast<unsigned long>(rtcm_wifi_rx_frames));
  draw_label_value(306, "RTCM", value);
  std::snprintf(value, sizeof(value), "%lu bytes  type %u",
                static_cast<unsigned long>(rtcm_forwarded_bytes),
                rtcm_last_message);
  draw_label_value(337, "DATA", value);
  draw_button(6,layout::kPhone,"PHONE / TABLET","",false);
}

void draw_phone_connection(uint32_t now) {
  const bool reveal = phone_key_shown_ms && now-phone_key_shown_ms < 30000;
  const bool confirm = phone_key_confirm_ms && now-phone_key_confirm_ms < 10000;
  if (!reveal) phone_key_shown_ms = 0;
  if (!confirm) phone_key_confirm_ms = 0;
  // The cache never stores the credential itself.
  char signature[128];
  std::snprintf(signature,sizeof(signature),"%u|%u|%u|%u|%s|%s|%lu",is_base(),rover_ap_ready(),reveal,
                rover_ap_clients(),rover_ap_error(),rover_ap_address(),static_cast<unsigned long>(phone_key_shown_ms));
  if (ui_region_changed(8,60,signature)) {
    display->fillRect(8,58,304,150,colors::kBackground);
    draw_fitted_text(12,64,296,"1. JOIN WI-FI",1,colors::kMuted);
    draw_fitted_text(12,83,296,is_base() ? (using_local_router()?"JOIN THE LOCAL ROUTER":"JOIN BASE WI-FI") : rover_ap_ssid(),2,RGB565_WHITE);
    char state[64];
    std::snprintf(state,sizeof(state),"%s  PHONES: %u",rover_ap_ready() ? "WPA2 READY" : "AP UNAVAILABLE",rover_ap_clients());
    draw_fitted_text(12,113,296,is_base()?"OPEN BASE WEB FOR RECEIVER SETUP":state,1,is_base()||rover_ap_ready() ? colors::kAccent : colors::kWarning);
    draw_fitted_text(12,142,296,"PASSWORD (HIDDEN AFTER 30 SECONDS)",1,colors::kMuted);
    draw_fitted_text(12,164,296,reveal && !is_base() ? rover_ap_password() : "****.****",2,RGB565_WHITE);
    char control[64];std::snprintf(control,sizeof(control),"WEB CONTROL PIN: %s",reveal?survey_control_pin():"******");
    draw_fitted_text(12,192,296,rover_ap_error()[0]?rover_ap_error():control,1,colors::kWarning);
  }
  draw_button(7,layout::kShowKey,reveal ? "HIDE KEY" : "SHOW KEY","",reveal);
  draw_button(8,layout::kNewKey,confirm ? "CONFIRM" : "NEW KEY","",confirm,!is_base());
  char bottom[80]; std::snprintf(bottom,sizeof(bottom),"%u|%s",confirm,rover_ap_address());
  if (ui_region_changed(9,272,bottom)) {
    display->fillRect(8,270,304,156,colors::kBackground);
    draw_fitted_text(12,276,296,"2. OPEN IN CHROME",1,colors::kMuted);
    char url[48]; const auto base_ip=using_local_router()?WiFi.localIP().toString():WiFi.softAPIP().toString();
    std::snprintf(url,sizeof(url),"http://%s",is_base()?base_ip.c_str():rover_ap_address());
    draw_fitted_text(12,298,296,url,2,RGB565_WHITE);
    draw_fitted_text(12,330,296,"Choose 'Stay connected' if Android",1,colors::kMuted);
    draw_fitted_text(12,348,296,"reports no Internet. This is normal.",1,colors::kMuted);
    draw_fitted_text(12,382,296,confirm ? "Replace key? Phones will disconnect." : "Open /survey for jobs and controls.",1,confirm ? colors::kWarning : colors::kMuted);
    draw_fitted_text(12,402,296,confirm ? "Tap CONFIRM within 10s; leave to cancel." : "Use WEB CONTROL PIN to pair browser.",1,colors::kMuted);
  }
}

void draw_dynamic_screen() {
  if (!display_ready) return;
  const uint32_t now = millis();
  draw_header(now);
  if (current_page == ScreenPage::kGpsDetails) {
    draw_gps_details(now);
  } else if (current_page == ScreenPage::kWifiDetails) {
    draw_wifi_details(now);
  } else if (current_page == ScreenPage::kSettings) {
    draw_settings();
  } else if (current_page == ScreenPage::kPhone) {
    draw_phone_connection(now);
  } else {
    draw_main_dashboard(now);
  }
  draw_navigation();
}

void change_page(ScreenPage page) {
  if (page == current_page) return;
  current_page = page;
  phone_key_shown_ms = phone_key_confirm_ms = 0;
  pending_role = device_config.role;
  reset_ui_region_cache();
  const char *name = page == ScreenPage::kGpsDetails
                         ? "GPS DETAILS"
                         : page == ScreenPage::kWifiDetails ? "WIFI DETAILS"
                         : page == ScreenPage::kSettings ? "SETUP"
                         : page == ScreenPage::kPhone ? "PHONE" : "MAIN";
  Serial.printf("UI PAGE: %s\n", name);
  if (display_ready) {
    draw_static_screen();
    draw_dynamic_screen();
  }
}

void finish_swipe() {
  const int16_t dx = touch_last_x - touch_start_x;
  const int16_t dy = touch_last_y - touch_start_y;
  const int16_t abs_dx = std::abs(dx);
  const int16_t abs_dy = std::abs(dy);
  if (touch_cancelled) return;
  if (!touch_moved && millis() - touch_start_ms < 1200) {
    const TouchAction action = touch_action(current_page, touch_start_x, touch_start_y);
    if (action != touch_action(current_page, touch_last_x, touch_last_y)) return;
    Serial.printf("UI TAP: x=%d y=%d action=%u\n", touch_last_x, touch_last_y, static_cast<unsigned>(action));
    if (action >= TouchAction::kHome && action <= TouchAction::kSettings) {
      change_page(static_cast<ScreenPage>(static_cast<uint8_t>(action) - 1));
      return;
    }
    if (profile_running) return;
    if (action == TouchAction::kPhone) change_page(ScreenPage::kPhone);
    else if (action == TouchAction::kShowKey) {
      phone_key_shown_ms = phone_key_shown_ms ? 0 : millis();
      phone_key_confirm_ms = 0;
    } else if (action == TouchAction::kNewKey && !is_base()) {
      if (phone_key_confirm_ms && millis()-phone_key_confirm_ms < 10000) {
        rotate_rover_ap_password();
        survey_revoke_control();
        phone_key_shown_ms = phone_key_confirm_ms = 0;
        reset_ui_region_cache();
      } else phone_key_confirm_ms = millis();
    }
    if (action == TouchAction::kBase) pending_role = DeviceRole::kBase;
    else if (action == TouchAction::kRover) pending_role = DeviceRole::kRover;
    else if (action == TouchAction::kApply) {
      DeviceConfig requested = device_config;
      requested.role = pending_role;
      if (requested.role != device_config.role) requested.base_rtcm = true;
      select_config(requested);
    } else if (action == TouchAction::kAuto) select_brightness(BrightnessMode::kAutomatic);
    else if (action == TouchAction::kDay) select_brightness(BrightnessMode::kDay);
    else if (action == TouchAction::kNight) select_brightness(BrightnessMode::kNight);
    draw_dynamic_screen();
    return;
  }
  if (abs_dy < 70 || abs_dy <= abs_dx) return;

  if (current_page == ScreenPage::kMain && touch_start_y <= 160 && dy > 0) {
    change_page(ScreenPage::kGpsDetails);
  } else if (current_page == ScreenPage::kMain && touch_start_y >= 320 &&
             dy < 0) {
    change_page(ScreenPage::kWifiDetails);
  } else if (current_page == ScreenPage::kGpsDetails && dy < 0) {
    change_page(ScreenPage::kMain);
  } else if (current_page == ScreenPage::kWifiDetails && dy > 0) {
    change_page(ScreenPage::kMain);
  }
}

void service_swipe_navigation() {
  if (!touch_ready) return;
  const uint32_t now = millis();
  if (now - last_touch_poll_ms < 20) return;
  last_touch_poll_ms = now;

  int16_t x = 0;
  int16_t y = 0;
  const TouchRead state = read_touch(x, y);
  if (state == TouchRead::kContact) {
    if (!touch_active) {
      touch_active = true;
      touch_moved = false;
      touch_start_ms = now;
      touch_start_x = x;
      touch_start_y = y;
    }
    touch_last_x = x;
    touch_last_y = y;
    if (std::abs(x - touch_start_x) > 20 || std::abs(y - touch_start_y) > 20) touch_moved = true;
    touch_last_seen_ms = now;
  } else if (state == TouchRead::kError || state == TouchRead::kMultiple) {
    touch_cancelled = true;
  } else if (touch_active && now - touch_last_seen_ms >= 60) {
    touch_active = false;
    finish_swipe();
    touch_cancelled = false;
  } else if (!touch_active) {
    touch_cancelled = false;
  }
}

void handle_line(char *line) {
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
        sd_log_event("PROFILE_FAILED", command);
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
  if (parse_gga(line, parsed)) {
    latest_gga = parsed;
    ++gga_count;
    last_gga_ms = millis();
    const char *label = fix_label(latest_gga.quality);
    if (std::strcmp(last_displayed_fix_label, label) != 0) {
      std::strncpy(last_displayed_fix_label, label,
                   sizeof(last_displayed_fix_label) - 1);
      last_displayed_fix_label[sizeof(last_displayed_fix_label) - 1] = '\0';
      Serial.printf("DISPLAY FIX STATE: %s (GGA quality %d)\n", label,
                    latest_gga.quality);
    }
  }

  HorizontalAccuracyData parsed_accuracy = latest_horizontal_accuracy;
  if (parse_bestnav_accuracy(line, parsed_accuracy)) {
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
  if (parse_rmc_time(line, parsed_time)) {
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

void handle_complete_rtcm(const uint8_t *frame, size_t frame_length) {
  if(is_base()) capture_reference(frame,frame_length);
  ++rtcm_uart_frames;
  rtcm_last_message = rtcm_message_type(frame, frame_length);
  if (is_base()) rtcm_last_rx_ms = millis();

  if (is_base() && unit_profile_applied && device_config.base_rtcm &&
      send_rtcm_packet(frame, frame_length, rtcm_last_message)) {
    ++rtcm_wifi_tx_frames;
  }
}

bool consume_rtcm_byte(uint8_t incoming) {
  const uint32_t now = millis();
  if (rtcm_frame_length > 0 && now - rtcm_last_byte_ms > 250) {
    rtcm_frame_length = 0;
    rtcm_expected_length = 0;
    ++rtcm_uart_bad;
  }

  if (rtcm_frame_length == 0) {
    if (incoming != 0xD3) return false;
    rtcm_frame[0] = incoming;
    rtcm_frame_length = 1;
    rtcm_last_byte_ms = now;
    return true;
  }

  rtcm_last_byte_ms = now;
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
    if (valid_rtcm_frame(rtcm_frame, rtcm_frame_length)) {
      handle_complete_rtcm(rtcm_frame, rtcm_frame_length);
    } else {
      ++rtcm_uart_bad;
    }
    rtcm_frame_length = 0;
    rtcm_expected_length = 0;
  }
  return true;
}

void read_gnss() {
  while (gnss.available() > 0) {
    const uint8_t raw = static_cast<uint8_t>(gnss.read());
    ++byte_count;
    last_rx_ms = millis();

    if (consume_rtcm_byte(raw)) continue;

    const char incoming = static_cast<char>(raw);

    if (incoming == '\r') continue;
    if (incoming == '\n') {
      if (rx_length > 0) {
        rx_line[rx_length] = '\0';
        handle_line(rx_line);
        rx_length = 0;
      }
      continue;
    }

    if (rx_length < sizeof(rx_line) - 1) {
      rx_line[rx_length++] = incoming;
    } else {
      rx_length = 0;
      ++checksum_errors;
      Serial.println("UM980> RX LINE OVERFLOW");
    }
  }
}

void send_command(const char *command) {
  Serial.print("ESP32> ");
  Serial.println(command);
  gnss.print(command);
  gnss.print("\r\n");
}

void apply_unit_profile();

void service_gnss_startup() {
  const uint32_t now = millis();
  if (profile_running) return;
  const bool fresh_gga = last_gga_ms > 0 && now - last_gga_ms < 5000;
  // UNLOG/profile changes can finish before the next 1 Hz GGA arrives.
  if (unit_profile_applied && !fresh_gga && now - profile_complete_ms < 2000) return;

  if (version_ok && fresh_gga) {
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
    std::strcpy(receiver_role, "UNKNOWN");
    rtcm_command_acks = 0;
  }

  gnss_startup_complete = false;
  if (now < next_gnss_handshake_ms) return;

  ++gnss_handshake_attempts;
  Serial.printf("GNSS STARTUP: attempt %lu\n",
                static_cast<unsigned long>(gnss_handshake_attempts));
  send_command("VERSION");
  delay(100);
  send_command("GPGGA COM2 1");
  delay(100);
  send_command("GPRMC COM2 1");
  delay(100);
  send_command("BESTNAVA COM2 1");
  delay(100);
  send_command("MODE");
  next_gnss_handshake_ms = millis() + 3000;
}

void apply_unit_profile() {
  survey_reference_ms=0;
  if(is_base() && base_settings_failed){unit_profile_applied=false;profile_failed=true;profile_running=false;return;}
  unit_profile_applied = false;
  profile_running = true;
  profile_failed = profile_waiting = profile_ack = profile_mode_verified = false;
  profile_step = profile_attempts = 0;
  rtcm_command_acks = 0;
  latest_gga = GgaData{};
  latest_horizontal_accuracy = HorizontalAccuracyData{};
  Serial.printf("PROFILE START: %s\n", is_base() ? "BASE_TEST" : "ROVER_SURVEY");
}

void service_profile() {
  if (!profile_running) return;
  const uint32_t now = millis();
  const char *command = active_profile_command(profile_step);
  if (profile_waiting) {
    const bool verified = std::strcmp(command, "MODE") != 0 || profile_mode_verified;
    if (profile_ack && verified && now - profile_sent_ms >= 100) {
      ++profile_step;
      profile_attempts = 0;
      profile_waiting = false;
      command = active_profile_command(profile_step);
      if (command == nullptr) {
        profile_running = false;
        unit_profile_applied = true;
        profile_complete_ms = now;
        Serial.printf("PROFILE VERIFIED: %s\n", receiver_role);
        sd_log_config(is_base() ? "BASE_TEST" : "ROVER_SURVEY",
                      device_config.base_rtcm ? "VERIFIED|RTCM_ENABLED_IF_BASE" : "VERIFIED|RTCM_OFF");
        return;
      }
    } else if (now - profile_sent_ms < 2000) {
      return;
    } else if (profile_attempts >= 3) {
      profile_running = false;
      profile_failed = true;
      Serial.printf("PROFILE FAILED: timeout on %s\n", command);
      sd_log_event("PROFILE_FAILED", command);
      return;
    } else {
      profile_waiting = false;
    }
  }
  if (!profile_waiting) {
    profile_ack = profile_mode_verified = false;
    profile_waiting = true;
    profile_sent_ms = now;
    ++profile_attempts;
    send_command(command);
  }
}

void print_console_help() {
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

void handle_usb_command(const char *command) {
  Serial.print("CONSOLE> ");
  Serial.println(command);

  if (profile_running) {
    Serial.println("CONSOLE> profile in progress; retry after verification");
    return;
  }

  if (std::strcmp(command, "help") == 0) {
    print_console_help();
  } else if (std::strcmp(command, "role?") == 0) {
    send_command("MODE");
  } else if (std::strcmp(command, "config?") == 0) {
    Serial.printf("CONFIG: role=%s brightness=%u rtcm=%s wifi=%s storage=%s profile=%s\n",
                  is_base() ? "BASE" : "ROVER", static_cast<unsigned>(brightness_mode),
                  device_config.base_rtcm ? "ON" : "OFF", wifi_transport_label(),
                  config_error ? "ERROR" : config_saved ? "SAVED" : "DEFAULTS",
                  unit_profile_applied ? "VERIFIED" : profile_failed ? "FAILED" : "WAITING");
  } else if (std::strcmp(command,"phone?") == 0) {
    Serial.printf("PHONE WIFI: %s SSID=%s IP=%s clients=%u UI=%s error=%s\n",
                  rover_ap_ready() ? "READY" : "OFF",rover_ap_ssid(),rover_ap_address(),rover_ap_clients(),kWebUiVersion,rover_ap_error());
  } else if (std::strcmp(command, "role rover") == 0) {
    select_role(DeviceRole::kRover);
  } else if (std::strcmp(command, "role base-test") == 0) {
    Serial.println("WARNING: temporary averaged base for functional testing only.");
    select_role(DeviceRole::kBase);
  } else if (std::strcmp(command, "wifi?") == 0) {
    const IPAddress ip = is_base() && !using_local_router()
                           ? WiFi.softAPIP() : WiFi.localIP();
    Serial.printf("WIFI CONFIG: transport=%s role=%s ssid=%s station=%s udp=%s ip=%s peer=%s\n",
                  wifi_transport_label(), is_base() ? "BASE" : "ROVER",
                  active_wifi_ssid(), station_connected() ? "UP" : "DOWN",
                  wifi_udp_started ? "UP" : "DOWN", ip.toString().c_str(),
                  wifi_peer_known ? wifi_peer.toString().c_str() : "NONE");
  } else if (std::strcmp(command, "wifi direct") == 0) {
    select_wifi_mode(WiFiMode::kDirect);
  } else if (std::strcmp(command, "wifi local") == 0) {
    select_wifi_mode(WiFiMode::kLocalRouter);
  } else if (std::strcmp(command, "rtcm?") == 0) {
    Serial.printf("RTCM STATUS: UART=%lu TX=%lu RX=%lu bytes=%lu UART_BAD=%lu NET_BAD=%lu last=%u age_ms=%lu\n",
                  static_cast<unsigned long>(rtcm_uart_frames),
                  static_cast<unsigned long>(rtcm_wifi_tx_frames),
                  static_cast<unsigned long>(rtcm_wifi_rx_frames),
                  static_cast<unsigned long>(rtcm_forwarded_bytes),
                  static_cast<unsigned long>(rtcm_uart_bad),
                  static_cast<unsigned long>(wifi_invalid_packets),
                  rtcm_last_message,
                  rtcm_last_rx_ms == 0
                      ? 0UL
                      : static_cast<unsigned long>(millis() - rtcm_last_rx_ms));
  } else if (std::strcmp(command, "accuracy?") == 0) {
    Serial.printf("ACCURACY STATUS: BESTNAV=%lu lat_sigma=%.4f m lon_sigma=%.4f m H-ACC(1DRMS)=%.4f m age_ms=%lu usable=%s\n",
                  static_cast<unsigned long>(bestnav_count),
                  latest_horizontal_accuracy.latitude_sigma_m,
                  latest_horizontal_accuracy.longitude_sigma_m,
                  latest_horizontal_accuracy.horizontal_1drms_m,
                  latest_horizontal_accuracy.received
                      ? static_cast<unsigned long>(millis() -
                                                   latest_horizontal_accuracy.received_ms)
                      : 0UL,
                  latest_gga.received && latest_gga.quality > 0 &&
                          !(std::strcmp(receiver_role, "BASE") == 0 &&
                            latest_gga.quality == 7)
                      ? "YES"
                      : "NO");
  } else if (std::strcmp(command, "time?") == 0) {
    const uint32_t now = millis();
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (local_time_utc_minus_6(now, year, month, day, hour, minute, second)) {
      Serial.printf("GNSS TIME: VALID UTC=%04u-%02u-%02u %02u:%02u:%02u LOCAL(UTC-6)=%04u-%02u-%02u %02u:%02u:%02u NAV=%s\n",
                    latest_gnss_time.year, latest_gnss_time.month,
                    latest_gnss_time.day, latest_gnss_time.hour,
                    latest_gnss_time.minute, latest_gnss_time.second, year,
                    month, day, hour, minute, second,
                    latest_gnss_time.navigation_valid ? "VALID" : "INVALID");
    } else {
      Serial.println("GNSS TIME: INVALID OR STALE; AUTO BRIGHTNESS IS FULL");
    }
  } else if (std::strcmp(command, "brightness?") == 0) {
    Serial.printf("BRIGHTNESS: %s current=%u target=%u PWM=%s hardware_duty=%lu frequency=%lu Hz\n",
                  brightness_mode_label(), static_cast<unsigned>(backlight_duty),
                  static_cast<unsigned>(target_backlight_duty), backlight_pwm_ready ? "PASS" : "FAIL",
                  static_cast<unsigned long>(ledcRead(board::kBacklightPwmChannel)),
                  static_cast<unsigned long>(ledcReadFreq(board::kBacklightPwmChannel)));
  } else if (std::strcmp(command, "brightness auto") == 0) {
    select_brightness(BrightnessMode::kAutomatic);
  } else if (std::strcmp(command, "brightness day") == 0) {
    select_brightness(BrightnessMode::kDay);
  } else if (std::strcmp(command, "brightness night") == 0) {
    select_brightness(BrightnessMode::kNight);
  } else if (std::strcmp(command, "rtcm base-test") == 0) {
    if (!is_base()) {
      Serial.println("CONSOLE> rejected; select base role first");
    } else {
      DeviceConfig requested = device_config;
      requested.base_rtcm = true;
      select_config(requested);
    }
  } else if (std::strcmp(command, "rtcm off") == 0) {
    if (!is_base()) {
      Serial.println("CONSOLE> rejected; select base role first");
    } else {
      DeviceConfig requested = device_config;
      requested.base_rtcm = false;
      select_config(requested);
    }
  } else if (command[0] != '\0') {
    Serial.println("CONSOLE> rejected; type help for the safe command list");
  }
}

void read_usb_console() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\r') continue;
    if (incoming == '\n') {
      if (usb_length > 0) {
        usb_line[usb_length] = '\0';
        handle_usb_command(usb_line);
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

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("TopoRTK UM980 display demo");
  Serial.printf("Instrument unit: %c\n", board::kUnitLabel);
  load_config();
  load_base_settings();

  Wire.begin(board::kI2cSda, board::kI2cScl, 400000);
  const bool expander_ready = io_expander.begin();
  Serial.printf("TCA9554: %s\n", expander_ready ? "PASS" : "FAIL");
  if (expander_ready) {
    io_expander.pinMode1(board::kLcdResetExpanderPin, OUTPUT);
    reset_lcd();
  }

  touch_ready = i2c_device_present(board::kTouchAddress);
  Serial.printf("FT6336 touch navigation: %s\n",
                touch_ready ? "PASS" : "FAIL");

  display_ready = expander_ready && display->begin();
  Serial.printf("ST7796: %s\n", display_ready ? "PASS" : "FAIL");
  setup_backlight();

  if (display_ready) {
    draw_static_screen();
    draw_dynamic_screen();
  }

  gnss.setRxBufferSize(2048);
  gnss.begin(board::kGnssBaud, SERIAL_8N1, board::kGnssRx, board::kGnssTx);
  next_gnss_handshake_ms = millis() + 1500;
  print_console_help();
  start_wifi();
  setup_sd_logging();
  survey_begin(sd_ready && sd_test_passed);
  Serial.println("BOOT COMPLETE");
}

void service_survey() {
  survey::BaseRequest request;
  if(survey_take_base(request)) {
    base_attempt_revision=request.revision;
    base_settings_failed=true;
    if(is_base() && !profile_running && request.revision==base_settings.revision+1) {
      BaseSettings next{};next.fixed=request.fixed?1:0;next.revision=request.revision;
      next.latitude=request.position.latitude;next.longitude=request.position.longitude;next.height=request.position.height;
      next.checksum=survey::crc32(std::string(reinterpret_cast<const char *>(&next),offsetof(BaseSettings,checksum)));
      Preferences p;BaseSettings check{};
      if(p.begin("topobase",false)) {
        const bool saved=p.putBytes("settings",&next,sizeof(next))==sizeof(next) && p.getBytes("settings",&check,sizeof(check))==sizeof(check) && std::memcmp(&next,&check,sizeof(next))==0;
        p.end();if(saved){base_settings=next;base_settings_failed=false;apply_unit_profile();}
      }
    }
    if(base_settings_failed){unit_profile_applied=false;profile_failed=true;}
  }
  const uint32_t now=millis();survey::Fix f;f.now=now;f.rover=!is_base();
  f.unit=board::kUnitLabel;f.boot_id=web_boot_id();f.reset_reason=esp_reset_reason();f.free_heap=ESP.getFreeHeap();f.min_heap=ESP.getMinFreeHeap();f.free_psram=ESP.getFreePsram();
  f.profile_ok=unit_profile_applied&&!profile_failed;f.base_apply_pending=profile_running;f.base_apply_failed=base_settings_failed||profile_failed;f.base_revision=base_settings.revision;
  f.base_attempt_revision=base_attempt_revision;f.base_fixed=base_settings.fixed;f.base_setting={base_settings.latitude,base_settings.longitude,base_settings.height};
  f.position=latest_horizontal_accuracy.position;f.position_valid=latest_horizontal_accuracy.position_valid;
  f.received=latest_horizontal_accuracy.received_ms;f.epoch=latest_horizontal_accuracy.epoch;
  f.fixed=latest_horizontal_accuracy.rtk_fixed && latest_gga.received && latest_gga.quality==4 && now-last_gga_ms<1500;
  f.hacc=latest_horizontal_accuracy.horizontal_1drms_m;f.vacc=latest_horizontal_accuracy.vertical_sigma_m;
  f.linked=peer_linked(now);f.correction_age=rtcm_last_rx_ms?now-rtcm_last_rx_ms:UINT32_MAX;
  f.correction_age=std::max(f.correction_age,latest_horizontal_accuracy.differential_age_ms);
  f.position_valid=f.position_valid && latest_horizontal_accuracy.solution_station==survey_station;
  f.reference_valid=survey_reference_ms!=0;f.reference=survey_reference;f.station=survey_station;
  f.reference_age=survey_reference_ms?now-survey_reference_ms:UINT32_MAX;f.satellites=latest_gga.satellites;
  if(fresh_gnss_time(now))std::snprintf(f.utc,sizeof(f.utc),"%04u-%02u-%02uT%02u:%02u:%02uZ",latest_gnss_time.year,latest_gnss_time.month,latest_gnss_time.day,latest_gnss_time.hour,latest_gnss_time.minute,latest_gnss_time.second);
  survey_update(f);
}

void loop() {
  read_usb_console();
  read_gnss();
  service_gnss_startup();
  service_profile();
  service_wifi();
  service_survey();
  service_swipe_navigation();
  service_brightness();
  service_sd_logging();
  service_web_status();

  const uint32_t now = millis();
  if (now - last_screen_ms >= 250) {
    last_screen_ms = now;
    draw_dynamic_screen();
  }
  delay(2);
}
