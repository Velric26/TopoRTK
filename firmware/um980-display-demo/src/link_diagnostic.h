#pragma once
#include <cstddef>
#include <cstdint>
class IPAddress;
constexpr size_t kDiagnosticCapacity=6144;
void diagnostic_begin();
void diagnostic_service(uint32_t now,bool rover,IPAddress peer,bool profile_busy);
bool diagnostic_busy();
bool diagnostic_request(const char *json);
bool diagnostic_snapshot(char *out,size_t capacity);
// Main-loop-only live correction adapter. The diagnostic owns UART2 and
// excludes tests/probes while a live session is selected.
bool correction_radio_active();
bool correction_radio_linked(uint32_t now);
bool correction_radio_submit(const uint8_t *frame,size_t size,uint32_t now);
void correction_radio_stop();
// Implemented by main.cpp: copied into the bounded COM2 queue, never direct UART.
bool correction_radio_input(const uint8_t *frame,size_t size,uint32_t at);
void correction_output_reset();
struct CorrectionOutputStats {uint32_t forwarded=0,expired=0,overflow=0,waiting=0,faults=0;unsigned queued=0;};
CorrectionOutputStats correction_output_stats();
