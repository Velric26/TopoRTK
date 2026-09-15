// Focused native regression for the shared radio_transport::Decoder framing:
// family isolation, resynchronization and counter accounting. Header-only
// production implementation; no Arduino or hardware required.
//   g++ -std=c++11 -Wall -Wextra -Werror -Isrc -Itest test/radio_framing_cases.cpp -o .pio/test_radio_framing.exe
#include "radio_transport.h"
#include "link_diagnostic_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using radio_transport::Decoder;
using radio_transport::Frame;
using radio_transport::FrameKind;

// Header fixtures only; never transmitted to physical hardware or GNSS UART.
correction::Packet make_rtm1(uint32_t session, uint32_t sequence, uint8_t version = 1, uint8_t kind = 1) {
  correction::Packet p{};
  std::memcpy(p.bytes, "RTM1", 4);
  p.bytes[4] = version; p.bytes[5] = kind; p.bytes[6] = 0; p.bytes[7] = 0;
  correction::put32(p.bytes + 8, session);
  correction::put32(p.bytes + 12, sequence);
  for (size_t i = 32; i < 40; ++i) p.bytes[i] = uint8_t(i + sequence);
  correction::seal(p);
  return p;
}

correction::Packet make_rtc1(uint8_t type) {
  correction::Packet p{};
  auto b = p.bytes;
  std::memcpy(b, "RTC1", 4);
  b[4] = 1; b[5] = type; b[6] = 1; b[7] = 0;
  correction::put32(b + 8, 123456);
  correction::put16(b + 12, 30);
  correction::put32(b + 16, 7);
  correction::seal(p);
  return p;
}

linktest::Packet make_rtdg(uint8_t kind) {
  linktest::Packet p{};
  p.kind = kind; p.node = 0; p.run = 999999;
  p.sequence = 3; p.sent = 3; p.received = 2;
  for (size_t i = 0; i < sizeof(p.payload); ++i) p.payload[i] = uint8_t(i + 3);
  linktest::seal(p);
  return p;
}

bool feed_one(Decoder &d, const uint8_t *bytes, size_t n, Frame &out) {
  for (size_t i = 0; i < n; ++i) if (d.byte(bytes[i], out)) return true;
  return false;
}

// Feeds n bytes and returns every completed frame in order.
int feed_all(Decoder &d, const uint8_t *bytes, size_t n, Frame *frames, int max_frames) {
  int count = 0;
  Frame out;
  for (size_t i = 0; i < n; ++i) if (d.byte(bytes[i], out)) {
    assert(count < max_frames);
    frames[count++] = out;
  }
  return count;
}

void expect(const char *what, const correction::Packet &sent, const Frame &got, FrameKind kind) {
  assert(got.kind == kind);
  assert(std::memcmp(got.packet.bytes, sent.bytes, correction::packet_size) == 0);
  std::printf("ok: %s\n", what);
}

