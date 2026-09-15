// Device settings service (R10a slice 4): the two NVS records and the applied
// configuration moved out of main.cpp. Bodies are unchanged except that the
// removed globals became file-local state, the stores arrive through `begin`,
// `millis()` became the caller's `now_ms`, and the receiver/link/UI/correction
// actions a change requires arrive as the installed hooks.
#include "device_settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

#include "survey_engine.h"

#ifndef TOPORTK_UNIT_ID
#define TOPORTK_UNIT_ID 1
#endif

namespace device_settings {
namespace {

Preferences *config_store = nullptr;
Preferences *base_store = nullptr;
SettingsHooks hooks{};

DeviceConfig device_config;
bool preferences_ready = false;
bool config_saved = false;
bool config_error = false;

// The base record is one versioned blob: magic, the fixed flag, the survey
// revision, the coordinate and a CRC32 over everything before it.
constexpr uint32_t kBaseMagic = 0x54425331U;
struct BaseSettings {
  uint32_t magic = kBaseMagic, fixed = 0, revision = 0;
  double latitude = 0, longitude = 0, height = 0;
  uint32_t checksum = 0;
};
BaseSettings base_settings;
bool base_settings_failed = false;
uint32_t base_attempt_revision = 0;

bool is_base() { return device_config.role == DeviceRole::kBase; }

// The receiver profile takes the applied coordinate; this owner reports a
// validation failure instead: a boot load blocks Base alone (a Rover keeps
// running), a survey application failure stops either role.
void publish_base_coordinate() {
  hooks.receiver_base_coordinate(base_settings.fixed != 0, base_settings.latitude,
                                 base_settings.longitude, base_settings.height);
}

// Writes the record and reads it back: a store that cannot confirm the value
// leaves the selection unapplied.
bool save_config(const DeviceConfig &requested) {
  const uint32_t word = encode_config(requested);
  if (config_saved && !config_error && word == encode_config(device_config)) return true;
  if (!preferences_ready) preferences_ready = config_store->begin("toportk", false);
  if (!preferences_ready || config_store->putUInt("config", word) != sizeof(word) ||
      config_store->getUInt("config", 0) != word) {
    config_error = true;
    Serial.println("CONFIG SAVE: FAILED; selection was not applied");
    hooks.log_event("CONFIG_ERROR", "NVS_WRITE_FAILED");
    return false;
  }
  config_saved = true;
  config_error = false;
  Serial.println("CONFIG SAVE: PASS");
  return true;
}

}  // namespace

void begin(Preferences &config_store_in, Preferences &base_store_in,
           const SettingsHooks &installed) {
  config_store = &config_store_in;
  base_store = &base_store_in;
  hooks = installed;
}

void load() {
  device_config.role = TOPORTK_UNIT_ID == 1 ? DeviceRole::kBase : DeviceRole::kRover;
  preferences_ready = config_store->begin("toportk", false);
  config_error = !preferences_ready;
  if (preferences_ready && config_store->isKey("config")) {
    config_saved = decode_config(config_store->getUInt("config", 0), device_config);
    config_error = !config_saved;
  }
  hooks.receiver_config(device_config);
}

void load_base() {
  if (!base_store->begin("topobase", false)) {
    base_settings_failed = true;
    hooks.receiver_base_failure(false);
    return;
  }
  if (base_store->isKey("settings")) {
    BaseSettings saved;
    const bool ok = base_store->getBytes("settings", &saved, sizeof(saved)) == sizeof(saved) &&
                    saved.magic == kBaseMagic && saved.fixed <= 1 &&
                    saved.checksum == survey::crc32(std::string(reinterpret_cast<const char *>(&saved),
                                                                offsetof(BaseSettings, checksum))) &&
                    std::isfinite(saved.latitude) && std::isfinite(saved.longitude) &&
                    std::isfinite(saved.height) &&
                    saved.latitude >= -80 && saved.latitude <= 84 &&
                    std::abs(saved.longitude) <= 180 &&
                    saved.height >= -1000 && saved.height <= 10000;
    if (ok) base_settings = saved; else base_settings_failed = true;
  }
  base_store->end();
  if (base_settings_failed) hooks.receiver_base_failure(false);
  else publish_base_coordinate();
}

bool apply(const DeviceConfig &requested, uint32_t now_ms) {
  const bool receiver_profile_running = hooks.profile_running();
  const bool receiver_profile_failed = hooks.profile_failed();
  if (hooks.diagnostic_busy() || hooks.ota_locked()) return false;
  if (receiver_profile_running) return false;
  if (!save_config(requested)) return false;
  const bool role_changed = requested.role != device_config.role;
  const bool profile_changed = role_changed || requested.base_rtcm != device_config.base_rtcm;
  const bool wifi_changed = requested.wifi_mode != device_config.wifi_mode;
  const BrightnessMode previous_brightness = device_config.brightness;
  device_config = requested;
  hooks.receiver_config(device_config);
  if (previous_brightness != requested.brightness) hooks.brightness_step_reset(now_ms);
  if (role_changed || wifi_changed) hooks.link_restart();
  if (wifi_changed) {
    hooks.log_event("WIFI_MODE", hooks.local_router() ? "LOCAL_ROUTER" : "DIRECT_LINK");
  }
  if (profile_changed || receiver_profile_failed) {
    hooks.correction_reset();
    hooks.receiver_profile_reset();
    hooks.log_event("CONFIG_SELECTED", is_base() ? "BASE_TEST" : "ROVER_SURVEY");
  }
  return true;
}

void set_role(DeviceRole role, uint32_t now_ms) {
  DeviceConfig requested = device_config;
  requested.role = role;
  requested.base_rtcm = true;
  apply(requested, now_ms);
}

void set_brightness(BrightnessMode mode, uint32_t now_ms) {
  DeviceConfig requested = device_config;
  requested.brightness = mode;
  apply(requested, now_ms);
}

void set_wifi_mode(WiFiMode mode, uint32_t now_ms) {
  DeviceConfig requested = device_config;
  requested.wifi_mode = mode;
  apply(requested, now_ms);
}

bool apply_base(const survey::Position &position, bool fixed, uint32_t revision) {
  base_attempt_revision = revision;
  base_settings_failed = true;
  bool applied = false;
  if (is_base() && !hooks.profile_running() &&
      revision == base_settings.revision + 1) {
    BaseSettings next{};
    next.fixed = fixed ? 1 : 0;
    next.revision = revision;
    next.latitude = position.latitude;
    next.longitude = position.longitude;
    next.height = position.height;
    next.checksum = survey::crc32(std::string(reinterpret_cast<const char *>(&next),
                                              offsetof(BaseSettings, checksum)));
    BaseSettings check{};
    if (base_store->begin("topobase", false)) {
      const bool saved = base_store->putBytes("settings", &next, sizeof(next)) == sizeof(next) &&
                         base_store->getBytes("settings", &check, sizeof(check)) == sizeof(check) &&
                         std::memcmp(&next, &check, sizeof(next)) == 0;
      base_store->end();
      if (saved) {
        base_settings = next;
        base_settings_failed = false;
        applied = true;
        publish_base_coordinate();
        hooks.receiver_apply_profile();
      }
    }
  }
  if (!applied) hooks.receiver_base_failure(true);
  return applied;
}

SettingsSnapshot snapshot() {
  SettingsSnapshot out;
  out.config = device_config;
  out.saved = config_saved;
  out.error = config_error;
  out.base_failed = base_settings_failed;
  out.base_revision = base_settings.revision;
  out.base_attempt_revision = base_attempt_revision;
  out.base_fixed = base_settings.fixed != 0;
  out.base_latitude = base_settings.latitude;
  out.base_longitude = base_settings.longitude;
  out.base_height = base_settings.height;
  return out;
}

DeviceConfig config() { return device_config; }

}  // namespace device_settings
