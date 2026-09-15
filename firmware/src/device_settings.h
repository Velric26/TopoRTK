#pragma once
// Device settings service (R10a slice 4), extracted from main.cpp. Sole owner
// of the two persisted records and of the configuration they apply: the
// `toportk/config` word behind `DeviceConfig` and the `topobase/settings` blob
// behind the saved base coordinate, with the same encodings, the same
// write-then-read-back check (a readback that disagrees means "not applied")
// and the same role/profile/brightness consequences main.cpp used to run in
// load_config, save_config, select_config, select_role, select_brightness,
// select_wifi_mode, load_base_settings and the base application of
// service_survey.
//
// It owns no hardware and no other owner's state. The receiver, the link, the
// backlight step, the correction queue and the CSV session arrive as the hooks
// the composition root installs, and the link/transport preference is never
// duplicated here: the only transport fact this policy needs is which Wi-Fi
// mode the link owner actually selected, and it asks for it.

#include <cstdint>
#include "device_config.h"
#include "survey_math.h"

class Preferences;

// Facts and actions owned elsewhere. The composition root fills every field
// before `begin`.
struct SettingsHooks {
  // Facts the policy gates a request on, read at the moment of the request
  // exactly as the receiver snapshot it used to read.
  bool (*diagnostic_busy)();
  bool (*ota_locked)();
  bool (*profile_running)();   // the receiver profile sequencer is busy
  bool (*profile_failed)();    // the receiver's last profile failed
  bool (*local_router)();      // selected Wi-Fi mode, read after a link restart
  // Receiver owner.
  void (*receiver_config)(const DeviceConfig &config);
  void (*receiver_base_coordinate)(bool fixed, double latitude,
                                   double longitude, double height);
  void (*receiver_apply_profile)();
  void (*receiver_base_failure)(bool blocks_any_role);
  // Link owner: a role or Wi-Fi change restarts the selected transport.
  void (*link_restart)();
  // Backlight: a brightness change restarts the fade from the current duty.
  void (*brightness_step_reset)(uint32_t now_ms);
  // A profile change drops the correction queue and the receiver's proof.
  void (*correction_reset)();
  void (*receiver_profile_reset)();
  // Storage owner's CSV session line.
  void (*log_event)(const char *event, const char *detail);
};

// What the LCD, the web status, the console, the survey glue and the CSV
// writers read. A value copy: no pointer into settings state and no mutation
// path back into it.
struct SettingsSnapshot {
  DeviceConfig config;
  bool saved = false;      // a stored record was read and applied
  bool error = false;      // the store failed, or the stored record was rejected
  // The saved base record the survey glue reports and the receiver publishes.
  bool base_failed = false;
  uint32_t base_revision = 0, base_attempt_revision = 0;
  bool base_fixed = false;
  double base_latitude = 0, base_longitude = 0, base_height = 0;
};

namespace device_settings {

// Keeps the two stores the root owns: `toportk` for the device config and
// `topobase` for the base record. They must stay separate objects, because
// `Preferences::begin` rebinds the namespace of the object it is called on.
void begin(Preferences &config_store, Preferences &base_store,
           const SettingsHooks &hooks);

// Boot: this unit's default role, then the stored record when one decodes. A
// rejected word leaves the defaults in place with `error` set, exactly as the
// load in main.cpp did.
void load();
// The saved base record: a valid record is published to the receiver, a
// missing or rejected one reports the boot failure that blocks Base alone.
void load_base();

// Typed requests. `now_ms` is the caller's evaluation time; it carries the
// value main.cpp read from `millis()`.
bool apply(const DeviceConfig &requested, uint32_t now_ms);
void set_role(DeviceRole role, uint32_t now_ms);
void set_brightness(BrightnessMode mode, uint32_t now_ms);
void set_wifi_mode(WiFiMode mode, uint32_t now_ms);
// The survey owner's request: records the coordinate, reads it back, publishes
// it and re-profiles the receiver. False whenever the record was not stored and
// verified; the receiver owner is told either way.
bool apply_base(const survey::Position &position, bool fixed, uint32_t revision);

SettingsSnapshot snapshot();
// The applied record alone, for the callers that ask for a role or a brightness
// and never read the base record.
DeviceConfig config();

}  // namespace device_settings
