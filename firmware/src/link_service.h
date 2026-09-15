#pragma once
#include "link_operation.h"
#include "pair_session.h"
#include <ArduinoJson.h>
class IPAddress;

// Main-loop owner of the selected production link, pair negotiation, the live
// SiK bridge and the pair-wide link operation (R6). Hardware adapters own bytes;
// diagnostics may reserve UART2, but never own a production session. HTTP reads
// published snapshots only and never writes NVS.
namespace link_service {
using pair_session::Transport;
void begin(bool rover, uint32_t now);
void service(uint32_t now, bool rover, bool radio_reserved);
pair_session::Snapshot snapshot(uint32_t now);
bool connected(uint32_t now);
uint32_t session();
uint32_t peer_boot();
bool radio_active();
bool radio_submit(const uint8_t *frame, size_t size, uint32_t now);
void clear_pending();
IPAddress wifi_peer();
void note_incompatible(uint32_t now);
void write_json(JsonObject out, uint32_t now);

// Local recovery selection (diagnostics/touchscreen). Refused while a pair-wide
// operation is admitted; performs a checked local write and clears an
// interrupted operation record.
bool select(Transport transport, bool rover, uint32_t now);
// Pair-wide operation admission from an accepted HTTP request; the main loop
// performs the authoritative admission and reports the outcome in the settings
// snapshot. Returns false with a reason when the request is refused outright.
bool request_operation(link_operation::Kind kind, Transport transport, const char *id,
                       uint32_t revision, link_operation::Reason &reason, uint8_t profile = 0);
bool cancel_operation(const char *id, link_operation::Reason &reason);
// Published settings snapshot: written on the main loop, read by the HTTP task.
void service_settings(uint32_t now, bool corrections_fresh);
bool settings_snapshot(char *out, size_t capacity);
}  // namespace link_service

// Composition-root COM2 safety boundary: no transport writes receiver bytes.
bool correction_link_input(const uint8_t *frame, size_t size, uint32_t at);
void correction_output_reset();
struct CorrectionOutputStats {
  uint32_t forwarded=0, expired=0, overflow=0, waiting=0, faults=0;
  unsigned queued=0;
};
CorrectionOutputStats correction_output_stats();
