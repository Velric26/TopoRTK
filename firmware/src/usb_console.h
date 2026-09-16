#pragma once
// USB (UART0) console (R10a slice 5), extracted from main.cpp. Sole owner of
// the command surface: the allowlisted verbs, the help text, their wording and
// their refusals, including the receiver-profile-in-progress refusal and the
// base-role gate on the RTCM test verbs.
//
// It consumes published snapshots and issues typed requests only: the receiver,
// settings, correction, diagnostic, phone-AP, network and backlight owners are
// read through their own snapshots, and a verb that changes something calls the
// owner's request. It holds no NVS handle, writes no UART1/COM2 byte and
// mutates no transport itself - a console verb can only ask.
//
// The facts it prints but cannot read - the applied role, the Wi-Fi direct peer
// and its packet counters, and the brightness label the LCD also shows - arrive
// as one value copy the composition root builds, which keeps their single
// definition in the root.

#include <cstdint>

#if __has_include(<IPAddress.h>)
#include <IPAddress.h>
#else
#include "host_hardware.h"
#endif

// Composition-root facts the console reports. A value copy: the console never
// reads the root's Wi-Fi, role or brightness state itself.
struct ConsoleInputs {
  bool base = false;      // the applied role (the root's is_base)
  bool peer_known = false;  // a Wi-Fi direct peer has been heard this boot
  IPAddress peer;
  uint32_t rtcm_wifi_tx_frames = 0;
  uint32_t rtcm_wifi_rx_frames = 0;
  uint32_t invalid_packets = 0;
  const char *brightness_label = "AUTO NO GPS";  // the root's brightness text
};

// The root builds one copy when it is needed: only for a complete command line,
// so the poll path costs nothing while nothing is typed. `service` calls it.
using ConsoleFacts = ConsoleInputs (*)();

namespace usb_console {

// The safe command list, printed at boot and by `help`.
void print_help();

// One command line, exactly as the console parsed it.
void handle(const char *command, const ConsoleInputs &in);

// Drains UART0 and runs every complete line through `handle`.
void service(ConsoleFacts facts);

}  // namespace usb_console
