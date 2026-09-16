#pragma once
// Platform-header adapter for <Arduino_GFX_Library.h>. This directory precedes
// the PlatformIO GFX library on the include path, so the drawing calls reach the
// double's pixel buffer in test/host_hardware.h rather than the real panel
// driver.
#include "../host_hardware.h"
