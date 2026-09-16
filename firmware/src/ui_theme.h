#pragma once

// Light high-contrast outdoor palette (R2b). Ratios are in-memory normalized
// RGB565 relative-luminance calculations against the actual components, not
// measured panel luminance. Green is reserved for the required
// ready/connected/fixed state; selection uses the navy fill, never green.
// Include after the platform header that defines the RGB565_* constants.
namespace colors {
constexpr uint16_t kBackground = 0xFFFF;      // white page
constexpr uint16_t kPanel = 0xFFFF;           // white cards
constexpr uint16_t kPrimary = 0x0000;         // primary text on white (21.0:1)
constexpr uint16_t kSecondary = 0x4208;       // inactive explanations (10.2:1)
constexpr uint16_t kHeaderReady = 0x07E0;     // READY banner fill, black text (15.3:1)
constexpr uint16_t kHeaderNotReady = 0xAD55;  // NOT READY banner fill, black text (9.1:1)
constexpr uint16_t kSelected = 0x0010;        // selected control fill, white text (15.8:1)
constexpr uint16_t kReadyText = 0x0300;       // ready/good text on white (7.7:1)
constexpr uint16_t kError = 0xA000;           // error text on white (8.1:1)
constexpr uint16_t kWarningBg = 0xFFE0;       // warning panel fill, black text (19.6:1)
constexpr uint16_t kFailFill = 0xF800;        // RGB565 red: failed link, stale solution
constexpr uint16_t kAccent = 0x0010;          // selection marker
constexpr uint16_t kOutline = 0x0000;         // two-pixel control outlines
}  // namespace colors
