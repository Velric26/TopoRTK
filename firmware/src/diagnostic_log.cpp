// Optional SD diagnostic log (R10a slice 4): the CSV session moved out of
// main.cpp. The rows, headers, event names and the survey SD mutex discipline
// are the ones main.cpp wrote; R10b gave the module the prescribed shape - a
// fixed 16-record queue whose every record carries its own 160-byte destination
// and 512-byte text, producers that only copy into it, and a dedicated
// low-priority writer stage that drains it under the journal's own SD mutex.
#include "diagnostic_log.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

#include <cstdio>
#include <cstring>

#include "survey_service.h"

namespace diagnostic_log {
namespace {

const DiagnosticHooks *hooks = nullptr;
char unit_label = 0;

bool sd_ready = false;
bool sd_test_passed = false;
char sd_session_path[128] = {};
bool sd_state_initialized = false;
bool sd_last_uart_active = false;
bool sd_last_linked = false;
bool sd_last_rtcm_active = false;
int sd_last_fix_quality = -1;
char sd_last_role[8] = {};
uint32_t last_sd_solution_ms = 0;

// The prescribed queue (R10 step 4): 16 records, each one 160-byte destination
// plus one 512-byte text. Every producer fills a record with one bounded copy of
// its already-formatted row, so producer cost is constant no matter how many rows
// a turn produces and no allocation happens on the hot path. The destination
// travels in the record, so a session change between queueing and writing cannot
// misroute a queued row. Rows are per-file FIFO; the two files have never had a
// cross-file order.
struct Record {
  char destination[kDestinationBytes];
  char text[kTextBytes];
};
static_assert(sizeof(Record) == kDestinationBytes + kTextBytes,
              "One record is one 160-byte destination plus one 512-byte text");
static_assert((kQueueRecords + 1) * sizeof(Record) <= 12u * 1024u,
              "Bounded optional-log queue memory");
Record records[kQueueRecords];
unsigned queue_head = 0, queue_count = 0;
// The one-per-second sample keeps its own single record: a newer sample replaces
// one the card has not taken yet and the replaced row counts as dropped - the
// supersede rule the per-second sample has always had - so a 16-deep backlog of
// event rows can never delay the sample and the queue itself stays per-file FIFO.
Record solution_record;
bool solution_pending = false;
uint32_t dropped_rows = 0, dropped_queue_full = 0, dropped_oversized = 0;
bool drop_reported = false;

// The writer's own availability. Set when a session mounted, passed its readback
// test and had its headers written; cleared when the card refuses a write or a
// flush, which is the only failure the writer reacts to by leaving the card
// alone - a busy journal mutex or a closed session merely drops the record.
bool writer_available = false;
uint32_t writer_failed_ms = 0;

void copy_text(char *out, size_t capacity, const char *text) {
  size_t index = 0;
  while (text != nullptr && text[index] != '\0' && index + 1 < capacity) {
    out[index] = text[index];
    ++index;
  }
  out[index] = '\0';
}

// The three outcomes one append can have. `kRefused` is the optional log saying
// no without the card having failed (closed session, mutex held); `kFailed` is
// the card refusing the open or the bytes, which marks optional logging
// unavailable.
enum WriteResult { kWriteWritten, kWriteRefused, kWriteFailed };

// One append under the journal's own SD mutex, taken with no wait: a mutex the
// survey journal holds refuses the record instead of delaying the writer, so the
// authoritative journal's commits are never waited on and never moved. A flush
// that loses bytes is not observable through this API (`File::flush` returns
// void), so a card that accepted the open but not the bytes surfaces as the
// short print this returns.
WriteResult write_record(const char *destination, const char *text) {
  if (!sd_ready || destination == nullptr || text == nullptr) return kWriteRefused;
  if (!survey_sd_lock()) return kWriteRefused;
  struct Unlock { ~Unlock() { survey_sd_unlock(); } } unlock;
  File file = SD_MMC.open(destination, FILE_APPEND);
  if (!file) return kWriteFailed;
  const size_t written = file.print(text);
  file.flush();
  file.close();
  return written == std::strlen(text) ? kWriteWritten : kWriteFailed;
}

// One bounded console line per failure episode, so a card that has stopped
// taking rows is visible without a line per row; a successful commit clears it.
void report_drop() {
  if (drop_reported) return;
  drop_reported = true;
  Serial.printf("SD: LOG DROPPED %lu optional row(s); oversized or full queue, closed session, busy journal mutex or refusing card\n",
                static_cast<unsigned long>(dropped_rows));
}

void drop_oversized() {
  ++dropped_rows;
  ++dropped_oversized;
  report_drop();
}

void drop_full_queue() {
  ++dropped_rows;
  ++dropped_queue_full;
  report_drop();
}

// The destination the record carries: the file of the session that produced the
// row. The queue never re-derives it, and the session path is 128 bytes, so the
// prescribed 160-byte destination holds every name it can build.
bool destination_of(char *out, const char *file) {
  const int needed = std::snprintf(out, kDestinationBytes, "%s/%s", sd_session_path, file);
  return needed > 0 && static_cast<size_t>(needed) < kDestinationBytes;
}

// One record, one bounded copy: both fields are sized by the record, so no row
// can overrun it and no heap is touched.
void place(Record &record, const char *destination, const char *line) {
  copy_text(record.destination, sizeof(record.destination), destination);
  copy_text(record.text, sizeof(record.text), line);
}

bool enqueue(const char *destination, const char *line) {
  if (std::strlen(line) + 1 > kTextBytes) {   // cannot happen from a 256/320-byte producer
    drop_oversized();
    return false;
  }
  if (queue_count == kQueueRecords) {         // the turn produced more than the queue holds
    drop_full_queue();
    return false;
  }
  place(records[(queue_head + queue_count) % kQueueRecords], destination, line);
  ++queue_count;
  return true;
}

// The sample's own record is filled the same way; the row it replaces was
// produced and never written, so it counts as dropped.
void enqueue_solution(const char *destination, const char *line) {
  if (std::strlen(line) + 1 > kTextBytes) {   // cannot happen from a 320-byte buffer
    drop_oversized();
    return;
  }
  place(solution_record, destination, line);
  if (solution_pending) {
    ++dropped_rows;
    report_drop();
  }
  solution_pending = true;
}

// A card that refused the bytes is not asked again per row: optional logging is
// marked unavailable and the writer leaves the card alone until the retry
// window elapses. Nothing here waits on or touches the survey journal.
void mark_unavailable(uint32_t now_ms) {
  writer_available = false;
  writer_failed_ms = now_ms;
  Serial.printf("SD: OPTIONAL LOG UNAVAILABLE %s; card refused a write, corrections are unaffected\n",
                sd_session_path);
}

// One record's append, with the accounting every caller shares: a written record
// clears the drop report, anything else is counted, and a card that refused the
// open or the bytes marks optional logging unavailable and ends the drain.
// Returns false when the card failed, so the caller stops asking it anything.
bool commit_record(const Record &record, uint32_t now_ms) {
  const WriteResult result = write_record(record.destination, record.text);
  if (result == kWriteWritten) {
    drop_reported = false;
    return true;
  }
  ++dropped_rows;
  report_drop();
  if (result == kWriteFailed) {
    mark_unavailable(now_ms);
    return false;
  }
  return true;   // the optional log refused the record (no session, mutex held)
}

}  // namespace

void event(const char *name, const char *detail, const LogTime &time) {
  // The readback test is the session's admission gate for every row, the events
  // file included: a session that cannot be proven takes no row at all.
  if (!sd_ready || !sd_test_passed || sd_session_path[0] == '\0') return;
  char destination[kDestinationBytes] = {};
  if (!destination_of(destination, "events.csv")) {
    drop_oversized();
    return;
  }
  char utc[24] = "---";
  if (time.utc.valid) {
    std::snprintf(utc, sizeof(utc), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  time.utc.year, time.utc.month, time.utc.day, time.utc.hour,
                  time.utc.minute, time.utc.second);
  }
  char line[kEventLineBytes] = {};
  const int needed = std::snprintf(line, sizeof(line), "%lu,%c,%s,%s,%s\n",
                static_cast<unsigned long>(time.now_ms), unit_label, utc,
                name == nullptr ? "UNKNOWN" : name,
                detail == nullptr ? "" : detail);
  // An event line that does not fit its 256-byte buffer is an oversized record:
  // dropped and counted, never written truncated.
  if (needed < 0 || static_cast<size_t>(needed) >= sizeof(line)) {
    drop_oversized();
    return;
  }
  enqueue(destination, line);
}

void config(const char *profile, const char *notes, const LogTime &time) {
  if (!sd_ready || !sd_test_passed || sd_session_path[0] == '\0') return;
  char destination[kDestinationBytes] = {};
  if (!destination_of(destination, "config.csv")) {
    drop_oversized();
    return;
  }
  char line[kEventLineBytes] = {};
  const int needed = std::snprintf(line, sizeof(line), "%lu,%c,%s,%s\n",
                static_cast<unsigned long>(time.now_ms), unit_label,
                profile == nullptr ? "UNKNOWN" : profile,
                notes == nullptr ? "" : notes);
  if (needed < 0 || static_cast<size_t>(needed) >= sizeof(line)) {
    drop_oversized();
    return;
  }
  enqueue(destination, line);
  event("CONFIG_PROFILE", profile == nullptr ? "UNKNOWN" : profile, time);
}

void solution(const DiagnosticInputs &in) {
  if (!sd_ready || !sd_test_passed || sd_session_path[0] == '\0' ||
      !in.solution_fresh) {
    return;
  }
  char destination[kDestinationBytes] = {};
  if (!destination_of(destination, "solution.csv")) {
    drop_oversized();
    return;
  }
  char hacc[24] = {};
  hooks->accuracy_text(hacc, sizeof(hacc), in.time.now_ms);
  char rssi[8] = "";
  if (in.rssi_valid) {
    std::snprintf(rssi, sizeof(rssi), "%d", in.rssi_dbm);
  }
  char line[kSolutionLineBytes] = {};
  const int needed = std::snprintf(line, sizeof(line),
                "%lu,%s,%s,%s,%.8f,%.8f,%.3f,%d,%.2f,%s,%ld,%s,%lu,%lu,%lu,%lu,%lu,%lu,%s\n",
                static_cast<unsigned long>(in.time.now_ms),
                in.time.utc.valid ? in.gga_utc : "---",
                in.time.utc.valid ? "VALID" : "TIME_WAIT",
                in.fix_text, in.latitude, in.longitude, in.altitude,
                in.satellites, in.hdop, hacc, in.rtcm_age_ms, rssi,
                static_cast<unsigned long>(in.rtcm_uart_frames),
                static_cast<unsigned long>(in.rtcm_wifi_tx_frames),
                static_cast<unsigned long>(in.rtcm_wifi_rx_frames),
                static_cast<unsigned long>(in.forwarded_bytes),
                static_cast<unsigned long>(in.sequence_gaps),
                static_cast<unsigned long>(in.invalid_packets),
                in.transport);
  if (needed < 0 || static_cast<size_t>(needed) >= sizeof(line)) {
    drop_oversized();
    return;
  }
  enqueue_solution(destination, line);
}

void begin(const SdPort &port, const DiagnosticHooks &installed, const LogTime &time) {
  hooks = &installed;
  unit_label = port.unit;
  // A mount attempt starts with the writer marked unavailable: only a session
  // that mounts and passes its readback test may take records.
  writer_available = false;
  writer_failed_ms = time.now_ms;
  SD_MMC.setPins(port.clock, port.command, port.data0);
  if (!SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("SD: MOUNT FAIL");
    return;
  }
  sd_ready = true;
  Serial.printf("SD: MOUNT PASS card=%llu MB free=%llu MB\n",
                static_cast<unsigned long long>(SD_MMC.cardSize() / (1024ULL * 1024ULL)),
                static_cast<unsigned long long>((SD_MMC.totalBytes() - SD_MMC.usedBytes()) /
                                                (1024ULL * 1024ULL)));

  SD_MMC.mkdir("/TOPO-RTK");
  char unit_path[48] = {};
  std::snprintf(unit_path, sizeof(unit_path), "/TOPO-RTK/UNIT-%c", unit_label);
  SD_MMC.mkdir(unit_path);
  char sessions_path[80] = {};
  std::snprintf(sessions_path, sizeof(sessions_path), "%s/SESSIONS", unit_path);
  SD_MMC.mkdir(sessions_path);
  // Repeated boot timing must not collide with an existing diagnostic session.
  for(unsigned attempt=0;attempt<1000;++attempt){
    std::snprintf(sd_session_path,sizeof(sd_session_path),"%s/BOOT-%lu-%u",sessions_path,static_cast<unsigned long>(time.now_ms),attempt);
    if(!SD_MMC.exists(sd_session_path))break;
  }
  if (!SD_MMC.mkdir(sd_session_path)) {
    Serial.printf("SD: SESSION DIR FAIL %s\n", sd_session_path);
    sd_ready = false;
    return;
  }

  const char *test_path = "/TOPO-RTK/SD-READBACK-TEST.TXT";
  File test = SD_MMC.open(test_path, FILE_WRITE);
  if (!test) {
    Serial.println("SD: TEST WRITE FAIL");
    sd_ready = false;
    return;
  }
  const char *test_text = "TopoRTK SD readback test v1\n";
  const size_t test_written = test.print(test_text);
  test.flush();
  test.close();
  File readback = SD_MMC.open(test_path, FILE_READ);
  char readback_text[64] = {};
  const size_t read_count = readback ? readback.readBytes(readback_text,
                                                           sizeof(readback_text) - 1)
                                     : 0;
  if (readback) readback.close();
  sd_test_passed = test_written == std::strlen(test_text) &&
                   std::strncmp(readback_text, test_text, std::strlen(test_text)) == 0 &&
                   read_count == std::strlen(test_text);
  Serial.printf("SD: READBACK %s\n", sd_test_passed ? "PASS" : "FAIL");
  if (!sd_test_passed) return;

  char path[kDestinationBytes] = {};
  std::snprintf(path, sizeof(path), "%s/events.csv", sd_session_path);
  write_record(path, "uptime_ms,unit,utc,event,detail\n");
  std::snprintf(path, sizeof(path), "%s/solution.csv", sd_session_path);
  write_record(path,
              "uptime_ms,utc_time,utc_status,fix,latitude,longitude,altitude_m,satellites,hdop,h_acc,rtcm_age_ms,link_rssi_dbm,rtcm_uart_frames,rtcm_wifi_tx_frames,rtcm_wifi_rx_frames,rtcm_forwarded_bytes,wifi_sequence_gaps,wifi_invalid_packets,link_transport\n");
  std::snprintf(path, sizeof(path), "%s/config.csv", sd_session_path);
  write_record(path, "uptime_ms,unit,profile,notes\n");
  char session_path[kDestinationBytes] = {};
  std::snprintf(session_path, sizeof(session_path), "%s/session.json", sd_session_path);
  File session = SD_MMC.open(session_path, FILE_WRITE);
  if (session) {
    session.printf("{\"unit\":\"%c\",\"boot_uptime_ms\":%lu,\"card_bytes\":%llu}\n",
                   unit_label, static_cast<unsigned long>(time.now_ms),
                   static_cast<unsigned long long>(SD_MMC.cardSize()));
    session.flush();
    session.close();
  }
  // The session is proven: the writer may now take records, including the ones
  // the producers queue from here on.
  writer_available = true;
  event("BOOT", "SD_READY|READBACK_PASS", time);
}

void service(const DiagnosticInputs &in) {
  // The service turn only produces records - one bounded copy each, no card
  // access - so nothing here can be delayed by the card, the session or the
  // journal's mutex. The writer stage drains them at the end of the turn.
  if (!sd_state_initialized) {
    sd_state_initialized = true;
    sd_last_uart_active = in.uart_active;
    sd_last_linked = in.link_connected;
    sd_last_rtcm_active = in.rtcm_active;
    sd_last_fix_quality = in.fix_quality;
    std::strncpy(sd_last_role, in.role, sizeof(sd_last_role) - 1);
    event("STATE", "INITIAL", in.time);
  } else {
    if (in.uart_active != sd_last_uart_active) {
      sd_last_uart_active = in.uart_active;
      event("UART", in.uart_active ? "RECEIVING" : "OFFLINE", in.time);
    }
    if (in.link_connected != sd_last_linked) {
      sd_last_linked = in.link_connected;
      event("LINK", in.link_connected ? "CONNECTED" : "DISCONNECTED", in.time);
    }
    if (in.rtcm_active != sd_last_rtcm_active) {
      sd_last_rtcm_active = in.rtcm_active;
      event("RTCM", in.rtcm_active ? "ACTIVE" : "INACTIVE", in.time);
    }
    if (in.fix_quality != sd_last_fix_quality) {
      sd_last_fix_quality = in.fix_quality;
      event("GNSS_FIX", in.fix_text, in.time);
    }
    if (std::strcmp(sd_last_role, in.role) != 0) {
      std::strncpy(sd_last_role, in.role, sizeof(sd_last_role) - 1);
      event("ROLE", in.role, in.time);
    }
  }
  if (in.time.now_ms - last_sd_solution_ms >= 1000) {
    last_sd_solution_ms = in.time.now_ms;
    solution(in);
  }
}

// The dedicated low-priority writer: at most kWriterRecordsPerTurn records per
// call, each one append under the journal's SD mutex, on the loop task and after
// every other service of the turn.
void writer(uint32_t now_ms) {
  // No session, no card: the queue simply holds its records, unopened and
  // uncounted, exactly as a missing card has always behaved.
  if (!sd_ready || !sd_test_passed || sd_session_path[0] == '\0') return;
  if (!writer_available) {
    // Optional logging is unavailable: no card work at all until the retry
    // window has elapsed, then one probe - so a card that refuses writes costs
    // the loop nothing while it stays broken and is picked up again when it
    // recovers.
    if (now_ms - writer_failed_ms < kWriterRetryMs) return;
    writer_available = true;
  }
  unsigned committed = 0;
  // The queued per-second sample goes first: it is the one record another
  // producer supersedes, so leaving it behind a deep backlog would cost the log
  // its freshness.
  if (solution_pending) {
    ++committed;
    const Record &record = solution_record;
    solution_pending = false;
    if (!commit_record(record, now_ms)) return;
  }
  while (committed < kWriterRecordsPerTurn && queue_count > 0) {
    ++committed;
    const Record &record = records[queue_head];
    queue_head = (queue_head + 1) % kQueueRecords;
    --queue_count;
    if (!commit_record(record, now_ms)) return;
  }
}

DiagnosticSnapshot snapshot() {
  DiagnosticSnapshot out;
  out.ready = sd_ready;
  out.verified = sd_test_passed;
  std::strncpy(out.session, sd_session_path, sizeof(out.session) - 1);
  out.dropped = dropped_rows;
  out.dropped_queue_full = dropped_queue_full;
  out.dropped_oversized = dropped_oversized;
  out.pending = queue_count + (solution_pending ? 1u : 0u);
  out.available = writer_available;
  return out;
}

}  // namespace diagnostic_log
