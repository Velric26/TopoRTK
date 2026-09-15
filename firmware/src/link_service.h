#pragma once
#include "pair_session.h"
class IPAddress;
#include <ArduinoJson.h>

// Main-loop owner of the selected production link, pair negotiation and SiK
// bridge. Hardware adapters own bytes; diagnostics may reserve UART2, but never
// own a production session. HTTP reads the published diagnostic snapshot only.
namespace link_service {
using pair_session::Transport;
void begin(bool rover, uint32_t now);
void service(uint32_t now, bool rover, bool radio_reserved);
bool select(Transport transport, bool rover, uint32_t now);
Transport selected();
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
}  // namespace link_service

// Composition-root COM2 safety boundary: no transport writes receiver bytes.
bool correction_link_input(const uint8_t *frame, size_t size, uint32_t at);
void correction_output_reset();
struct CorrectionOutputStats {
  uint32_t forwarded=0, expired=0, overflow=0, waiting=0, faults=0;
  unsigned queued=0;
};
CorrectionOutputStats correction_output_stats();
