#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "correction_transport.h"

// Sole owner of the SiK UART2 stream and its wire framing. The Decoder is a
// portable, allocation-free fixed-window state machine; the hardware functions
// are ESP32-only and are the only callers of HardwareSerial APIs for UART2.
// No policy, fault decisions, NVS or logging lives here: consumers log frames
// and decide what a frame means.
namespace radio_transport {

enum class FrameKind : uint8_t { Rtcm, PairControl, Diagnostic };

struct Frame {
  correction::Packet packet{};
  FrameKind kind = FrameKind::Rtcm;
};

struct DecodeStats {
  uint32_t rtcm_errors = 0;
  uint32_t pair_errors = 0;
  uint32_t diagnostic_errors = 0;
  uint32_t discarded_bytes = 0;
};

// One fixed 256-byte sliding window for RTM1, RTC1 and RTDG. All three
// families carry a trailing CRC32 over bytes 0..251, validated through the
// shared correction::crc32 helper (no parallel CRC implementation). A window
// commits to a family as soon as its first four bytes name one, so a
// marker-like sequence inside a payload can never start a second frame:
// CRC-valid envelopes are consumed whole even when the consumer later rejects
// their body version/kind. A full window whose CRC fails resynchronizes by
// one byte and counts its claimed family; a full window whose leading bytes no
// longer name a family (mid-resync) and any other unrecognized prefix
// increment discarded_bytes one byte at a time. Body policy stays in
// consumers.
class Decoder {
 public:
  void reset() { used_ = 0; stats_ = DecodeStats{}; }
  bool byte(uint8_t value, Frame &out) {
    pending_.bytes[used_++] = value;
    FrameKind kind;
    if (used_ == 4 && !family(kind)) { resync(); ++stats_.discarded_bytes; return false; }
    if (used_ != correction::packet_size) return false;
    if (!family(kind)) { resync(); ++stats_.discarded_bytes; return false; }
    if (correction::u32(pending_.bytes + 252) != correction::crc32(pending_.bytes, 252)) {
      switch (kind) {
        case FrameKind::Rtcm: ++stats_.rtcm_errors; break;
        case FrameKind::PairControl: ++stats_.pair_errors; break;
        case FrameKind::Diagnostic: ++stats_.diagnostic_errors; break;
      }
      resync();
      return false;
    }
    out.packet = pending_;
    out.kind = kind;
    used_ = 0;
    return true;
  }
  const DecodeStats &stats() const { return stats_; }

 private:
  bool family(FrameKind &kind) const {
    if (!std::memcmp(pending_.bytes, "RTM1", 4)) { kind = FrameKind::Rtcm; return true; }
    if (!std::memcmp(pending_.bytes, "RTC1", 4)) { kind = FrameKind::PairControl; return true; }
    if (!std::memcmp(pending_.bytes, "RTDG", 4)) { kind = FrameKind::Diagnostic; return true; }
    return false;
  }
  void resync() { std::memmove(pending_.bytes, pending_.bytes + 1, --used_); }
  correction::Packet pending_{};
  size_t used_ = 0;
  DecodeStats stats_{};
};

struct UartStats {
  bool observed = false;
  uint32_t fifo = 0, buffer = 0, frame = 0, parity = 0, brk = 0;
  uint32_t rx_bytes = 0, tx_bytes = 0, rx_peak = 0, service_gap = 0, tx_wait = 0, short_writes = 0;
};

// UART2 lifecycle: 57600 8N1 on RX18/TX17 with 4096-byte RX and 1024-byte TX
// buffers, plus the receive-error counters. Only this module constructs UART2.
void begin();
bool started();

// Feeds at most byte_budget buffered UART bytes into the Decoder. Returns true
// with exactly one complete frame, false when the budget or the hardware input
// is exhausted. Partial frames are retained across calls; byte_budget is
// decreased by the bytes actually consumed. Counted toward rx_bytes while an
// observation is active.
bool receive(Frame &out, size_t &byte_budget);

// Returns -1 on TX backpressure (insufficient complete capacity before the
// write), otherwise the actual written count, which a caller may compare
// against the requested size to detect short writes. Counted toward tx_wait /
// tx_bytes / short_writes while an observation is active.
int send(const uint8_t *data, size_t size);

// Restarts frame assembly and zeroes the Decoder counters. Buffered hardware
// bytes are not touched.
void reset_rx();

// Owner handoff: drops the bytes currently buffered by the hardware and the
// parser state without waiting for future bytes.
void discard_input();

// Raw modem probe byte for the ATI dialog; -1 when no byte is buffered.
int read_probe_byte();

// Paired-run UART observation. Byte, work and error counters only move between
// observe_start and observe_stop; the receive-error callback counts under a
// port critical section. observe_tick samples the RX backlog, so call it once
// per service turn before receive() to mirror the previous sampling point.
void observe_start(uint32_t now);
void observe_tick(uint32_t now);
void observe_stop();
UartStats observation();
DecodeStats decode_stats();

static_assert(sizeof(Decoder) <= 320, "Decoder memory budget");
}  // namespace radio_transport
