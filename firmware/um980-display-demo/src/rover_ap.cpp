#include "rover_ap.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <cstring>
#include <cstdio>

namespace {
Preferences storage;
bool opened = false, enabled = false, ready = false;
char ssid[32] = {}, password[17] = {}, address[20] = "192.168.8.1";
const char *failure = "";
uint32_t last_service = 0;
bool valid(const char *key) {
  if (std::strlen(key) != 9 || key[4] != '.') return false;
  for (int i=0; i<9; ++i) if (i != 4 && (key[i] < '0' || key[i] > '9')) return false;
  return true;
}
bool legacy_key(const char *key) {
  if (std::strlen(key) != 16) return false;
  for (int i=0; i<16; ++i) if (!std::strchr("ABCDEFGHJKLMNPQRSTUVWXYZ23456789",key[i])) return false;
  return true;
}
bool save_new_password() {
  if (!opened) { failure = "Password storage unavailable"; return false; }
  char candidate[17] = {}, verify[17] = {};
  // Called only after Wi-Fi starts, so the hardware RNG has RF entropy.
  for (int i=0; i<9; ++i) {
    if (i==4) { candidate[i]='.'; continue; }
    uint8_t digit;
    do { esp_fill_random(&digit,1); } while (digit >= 250);
    candidate[i] = '0' + digit%10;
  }
  if (storage.putString("password",candidate) != 9 ||
      storage.getString("password",verify,sizeof(verify)) != 10 ||
      std::strcmp(candidate,verify) != 0) {
    failure = "Password save failed";
    return false;
  }
  std::memcpy(password,candidate,sizeof(password));
  failure = "";
  return true;
}
uint32_t number(IPAddress ip) {
  return (uint32_t(ip[0])<<24) | (uint32_t(ip[1])<<16) | (uint32_t(ip[2])<<8) | ip[3];
}
bool overlaps_station(IPAddress ap) {
  if (WiFi.status() != WL_CONNECTED) return false;
  const uint32_t sta=number(WiFi.localIP()), mask=number(WiFi.subnetMask()), candidate=number(ap);
  return (sta & mask) == (candidate & mask) || (sta & 0xffffff00U) == (candidate & 0xffffff00U);
}
IPAddress choose_address() {
  const IPAddress first(192,168,8,1), second(172,22,42,1);
  return overlaps_station(first) ? second : first;
}
bool begin_ap() {
  ready = false;
  const IPAddress ip = choose_address();
  if (overlaps_station(ip)) { failure = "Phone network address conflict"; return false; }
  std::snprintf(address,sizeof(address),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);
  // softAP adds AP to the running STA; it never disconnects the correction STA.
  ready = WiFi.softAP(ssid,password,6,false,3) &&
          WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0));
  failure = ready ? "" : "Phone Wi-Fi start failed";
  if (!ready) WiFi.enableAP(false);
  Serial.printf("PHONE WIFI: %s SSID=%s IP=%s (key on touchscreen only)\n",
                ready ? "READY" : "FAILED",ssid,address);
  return ready;
}
}

void start_rover_ap(char unit) {
  enabled = true; ready = false;
  uint8_t mac[6] = {}; WiFi.macAddress(mac);
  std::snprintf(ssid,sizeof(ssid),"TopoRTK-Rover-%c-%02X%02X",unit,mac[4],mac[5]);
  if (!opened) opened = storage.begin("topoap",false);
  password[0] = '\0';
  if (!opened) { failure = "Password storage unavailable"; return; }
  if (storage.isKey("password")) {
    const size_t length = storage.getString("password",password,sizeof(password));
    if (length==17 && legacy_key(password)) {
      // One-time migration requested for the first phone-Wi-Fi prototype.
      password[0]='\0';
      if (!save_new_password()) return;
    } else if (length != 10 || !valid(password)) {
      password[0] = '\0'; failure = "Stored key invalid; tap New key"; return;
    }
  } else if (!save_new_password()) return;
  begin_ap();
}
void stop_rover_ap() {
  if (enabled) WiFi.enableAP(false);
  enabled = ready = false;
  password[0] = '\0'; failure = "";
}
void service_rover_ap() {
  if (!enabled || !valid(password) || millis()-last_service < 1000) return;
  last_service = millis();
  if (ready && overlaps_station(WiFi.softAPIP())) {
    WiFi.enableAP(false); ready = false;
  }
  if (!ready) begin_ap();
}
bool rotate_rover_ap_password() {
  if (!enabled) return false;
  if (!opened) opened = storage.begin("topoap",false);
  if (!save_new_password()) return false;
  // Disconnect phone clients, preserving station/UDP/receiver state.
  WiFi.enableAP(false);
  return begin_ap();
}
bool rover_ap_ready() { return ready; }
const char *rover_ap_ssid() { return ssid; }
const char *rover_ap_password() { return password; }
const char *rover_ap_address() { return address; }
const char *rover_ap_error() { return failure; }
uint8_t rover_ap_clients() { return ready ? WiFi.softAPgetStationNum() : 0; }
