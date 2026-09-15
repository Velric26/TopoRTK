#pragma once

// Sole owner of the Wi-Fi station/AP lifecycle: mode switching, the direct
// Base AP, the local-router station, the 10 s reconnect timer and the phone
// rover AP lifecycle (implemented in rover_ap.cpp). This service never
// touches corrections, GNSS/receiver, radio sessions, survey state or UI
// timers; main composes those explicit hooks around restart().

#include <stddef.h>
#include <stdint.h>

#if __has_include(<IPAddress.h>)
#include <IPAddress.h>
#else
#include "host_hardware.h"
#endif

#include "device_config.h"

namespace network_service {

// Rebuilds the whole Wi-Fi topology from the configuration. Only the role
// and Wi-Fi mode are taken from the config; a mode/role change later calls
// this again. Stops every wifi_transport socket (consumers lazily restart
// their channels), restarts the phone rover AP for the Rover, and starts the
// Corrections UDP channel when the topology makes it immediately available.
void restart(const DeviceConfig &config, char unit, uint32_t now);

// Per-loop service: rover AP servicing, lazy Corrections UDP start once the
// station is up, and the existing 10 s station reconnect timer.
void service(uint32_t now);

bool station_connected();
bool local_router();

// SSID of the active network (never a credential). "LOCAL ROUTER" or
// "DIRECT LINK".
const char *ssid();
const char *label();

// AP address when acting as Base with the direct link, station address
// otherwise.
IPAddress address();

// True when `ip` belongs to the upstream station subnet. Phone AP clients
// are never on it, so Rover input from the phone subnet can be rejected.
bool on_station_subnet(IPAddress ip);

// Directed-broadcast address of the upstream station subnet.
IPAddress station_broadcast();

// Station RSSI in dBm, or 0 when no station link exists.
int16_t rssi();

// Stations associated with the direct AP; 0 on the local router.
unsigned clients();

}  // namespace network_service
