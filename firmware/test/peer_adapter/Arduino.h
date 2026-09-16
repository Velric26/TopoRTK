#pragma once
// Platform-header adapter for <Arduino.h>. run_update_tests.py includes the
// unmodified src/peer_update.cpp twice, once per simulated unit, so its
// platform includes resolve to this header and forward to this harness's
// peer double. Nothing else is declared here.
#include "../peer_hardware.h"
