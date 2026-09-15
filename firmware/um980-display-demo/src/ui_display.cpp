// Display primitives, repaint cache and backlight moved verbatim from
// main.cpp (R2a) and restyled by the R2b light high-contrast contract:
// size-2 legibility floor, white cards with two-pixel outlines, paged detail
// rows, and word-wrapped multi-line text. millis() stays an explicit
// parameter; board constants arrive once through UiBoardConfig.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "ui_display.h"
#include "ui_theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
struct UiRegionCache {
  bool valid = false;
  ScreenPage page = ScreenPage::kMain;
  uint8_t type = 0;
  int16_t y = 0;
  char signature[128] = {};
};
constexpr size_t kUiRegionCacheCount = 24;
UiRegionCache ui_region_cache[kUiRegionCacheCount];

int16_t panel_width = 320;
uint8_t backlight_pin = 6;
uint8_t day_duty = 255;
uint8_t night_duty = 0;
uint8_t backlight_pwm_channel = 0;
uint32_t backlight_pwm_hz = 5000;
uint16_t backlight_fade_ms = 800;
uint8_t target_backlight_duty = 255;
uint32_t last_brightness_step_ms = 0;

bool fresh_clock(uint32_t now, const GnssTimeData &time) {
  return time.valid && time.received_ms > 0 &&
         now - time.received_ms < 3000;
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
}  // namespace

uint8_t backlight_duty = 255;
bool backlight_pwm_ready = false;

void ui_display_begin(const UiBoardConfig &config) {
  panel_width = config.width;
  backlight_pin = config.backlight_pin;
  day_duty = config.day_duty;
  night_duty = config.night_duty;
  backlight_pwm_channel = config.pwm_channel;
  backlight_pwm_hz = config.pwm_hz;
  backlight_fade_ms = config.fade_ms;
}

int16_t ui_width() { return panel_width; }

uint8_t ui_backlight_target() { return target_backlight_duty; }

void ui_reset_brightness_step(uint32_t now) { last_brightness_step_ms = now; }

void ui_reset_region_cache() {
  for (UiRegionCache &region : ui_region_cache) region.valid = false;
}

