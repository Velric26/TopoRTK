#include "radio_transport.h"

#include <HardwareSerial.h>

#include <algorithm>

namespace {

HardwareSerial radio_uart(2);
bool uart_started = false;
radio_transport::Decoder stream;

struct UartErrors { uint32_t fifo = 0, buffer = 0, frame = 0, parity = 0, brk = 0; };
portMUX_TYPE uart_guard = portMUX_INITIALIZER_UNLOCKED;
UartErrors uart_errors;
bool monitoring = false;
radio_transport::UartStats work;
uint32_t last_service = 0;

void radio_error(hardwareSerial_error_t error) {
  // HardwareSerial event task: counters only, no UART reads, JSON or logging.
  portENTER_CRITICAL(&uart_guard);
  if (monitoring) switch (error) {
    case UART_FIFO_OVF_ERROR: ++uart_errors.fifo; break;
    case UART_BUFFER_FULL_ERROR: ++uart_errors.buffer; break;
    case UART_FRAME_ERROR: ++uart_errors.frame; break;
    case UART_PARITY_ERROR: ++uart_errors.parity; break;
    case UART_BREAK_ERROR: ++uart_errors.brk; break;
    default: break;
  }
  portEXIT_CRITICAL(&uart_guard);
}

}  // namespace

namespace radio_transport {

void begin() {
  if (uart_started) return;
  radio_uart.setRxBufferSize(4096);
  radio_uart.setTxBufferSize(1024);
  radio_uart.onReceiveError(radio_error);
  radio_uart.begin(57600, SERIAL_8N1, 18, 17);
  uart_started = true;
}

bool started() { return uart_started; }

bool receive(Frame &out, size_t &byte_budget) {
  if (!uart_started) return false;
  const bool obs = monitoring;
  size_t consumed = 0;
  while (consumed < byte_budget && radio_uart.available()) {
    const uint8_t value = uint8_t(radio_uart.read());
    ++consumed;
    if (obs) ++work.rx_bytes;
    if (stream.byte(value, out)) { byte_budget -= consumed; return true; }
  }
  byte_budget -= consumed;
  return false;
}

int send(const uint8_t *data, size_t size) {
  if (!uart_started || !data) return -1;
  if (!size) return 0;
  const bool obs = monitoring;
  if (int(radio_uart.availableForWrite()) < int(size)) { if (obs) ++work.tx_wait; return -1; }
  const size_t written = radio_uart.write(data, size);
  if (obs) { work.tx_bytes += uint32_t(written); if (written != size) ++work.short_writes; }
  return int(written);
}

void reset_rx() { stream.reset(); }

void discard_input() {
  // Bounded to the bytes already buffered by the driver; never waits for
  // future input.
  while (radio_uart.available()) radio_uart.read();
  stream.reset();
}

int read_probe_byte() { return uart_started ? radio_uart.read() : -1; }

void observe_start(uint32_t now) {
  work = UartStats{};
  work.observed = true;
  last_service = now;
  portENTER_CRITICAL(&uart_guard);
  uart_errors = UartErrors{};
  monitoring = true;
  portEXIT_CRITICAL(&uart_guard);
}

void observe_tick(uint32_t now) {
  if (!monitoring) return;
  work.service_gap = std::max(work.service_gap, now - last_service);
  last_service = now;
  work.rx_peak = std::max(work.rx_peak, uint32_t(std::max(0, int(radio_uart.available()))));
}

void observe_stop() {
  portENTER_CRITICAL(&uart_guard);
  monitoring = false;
  portEXIT_CRITICAL(&uart_guard);
}

UartStats observation() {
  UartErrors e;
  portENTER_CRITICAL(&uart_guard);
  e = uart_errors;
  portEXIT_CRITICAL(&uart_guard);
  UartStats s = work;
  s.fifo = e.fifo; s.buffer = e.buffer; s.frame = e.frame;
  s.parity = e.parity; s.brk = e.brk;
  return s;
}

DecodeStats decode_stats() { return stream.stats(); }

}  // namespace radio_transport