int main() {
  // 1. Back-to-back families, byte-at-a-time, with kind dispatch and clean
  //    counters. Fragmentation across byte() calls retains partial frames.
  {
    Decoder d; Frame out;
    const auto rtm1 = make_rtm1(424242, 1);
    const auto rtc1 = make_rtc1(1);
    const auto rtdg = make_rtdg(linktest::Hello);
    uint8_t stream_bytes[correction::packet_size * 3];
    std::memcpy(stream_bytes + 0 * correction::packet_size, rtm1.bytes, correction::packet_size);
    std::memcpy(stream_bytes + 1 * correction::packet_size, rtc1.bytes, correction::packet_size);
    std::memcpy(stream_bytes + 2 * correction::packet_size, &rtdg, correction::packet_size);
    Frame frames[3];
    assert(feed_all(d, stream_bytes, sizeof(stream_bytes), frames, 3) == 3);
    expect("RTM1 back-to-back first", rtm1, frames[0], FrameKind::Rtcm);
    expect("RTC1 back-to-back second", rtc1, frames[1], FrameKind::PairControl);
    // RTDG body bytes are transparent to the framer; kind dispatch only.
    assert(frames[2].kind == FrameKind::Diagnostic);
    assert(std::memcmp(frames[2].packet.bytes, &rtdg, correction::packet_size) == 0);
    const auto &s = d.stats();
    assert(!s.rtcm_errors && !s.pair_errors && !s.diagnostic_errors && !s.discarded_bytes);
    // Nothing is pending: extra bytes are required for the next frame.
    uint8_t pad[255] = {};
    assert(feed_all(d, pad, sizeof(pad), frames, 3) == 0);
    d.reset();
    assert(d.stats().rtcm_errors == 0 && d.stats().discarded_bytes == 0);
    std::printf("ok: three families back-to-back, counters clean\n");
  }

  // 2. A complete RTC1 envelope embedded inside a valid RTM1 payload cannot
  //    become a second frame; markers are never scanned inside a committed
  //    candidate.
  {
    Decoder d; Frame out;
    auto rtm1 = make_rtm1(424242, 2);
    const auto rtc1 = make_rtc1(2);
    std::memcpy(rtm1.bytes + 32, rtc1.bytes, correction::header_size + 100); // marker + control body
    std::memcpy(rtm1.bytes + 140, "RTDG", 4);
    std::memcpy(rtm1.bytes + 160, "RTM1", 4);
    correction::seal(rtm1);
    assert(feed_one(d, rtm1.bytes, correction::packet_size, out));
    expect("nested RTC1/RTDG inside RTM1 payload isolated", rtm1, out, FrameKind::Rtcm);
    uint8_t tail[correction::packet_size - 1] = {};
    assert(!feed_one(d, tail, sizeof(tail), out)); // no second frame without a full next envelope
    assert(!d.stats().pair_errors && !d.stats().diagnostic_errors);
    assert(d.stats().discarded_bytes == correction::packet_size - 4); // zero tail: unknown prefixes
    std::printf("ok: marker-like payload bytes never split a frame\n");
  }

  // 3. Unknown prefix bytes are discarded one for one and a valid frame after
  //    arbitrary garbage still decodes.
  {
    Decoder d; Frame out;
    uint8_t garbage[300];
    std::memset(garbage, 'A', sizeof(garbage));
    Frame frames[1];
    assert(feed_all(d, garbage, sizeof(garbage), frames, 1) == 0);
    assert(d.stats().discarded_bytes == sizeof(garbage) - 3); // 3 bytes still pending
    const auto rtm1 = make_rtm1(424242, 3);
    assert(feed_one(d, rtm1.bytes, correction::packet_size, out));
    expect("valid RTM1 after garbage prefix", rtm1, out, FrameKind::Rtcm);
    assert(d.stats().discarded_bytes == sizeof(garbage));
    assert(!d.stats().rtcm_errors);
    std::printf("ok: unknown prefixes accounted, stream resynchronizes\n");
  }

  // 4. Bad CRC counts the correct family and resynchronizes by one byte: the
  //    following valid RTDG frame decodes, its 255 surviving tail bytes are
  //    discarded during the slide, and no diagnostic error is invented.
  {
    Decoder d; Frame out;
    auto bad = make_rtm1(424242, 4);
    bad.bytes[100] ^= 0x40;
    assert(!feed_one(d, bad.bytes, correction::packet_size, out));
    assert(d.stats().rtcm_errors == 1);
    const auto rtdg = make_rtdg(linktest::Data);
    assert(feed_one(d, reinterpret_cast<const uint8_t *>(&rtdg), sizeof(rtdg), out));
    assert(out.kind == FrameKind::Diagnostic);
    assert(std::memcmp(out.packet.bytes, &rtdg, correction::packet_size) == 0);
    assert(d.stats().rtcm_errors == 1);
    assert(!d.stats().diagnostic_errors);
    assert(d.stats().discarded_bytes == correction::packet_size - 1); // one-byte resync
    std::printf("ok: bad-CRC RTM1 resync preserves the following RTDG frame\n");
  }

  // 5. Each family counts its own bad-CRC candidates, including two corrupt
  //    envelopes of different families in a row before a good one.
  {
    Decoder d; Frame out;
    auto bad_rtc = make_rtc1(3); bad_rtc.bytes[60] ^= 1;
    auto bad_rtdg = make_rtdg(linktest::Result); reinterpret_cast<uint8_t *>(&bad_rtdg)[70] ^= 2;
    const auto rtc1 = make_rtc1(3);
    uint8_t stream_bytes[correction::packet_size * 3];
    std::memcpy(stream_bytes + 0 * correction::packet_size, bad_rtc.bytes, correction::packet_size);
    std::memcpy(stream_bytes + 1 * correction::packet_size, &bad_rtdg, correction::packet_size);
    std::memcpy(stream_bytes + 2 * correction::packet_size, rtc1.bytes, correction::packet_size);
    Frame frames[1];
    assert(feed_all(d, stream_bytes, sizeof(stream_bytes), frames, 1) == 1);
    expect("good RTC1 after two corrupt envelopes", rtc1, frames[0], FrameKind::PairControl);
    assert(d.stats().pair_errors == 1 && d.stats().diagnostic_errors == 1 && !d.stats().rtcm_errors);
    std::printf("ok: per-family bad-CRC accounting through consecutive resyncs\n");
  }

  // 6. CRC-valid envelopes are consumed whole even when the body version/kind
  //    is garbage: body policy stays with the consumer, and their embedded
  //    markers still cannot split frames.
  {
    Decoder d; Frame out;
    const auto odd = make_rtm1(424242, 5, 9, 7);
    const auto odd_rtdg = make_rtdg(uint8_t(200));
    assert(feed_one(d, odd.bytes, correction::packet_size, out));
    expect("body-invalid RTM1 delivered whole", odd, out, FrameKind::Rtcm);
    assert(feed_one(d, reinterpret_cast<const uint8_t *>(&odd_rtdg), sizeof(odd_rtdg), out));
    assert(out.kind == FrameKind::Diagnostic && out.packet.bytes[5] == 200);
    assert(!d.stats().rtcm_errors && !d.stats().diagnostic_errors);
    std::printf("ok: body validation deferred to consumers\n");
  }

  // 7. A frame split across two arbitrary feed points decodes exactly once,
  //    with no duplicate or partial delivery.
  {
    Decoder d; Frame out;
    const auto rtc1 = make_rtc1(2);
    assert(!feed_one(d, rtc1.bytes, 77, out));
    assert(!feed_one(d, rtc1.bytes + 77, 100, out));
    assert(feed_one(d, rtc1.bytes + 177, correction::packet_size - 177, out));
    expect("RTC1 fragmented across three feeds", rtc1, out, FrameKind::PairControl);
    assert(!feed_one(d, rtc1.bytes, 0, out));
    std::printf("ok: fragmented envelope retained across calls\n");
  }

  std::printf("PASS: radio framing cases\n");
  return 0;
}