bool ui_region_changed(ScreenPage page, uint8_t type, int16_t y,
                       const char *signature) {
  UiRegionCache *empty = nullptr;
  for (UiRegionCache &region : ui_region_cache) {
    if (!region.valid) {
      if (empty == nullptr) empty = &region;
      continue;
    }
    if (region.page != page || region.type != type || region.y != y) {
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
    empty->page = page;
    empty->type = type;
    empty->y = y;
    std::strncpy(empty->signature, signature, sizeof(empty->signature) - 1);
    empty->signature[sizeof(empty->signature) - 1] = '\0';
  }
  return true;
}

void draw_fitted_text(int16_t x, int16_t y, int16_t width, const char *text,
                      uint8_t size, uint16_t color) {
  // Operational text never shrinks below size 2 (R2b legibility floor).
  while (size > 2 && std::strlen(text) * 6 * size > static_cast<size_t>(width)) --size;
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

// Debug page 30-pixel row: size-2 label and value (the only single-line row
// style left; GPS/Link pages use the paged 52-pixel rows).
void draw_label_value(ScreenPage page, int16_t y, const char *label, const char *value) {
  (void)page;
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%s", label, value);
  if (!ui_region_changed(ScreenPage::kDebug, 2, y, signature)) return;
  display->fillRect(0, y, panel_width, 30, colors::kBackground);
  display->drawFastHLine(10, y + 29, 300, 0xC618);
  display->setTextSize(2);
  display->setTextColor(colors::kSecondary);
  display->setCursor(10, y + 7);
  display->print(label);
  draw_fitted_text(92, y + 7, 218, value, 2, colors::kPrimary);
}

void draw_detail_row(ScreenPage page, uint8_t row, int16_t y,
                     const char *label, const char *value) {
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%u|%s|%s", row, label, value);
  if (!ui_region_changed(page, 2, y, signature)) return;
  display->fillRect(0, y, panel_width, 52, colors::kBackground);
  display->drawFastHLine(10, y + 51, 300, 0xC618);
  draw_fitted_text(10, y + 3, 300, label, 2, colors::kSecondary);
  draw_fitted_text(10, y + 27, 300, value, 2, colors::kPrimary);
}

uint8_t draw_wrapped(int16_t x, int16_t y, int16_t width, const char *text,
                     uint8_t lines, uint16_t color) {
  const uint8_t size = 2;
  const size_t columns = static_cast<size_t>(width / (6 * size));
  uint8_t used = 0;
  const char *cursor = text;
  while (*cursor != '\0' && used < lines) {
    size_t span = std::strlen(cursor);
    if (span > columns) {
      span = columns;
      for (size_t scan = span; scan > 0; --scan) {
        if (cursor[scan] == ' ') { span = scan; break; }
      }
      while (span > 0 && cursor[span - 1] == ' ') --span;
      if (span == 0) span = columns;  // A single long word fills the line.
    }
    char line[64] = {};
    std::strncpy(line, cursor, std::min(span, sizeof(line) - 1));
    draw_fitted_text(x, y + used * 20, width, line, size, color);
    ++used;
    cursor += span;
    while (*cursor == ' ') ++cursor;
  }
  return used;
}

void draw_status_card(ScreenPage page, int16_t y, int16_t height, const char *title,
                      const char *value, uint16_t color, uint8_t value_size) {
  (void)page;
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%d|%s|%s|%u|%u", height,
                title, value, color, value_size);
  if (!ui_region_changed(ScreenPage::kMain, 1, y, signature)) return;
  display->fillRoundRect(8, y, panel_width - 16, height, 6, colors::kPanel);
  display->fillRoundRect(8, y + 10, 4, height - 20, 2, color);
  display->setTextSize(2);
  display->setTextColor(colors::kSecondary);
  display->setCursor(18, y + 8);
  display->print(title);
  draw_fitted_text(20, y + 30, 282, value, value_size, color);
  display->drawRoundRect(8, y, panel_width - 16, height, 6, colors::kOutline);
  display->drawRoundRect(9, y + 1, panel_width - 18, height - 2, 6, colors::kOutline);
}

void draw_button(ScreenPage page, uint8_t id, const TouchRect &rect, const char *label,
                 const char *subtitle, bool selected, bool enabled) {
  char signature[128] = {};
  std::snprintf(signature, sizeof(signature), "%s|%s|%u|%u", label, subtitle, selected, enabled);
  if (!ui_region_changed(page, 10 + id, rect.y, signature)) return;
  const uint16_t background = selected && enabled ? colors::kSelected : colors::kPanel;
  const uint16_t foreground = selected && enabled ? RGB565_WHITE : colors::kPrimary;
  display->fillRoundRect(rect.x, rect.y, rect.width, rect.height, 8, background);
  display->drawRoundRect(rect.x, rect.y, rect.width, rect.height, 8, colors::kOutline);
  display->drawRoundRect(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, 7, colors::kOutline);
  const int16_t text_width = std::strlen(label) * 12;
  draw_fitted_text(rect.x + (rect.width - text_width) / 2,
                   rect.y + (subtitle[0] ? 12 : (rect.height - 16) / 2),
                   rect.width - 12, label, 2, foreground);
  if (subtitle[0]) {
    const int16_t sub_width = std::strlen(subtitle) * 12;
    draw_fitted_text(rect.x + (rect.width - sub_width) / 2,
                     rect.y + 46, rect.width - 12, subtitle, 2,
                     selected ? RGB565_WHITE : colors::kSecondary);
  }
}

uint8_t automatic_brightness_target(uint32_t now, const GnssTimeData &time) {
  const uint8_t kNightDuty = night_duty;
  const uint8_t kDayDuty = day_duty;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (!local_time_utc_minus_6(now, time, year, month, day, hour, minute, second)) {
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

void service_brightness(uint32_t now, BrightnessMode mode,
                        const GnssTimeData &time) {
  if (mode == BrightnessMode::kDay) {
    target_backlight_duty = day_duty;
  } else if (mode == BrightnessMode::kNight) {
    target_backlight_duty = night_duty;
  } else {
    target_backlight_duty = automatic_brightness_target(now, time);
  }

  if (backlight_duty == target_backlight_duty) {
    last_brightness_step_ms = now;
    return;
  }
  const uint32_t elapsed = now - last_brightness_step_ms;
  if (elapsed < 20) return;
  last_brightness_step_ms = now;
  // Catch up by elapsed time even when SD writes or a page redraw delay the loop.
  const int step = std::max(1U, std::min(elapsed, static_cast<uint32_t>(backlight_fade_ms)) * 255U / backlight_fade_ms);
  if (backlight_duty < target_backlight_duty) {
    backlight_duty = std::min(static_cast<int>(target_backlight_duty), backlight_duty + step);
  } else {
    backlight_duty = std::max(static_cast<int>(target_backlight_duty), backlight_duty - step);
  }
  if (backlight_pwm_ready) ledcWrite(backlight_pwm_channel, backlight_duty);
}

void setup_backlight(BrightnessMode mode, uint32_t now) {
  backlight_duty = mode == BrightnessMode::kNight ? night_duty : day_duty;
  target_backlight_duty = backlight_duty;
  last_brightness_step_ms = now;
  backlight_pwm_ready = ledcSetup(backlight_pwm_channel, backlight_pwm_hz, 8) != 0;
  if (backlight_pwm_ready) {
    ledcAttachPin(backlight_pin, backlight_pwm_channel);
    ledcWrite(backlight_pwm_channel, backlight_duty);
  } else {
    pinMode(backlight_pin, OUTPUT);
    digitalWrite(backlight_pin, HIGH);
  }
  Serial.printf("BACKLIGHT: PWM=%s GPIO=%d frequency=%lu Hz duty=%lu\n",
                backlight_pwm_ready ? "PASS" : "FAIL", backlight_pin,
                static_cast<unsigned long>(ledcReadFreq(backlight_pwm_channel)),
                static_cast<unsigned long>(ledcRead(backlight_pwm_channel)));
}

bool local_time_utc_minus_6(uint32_t now, const GnssTimeData &time,
                            uint16_t &year, uint8_t &month, uint8_t &day,
                            uint8_t &hour, uint8_t &minute, uint8_t &second) {
  if (!fresh_clock(now, time)) return false;
  year = time.year;
  month = time.month;
  day = time.day;
  minute = time.minute;
  second = time.second;
  int local_hour = static_cast<int>(time.hour) - 6;
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
