// Correction output service (R10a slice 3): the bounded COM2 queue, the station
// guard, the observation health and the output counters moved out of main.cpp.
// Bodies are unchanged except that `millis()` became the caller's `now_ms`, the
// removed globals became file-local state, and the selected role arrives as the
// installed gate. The output path still writes through the sole UART1 owner.
#include "correction_service.h"

#include "correction_queue.h"
#include "correction_transport.h"
#include "debug_service.h"
#include "gnss_service.h"
#include "link_diagnostic.h"
#include "link_service.h"
#include "ota_service.h"

namespace correction_service {
namespace {

const CorrectionGate *gate = nullptr;

correction::Health health_state;
correction::BurstQueue output_queue;
correction::StationGuard station_guard;
uint32_t forwarded_frames = 0, forward_waits = 0, forward_faults = 0;
uint32_t bytes_forwarded = 0;
bool output_fault = false;

// Admission policy shared by both requests: the selected role, the verified
// receiver profile, a reserved diagnostic port, an admitted OTA pause, a
// latched output fault and the selected link's connectivity. Unchanged from
// main.cpp's queue_correction.
bool blocked(uint32_t now_ms) {
  return gate->role_is_base() || !gnss_service::snapshot().profile_applied ||
         diagnostic_busy() || ota_paused() || output_fault ||
         !link_service::connected(now_ms);
}

}  // namespace

void begin(const CorrectionGate &installed) { gate = &installed; }

bool admit(const uint8_t *frame, size_t size, uint32_t at, uint32_t now_ms) {
  if (blocked(now_ms)) return false;
  if (!station_guard.accept(frame, size)) return false;
  return output_queue.enqueue(frame, size, at, now_ms);
}

void service_output(uint32_t now_ms) {
  if (blocked(now_ms)) {
    output_queue.clear();
    return;
  }
  const auto *frame = output_queue.front(now_ms);
  if (!frame) return;
  // The receiver owner reports UART buffer admission, not receiver
  // acknowledgement; a zero write means the TX ring cannot hold the whole frame.
  const size_t written =
      gnss_service::write_frame(frame->data, frame->size, now_ms);
  if (!written) {
    ++forward_waits;
    return;
  }
  debug_frame(debugmode::Channel::GnssTx, frame->data, written);
  if (written != frame->size) {
    ++forward_faults;
    output_fault = true;
    health_state.reset();
    output_queue.clear();
    return;
  }
  ++forwarded_frames;
  bytes_forwarded += written;
  health_state.observe(frame->data, frame->size, frame->at);
  output_queue.pop(frame);
}

void reset() {
  health_state.reset();
  output_queue.clear();
  station_guard.reset();
  gnss_service::reset_reference();
  output_fault = false;
}

CorrectionSnapshot snapshot() {
  CorrectionSnapshot out;
  out.forwarded = forwarded_frames;
  out.forwarded_bytes = bytes_forwarded;
  out.expired = output_queue.expired;
  out.overflow = output_queue.overflow;
  out.waiting = forward_waits;
  out.faults = forward_faults;
  out.queued = output_queue.size();
  out.fault = output_fault;
  out.station = station_guard.station();
  return out;
}

correction::Health &health() { return health_state; }

}  // namespace correction_service
