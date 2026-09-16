// Board hardware owner (R10a slice 5), extracted from main.cpp: the objects and
// the register access behind the LCD, the IO expander and the touchscreen, with
// the pin map in board_hardware.h. Nothing here decides anything - the panel and
// touch readiness it publishes are its only output, and the root and the UI
// apply whatever policy reads them.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include <Wire.h>

#include "board_hardware.h"

TCA9554 io_expander(board::kIoExpanderAddress);
Arduino_DataBus *display_bus = new Arduino_ESP32SPI(
    board::kLcdDataCommand, board::kLcdChipSelect, board::kSpiClock,
    board::kSpiMosi, board::kSpiMiso);
Arduino_GFX *display = new Arduino_ST7796(
    display_bus, board::kLcdReset, board::kDisplayRotation, true,
    board::kWidth, board::kHeight);
bool display_ready = false;
bool touch_ready = false;

namespace board_hardware {
namespace {

// The expander's LCD reset pulse: high, 10 ms, low, 10 ms, high, 200 ms.
void reset_lcd() {
  io_expander.write1(board::kLcdResetExpanderPin, HIGH);
  delay(10);
  io_expander.write1(board::kLcdResetExpanderPin, LOW);
  delay(10);
  io_expander.write1(board::kLcdResetExpanderPin, HIGH);
  delay(200);
}

// A zero-length transaction: 0 answers a device that acknowledged.
bool i2c_device_present(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

}  // namespace

void begin() {
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
}

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

}  // namespace board_hardware
