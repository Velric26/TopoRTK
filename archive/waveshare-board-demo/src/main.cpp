#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include <Wire.h>

namespace board {
constexpr int kBacklight = 6;
constexpr int kSpiMiso = 2;
constexpr int kSpiMosi = 1;
constexpr int kSpiClock = 5;
constexpr int kLcdChipSelect = -1;
constexpr int kLcdDataCommand = 3;
constexpr int kLcdReset = -1;
constexpr int kWidth = 320;
constexpr int kHeight = 480;
constexpr int kI2cSda = 8;
constexpr int kI2cScl = 7;
constexpr uint8_t kIoExpanderAddress = 0x20;
constexpr uint8_t kTouchAddress = 0x38;
constexpr uint8_t kLcdResetExpanderPin = 1;
}  // namespace board

TCA9554 io_expander(board::kIoExpanderAddress);
Arduino_DataBus *display_bus = new Arduino_ESP32SPI(
    board::kLcdDataCommand, board::kLcdChipSelect, board::kSpiClock,
    board::kSpiMosi, board::kSpiMiso);
Arduino_GFX *display = new Arduino_ST7796(
    display_bus, board::kLcdReset, 0, true, board::kWidth, board::kHeight);

bool display_ready = false;
bool touch_ready = false;
bool touch_was_down = false;
uint32_t touch_count = 0;
uint32_t last_heartbeat_ms = 0;

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
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = Wire.requestFrom(
      static_cast<int>(board::kTouchAddress), static_cast<int>(kReadLength));
  if (received != kReadLength) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < kReadLength; ++i) {
    data[i] = Wire.read();
  }

  const uint8_t points = data[2] & 0x0F;
  if (points == 0 || points == 0x0F) {
    return false;
  }

  x = static_cast<int16_t>(((data[3] & 0x0F) << 8) | data[4]);
  y = static_cast<int16_t>(((data[5] & 0x0F) << 8) | data[6]);
  return x >= 0 && x < board::kWidth && y >= 0 && y < board::kHeight;
}

void draw_static_screen() {
  display->fillScreen(RGB565_BLACK);

  display->fillRect(0, 0, board::kWidth, 64, RGB565_NAVY);
  display->setTextColor(RGB565_WHITE);
  display->setTextSize(3);
  display->setCursor(18, 18);
  display->print("TopoRTK");

  display->setTextSize(2);
  display->setTextColor(RGB565_CYAN);
  display->setCursor(18, 86);
  display->println("Board demo");

  display->setTextColor(RGB565_WHITE);
  display->setCursor(18, 126);
  display->println("Display: PASS");
  display->setCursor(18, 158);
  display->printf("Touch:   %s", touch_ready ? "READY" : "FAIL");

  display->fillRect(18, 210, 284, 80, RGB565_DARKGREEN);
  display->setTextColor(RGB565_WHITE);
  display->setCursor(46, 226);
  display->println("TOUCH HERE");
  display->setCursor(71, 258);
  display->println("to test");

  display->fillRect(0, 332, 107, 48, RGB565_RED);
  display->fillRect(107, 332, 106, 48, RGB565_GREEN);
  display->fillRect(213, 332, 107, 48, RGB565_BLUE);

  display->setTextColor(RGB565_LIGHTGREY);
  display->setCursor(18, 406);
  display->println("Touches: 0");
  display->setCursor(18, 438);
  display->println("X: ---  Y: ---");
}

void draw_touch(int16_t x, int16_t y) {
  display->fillCircle(x, y, 7, RGB565_YELLOW);
  display->fillRect(18, 400, 292, 66, RGB565_BLACK);
  display->setTextSize(2);
  display->setTextColor(RGB565_LIGHTGREY);
  display->setCursor(18, 406);
  display->printf("Touches: %lu", static_cast<unsigned long>(touch_count));
  display->setCursor(18, 438);
  display->printf("X: %3d  Y: %3d", x, y);
}

void print_device_info() {
  Serial.println();
  Serial.println("TopoRTK Waveshare board demo");
  Serial.printf("Chip: %s rev %u, %u cores at %u MHz\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("Flash: %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM: %u bytes, free %u bytes\n", ESP.getPsramSize(),
                ESP.getFreePsram());
  Serial.printf("Heap: %u bytes free\n", ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  print_device_info();

  Wire.begin(board::kI2cSda, board::kI2cScl, 400000);

  const bool expander_ready = io_expander.begin();
  Serial.printf("TCA9554 I/O expander at 0x20: %s\n",
                expander_ready ? "PASS" : "FAIL");
  if (expander_ready) {
    io_expander.pinMode1(board::kLcdResetExpanderPin, OUTPUT);
    reset_lcd();
  }

  touch_ready = i2c_device_present(board::kTouchAddress);
  Serial.printf("FT6336 touch controller at 0x38: %s\n",
                touch_ready ? "PASS" : "FAIL");

  display_ready = expander_ready && display->begin();
  Serial.printf("ST7796 display initialization: %s\n",
                display_ready ? "PASS" : "FAIL");

  pinMode(board::kBacklight, OUTPUT);
  digitalWrite(board::kBacklight, HIGH);

  if (display_ready) {
    draw_static_screen();
  }

  Serial.println("Touch the display; coordinates will appear here and on screen.");
  Serial.println("BOOT COMPLETE");
}

void loop() {
  int16_t x = 0;
  int16_t y = 0;
  const bool touch_is_down = touch_ready && read_touch(x, y);

  if (touch_is_down) {
    if (!touch_was_down) {
      ++touch_count;
    }
    if (display_ready) {
      draw_touch(x, y);
    }
    Serial.printf("TOUCH count=%lu x=%d y=%d\n",
                  static_cast<unsigned long>(touch_count), x, y);
    delay(35);
  }
  touch_was_down = touch_is_down;

  const uint32_t now = millis();
  if (now - last_heartbeat_ms >= 5000) {
    last_heartbeat_ms = now;
    Serial.printf("HEARTBEAT uptime_s=%lu free_heap=%u\n",
                  static_cast<unsigned long>(now / 1000), ESP.getFreeHeap());
  }

  delay(10);
}

