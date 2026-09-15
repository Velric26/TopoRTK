#pragma once

// Single owner of every production WiFiUDP socket. Corrections (22345),
// diagnostics (22346) and peer-update (22347) datagram channels are bound,
// read and written here and nowhere else. Channel policy (which sender to
// trust, what the payload means, when a channel is needed) stays with the
// consumers; this module only moves complete datagrams.

#if __has_include(<IPAddress.h>)
#include <IPAddress.h>
#else
#include "host_hardware.h"
#endif

namespace wifi_transport {


enum class Channel : uint8_t { Corrections, Diagnostics, Peer };

// Binds the channel's UDP port. Returns false when the socket could not be
// opened; a stopped channel may be started again at any time by its owning
// consumer, independent of radio or station state.
bool start(Channel channel);

bool started(Channel channel);

// Releases every socket and clears all ownership flags. Called by
// network_service on Wi-Fi restart; consumers lazily start again afterwards.
void stop_all();

// Receives at most one datagram per call. Returns 0 when no datagram is
// pending, -1 when the datagram was discarded (larger than `capacity` or not
// fully readable), or the complete datagram size when it was copied into
// `out`. The sender address is captured before any read. An oversized or
// partially read datagram is fully drained with a small fixed scratch buffer
// so no truncated bytes are ever exposed as success.
int receive(Channel channel, uint8_t *out, size_t capacity,
            IPAddress &sender);

// Sends one whole datagram. Success requires the complete payload to be
// accepted into the packet and endPacket to report a send; there is no
// partial-datagram success.
bool send(Channel channel, IPAddress destination, const uint8_t *data,
          size_t size);

}  // namespace wifi_transport
