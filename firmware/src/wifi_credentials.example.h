#pragma once

// LOCAL ONLY: copy this file to wifi_credentials.h, then replace both values.
// Never commit wifi_credentials.h. Use a 2.4 GHz WPA2/WPA3-compatible network.
// These credentials will be used by the planned local-router (STA) mode; they
// are intentionally not used by the current validated Base-AP/Rover-STA test.
namespace toportk::wifi_credentials {
constexpr char kLocalSsid[] = "REPLACE_WITH_2_4_GHZ_SSID";
constexpr char kLocalPassword[] = "REPLACE_WITH_WIFI_PASSWORD";
}  // namespace toportk::wifi_credentials
