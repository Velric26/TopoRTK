#pragma once
// Optional SD diagnostic log (R10a slice 4, R10b prescribed queue). Owns the
// best-effort CSV session that is separate from the authoritative survey
// journal: the card mount test and its readback result, the session directory
// naming, the events/config/solution writers and their headers and rows, the
// one per-second solution sample, and the shared survey SD mutex discipline
// around every append.
//
// R10 step 4's structure: a fixed 16-record queue, each record one 160-byte
// destination path plus one 512-byte text buffer, filled by one bounded copy
// from a producer - no hot-path heap growth - plus the per-second sample's own
// single record (the one record a later producer supersedes). The destination is
// queued with its row, so a session change between queueing and writing cannot
// misroute an old event. The producers never touch the card; a dedicated
// low-priority `writer` stage drains the queue, at most kWriterRecordsPerTurn
// records per call and always under the journal's own `survey_sd_lock` taken with
// no wait. A slow, full, busy or failing card therefore costs the turn a bounded
// number of appends and can never block the correction output written earlier in
// the same turn.
//
// Scheduling (R10 step 4), stated rather than implied: the writer is a stage of
// the loop task, not a second FreeRTOS task. It runs at the end of the turn,
// after every other service, which is the lowest priority the loop has; one call
// commits at most kWriterRecordsPerTurn records and every append takes
// `survey_sd_lock` with a zero timeout, so a call costs at most that many
// appends and never waits on the survey worker's own `portMAX_DELAY` use of the
// same mutex or on the correction output written earlier in the turn. A second
// task could only poll that same zero-timeout mutex from another context - adding
// SD-bus contention and queue-index races with the journal's worker while still
// owing the same per-turn bound - or block on the card, which is what the loop
// must never do. A budgeted stage on the loop task bounds the turn
// deterministically on the instrument and in the host doubles alike, so the
// offline cases exercise the very code the hardware run measures.
//
// A row longer than the buffer that formats it, and a row the queue cannot
// hold, are dropped and counted in `snapshot().dropped`, with
// `dropped_oversized`/`dropped_queue_full` naming which. A missing card, or a
// session whose readback test failed, produces no row at all and is not counted:
// the card and the readback test gate every producer and the writer alike, which
// is the gate this module has always documented. A write/flush the card refuses
// marks optional logging unavailable (`available`) instead of retrying per row:
// the writer then leaves the card alone until kWriterRetryMs has elapsed and
// probes it once. The authoritative survey journal keeps its own writes, commits,
// exports and recovery, none of which this module touches. It reads nothing on
// its own - the root passes one value snapshot per call, so the receiver/link
// facts the surfaces show stay in the root - and it prints the same console lines
// it printed from main.cpp.

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
  // Rows this optional session produced and never wrote: the queue was full, a
  // row did not fit the buffer that formats it, a newer per-second solution
  // sample superseded one the card had not taken yet, or the session, the shared
  // journal's mutex or the card refused the record - or the card refused a write
  // and the writer marked the optional log unavailable. A missing card or a
  // closed session produces no row at all and is not counted - the same gate
  // main.cpp always applied. The authoritative survey journal keeps its own
  // writes and is not counted here.
  uint32_t dropped = 0;
  uint32_t dropped_queue_full = 0;   // of `dropped`: the 16-record queue was full
  uint32_t dropped_oversized = 0;    // of `dropped`: the row did not fit its buffer
  // Records waiting for the writer: the queue plus the one-per-second sample.
  unsigned pending = 0;
  // The writer is taking records: the session mounted, passed its readback test
  // and has not had a write refused since (or the retry window has elapsed and
  // it is probing again).
  bool available = false;
};

namespace diagnostic_log {

// The prescribed structures (R10 step 4). Measured: a worst-case turn produced
// four rows (link, rtcm and fix edges plus the one-second solution sample) and
// the receiver's own profile-completion observer two more; with the host double's
// 40 ms card one such turn cost the loop 160 ms of clock, and the observer's two
// rows cost 80 ms inside the receiver's call. Producers are now one bounded copy
// each, and one writer call commits at most kWriterRecordsPerTurn records - the
// same three the measured bound allowed, one solution record plus two event rows
// - i.e. at most 120 ms with that card and no card work at all while the writer
// is marked unavailable.
constexpr unsigned kQueueRecords = 16;        // fixed queue depth per R10 step 4
constexpr size_t kDestinationBytes = 160;     // destination path per record
constexpr size_t kTextBytes = 512;            // text per record
constexpr size_t kEventLineBytes = 256;       // existing event/config line buffer
constexpr size_t kSolutionLineBytes = 320;    // existing solution line buffer
// Strict per-turn drain budget: one solution record plus two event rows.
constexpr unsigned kWriterRecordsPerTurn = 3;
// A refused write marks optional logging unavailable for this long; the writer
// then probes the card once, so an unplugged or failing card costs no time at
// all and a card that recovers is picked up again without a reboot.
constexpr uint32_t kWriterRetryMs = 5000;

void begin(const SdPort &port, const DiagnosticHooks &hooks, const LogTime &time);

// The loop's producer call: session state changes and the per-second solution
// sample, each one a bounded copy into the queue. It never opens the card. A row
// the session has no use for produces no record at all; a row the queue or its
// own buffer cannot hold is counted in `snapshot().dropped`.
void service(const DiagnosticInputs &in);
// The dedicated low-priority writer: one bounded drain of the queue, at most
// kWriterRecordsPerTurn records, each under `survey_sd_lock` taken with no wait.
// Call it last in the turn: a record the session, the mutex or the card refuses
// is skipped and counted in `snapshot().dropped` rather than retried in place,
// and a refused write marks optional logging unavailable until kWriterRetryMs
// has elapsed.
void writer(uint32_t now_ms);
// One solution row, without the state-change events: fills the queued
// one-per-second sample record that the writer commits.
void solution(const DiagnosticInputs &in);

// The storage owner's session lines, called where the fact became true.
void event(const char *event, const char *detail, const LogTime &time);
void config(const char *profile, const char *notes, const LogTime &time);

DiagnosticSnapshot snapshot();

}  // namespace diagnostic_log
