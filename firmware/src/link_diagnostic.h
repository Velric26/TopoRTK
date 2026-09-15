#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
class IPAddress;
constexpr size_t kDiagnosticCapacity=6144;
void diagnostic_begin();
void diagnostic_service(uint32_t now,bool rover,IPAddress peer,bool profile_busy);
bool diagnostic_busy();
bool diagnostic_request(const char *json);
bool diagnostic_snapshot(char *out,size_t capacity);
// Latest stored report per medium for the settings snapshot; never null-values
// a report that was not produced for that medium.
void diagnostic_tests_json(JsonObject out);
// Device-local half of a pair operation: starts the quick paired test for one
// medium, with a locally generated run id.
// transport: 'sik' => the paired RTCM engine (profile 0 clean, 1 injected);
//            'wifi' => the classic packet engine with its canonical quick profile
//            (mode 0 base-to-rover, 30 s, 1000 ms) and only profile 0.
// Returns false when an engine is busy, a probe is running, the profile is not
// supported for that medium, config validation fails, or the socket/UART is
// unavailable — never partially arms. The tested medium is brought up by this
// call itself (UART2 begin / diagnostics UDP bind), so no prior probe or route
// selection is needed, and a start inside a link operation leaves that
// operation's diagnostic reservation alone. A refusal records its branch as
// "<medium>:<branch>" (for example "sik:engine_busy") in the diagnostic
// snapshot's quick_test_error, cleared by the next start that arms.
bool diagnostic_quick_test_start(uint8_t transport,uint8_t profile,uint32_t now);
// Same, with an explicit run id. A paired run needs one id both instruments use:
// each engine ignores a peer whose run id differs, so a pair operation passes
// the same value here on both units instead of letting each generate its own.
// The id must be 100000..999999 and differ from the target engine's current one.
bool diagnostic_quick_test_start(uint8_t transport,uint8_t profile,uint32_t run,uint32_t now);
// True while either engine is armed/running (the run is still in flight).
bool diagnostic_quick_test_busy(uint32_t now);
// True once the run reached a terminal state (Done or Failed), regardless of pass/fail.
bool diagnostic_quick_test_finished(uint32_t now);
// The local verdict, valid only when finished(): pair_pass (never true without the
// peer's own result).
bool diagnostic_quick_test_pass(uint32_t now);
// Aborts an in-flight quick test with the given reason; safe to call when idle.
void diagnostic_quick_test_cancel(const char *why,uint32_t now);
