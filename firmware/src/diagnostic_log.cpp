// Optional SD diagnostic log (R10a slice 4): the CSV session moved out of
// main.cpp. Bodies are unchanged except that the removed globals became
// file-local state, `millis()` and the receiver snapshot became the root's
// `LogTime`/`DiagnosticInputs`, and the accuracy text arrives through the
// installed hook. The rows, headers, event names and the survey SD mutex
// discipline are the ones main.cpp wrote.
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

// Best effort: a unavailable card, an unopened session or a mutex the survey
// journal is holding drops the line instead of waiting for it.
bool append_text(const char *path, const char *text) {
  if (!sd_ready || path == nullptr || text == nullptr) return false;
  if(!survey_sd_lock())return false;
  struct Unlock {~Unlock(){survey_sd_unlock();}} unlock;
  File file = SD_MMC.open(path, FILE_APPEND);
  if (!file) return false;
  const size_t written = file.print(text);
  file.flush();
  file.close();
  return written == std::strlen(text);
}

}  // namespace

void event(const char *name, const char *detail, const LogTime &time) {
  if (!sd_ready || sd_session_path[0] == '\0') return;
  char path[160] = {};
  std::snprintf(path, sizeof(path), "%s/events.csv", sd_session_path);
  char utc[24] = "---";
  if (time.utc.valid) {
    std::snprintf(utc, sizeof(utc), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  time.utc.year, time.utc.month, time.utc.day, time.utc.hour,
                  time.utc.minute, time.utc.second);
  }
  char line[256] = {};
  std::snprintf(line, sizeof(line), "%lu,%c,%s,%s,%s\n",
                static_cast<unsigned long>(time.now_ms), unit_label, utc,
                name == nullptr ? "UNKNOWN" : name,
                detail == nullptr ? "" : detail);
  append_text(path, line);
}

void config(const char *profile, const char *notes, const LogTime &time) {
  if (!sd_ready || !sd_test_passed || sd_session_path[0] == '\0') return;
  char path[160] = {};
  std::snprintf(path, sizeof(path), "%s/config.csv", sd_session_path);
  char line[256] = {};
  std::snprintf(line, sizeof(line), "%lu,%c,%s,%s\n",
                static_cast<unsigned long>(time.now_ms), unit_label,
                profile == nullptr ? "UNKNOWN" : profile,
                notes == nullptr ? "" : notes);
  append_text(path, line);
  event("CONFIG_PROFILE", profile == nullptr ? "UNKNOWN" : profile, time);
}

void solution(const DiagnosticInputs &in) {
  if (!sd_ready || !sd_test_passed || sd_session_path[0] == '\0' ||
      !in.solution_fresh) {
    return;
  }
  char path[160] = {};
  std::snprintf(path, sizeof(path), "%s/solution.csv", sd_session_path);
  char hacc[24] = {};
  hooks->accuracy_text(hacc, sizeof(hacc), in.time.now_ms);
  char rssi[8] = "";
  if (in.rssi_valid) {
    std::snprintf(rssi, sizeof(rssi), "%d", in.rssi_dbm);
  }
  char line[320] = {};
  std::snprintf(line, sizeof(line),
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
  append_text(path, line);
}

void begin(const SdPort &port, const DiagnosticHooks &installed, const LogTime &time) {
  hooks = &installed;
  unit_label = port.unit;
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

  char path[160] = {};
  std::snprintf(path, sizeof(path), "%s/events.csv", sd_session_path);
  append_text(path, "uptime_ms,unit,utc,event,detail\n");
  std::snprintf(path, sizeof(path), "%s/solution.csv", sd_session_path);
  append_text(path,
              "uptime_ms,utc_time,utc_status,fix,latitude,longitude,altitude_m,satellites,hdop,h_acc,rtcm_age_ms,link_rssi_dbm,rtcm_uart_frames,rtcm_wifi_tx_frames,rtcm_wifi_rx_frames,rtcm_forwarded_bytes,wifi_sequence_gaps,wifi_invalid_packets,link_transport\n");
  std::snprintf(path, sizeof(path), "%s/config.csv", sd_session_path);
  append_text(path, "uptime_ms,unit,profile,notes\n");
  char session_path[160] = {};
  std::snprintf(session_path, sizeof(session_path), "%s/session.json", sd_session_path);
  File session = SD_MMC.open(session_path, FILE_WRITE);
  if (session) {
    session.printf("{\"unit\":\"%c\",\"boot_uptime_ms\":%lu,\"card_bytes\":%llu}\n",
                   unit_label, static_cast<unsigned long>(time.now_ms),
                   static_cast<unsigned long long>(SD_MMC.cardSize()));
    session.flush();
    session.close();
  }
  event("BOOT", "SD_READY|READBACK_PASS", time);
}

void service(const DiagnosticInputs &in) {
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

DiagnosticSnapshot snapshot() {
  DiagnosticSnapshot out;
  out.ready = sd_ready;
  out.verified = sd_test_passed;
  std::strncpy(out.session, sd_session_path, sizeof(out.session) - 1);
  return out;
}

}  // namespace diagnostic_log
