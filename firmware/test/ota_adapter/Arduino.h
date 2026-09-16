#pragma once
// Platform-header adapter for <Arduino.h>. run_update_tests.py compiles the
// unmodified src/ota_service.cpp, so its platform includes resolve to a header
// like this one and forward to this harness's OTA double. Nothing else is
// declared here; <ArduinoJson.h> is the real library and is not adapted.
#include "../ota_hardware.h"
