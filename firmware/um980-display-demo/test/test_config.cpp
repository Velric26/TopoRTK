#include "../src/device_config.h"
#include "../src/receiver_reply.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
  assert(valid_receiver_reply("$command,UNLOG COM2,response: OK*52"));
  assert(valid_receiver_reply("$command,MODE,response: OK*5D"));
  assert(valid_receiver_reply("#MODE,98,GPS,UNKNOWN,1,219000,0,0,18,955;MODE ROVER SURVEY,*5C"));
  assert(!valid_receiver_reply("$command,MODE,response: OK*79"));
  assert(!valid_receiver_reply("$command,MODE,response: OK*5Dextra"));
  assert(!valid_receiver_reply("$command,MODE,response: OK"));
  for (unsigned role = 0; role < 2; ++role) {
    for (unsigned brightness = 0; brightness < 3; ++brightness) {
      for (bool rtcm : {false, true}) {
        for (unsigned wifi = 0; wifi < 2; ++wifi) {
          DeviceConfig config;
          config.role = static_cast<DeviceRole>(role);
          config.brightness = static_cast<BrightnessMode>(brightness);
          config.base_rtcm = rtcm;
          config.wifi_mode = static_cast<WiFiMode>(wifi);
          DeviceConfig restored;
          assert(decode_config(encode_config(config), restored));
          assert(encode_config(restored) == encode_config(config));
          // Switching away from base must clear all prior COM2 outputs first.
          assert(std::strcmp(profile_command(restored, 0), "UNLOG COM2") == 0);
          assert(std::strcmp(profile_command(restored, 1), role ? "MODE BASE" : "MODE ROVER SURVEY") == 0);
          unsigned rtcm_count = 0;
          unsigned step = 0;
          for (; profile_command(restored, step); ++step) {
            assert(step < 12);
            if (std::strncmp(profile_command(restored, step), "RTCM", 4) == 0) ++rtcm_count;
          }
          assert(rtcm_count == (role && rtcm ? 6U : 0U));
          assert(std::strcmp(profile_command(restored, step - 1), "MODE") == 0);
        }
      }
    }
  }
  DeviceConfig unchanged;
  const uint32_t original = encode_config(unchanged);
  DeviceConfig v1;
  assert(decode_config(0x5452010DU, v1));
  assert(v1.wifi_mode == WiFiMode::kDirect);
  for (uint32_t invalid : {0U, 0xFFFFFFFFU, 0x54520206U, 0x54520220U, 0x54520110U}) {
    assert(!decode_config(invalid, unchanged));
    assert(encode_config(unchanged) == original);
  }
  std::cout << "PASS: configuration round trips, invalid storage, role/RTCM profiles\n";
}
