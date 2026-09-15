#pragma once
// Correction output service (R10a slice 3), extracted from main.cpp. Sole owner
// of the Rover correction path: the bounded COM2 burst queue, the station guard
// that latches a reference, the observation health and every output counter,
// together with the admission policy that decides whether a frame from the
// selected production medium may enter them.
//
// It reuses the portable cores unchanged (`correction_queue.h` for the bounded
// queue and the station guard, `correction_health.h` for observation health,
// `correction_transport.h` for the wire format) and never drives a serial port
// itself: every receiver byte leaves through the sole UART1 owner
// (`gnss_service::write_frame`), so COM2 keeps one writer and one framing
// implementation.
//
// The composition root keeps the loop order and asks for work through typed
// requests. It also keeps the decision of which transport carries a Base frame:
// the Wi-Fi/radio envelopes bind pair-session identity, not the COM2 output
// path, so they stay with the transport owner. Only frames the selected
// production session delivered are admitted; no selftest or synthetic data
// path feeds the COM2 queue.

#include <cstddef>
#include <cstdint>
#include "correction_health.h"

// The one admission fact the service cannot read for itself: the selected role
// lives in the composition root's device config, exactly as the receiver
// owner receives it. Every other gate reads the service that publishes it
// (verified receiver profile, diagnostics, OTA pause, selected link).
struct CorrectionGate {
  bool (*role_is_base)();
};

// Facts today's consumers read: the status/LCD surfaces, the web status JSON,
// the CSV writers, the dashboard and the link service's diagnostics. A value
// copy: no pointer into service state and no mutation path back into it.
struct CorrectionSnapshot {
  uint32_t forwarded = 0;        // whole frames written to COM2
  uint32_t forwarded_bytes = 0;  // bytes written (console, CSV, LCD, web)
  uint32_t expired = 0;          // queue entries dropped by the age limit
  uint32_t overflow = 0;         // observation slots taken from the oldest
  uint32_t waiting = 0;          // polls that found the TX ring unable to take a frame
  uint32_t faults = 0;           // short writes that latched the output fault
  unsigned queued = 0;           // frames waiting in the queue
  bool fault = false;            // latched: a short write stopped the output
  int station = -1;              // latched reference station, -1 = none yet
};

namespace correction_service {

// Installs the gate. The composition root supplies it before the first request.
void begin(const CorrectionGate &gate);

// Typed requests. `at` is when the frame reached the instrument and `now_ms` the
// caller's evaluation time; both carry the values main.cpp used to read from
// `millis()`. Admission refuses the frame without queueing it when any gate is
// closed or the station guard rejects it.
bool admit(const uint8_t *frame, size_t size, uint32_t at, uint32_t now_ms);

// Writes at most one queued frame to COM2: a zero write is backpressure and is
// retried, a short write latches the output fault and drops the queue. A closed
// gate discards whatever is queued.
void service_output(uint32_t now_ms);

// Session/role/config change: drops the queue, the station latch, the
// observation health and the receiver's reference age, and clears the latched
// fault. Output counters survive, as they always have.
void reset();

CorrectionSnapshot snapshot();

// The observation state itself, which instrument_status binds to the receiver's
// solution. The service is its only production writer; the host suite reads the
// same instance the composition root used to own.
correction::Health &health();

}  // namespace correction_service
