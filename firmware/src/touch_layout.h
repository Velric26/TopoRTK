#pragma once

#include <cstdint>

enum class ScreenPage : uint8_t { kMain, kGpsDetails, kWifiDetails, kSettings, kDebug };
enum class TouchAction : uint8_t {
  kNone, kHome, kGps, kLink, kSettings, kBase, kRover, kApply, kAuto, kDay, kNight,
  kShowKey, kNewKey, kDebug, kDebugToggle, kDetailPrev, kDetailNext,
  // Link-mode page (R6b): pair-wide medium selection, never a session code.
  kLinkRadio, kLinkWifi, kLinkRecover,
  // Designed instrument restart on the Debug page (two-tap confirm).
  kRestart
};

struct TouchRect {
  int16_t x, y, width, height;
  bool contains(int16_t px, int16_t py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }
};

namespace layout {
constexpr int16_t kNavY = 432;
constexpr TouchRect kBase{8, 90, 148, 72};
constexpr TouchRect kRover{164, 90, 148, 72};
constexpr TouchRect kApply{8, 232, 304, 48};
constexpr TouchRect kAuto{8, 320, 96, 48};
constexpr TouchRect kDay{112, 320, 96, 48};
constexpr TouchRect kNight{216, 320, 96, 48};
constexpr TouchRect kShowKey{8, 212, 148, 48};
constexpr TouchRect kNewKey{164, 212, 148, 48};
// Link mode (fourth Link page): pick the medium, then confirm; the recovery
// button is only offered when pair confirmation cannot be obtained.
constexpr TouchRect kLinkRadio{8, 212, 148, 48};
constexpr TouchRect kLinkWifi{164, 212, 148, 48};
// Tall enough for its "FOR RECOVERY" subtitle, which draw_button places at
// rect.y+46: at 48 high it would overlap the hint line below.
constexpr TouchRect kLinkRecover{8, 268, 304, 72};
constexpr TouchRect kDebug{212,380,100,44};
constexpr TouchRect kDebugToggle{8,244,304,56};
// Debug page, below the compressed info line: the designed software restart. It
// carries a subtitle, and draw_button places that at rect.y+46, so the rect is
// tall enough to contain it.
constexpr TouchRect kRestart{8,336,304,72};
// GPS: one full-width button flips between its two detail pages.
constexpr TouchRect kDetailToggle{8, 380, 304, 44};
// Link: three pages (rows, counters, phone) navigated with PREV/NEXT.
constexpr TouchRect kDetailPrev{8, 380, 148, 44};
constexpr TouchRect kDetailNext{164, 380, 148, 44};
}

// detail_page selects the shown page of the paged GPS/Link details.
inline TouchAction touch_action(ScreenPage page, uint8_t detail_page, int16_t x, int16_t y) {
  if (x < 0 || x >= 320 || y < 0 || y >= 480) return TouchAction::kNone;
  if (y >= layout::kNavY) return static_cast<TouchAction>(1 + x / 80);
  if (page == ScreenPage::kGpsDetails) {
    if (layout::kDetailToggle.contains(x,y)) return TouchAction::kDetailNext;
  }
  if (page == ScreenPage::kWifiDetails) {
    if (layout::kDetailPrev.contains(x,y)) return TouchAction::kDetailPrev;
    if (layout::kDetailNext.contains(x,y)) return TouchAction::kDetailNext;
    if (detail_page == 2) {
      if (layout::kShowKey.contains(x,y)) return TouchAction::kShowKey;
      if (layout::kNewKey.contains(x,y)) return TouchAction::kNewKey;
    }
    if (detail_page == 3) {
      if (layout::kLinkRadio.contains(x,y)) return TouchAction::kLinkRadio;
      if (layout::kLinkWifi.contains(x,y)) return TouchAction::kLinkWifi;
      if (layout::kLinkRecover.contains(x,y)) return TouchAction::kLinkRecover;
    }
  }
  if(page==ScreenPage::kDebug&&layout::kDebugToggle.contains(x,y))return TouchAction::kDebugToggle;
  if(page==ScreenPage::kDebug&&layout::kRestart.contains(x,y))return TouchAction::kRestart;
  if(page==ScreenPage::kSettings&&layout::kDebug.contains(x,y))return TouchAction::kDebug;
  if (page != ScreenPage::kSettings) return TouchAction::kNone;
  if (layout::kBase.contains(x, y)) return TouchAction::kBase;
  if (layout::kRover.contains(x, y)) return TouchAction::kRover;
  if (layout::kApply.contains(x, y)) return TouchAction::kApply;
  if (layout::kAuto.contains(x, y)) return TouchAction::kAuto;
  if (layout::kDay.contains(x, y)) return TouchAction::kDay;
  if (layout::kNight.contains(x, y)) return TouchAction::kNight;
  return TouchAction::kNone;
}
