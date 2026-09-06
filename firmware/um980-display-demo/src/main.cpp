#include <Arduino.h>
#include <Arduino_GFX_Library.h>
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
constexpr uint8_t kVersion = 1;
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
constexpr uint16_t kBackground = 0x0861;
constexpr uint16_t kHeader = 0x001F;
constexpr uint16_t kPanel = 0x10A2;
constexpr uint16_t kMuted = 0xBDF7;
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

struct __attribute__((packed)) WiFiTestPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t packet_size;
  uint32_t sequence;
  uint32_t sender_ms;
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
bool version_ok = false;
char rx_line[256] = {};
size_t rx_length = 0;
char usb_line[64] = {};
size_t usb_length = 0;
char receiver_role[8] = "UNKNOWN";
uint32_t byte_count = 0;
uint32_t line_count = 0;
uint32_t gga_count = 0;
uint32_t checksum_errors = 0;
uint32_t last_rx_ms = 0;
uint32_t last_screen_ms = 0;
bool wifi_udp_started = false;
bool wifi_peer_known = false;
IPAddress wifi_peer;
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
      ++wifi_rx_packets;
    } else if (!network::kAccessPoint && packet.type == network::kTestPacket) {
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

int hex_value(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
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

const char *fix_label(int quality) {
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
  switch (quality) {
    case 4: return RGB565_GREEN;
    case 5: return RGB565_YELLOW;
    case 1:
    case 2: return RGB565_CYAN;
    default: return RGB565_RED;
  }
}

void draw_label_value(int16_t y, const char *label, const char *value) {
  display->fillRect(0, y, board::kWidth, 30, colors::kBackground);
  display->setTextSize(2);
  display->setTextColor(colors::kMuted);
  display->setCursor(10, y + 7);
  display->print(label);
  display->setTextColor(RGB565_WHITE);
  display->setCursor(92, y + 7);
  display->print(value);
}

void draw_static_screen() {
  display->fillScreen(colors::kBackground);
  display->fillRect(0, 0, board::kWidth, 52, colors::kHeader);
  display->setTextColor(RGB565_WHITE);
  display->setTextSize(2);
  display->setCursor(12, 10);
  display->println("TopoRTK / UM980");
  display->setTextSize(1);
  display->setCursor(12, 34);
  display->println("BDRTK TTL2 115200 -> GPIO44");

  display->fillRoundRect(276, 6, 38, 40, 6, RGB565_YELLOW);
  display->setTextColor(RGB565_BLACK);
  display->setTextSize(3);
  display->setCursor(286, 14);
  display->print(board::kUnitLabel);

  display->fillRect(0, 444, board::kWidth, 36, colors::kPanel);
}

void draw_wifi_status() {
  display->fillRect(0, 444, board::kWidth, 36, colors::kPanel);
  display->setTextSize(1);
  display->setTextColor(colors::kMuted);
  display->setCursor(10, 451);

  if (network::kAccessPoint) {
    display->printf("WIFI TEST AP  CLIENT:%u  PEER:%s",
                    WiFi.softAPgetStationNum(), wifi_peer_known ? "YES" : "NO");
    display->setCursor(10, 465);
    display->printf("TEST:%lu RTCM:%lu R-BAD:%lu",
                    static_cast<unsigned long>(wifi_tx_packets),
                    static_cast<unsigned long>(rtcm_wifi_tx_frames),
                    static_cast<unsigned long>(rtcm_uart_bad));
  } else {
    display->printf("WIFI TEST %s  RSSI:%d",
                    WiFi.status() == WL_CONNECTED ? "UP" : "DOWN",
                    WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    display->setCursor(10, 465);
    display->printf("TEST:%lu RTCM:%lu BAD:%lu",
                    static_cast<unsigned long>(wifi_rx_packets),
                    static_cast<unsigned long>(rtcm_wifi_rx_frames),
                    static_cast<unsigned long>(wifi_invalid_packets));
  }
}

void draw_dynamic_screen() {
  if (!display_ready) return;

  const uint32_t now = millis();
  const bool uart_active = byte_count > 0 && now - last_rx_ms < 3000;
  char value[64] = {};

  display->fillRect(0, 58, board::kWidth, 34, colors::kPanel);
  display->setTextSize(2);
  display->setTextColor(uart_active ? RGB565_GREEN : colors::kWarning);
  display->setCursor(10, 67);
  if (uart_active) {
    display->print("UART RECEIVING");
  } else {
    display->print("UART WAITING...");
  }
  display->setTextSize(1);
  display->setTextColor(colors::kMuted);
  display->setCursor(242, 63);
  display->print(version_ok ? "UM980 OK" : "NO ID");
  display->setCursor(242, 78);
  display->print(receiver_role);

  display->fillRect(0, 100, board::kWidth, 58, fix_color(latest_gga.quality));
  display->setTextSize(3);
  display->setTextColor(latest_gga.quality == 5 ? RGB565_BLACK : RGB565_WHITE);
  display->setCursor(12, 118);
  display->print(latest_gga.received ? fix_label(latest_gga.quality) : "NO GGA");

  draw_label_value(170, "UTC", latest_gga.utc);
  if (latest_gga.received && latest_gga.quality > 0) {
    std::snprintf(value, sizeof(value), "%.8f", latest_gga.latitude);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(204, "LAT", value);

  if (latest_gga.received && latest_gga.quality > 0) {
    std::snprintf(value, sizeof(value), "%.8f", latest_gga.longitude);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(238, "LON", value);

  std::snprintf(value, sizeof(value), "%d   HDOP %.2f",
                latest_gga.satellites, latest_gga.hdop);
  draw_label_value(272, "SATS", value);

  if (latest_gga.received && latest_gga.quality > 0) {
    std::snprintf(value, sizeof(value), "%.3f m", latest_gga.altitude);
  } else {
    std::strcpy(value, "---");
  }
  draw_label_value(306, "ALT", value);

  std::snprintf(value, sizeof(value), "%lu  LINES %lu",
                static_cast<unsigned long>(gga_count),
                static_cast<unsigned long>(line_count));
  draw_label_value(350, "GGA", value);

  std::snprintf(value, sizeof(value), "%lu   CS ERR %lu",
                static_cast<unsigned long>(byte_count),
                static_cast<unsigned long>(checksum_errors));
  draw_label_value(384, "BYTES", value);

  if (byte_count == 0) {
    std::strcpy(value, "never");
  } else {
    std::snprintf(value, sizeof(value), "%lu ms ago",
                  static_cast<unsigned long>(now - last_rx_ms));
  }
  draw_label_value(414, "LAST RX", value);
  draw_wifi_status();
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
  } else if (std::strstr(line, ";MODE BASE") != nullptr) {
    std::strcpy(receiver_role, "BASE");
  }

  GgaData parsed = latest_gga;
  if (parse_gga(line, parsed)) {
    latest_gga = parsed;
    ++gga_count;
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

void print_console_help() {
  Serial.println("Safe console commands:");
  Serial.println("  help           - show this list");
  Serial.println("  role?          - query current UM980 mode");
  Serial.println("  role rover     - set MODE ROVER SURVEY, then verify");
  Serial.println("  role base-test - set temporary MODE BASE, then verify");
  Serial.println("  rtcm?          - show RTCM bridge counters");
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
  } else if (std::strcmp(command, "role base-test") == 0) {
    Serial.println("WARNING: temporary averaged base for functional testing only.");
    send_command("MODE BASE");
    delay(100);
    send_command("MODE");
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
  } else if (std::strcmp(command, "rtcm base-test") == 0) {
    if (!network::kAccessPoint) {
      Serial.println("CONSOLE> rejected; RTCM base output is restricted to Unit A");
    } else {
      Serial.println("WARNING: enabling volatile RTCM MSM4 test output on COM2.");
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
  } else if (std::strcmp(command, "rtcm off") == 0) {
    if (!network::kAccessPoint) {
      Serial.println("CONSOLE> rejected; RTCM output control is restricted to Unit A");
    } else {
      send_command("UNLOG COM2");
      delay(100);
      send_command("GPGGA COM2 1");
      delay(100);
      send_command("MODE");
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

  display_ready = expander_ready && display->begin();
  Serial.printf("ST7796: %s\n", display_ready ? "PASS" : "FAIL");
  pinMode(board::kBacklight, OUTPUT);
  digitalWrite(board::kBacklight, HIGH);

  if (display_ready) {
    draw_static_screen();
    draw_dynamic_screen();
  }

  gnss.setRxBufferSize(2048);
  gnss.begin(board::kGnssBaud, SERIAL_8N1, board::kGnssRx, board::kGnssTx);
  delay(300);
  send_command("VERSION");
  delay(300);
  send_command("GPGGA 1");
  delay(100);
  send_command("MODE");
  print_console_help();
  start_wifi();
  Serial.println("BOOT COMPLETE");
}

void loop() {
  read_usb_console();
  read_gnss();
  service_wifi();

  const uint32_t now = millis();
  if (now - last_screen_ms >= 250) {
    last_screen_ms = now;
    draw_dynamic_screen();
  }
  delay(2);
}
