#pragma once
// UM980 receiver service (R10a slice 2), extracted from main.cpp. Sole UART1
// owner: port startup, ASCII line assembly, the RTCM byte/frame state machine,
// the receiver line handler, the allowlisted receiver commands and the
// startup/profile sequencers. It owns the receiver state (solution, role text,
// profile flags, byte/line/RTCM counters, the reference it captures) and
// publishes one value snapshot per evaluation; nothing outside writes receiver
// state.
//
// The facts it cannot own arrive as registered observations (`Observers`),
// called synchronously at the point in the receiver's own input handling where
// the fact became true, so CSV lines, correction resets and link admission keep
// their original ordering. The composition root supplies the role and the base
// coordinate it reads from NVS, keeps the loop order, and asks for work through
// typed requests. The allowlisted command table (`profile_command`) and the
// acknowledgement-before-profile-verified semantics are unchanged from main.cpp.

#include <cstddef>
#include <cstdint>
#include "device_config.h"
#include "gnss_parser.h"
#include "survey_math.h"

// Facts the rest of the firmware reads from the receiver owner. A value copy:
// no pointer into receiver state and no mutation path back into it.
struct GnssSnapshot {
  // Solution.
  GgaData gga;
  HorizontalAccuracyData accuracy;
  GnssTimeData time;
  uint32_t gga_ms = 0;            // arrival of the last accepted GGA line

  // Receiver identity, handshake and profile.
  const char *role = "UNKNOWN";   // acknowledged MODE role text
  bool version_ok = false;
  bool startup_complete = false;
  bool profile_applied = false;
  bool profile_running = false;
  bool profile_failed = false;

  // UART activity and parse counters.
  bool uart_seen = false;         // at least one receiver byte this boot
  uint32_t last_rx_ms = 0;
  uint32_t bytes = 0, lines = 0, gga_count = 0, bestnav_count = 0, rmc_count = 0;
  uint32_t checksum_errors = 0;   // parser rejects and the line-overflow reset

  // RTCM stream.
  uint32_t rtcm_frames = 0;       // complete frames read from COM2
  uint32_t rtcm_bad = 0;          // framing rejects and abandoned frames
  uint16_t rtcm_last_message = 0;
  uint32_t rtcm_last_rx_ms = 0;   // last receiver frame observed (either direction)

  // Reference/station captured from an RTCM 1005/1006 frame, whether the
  // receiver sent it (Base) or was sent it (Rover).
  survey::Cartesian reference;
  uint16_t station = 0;
  uint32_t reference_ms = 0;
};

// UART1 wiring. The board layer still owns the pins (R10a moves it to
// board_hardware); the receiver service owns the port and its buffer sizes.
struct GnssPort {
  int rx_pin;
  int tx_pin;
  uint32_t baud;
  int rx_buffer;
  int tx_buffer;
};

// Facts the receiver owner cannot perform itself. The composition root fills
// every field before `begin`.
struct GnssObservers {
  // Storage owner's CSV session lines: a rejected/timed-out profile command,
  // and a verified profile.
  void (*log_event)(const char *event, const char *detail);
  void (*log_config)(const char *profile, const char *notes);
  // Correction owner drops its receiver-derived state (queue, health, station,
  // reference age) when a profile starts or the receiver proof is lost.
  void (*receiver_reset)();
  // One complete RTCM frame read from the receiver, for the link and correction
  // owners to admit. Called with the frame's counters already updated.
  void (*forward_frame)(const uint8_t *frame, size_t length,
                        uint16_t message_type);
  // Read-only label policy for a GGA quality (instrument_status owns the text).
  const char *(*fix_label)(int quality);
};

namespace gnss_service {

// Per-turn input bound (R10b). Measured on the host doubles: one second of the
// receiver's Base COM2 output - the three 1 Hz sentences (GPGGA, GPRMC,
// BESTNAVA) plus a 1005 reference and a full-size 1029-byte MSM frame - is 1492
// bytes, 129.5 ms of 115200 8N1 line time, and arrived as one 1492-byte turn
// before this bound existed. The UART1 RX ring is 2048 bytes (main.cpp's
// GnssPort), i.e. 178 ms of line time.
//
// kInputBudgetBytes: what one turn reads unconditionally. 512 bytes is 44 ms of
// line time and ~17x the ~30 bytes an ordinary 2.6 ms turn receives, while still
// covering the three 1 Hz sentences or a full-size MSM frame.
//
// kInputBacklogMargin: past the budget a turn keeps reading only while the
// backlog is above this margin, so the turn ends with at most this many bytes
// unread - the invariant is `service_input` leaves the ring holding at most
// kInputBacklogMargin bytes, unless it read less than the budget. 256 bytes is
// 22 ms of line time, so a yielded turn leaves at least 1792 bytes (155 ms) of
// ring headroom: neither a long turn (an HTTP response, an optional commit) nor
// the next turn's arrival can overflow the ring and lose receiver bytes. A
// backlog above the margin is a burst that is drained within the same turn down
// to the margin, not deferred whole.
//
// Receiver bytes are never dropped to save time. The only reader that discards
// is discard_input (the OTA pause), and a bounded turn leaves its remainder in
// the ring for the next turn instead of clearing it, so `bytes`, `lines`,
// `checksum_errors` and `rtcm_bad` keep counting exactly the bytes the receiver
// read and the rejects its parser found.
constexpr uint32_t kInputBudgetBytes = 512;
constexpr int kInputBacklogMargin = 256;

// Constructs UART1, installs the observations and schedules the first startup
// handshake command 1500 ms later.
void begin(const GnssPort &port, const GnssObservers &observers, uint32_t now_ms);

// Role/base inputs the settings owner holds. Only role and base_rtcm select the
// profile table; the coordinate is what a fixed MODE BASE command carries.
void set_config(const DeviceConfig &config);
void set_base_coordinate(bool fixed, double latitude, double longitude,
                         double height);
// The saved coordinate cannot be used: `blocks_any_role` is a survey
// application failure (either role stops), false is a boot load failure that
// only stops Base.
void report_base_failure(bool blocks_any_role);

// Per-turn services, in the loop's existing order.
void service_input(uint32_t now_ms);     // RTCM framing and ASCII lines from UART1
void discard_input(uint32_t budget);     // OTA pause: read and drop without parsing
void service_startup(uint32_t now_ms);   // VERSION/GGA handshake, then the profile
void service_profile(uint32_t now_ms);   // acknowledgement/retry/timeout sequencer

// Typed requests.
void apply_unit_profile();
void reset_for_config_change();
void reset_input_state();
void reset_reference();
void query_role();
// Writes one whole COM2 frame: the bytes accepted, 0 when the TX ring cannot
// hold the complete frame (nothing written; the caller retries), or a short
// count when the port stopped early. Only a complete write updates the
// last-message/reference observations.
size_t write_frame(const uint8_t *frame, size_t length, uint32_t now_ms);

GnssSnapshot snapshot();
// The allowlisted profile command for a step (nullptr past the end of the
// table), and the command a running sequencer is waiting on ("" when idle).
const char *active_profile_command(uint8_t step);
const char *pending_profile_command();

}  // namespace gnss_service
