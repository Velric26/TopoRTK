#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <FS.h>
#include <SD_MMC.h>
#include <TCA9554.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

#ifndef TOPORTK_DISPLAY_ROTATION
#define TOPORTK_DISPLAY_ROTATION 2
#endif

#ifndef TOPORTK_UNIT_ID
#define TOPORTK_UNIT_ID 1
#endif

static_assert(TOPORTK_DISPLAY_ROTATION >= 0 && TOPORTK_DISPLAY_ROTATION <= 3,
              "TOPORTK_DISPLAY_ROTATION must be 0, 1, 2, or 3");
static_assert(TOPORTK_UNIT_ID >= 1 && TOPORTK_UNIT_ID <= 26,
              "TOPORTK_UNIT_ID must be between 1 (A) and 26 (Z)");

namespace board {
constexpr int kBacklight = 6;
constexpr uint8_t kBacklightPwmChannel = 0;
constexpr uint32_t kBacklightPwmHz = 5000;
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
constexpr char kSsid[] = "TopoRTK-Link-Test";
constexpr char kPassword[] = "TopoRTK-test-2026";
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
constexpr bool kAccessPoint = TOPORTK_UNIT_ID == 1;
}  // namespace network

namespace colors {
constexpr uint16_t kBackground = RGB565_BLACK;
constexpr uint16_t kHeaderReady = 0x0400;
constexpr uint16_t kHeaderNotReady = 0x4208;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kMuted = 0xD69A;
constexpr uint16_t kWarning = 0xFD20;
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

enum class BrightnessMode : uint8_t {
  kAutomatic,
  kDay,
  kNight,
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

enum class ScreenPage : uint8_t {
  kMain,
  kGpsDetails,
  kWifiDetails,
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
int16_t touch_start_x = 0;
int16_t touch_start_y = 0;
int16_t touch_last_x = 0;
int16_t touch_last_y = 0;
uint32_t touch_last_seen_ms = 0;
uint32_t last_touch_poll_ms = 0;
ScreenPage current_page = ScreenPage::kMain;
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
uint8_t backlight_duty = 255;
uint8_t target_backlight_duty = 255;
uint32_t last_brightness_step_ms = 0;
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
      !network::kAccessPoint && WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
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
  if (network::kAccessPoint || packet == nullptr ||
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
  return true;
}

void start_wifi() {
  WiFi.persistent(false);
  WiFi.setSleep(false);

  if (network::kAccessPoint) {
    WiFi.mode(WIFI_AP);
    const IPAddress address(192, 168, 4, 1);
    const IPAddress gateway(192, 168, 4, 1);
    const IPAddress subnet(255, 255, 255, 0);
    const bool configured = WiFi.softAPConfig(address, gateway, subnet);
    const bool started = WiFi.softAP(network::kSsid, network::kPassword, 6, false, 1);
    wifi_udp_started = started && wifi_udp.begin(network::kUdpPort);
    Serial.printf("WIFI TEST A: AP config=%s start=%s UDP=%s IP=%s\n",
                  configured ? "PASS" : "FAIL", started ? "PASS" : "FAIL",
                  wifi_udp_started ? "PASS" : "FAIL",
                  WiFi.softAPIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(network::kSsid, network::kPassword);
    last_wifi_connect_attempt_ms = millis();
    Serial.printf("WIFI TEST B: connecting to %s\n", network::kSsid);
  }
}

void receive_wifi_packets() {
  if (!wifi_udp_started) return;

  int packet_length = 0;
  while ((packet_length = wifi_udp.parsePacket()) > 0) {
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

    if (network::kAccessPoint && packet.type == network::kHelloPacket) {
      wifi_peer = wifi_udp.remoteIP();
      wifi_peer_known = true;
      wifi_peer_rssi_dbm = packet.link_rssi_dbm;
      wifi_last_peer_ms = millis();
      ++wifi_rx_packets;
    } else if (!network::kAccessPoint && packet.type == network::kTestPacket) {
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

  if (!network::kAccessPoint && WiFi.status() == WL_CONNECTED &&
      !wifi_udp_started) {
    wifi_udp_started = wifi_udp.begin(network::kUdpPort);
    Serial.printf("WIFI TEST B: connected UDP=%s IP=%s RSSI=%d dBm\n",
                  wifi_udp_started ? "PASS" : "FAIL",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }

  if (!network::kAccessPoint && WiFi.status() != WL_CONNECTED &&
      now - last_wifi_connect_attempt_ms >= 10000) {
    last_wifi_connect_attempt_ms = now;
    WiFi.disconnect();
    WiFi.begin(network::kSsid, network::kPassword);
    Serial.println("WIFI TEST B: retrying connection");
  }

  receive_wifi_packets();

  if (network::kAccessPoint && wifi_peer_known &&
      now - last_wifi_send_ms >= network::kTestIntervalMs) {
    last_wifi_send_ms = now;
    if (send_wifi_packet(wifi_peer, network::kTestPacket, ++wifi_tx_sequence)) {
      ++wifi_tx_packets;
    }
  } else if (!network::kAccessPoint && WiFi.status() == WL_CONNECTED &&
             wifi_udp_started &&
             now - last_wifi_send_ms >= network::kHelloIntervalMs) {
    last_wifi_send_ms = now;
    if (send_wifi_packet(IPAddress(192, 168, 4, 1), network::kHelloPacket,
                         ++wifi_tx_sequence)) {
      ++wifi_tx_packets;
    }
  }

  if (now - last_wifi_status_ms >= network::kStatusIntervalMs) {
    last_wifi_status_ms = now;
    if (network::kAccessPoint) {
      Serial.printf("WIFI TEST A: clients=%u peer=%s TX=%lu hello_RX=%lu bad=%lu RTCM_UART=%lu RTCM_TX=%lu RTCM_BAD=%lu last=%u\n",
                    WiFi.softAPgetStationNum(), wifi_peer_known ? "YES" : "NO",
                    static_cast<unsigned long>(wifi_tx_packets),
                    static_cast<unsigned long>(wifi_rx_packets),
                    static_cast<unsigned long>(wifi_invalid_packets),
                    static_cast<unsigned long>(rtcm_uart_frames),
                    static_cast<unsigned long>(rtcm_wifi_tx_frames),
                    static_cast<unsigned long>(rtcm_uart_bad), rtcm_last_message);
    } else {
      Serial.printf("WIFI TEST B: link=%s RSSI=%d TX_hello=%lu RX=%lu gap=%lu bad=%lu last=%lu RTCM_RX=%lu bytes=%lu type=%u\n",
                    WiFi.status() == WL_CONNECTED ? "UP" : "DOWN",
                    WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
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

bool read_touch(int16_t &x, int16_t &y) {
  constexpr uint8_t kFirstRegister = 0x00;
  constexpr size_t kReadLength = 7;
  uint8_t data[kReadLength] = {};

  Wire.beginTransmission(board::kTouchAddress);
  Wire.write(kFirstRegister);
  if (Wire.endTransmission(false) != 0) return false;

  const size_t received = Wire.requestFrom(
      static_cast<int>(board::kTouchAddress), static_cast<int>(kReadLength));
  if (received != kReadLength) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t index = 0; index < kReadLength; ++index) {
    data[index] = Wire.read();
  }

  const uint8_t points = data[2] & 0x0F;
  if (points == 0 || points == 0x0F) return false;
  x = static_cast<int16_t>(((data[3] & 0x0F) << 8) | data[4]);
  y = static_cast<int16_t>(((data[5] & 0x0F) << 8) | data[6]);
  return x >= 0 && x < board::kWidth && y >= 0 && y < board::kHeight;
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
  ++body;

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
  return true;
}

void format_horizontal_accuracy(char *output, size_t output_size,
                                uint32_t now) {
  if (!latest_gga.received || latest_gga.quality <= 0) {
    std::snprintf(output, output_size, "---");
    return;
  }
  if (std::strcmp(receiver_role, "BASE") == 0 && latest_gga.quality == 7) {
    std::snprintf(output, output_size, "N/A (BASE)");
    return;
  }
  if (!latest_horizontal_accuracy.received ||
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

void draw_label_value(int16_t y, const char *label, const char *value) {
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%s", label, value);
  if (!ui_region_changed(2, y, signature)) return;
  display->fillRect(0, y, board::kWidth, 30, colors::kBackground);
  display->setTextSize(2);
  display->setTextColor(colors::kMuted);
  display->setCursor(10, y + 7);
  display->print(label);
  display->setTextColor(RGB565_WHITE);
  display->setCursor(92, y + 7);
  display->print(value);
}

bool peer_linked(uint32_t now) {
  if (wifi_last_peer_ms == 0 || now - wifi_last_peer_ms > 3000) return false;
  if (network::kAccessPoint) {
    return wifi_peer_known && WiFi.softAPgetStationNum() > 0;
  }
  return WiFi.status() == WL_CONNECTED;
}

int16_t current_link_rssi() {
  if (!network::kAccessPoint && WiFi.status() == WL_CONNECTED) {
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
  if (!latest_gga.received) return false;
  if (std::strcmp(receiver_role, "BASE") == 0) return latest_gga.quality == 7;
  if (std::strcmp(receiver_role, "ROVER") == 0) return latest_gga.quality == 4;
  return false;
}

bool correction_link_connected(uint32_t now) {
  return peer_linked(now);
}

bool fresh_rover_corrections(uint32_t now) {
  return network::kAccessPoint ||
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
  } else {
    ready = correction_link_connected(now);
  }
  return ready ? colors::kHeaderReady : colors::kHeaderNotReady;
}

const char *device_role_title() {
  if (std::strcmp(receiver_role, "ROVER") == 0) return "Rover";
  if (std::strcmp(receiver_role, "BASE") == 0) return "Base";
  return "Starting";
}

const char *brightness_mode_label() {
  if (brightness_mode == BrightnessMode::kDay) return "DAY OVERRIDE";
  if (brightness_mode == BrightnessMode::kNight) return "NIGHT OVERRIDE";
  return fresh_gnss_time(millis()) ? "AUTO (GNSS)" : "AUTO (FULL)";
}

uint8_t automatic_brightness_target(uint32_t now) {
  constexpr uint8_t kNightDuty = 96;
  constexpr uint8_t kDayDuty = 255;
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
    target_backlight_duty = 255;
  } else if (brightness_mode == BrightnessMode::kNight) {
    target_backlight_duty = 96;
  } else {
    target_backlight_duty = automatic_brightness_target(now);
  }

  if (now - last_brightness_step_ms < 40 ||
      backlight_duty == target_backlight_duty) {
    return;
  }
  last_brightness_step_ms = now;
  backlight_duty += backlight_duty < target_backlight_duty ? 1 : -1;
  ledcWrite(board::kBacklightPwmChannel, backlight_duty);
}

bool sd_append_text(const char *path, const char *text) {
  if (!sd_ready || path == nullptr || text == nullptr) return false;
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
  std::snprintf(sd_session_path, sizeof(sd_session_path), "%s/BOOT-%lu",
                sessions_path, static_cast<unsigned long>(millis()));
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
  const bool rtcm_active = network::kAccessPoint
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
                      uint8_t value_size = 2) {
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%d|%s|%s|%u|%u", height,
                title, value, color, value_size);
  if (!ui_region_changed(1, y, signature)) return;
  display->fillRoundRect(8, y, board::kWidth - 16, height, 6, colors::kPanel);
  display->setTextSize(1);
  display->setTextColor(colors::kMuted);
  display->setCursor(18, y + 9);
  display->print(title);
  display->setTextSize(value_size);
  display->setTextColor(color);
  display->setCursor(18, y + 26);
  display->print(value);
}

void draw_header(uint32_t now) {
  char title[48] = {};
  char subtitle[64] = {};
  const uint16_t header_color = page_header_color(now);
  if (current_page == ScreenPage::kGpsDetails) {
    std::snprintf(title, sizeof(title), "GPS DETAILS - %s", device_role_title());
    std::snprintf(subtitle, sizeof(subtitle), "REQUIRED FIX: %s",
                  gps_required_fix() ? "YES" : "NO");
  } else if (current_page == ScreenPage::kWifiDetails) {
    std::snprintf(title, sizeof(title), "WIFI DETAILS - %s", device_role_title());
    std::snprintf(subtitle, sizeof(subtitle), "NETWORK: %s",
                  correction_link_connected(now) ? "CONNECTED" : "DISCONNECTED");
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
  display->setTextColor(RGB565_WHITE);
  display->setTextSize(2);
  display->setCursor(10, 8);
  display->print(title);

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

void draw_main_dashboard(uint32_t now) {
  const bool uart_active = byte_count > 0 && now - last_rx_ms < 3000;
  const bool linked = peer_linked(now);
  const int16_t rssi = current_link_rssi();
  char value[64] = {};

  draw_status_card(60, 70,
                   network::kAccessPoint ? "ROVER / BASE LINK"
                                         : "BASE / ROVER LINK",
                   linked ? "LINKED" : "NO LINK",
                   linked ? RGB565_GREEN : RGB565_RED);

  draw_status_card(138, 70, "GNSS SOLUTION",
                   latest_gga.received ? fix_label(latest_gga.quality)
                                       : "NO FIX",
                   fix_color(latest_gga.quality));

  format_horizontal_accuracy(value, sizeof(value), now);
  draw_status_card(216, 70, "HORIZONTAL UNCERTAINTY (1DRMS)", value,
                   std::strcmp(value, "---") == 0 ? colors::kWarning
                                                   : RGB565_WHITE);

  if (linked) {
    std::snprintf(value, sizeof(value), "%s  %d dBm",
                  link_quality_label(rssi), rssi);
  } else {
    std::strcpy(value, "---");
  }
  draw_status_card(294, 70, "CORRECTION LINK QUALITY", value,
                   linked ? link_quality_color(rssi) : RGB565_RED);

  const char *alert = "CHECK FIX QUALITY";
  uint16_t alert_color = colors::kWarning;
  if (!uart_active || !version_ok) {
    alert = "WARNING: UM980 OFFLINE";
    alert_color = RGB565_RED;
  } else if (!unit_profile_applied) {
    alert = "WAIT: CONFIGURING UM980";
  } else if (!linked) {
    alert = network::kAccessPoint ? "WARNING: ROVER LINK DOWN"
                                  : "WARNING: BASE LINK DOWN";
    alert_color = RGB565_RED;
  } else if (!network::kAccessPoint &&
             (rtcm_last_rx_ms == 0 || now - rtcm_last_rx_ms > 3000)) {
    alert = "WAIT: NO FRESH CORRECTIONS";
  } else if (std::strcmp(receiver_role, "BASE") == 0 &&
             (latest_gga.quality == 1 || latest_gga.quality == 2)) {
    alert = "WAIT: BASE SURVEYING";
  } else if (std::strcmp(receiver_role, "BASE") == 0 &&
             latest_gga.quality == 7) {
    alert = "BASE LOCKED - CONTROL UNVERIFIED";
    alert_color = RGB565_GREEN;
  } else if (latest_gga.quality == 4) {
    alert = "RTK FIXED - VERIFY CONTROL";
    alert_color = RGB565_GREEN;
  } else if (latest_gga.quality == 5) {
    alert = "WAIT: RTK FLOAT";
  } else if (latest_gga.quality == 0) {
    alert = "WAIT: NO GNSS FIX";
  }
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%u", alert, alert_color);
  if (ui_region_changed(3, 372, signature)) {
    display->fillRoundRect(8, 372, board::kWidth - 16, 64, 6, alert_color);
    display->setTextSize(1);
    display->setTextColor(alert_color == RGB565_YELLOW ? RGB565_BLACK
                                                        : RGB565_WHITE);
    display->setCursor(18, 400);
    display->print(alert);
  }
}

void draw_gps_details(uint32_t now) {
  char value[80] = {};
  const bool uart_active = byte_count > 0 && now - last_rx_ms < 3000;
  draw_label_value(58, "UART", uart_active ? "RECEIVING" : "WAITING");
  draw_label_value(90, "FIX",
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
  draw_label_value(122, "LOCAL", value);

  if (fresh_gnss_time(now)) {
    std::snprintf(value, sizeof(value), "%02u:%02u:%02u",
                  latest_gnss_time.hour, latest_gnss_time.minute,
                  latest_gnss_time.second);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(154, "UTC", value);
  if (fresh_gnss_time(now)) {
    std::snprintf(value, sizeof(value), "%04u-%02u-%02u",
                  latest_gnss_time.year, latest_gnss_time.month,
                  latest_gnss_time.day);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(186, "DATE", value);

  if (latest_gga.received && latest_gga.quality > 0) {
    std::snprintf(value, sizeof(value), "%.8f", latest_gga.latitude);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(218, "LAT", value);
  if (latest_gga.received && latest_gga.quality > 0) {
    std::snprintf(value, sizeof(value), "%.8f", latest_gga.longitude);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(250, "LON", value);

  std::snprintf(value, sizeof(value), "%d  HDOP %.2f",
                latest_gga.satellites, latest_gga.hdop);
  draw_label_value(282, "SATS", value);
  if (latest_gga.received && latest_gga.quality > 0) {
    std::snprintf(value, sizeof(value), "%.3f m", latest_gga.altitude);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(314, "ALT", value);

  format_horizontal_accuracy(value, sizeof(value), now);
  draw_label_value(346, "H-ACC", value);
  if (latest_horizontal_accuracy.received) {
    std::snprintf(value, sizeof(value), "N %.3f E %.3f m",
                  latest_horizontal_accuracy.latitude_sigma_m,
                  latest_horizontal_accuracy.longitude_sigma_m);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(378, "SIGMA", value);

  if (network::kAccessPoint) {
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
  draw_label_value(410, "RTCM", value);
}

void draw_wifi_details(uint32_t now) {
  char value[80] = {};
  const bool linked = peer_linked(now);
  const int16_t rssi = current_link_rssi();
  draw_label_value(58, "MODE", network::kAccessPoint ? "ACCESS POINT" : "STATION");
  draw_label_value(90, "SSID", network::kSsid);
  const IPAddress local_ip = network::kAccessPoint ? WiFi.softAPIP()
                                                   : WiFi.localIP();
  std::snprintf(value, sizeof(value), "%u.%u.%u.%u", local_ip[0], local_ip[1],
                local_ip[2], local_ip[3]);
  draw_label_value(122, "IP", value);
  draw_label_value(154, "LINK", linked ? "LINKED" : "NO LINK");
  if (linked) {
    std::snprintf(value, sizeof(value), "%d dBm  %s", rssi,
                  link_quality_label(rssi));
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(186, "RSSI", value);

  if (wifi_peer_known) {
    std::snprintf(value, sizeof(value), "%u.%u.%u.%u", wifi_peer[0],
                  wifi_peer[1], wifi_peer[2], wifi_peer[3]);
  } else if (!network::kAccessPoint && WiFi.status() == WL_CONNECTED) {
    std::strcpy(value, "192.168.4.1");
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(218, "PEER", value);
  std::snprintf(value, sizeof(value), "TX %lu  RX %lu",
                static_cast<unsigned long>(wifi_tx_packets),
                static_cast<unsigned long>(wifi_rx_packets));
  draw_label_value(250, "PKTS", value);
  std::snprintf(value, sizeof(value), "GAP %lu  BAD %lu",
                static_cast<unsigned long>(wifi_sequence_gaps),
                static_cast<unsigned long>(wifi_invalid_packets));
  draw_label_value(282, "NET", value);
  std::snprintf(value, sizeof(value), "TX %lu  RX %lu",
                static_cast<unsigned long>(rtcm_wifi_tx_frames),
                static_cast<unsigned long>(rtcm_wifi_rx_frames));
  draw_label_value(314, "RTCM", value);
  std::snprintf(value, sizeof(value), "%lu bytes  type %u",
                static_cast<unsigned long>(rtcm_forwarded_bytes),
                rtcm_last_message);
  draw_label_value(346, "DATA", value);
  if (wifi_last_peer_ms > 0) {
    std::snprintf(value, sizeof(value), "%lu ms ago",
                  static_cast<unsigned long>(now - wifi_last_peer_ms));
  } else {
    std::strcpy(value, "never");
  }
  draw_label_value(378, "LAST", value);
  std::snprintf(value, sizeof(value), "%s %u%%", brightness_mode_label(),
                static_cast<unsigned>((backlight_duty * 100U + 127U) / 255U));
  draw_label_value(410, "LIGHT", value);
}

void draw_dynamic_screen() {
  if (!display_ready) return;
  const uint32_t now = millis();
  draw_header(now);
  if (current_page == ScreenPage::kGpsDetails) {
    draw_gps_details(now);
  } else if (current_page == ScreenPage::kWifiDetails) {
    draw_wifi_details(now);
  } else {
    draw_main_dashboard(now);
  }
}

void change_page(ScreenPage page) {
  if (page == current_page) return;
  current_page = page;
  reset_ui_region_cache();
  const char *name = page == ScreenPage::kGpsDetails
                         ? "GPS DETAILS"
                         : page == ScreenPage::kWifiDetails ? "WIFI DETAILS"
                                                            : "MAIN";
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
  if (read_touch(x, y)) {
    if (!touch_active) {
      touch_active = true;
      touch_start_x = x;
      touch_start_y = y;
    }
    touch_last_x = x;
    touch_last_y = y;
    touch_last_seen_ms = now;
  } else if (touch_active && now - touch_last_seen_ms >= 60) {
    touch_active = false;
    finish_swipe();
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

  if (std::strstr(line, ";MODE ROVER") != nullptr) {
    std::strcpy(receiver_role, "ROVER");
    last_displayed_fix_label[0] = '\0';
  } else if (std::strstr(line, ";MODE BASE") != nullptr) {
    std::strcpy(receiver_role, "BASE");
    last_displayed_fix_label[0] = '\0';
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
  ++rtcm_uart_frames;
  rtcm_last_message = rtcm_message_type(frame, frame_length);
  rtcm_last_rx_ms = millis();

  if (network::kAccessPoint &&
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
  const bool fresh_gga = last_gga_ms > 0 && now - last_gga_ms < 5000;

  if (version_ok && fresh_gga) {
    if (!gnss_startup_complete) {
      gnss_startup_complete = true;
      Serial.printf("GNSS STARTUP: PASS after %lu attempt(s)\n",
                    static_cast<unsigned long>(gnss_handshake_attempts));
      if (!unit_profile_applied) apply_unit_profile();
    }
    return;
  }

  if (gnss_startup_complete) {
    Serial.println("GNSS STARTUP: link lost; automatic profile will be reapplied");
    version_ok = false;
    unit_profile_applied = false;
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

void enable_rtcm_base_output() {
  rtcm_command_acks = 0;
  send_command("RTCM1006 COM2 10");
  delay(100);
  send_command("RTCM1033 COM2 10");
  delay(100);
  send_command("RTCM1074 COM2 1");
  delay(100);
  send_command("RTCM1124 COM2 1");
  delay(100);
  send_command("RTCM1084 COM2 1");
  delay(100);
  send_command("RTCM1094 COM2 1");
}

void apply_unit_profile() {
  unit_profile_applied = true;
  send_command("GPGGA COM2 1");
  delay(100);
  send_command("GPRMC COM2 1");
  delay(100);
  send_command("BESTNAVA COM2 1");
  delay(100);
  if (network::kAccessPoint) {
    Serial.println("AUTO PROFILE: temporary base plus volatile RTCM output");
    Serial.println("WARNING: autonomous base is for functional testing only.");
    send_command("MODE BASE");
    delay(100);
    enable_rtcm_base_output();
    delay(100);
    send_command("MODE");
    sd_log_config("BASE_TEST", "MODE_BASE|RTCM_1006_1033_1074_1084_1094_1124|GPGGA_GPRMC_BESTNAVA");
  } else {
    Serial.println("AUTO PROFILE: survey rover mode");
    send_command("MODE ROVER SURVEY");
    delay(100);
    send_command("MODE");
    sd_log_config("ROVER_SURVEY", "GPGGA_GPRMC_BESTNAVA");
  }
}

void print_console_help() {
  Serial.println("Safe console commands:");
  Serial.println("  help           - show this list");
  Serial.println("  role?          - query current UM980 mode");
  Serial.println("  role rover     - set MODE ROVER SURVEY, then verify");
  Serial.println("  role base-test - set temporary MODE BASE, then verify");
  Serial.println("  rtcm?          - show RTCM bridge counters");
  Serial.println("  accuracy?      - show BESTNAV horizontal-accuracy parser status");
  Serial.println("  time?          - show GNSS UTC and fixed UTC-6 local time");
  Serial.println("  brightness auto|day|night - select display brightness mode");
  Serial.println("  rtcm base-test - Unit A: enable volatile COM2 MSM4 test output");
  Serial.println("  rtcm off       - Unit A: stop COM2 output, then restore GGA");
  Serial.println("No arbitrary passthrough and no SAVECONFIG are provided.");
}

void handle_usb_command(const char *command) {
  Serial.print("CONSOLE> ");
  Serial.println(command);

  if (std::strcmp(command, "help") == 0) {
    print_console_help();
  } else if (std::strcmp(command, "role?") == 0) {
    send_command("MODE");
  } else if (std::strcmp(command, "role rover") == 0) {
    send_command("MODE ROVER SURVEY");
    delay(100);
    send_command("MODE");
    sd_log_config("ROLE_ROVER", "MODE_ROVER_SURVEY_REQUEST");
  } else if (std::strcmp(command, "role base-test") == 0) {
    Serial.println("WARNING: temporary averaged base for functional testing only.");
    send_command("MODE BASE");
    delay(100);
    send_command("MODE");
    sd_log_config("ROLE_BASE_TEST", "MODE_BASE_REQUEST");
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
  } else if (std::strcmp(command, "brightness auto") == 0) {
    brightness_mode = BrightnessMode::kAutomatic;
    Serial.println("BRIGHTNESS: AUTO (GNSS time; full when uncertain)");
  } else if (std::strcmp(command, "brightness day") == 0) {
    brightness_mode = BrightnessMode::kDay;
    Serial.println("BRIGHTNESS: DAY OVERRIDE");
  } else if (std::strcmp(command, "brightness night") == 0) {
    brightness_mode = BrightnessMode::kNight;
    Serial.println("BRIGHTNESS: NIGHT OVERRIDE");
  } else if (std::strcmp(command, "rtcm base-test") == 0) {
    if (!network::kAccessPoint) {
      Serial.println("CONSOLE> rejected; RTCM base output is restricted to Unit A");
    } else {
      Serial.println("WARNING: enabling volatile RTCM MSM4 test output on COM2.");
      enable_rtcm_base_output();
      sd_log_config("RTCM_BASE_TEST", "VOLATILE_MSM4_OUTPUT");
    }
  } else if (std::strcmp(command, "rtcm off") == 0) {
    if (!network::kAccessPoint) {
      Serial.println("CONSOLE> rejected; RTCM output control is restricted to Unit A");
    } else {
      send_command("UNLOG COM2");
      delay(100);
      send_command("GPGGA COM2 1");
      delay(100);
      send_command("GPRMC COM2 1");
      delay(100);
      send_command("MODE");
      sd_log_config("RTCM_OFF", "UNLOG_COM2_RESTORE_GPGGA_GPRMC");
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
  ledcSetup(board::kBacklightPwmChannel, board::kBacklightPwmHz, 8);
  ledcAttachPin(board::kBacklight, board::kBacklightPwmChannel);
  ledcWrite(board::kBacklightPwmChannel, backlight_duty);

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
  Serial.println("BOOT COMPLETE");
}

void loop() {
  read_usb_console();
  read_gnss();
  service_gnss_startup();
  service_wifi();
  service_swipe_navigation();
  service_brightness();
  service_sd_logging();

  const uint32_t now = millis();
  if (now - last_screen_ms >= 250) {
    last_screen_ms = now;
    draw_dynamic_screen();
  }
  delay(2);
}
