#pragma once
// Display primitives, region repaint cache and backlight control extracted
// from main.cpp (R2a). Rendering is byte-identical to the pre-extraction
// drawing code; status policy stays with the composition root and arrives
// through UiFrame in ui_screens.h.
#include <cstdint>
#include "gnss_parser.h"
#include "device_config.h"
#include "touch_layout.h"

class Arduino_GFX;
// Construction stays with the board wiring (R10a moves it to board_hardware).
extern Arduino_GFX *display;

struct UiBoardConfig {
  int16_t width;
  uint8_t backlight_pin;
  uint8_t day_duty;
  uint8_t night_duty;
  uint8_t pwm_channel;
  uint32_t pwm_hz;
  uint16_t fade_ms;
};

void ui_display_begin(const UiBoardConfig &config);
int16_t ui_width();
uint8_t ui_backlight_target();
void ui_reset_brightness_step(uint32_t now);

// Region repaint cache. page/type/y identify a region; the signature is any
// string that changes when the region must repaint.
void ui_reset_region_cache();
bool ui_region_changed(ScreenPage page, uint8_t type, int16_t y,
                       const char *signature);

void draw_fitted_text(int16_t x, int16_t y, int16_t width, const char *text,
                      uint8_t size, uint16_t color);
void draw_label_value(ScreenPage page, int16_t y, const char *label,
                      const char *value);
// Paged 52-pixel detail row: size-2 label and value on separate lines.
void draw_detail_row(ScreenPage page, uint8_t row, int16_t y,
                     const char *label, const char *value);
// Greedy word wrap into at most `lines` size-2 lines; returns lines used.
uint8_t draw_wrapped(int16_t x, int16_t y, int16_t width, const char *text,
                     uint8_t lines, uint16_t color);
void draw_status_card(ScreenPage page, int16_t y, int16_t height,
                      const char *title, const char *value, uint16_t color,
                      uint8_t value_size = 3);
void draw_button(ScreenPage page, uint8_t id, const TouchRect &rect,
                 const char *label, const char *subtitle, bool selected,
                 bool enabled = true);

// Backlight. mode is the persisted device setting; time carries the GNSS
// clock used by the automatic curve.
extern uint8_t backlight_duty;
extern bool backlight_pwm_ready;
void setup_backlight(BrightnessMode mode, uint32_t now);
void service_brightness(uint32_t now, BrightnessMode mode,
                        const GnssTimeData &time);
uint8_t automatic_brightness_target(uint32_t now, const GnssTimeData &time);

// Pure UTC-6 conversion of a fresh GNSS clock; false when the clock is stale.
bool local_time_utc_minus_6(uint32_t now, const GnssTimeData &time,
                            uint16_t &year, uint8_t &month, uint8_t &day,
                            uint8_t &hour, uint8_t &minute, uint8_t &second);
