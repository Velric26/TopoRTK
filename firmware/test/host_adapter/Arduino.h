#pragma once
// Platform-header adapter for <Arduino.h>. run_host_tests.py compiles the
// unmodified src/*.cpp, so their platform includes resolve to a header like
// this one and forward to this harness's single hardware double. Nothing else
// is declared here.
#include "../host_hardware.h"
