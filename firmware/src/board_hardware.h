#pragma once
// Board hardware (R10a slice 5), extracted from main.cpp. Sole owner of the
// physical devices the display and the touchscreen sit behind: the board pin
// map, the LCD bus and panel construction, the TCA9554 IO expander with the LCD
// reset pulse it drives, the I2C bus and its presence probe, the FT6336
// register read with its error handling, and the panel/touchscreen readiness
// the UI and the root gate on.
//
// It exposes initialization and health, never domain policy: no receiver, link,
// settings, survey or UI decision is made or stored here. Every pin, bus,
// address, baud, buffer size and pulse timing is the value main.cpp used, and
// `begin` runs the boot steps in the order setup() ran them.
//
// The `board` pin namespace keeps the name it had in main.cpp: the host suite
// and the UI board config already read `board::kUnitLabel` and
// `board::kNightBacklightDuty`.

#include <cstdint>
#include "touch_input.h"

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

class Arduino_GFX;

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

// The panel handle ui_display.h declares: the board layer constructs it and
// every drawing module draws through it.
extern Arduino_GFX *display;
// Panel and touchscreen health. `begin` sets both; the root gates its drawing
// on `display_ready`, the touch poll on `touch_ready`, and the host suite
// drives the two directly.
extern bool display_ready;
extern bool touch_ready;

namespace board_hardware {

// The hardware boot sequence setup() ran, unchanged in order and in the three
// report lines it prints: I2C at 400 kHz, the expander and its LCD reset pulse,
// the touch presence probe, then the panel begin gated on the expander.
void begin();

// One FT6336 sample: the seven registers from 0x00 at its I2C address, with the
// same no-contact/multi-touch/out-of-range handling and the same rotation
// correction the root applied.
TouchRead read_touch(int16_t &x, int16_t &y);

}  // namespace board_hardware
