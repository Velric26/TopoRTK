#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
class IPAddress;
constexpr size_t kDiagnosticCapacity=6144;
void diagnostic_begin();
void diagnostic_service(uint32_t now,bool rover,IPAddress peer,bool profile_busy);
// True while this layer occupies the instrument: an engine run on either medium,
// the UART2 probe, or the bounded terminal result exchange that follows a SiK
// run (its engine has finished, but the verdict the link owner settles from is
// still incomplete). The same value is published as the snapshot's "busy", and
// the snapshot's "radio_owned" reports that UART2 share alone.
bool diagnostic_busy();
bool diagnostic_request(const char *json);
bool diagnostic_snapshot(char *out,size_t capacity);
// Latest stored report per medium for the settings snapshot; never null-values
// a report that was not produced for that medium.
void diagnostic_tests_json(JsonObject out);
// Device-local half of a pair operation: starts the paired test the operation
// admitted for one medium, with the shape both peers run.
// transport: 0 Wi-Fi => the classic packet engine; 1 SiK => the paired RTCM
//            engine. The requested shape must be one that medium offers
//            (link_operation::test_parameter_refusal owns that rule): Wi-Fi has
//            no fault-injection profile, SiK has no rate or direction knob.
// Returns false when the shape is not offered, an engine is busy, a probe is
// running, the run id is rejected, config validation fails, or the socket/UART
// is unavailable — never partially arms. The tested medium is brought up by this
// call itself (UART2 begin / diagnostics UDP bind), so no prior probe or route
// selection is needed, and a start inside a link operation leaves that
// operation's diagnostic reservation alone. A refusal records its branch as
// "<medium>:<branch>" (for example "sik:engine_busy") in the diagnostic
// snapshot's quick_test_error, cleared by the next start that arms.
bool diagnostic_quick_test_start(uint8_t transport,uint8_t profile,uint32_t run,uint16_t seconds,uint16_t rate,uint8_t mode,uint32_t now);
// Same, with the canonical quick profile (mode 0 base-to-rover, 30 s,
// 1000 bytes per second) and a locally generated run id, for the Settings
// quick tests.
bool diagnostic_quick_test_start(uint8_t transport,uint8_t profile,uint32_t now);
// Same, with an explicit run id and the canonical quick profile. A paired run
// needs one id both instruments use: each engine ignores a peer whose run id
// differs, so a pair operation passes the same value here on both units instead
// of letting each generate its own. The id must be 100000..999999 and differ
// from the target engine's current one.
bool diagnostic_quick_test_start(uint8_t transport,uint8_t profile,uint32_t run,uint32_t now);
// True while either engine is armed/running (the run is still in flight).
bool diagnostic_quick_test_busy(uint32_t now);
// True once the run reached a terminal state (Done or Failed), regardless of pass/fail.
bool diagnostic_quick_test_finished(uint32_t now);
// True once the run has ended *and* the final verdict is available, i.e. the
// peer's own report has arrived (or the run failed without one). pair_pass() is
// only meaningful when this is true; the operation's own run window is what
// fails a test whose peer never reports.
bool diagnostic_quick_test_ready(uint32_t now);
// The local verdict, valid only when finished(): pair_pass (never true without the
// peer's own result).
bool diagnostic_quick_test_pass(uint32_t now);
// Aborts an in-flight quick test with the given reason; safe to call when idle.
void diagnostic_quick_test_cancel(const char *why,uint32_t now);
