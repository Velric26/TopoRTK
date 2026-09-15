// Single definitions for the shared host doubles. host_hardware.h declares
// these extern so every compiled production translation unit (main, the
// extracted network/wifi services, the ui modules) sees the SAME state;
// per-TU static copies made service mutations invisible to the firmware tests.
#include "host_hardware.h"

uint32_t host_now = 10000;
uint32_t millis() { return host_now; }
HardwareSerial Serial;
HostWiFi WiFi;
