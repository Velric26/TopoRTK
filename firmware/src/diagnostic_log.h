#pragma once
// Optional SD diagnostic log (R10a slice 4), extracted from main.cpp. Owns the
// best-effort CSV session that is separate from the authoritative survey
// journal: the card mount test and its readback result, the session directory
// naming, the events/config/solution writers and their headers and rows, the
// one per-second solution sample, and the shared survey SD mutex discipline
// around every append.
//
// It is bounded best-effort and never blocks the correction loop: a producer
// only formats its row and enqueues it (R10b), one service call commits at most
// one solution sample plus kEventRowsPerTurn event rows, and a missing card, a
// closed session, a busy mutex or a card that refuses the write drops the row
// and counts it in `snapshot().dropped` instead of waiting or retrying in place.
// A failed readback stops the rows while the mount stays reported. It reads
// nothing on its own - the root passes one value snapshot per call, so the
// receiver/link facts the surfaces show stay in the root - and it prints the
// same console lines it printed from main.cpp.

#include <cstddef>
#include <cstdint>
#include "gnss_parser.h"

// SD wiring. The board layer still owns the pins (R10a moves it to
// board_hardware); the log owns the card and the session.
struct SdPort {
  int clock;
  int command;
  int data0;
  char unit;   // the unit letter every CSV row carries
};

// The root's clock: `millis()` at the call, and the receiver's UTC stamp the
// events row writes ("---" until one is valid).
struct LogTime {
  uint32_t now_ms = 0;
  GnssTimeData utc;
};

// The root's receiver, link and correction facts for one service call. A value
// copy: the session writers never read receiver or link state themselves.
struct DiagnosticInputs {
  LogTime time;
  bool base = false;
  bool uart_active = false;
  int fix_quality = -1;          // -1 when the receiver has sent no GGA
  const char *fix_text = "---";  // the label instrument_status owns for
                                 // fix_quality; a row is only written for a
                                 // received GGA, where the two agree
  const char *role = "UNKNOWN";  // acknowledged receiver role
  bool link_connected = false;
  bool rtcm_active = false;
  // solution.csv row.
  bool solution_fresh = false;   // a GGA within the 3 s window
  char gga_utc[16] = "---";
  double latitude = 0, longitude = 0, altitude = 0;
  int satellites = 0;
  double hdop = 0;
  long rtcm_age_ms = -1;
  bool rssi_valid = false;
  int rssi_dbm = 0;
  const char *transport = "-";   // "SIK", "WIFI" or "-"
  uint32_t rtcm_uart_frames = 0, rtcm_wifi_tx_frames = 0, rtcm_wifi_rx_frames = 0;
  uint32_t forwarded_bytes = 0, sequence_gaps = 0, invalid_packets = 0;
};

// Facts the log cannot derive itself.
struct DiagnosticHooks {
  // The root's horizontal-accuracy text (instrument_status owns the policy),
  // asked for only when a solution row is actually written.
  void (*accuracy_text)(char *out, size_t capacity, uint32_t now_ms);
};

// What the root gates storage on and the console reports.
struct DiagnosticSnapshot {
  bool ready = false;      // sd_ready: the card mounted with a session directory
  bool verified = false;   // sd_test_passed: the write/readback test passed
  char session[128] = {};
  // Rows this optional session produced and never wrote: the pending ring was
  // full, a newer per-second solution sample superseded one the card had not
  // taken, or the session, the card or the survey journal's SD mutex refused the
  // commit. A missing card or a closed session produces no row at all and is not
  // counted - the same gate main.cpp always applied. The authoritative survey
  // journal keeps its own writes and is not counted here.
  uint32_t dropped = 0;
  // Rows waiting for a bounded commit: the ring plus the one-per-second
  // solution slot.
  unsigned pending = 0;
};

namespace diagnostic_log {

// Per-turn commit bound (R10b). Measured: a worst-case turn produced four rows
// (link, rtcm and fix edges plus the one-second solution sample) and the
// receiver's own profile-completion observer two more; with the host double's
// 40 ms card one such turn cost the loop 160 ms of clock, and the observer's two
// rows cost 80 ms inside the receiver's call. The pending ring makes every
// producer O(1) and the commit budget makes a turn's card cost independent of
// how many rows that turn produced: one solution sample plus kEventRowsPerTurn
// event rows, i.e. at most 120 ms with that card and nothing at all when the
// card is absent or failing.
constexpr unsigned kPendingRows = 8;       // queue depth per turn's producers
constexpr unsigned kEventRowsPerTurn = 2;  // committed event rows per service call

void begin(const SdPort &port, const DiagnosticHooks &hooks, const LogTime &time);

// The loop's one call: session state changes, the per-second solution sample
// when the window has elapsed, then the bounded card commits. A row the session,
// the mutex or the card refuses is skipped and counted in `snapshot().dropped`
// rather than retried in place.
void service(const DiagnosticInputs &in);
// One solution row, without the state-change events: fills the pending solution
// slot that the next service call commits.
void solution(const DiagnosticInputs &in);

// The storage owner's session lines, called where the fact became true.
void event(const char *event, const char *detail, const LogTime &time);
void config(const char *profile, const char *notes, const LogTime &time);

DiagnosticSnapshot snapshot();

}  // namespace diagnostic_log
