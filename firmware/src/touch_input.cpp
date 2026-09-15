// Tap/swipe state machine moved verbatim from main.cpp (R2a). Gesture
// classification for taps resolves the TouchAction against the current page
// and detail page; swipe routing stays with the composition root handler.
#include "touch_input.h"
#include "ui_screens.h"
#include <cstdio>
#include <cstdlib>

namespace {
bool touch_active = false;
bool touch_cancelled = false;
bool touch_moved = false;
int16_t touch_start_x = 0;
int16_t touch_start_y = 0;
int16_t touch_last_x = 0;
int16_t touch_last_y = 0;
uint32_t touch_start_ms = 0;
uint32_t touch_last_seen_ms = 0;
void (*gesture_handler)(const UiGesture &gesture) = nullptr;
}  // namespace

void touch_input_begin() {
  touch_active = false;
  touch_cancelled = false;
  touch_moved = false;
  gesture_handler = nullptr;
}

void touch_input_on_gesture(void (*handler)(const UiGesture &gesture)) {
  gesture_handler = handler;
}

void touch_input_sample(TouchRead state, int16_t x, int16_t y, uint32_t now) {
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
    const int16_t dx = touch_last_x - touch_start_x;
    const int16_t dy = touch_last_y - touch_start_y;
    const int16_t abs_dx = std::abs(dx);
    const int16_t abs_dy = std::abs(dy);
    UiGesture gesture;
    gesture.x = touch_last_x;
    gesture.y = touch_last_y;
    gesture.dx = dx;
    gesture.dy = dy;
    if (!touch_cancelled && !touch_moved && now - touch_start_ms < 1200) {
      gesture.kind = UiGesture::Kind::kTap;
      gesture.action = touch_action(current_page, ui_detail_page(), touch_start_x, touch_start_y);
      // A tap that slides between targets before release is not a command.
      if (gesture.action == touch_action(current_page, ui_detail_page(), touch_last_x, touch_last_y) &&
          gesture_handler) {
        gesture_handler(gesture);
      }
    } else if (!touch_cancelled && !(abs_dy < 70 || abs_dy <= abs_dx)) {
      gesture.kind = UiGesture::Kind::kSwipe;
      gesture.x = touch_start_x;
      gesture.y = touch_start_y;
      if (gesture_handler) gesture_handler(gesture);
    }
    touch_cancelled = false;
  } else if (!touch_active) {
    touch_cancelled = false;
  }
}
