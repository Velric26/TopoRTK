#include "wifi_transport.h"

#include <WiFiUdp.h>

namespace wifi_transport {
namespace {

// Original production ports, unchanged.
constexpr uint16_t kCorrectionsPort = 22345;
constexpr uint16_t kDiagnosticsPort = 22346;
constexpr uint16_t kPeerPort = 22347;

struct SocketState {
  WiFiUDP udp;
  uint16_t port = 0;
  bool started = false;
};

SocketState corrections;
SocketState diagnostics;
SocketState peer_update;

SocketState &socket_for(Channel channel) {
  switch (channel) {
    case Channel::Diagnostics:
      return diagnostics;
    case Channel::Peer:
      return peer_update;
    case Channel::Corrections:
    default:
      return corrections;
  }
}

uint16_t port_for(Channel channel) {
  switch (channel) {
    case Channel::Diagnostics:
      return kDiagnosticsPort;
    case Channel::Peer:
      return kPeerPort;
    case Channel::Corrections:
    default:
      return kCorrectionsPort;
  }
}

// Fully drains whatever remains of the current datagram so the next
// parsePacket() starts on a fresh one.
void drain_current(SocketState &socket) {
  uint8_t scratch[64];
  while (socket.udp.available() > 0) {
    const int drained = socket.udp.read(scratch, sizeof(scratch));
    if (drained <= 0) break;
  }
}

}  // namespace

bool start(Channel channel) {
  SocketState &socket = socket_for(channel);
  if (socket.started) return true;
  if (socket.port == 0) socket.port = port_for(channel);
  socket.started = socket.udp.begin(socket.port);
  return socket.started;
}

bool started(Channel channel) { return socket_for(channel).started; }

void stop_all() {
  SocketState *const sockets[] = {&corrections, &diagnostics, &peer_update};
  for (SocketState *socket : sockets) {
    if (socket->started) socket->udp.stop();
    socket->started = false;
  }
}

int receive(Channel channel, uint8_t *out, size_t capacity,
            IPAddress &sender) {
  SocketState &socket = socket_for(channel);
  if (!socket.started || out == nullptr) return 0;

  const int size = socket.udp.parsePacket();
  if (size <= 0) return 0;

  // Capture the sender before touching the payload.
  sender = socket.udp.remoteIP();

  if (static_cast<size_t>(size) > capacity) {
    drain_current(socket);
    return -1;
  }

  const int bytes_read = socket.udp.read(out, size);
  if (bytes_read != size) {
    drain_current(socket);
    return -1;
  }
  return bytes_read;
}

bool send(Channel channel, IPAddress destination, const uint8_t *data,
          size_t size) {
  SocketState &socket = socket_for(channel);
  if (!socket.started || data == nullptr || size == 0) return false;

  if (!socket.udp.beginPacket(destination, socket.port)) return false;
  const size_t written = socket.udp.write(data, size);
  return written == size && socket.udp.endPacket() == 1;
}

}  // namespace wifi_transport
