#pragma once
#include <cstdint>

// Main-loop API. Credentials are available only to the local touchscreen.
void start_rover_ap(char unit);
void stop_rover_ap();
void service_rover_ap();
bool rotate_rover_ap_password();
bool rover_ap_ready();
const char *rover_ap_ssid();
const char *rover_ap_password();
const char *rover_ap_address();
const char *rover_ap_error();
uint8_t rover_ap_clients();
