#pragma once

#include <cstdint>

enum class DeviceRole : uint8_t { kRover, kBase };
enum class BrightnessMode : uint8_t { kAutomatic, kDay, kNight };
// Direct preserves the validated instrument-to-instrument AP bridge. Local
// router makes both instruments Wi-Fi clients for development and browser use.
enum class WiFiMode : uint8_t { kDirect, kLocalRouter };

struct DeviceConfig {
  DeviceRole role = DeviceRole::kRover;
  BrightnessMode brightness = BrightnessMode::kAutomatic;
  bool base_rtcm = true;
  WiFiMode wifi_mode = WiFiMode::kDirect;
};

// One versioned NVS value keeps related settings together during power loss.
// V1 records (without Wi-Fi mode) are accepted as Direct so validated devices
// retain their prior behavior after an update.
inline uint32_t encode_config(const DeviceConfig &config) {
  return 0x54520200U | static_cast<uint8_t>(config.role) |
         (static_cast<uint8_t>(config.brightness) << 1) |
         (config.base_rtcm ? 8U : 0U) |
         (static_cast<uint8_t>(config.wifi_mode) << 4);
}

inline bool decode_config(uint32_t word, DeviceConfig &config) {
  const uint32_t tag = word & 0xFFFFFFE0U;
  if ((tag != 0x54520100U && tag != 0x54520200U) ||
      ((word >> 1) & 3U) > 2U) {
    return false;
  }
  if (tag == 0x54520100U && (word & 0x10U) != 0) return false;
  if (tag == 0x54520200U && ((word >> 4) & 1U) > 1U) return false;
  config.role = static_cast<DeviceRole>(word & 1U);
  config.brightness = static_cast<BrightnessMode>((word >> 1) & 3U);
  config.base_rtcm = (word & 8U) != 0;
  config.wifi_mode = tag == 0x54520200U
                         ? static_cast<WiFiMode>((word >> 4) & 1U)
                         : WiFiMode::kDirect;
  return true;
}

inline const char *profile_command(const DeviceConfig &config, uint8_t step) {
  static const char *const common[] = {
      "UNLOG COM2", nullptr, "GPGGA COM2 1", "GPRMC COM2 1", "BESTNAVA COM2 1"};
  static const char *const rtcm[] = {
      "RTCM1006 COM2 10", "RTCM1033 COM2 10", "RTCM1074 COM2 1",
      "RTCM1124 COM2 1", "RTCM1084 COM2 1", "RTCM1094 COM2 1"};
  if (step == 1) return config.role == DeviceRole::kBase ? "MODE BASE" : "MODE ROVER SURVEY";
  if (step < 5) return common[step];
  if (config.role == DeviceRole::kBase && config.base_rtcm) {
    if (step < 11) return rtcm[step - 5];
    return step == 11 ? "MODE" : nullptr;
  }
  return step == 5 ? "MODE" : nullptr;
}
