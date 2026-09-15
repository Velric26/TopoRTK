#pragma once
// Touch state machine extracted from main.cpp (R2a). The composition root
// polls the FT6336 (hardware access stays with the board layer until R10a)
// and feeds samples here; completed taps and swipes are emitted as typed
// gestures to the handler registered with touch_input_on_gesture.
#include <cstdint>
#include "touch_layout.h"

enum class TouchRead : uint8_t { kError, kReleased, kContact, kMultiple };

struct UiGesture {
  enum class Kind : uint8_t { kTap, kSwipe };
  Kind kind = Kind::kTap;
  TouchAction action = TouchAction::kNone;  // Tap target; kNone for swipes.
  int16_t x = 0, y = 0;                     // Tap point / swipe start.
  int16_t dx = 0, dy = 0;                   // Swipe delta; zero for taps.
};

void touch_input_begin();
void touch_input_on_gesture(void (*handler)(const UiGesture &gesture));
// Feed one polled sample; expect the caller's existing poll cadence.
void touch_input_sample(TouchRead state, int16_t x, int16_t y, uint32_t now);
