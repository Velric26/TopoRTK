#include "network_service.h"

#include <Arduino.h>
#include <WiFi.h>

#include "rover_ap.h"
#include "wifi_credentials.h"
#include "wifi_transport.h"

namespace network_service {
namespace {

// Direct instrument-to-instrument AP identity and geometry, unchanged from
// the validated bridge. Never logged alongside or instead of a password.
constexpr char kDirectSsid[] = "TopoRTK-Link-Test";
constexpr char kDirectPassword[] = "TopoRTK-test-2026";
constexpr uint8_t kDirectChannel = 6;
constexpr uint8_t kDirectMaxClients = 1;
constexpr uint32_t kReconnectIntervalMs = 10000;

DeviceRole role = DeviceRole::kRover;
WiFiMode wifi_mode = WiFiMode::kDirect;
uint32_t last_connect_attempt_ms = 0;

bool is_base() { return role == DeviceRole::kBase; }

const char *role_label() { return is_base() ? "BASE" : "ROVER"; }

const char *active_wifi_ssid() {
  return local_router() ? toportk::wifi_credentials::kLocalSsid : kDirectSsid;
}

const char *active_wifi_password() {
  // Credentials are passed to the driver only; they are never logged.
  return local_router() ? toportk::wifi_credentials::kLocalPassword
                        : kDirectPassword;
}

}  // namespace

void restart(const DeviceConfig &config, char unit, uint32_t now) {
  // Config snapshot is deliberately role/network only.
  role = config.role;
  wifi_mode = config.wifi_mode;
  last_connect_attempt_ms = now;

  wifi_transport::stop_all();
  stop_rover_ap();
  WiFi.mode(WIFI_OFF);
  WiFi.persistent(false);
  WiFi.setSleep(false);

  if (!local_router() && is_base()) {
    WiFi.mode(WIFI_AP);
    const IPAddress address(192, 168, 4, 1);
    const IPAddress gateway(192, 168, 4, 1);
    const IPAddress subnet(255, 255, 255, 0);
    const bool configured = WiFi.softAPConfig(address, gateway, subnet);
    const bool started =
        WiFi.softAP(kDirectSsid, kDirectPassword, kDirectChannel, false,
                    kDirectMaxClients);
    if (started) wifi_transport::start(wifi_transport::Channel::Corrections);
    Serial.printf("WIFI BASE: AP config=%s start=%s UDP=%s IP=%s\n",
                  configured ? "PASS" : "FAIL", started ? "PASS" : "FAIL",
                  wifi_transport::started(wifi_transport::Channel::Corrections)
                      ? "PASS"
                      : "FAIL",
                  WiFi.softAPIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(active_wifi_ssid(), active_wifi_password());
    if (!is_base()) start_rover_ap(unit);
    Serial.printf("WIFI %s: %s connecting to %s\n", role_label(), label(),
                  active_wifi_ssid());
  }
}

void service(uint32_t now) {
  service_rover_ap();

  if (station_connected() &&
      !wifi_transport::started(wifi_transport::Channel::Corrections)) {
    const bool started =
        wifi_transport::start(wifi_transport::Channel::Corrections);
    Serial.printf("WIFI %s: %s connected UDP=%s IP=%s RSSI=%d dBm\n",
                  role_label(), label(), started ? "PASS" : "FAIL",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }

  const bool needs_station = local_router() || !is_base();
  if (needs_station && !station_connected() &&
      now - last_connect_attempt_ms >= kReconnectIntervalMs) {
    last_connect_attempt_ms = now;
    WiFi.disconnect();
    WiFi.begin(active_wifi_ssid(), active_wifi_password());
    Serial.printf("WIFI %s: %s retrying connection\n", role_label(), label());
  }
}

bool station_connected() { return WiFi.status() == WL_CONNECTED; }

bool local_router() { return wifi_mode == WiFiMode::kLocalRouter; }

const char *ssid() { return active_wifi_ssid(); }

const char *label() { return local_router() ? "LOCAL ROUTER" : "DIRECT LINK"; }

IPAddress address() {
  return is_base() && !local_router() ? WiFi.softAPIP() : WiFi.localIP();
}

bool on_station_subnet(IPAddress ip) {
  if (!station_connected()) return false;
  const IPAddress local = WiFi.localIP(), mask = WiFi.subnetMask();
  for (int i = 0; i < 4; ++i) {
    if ((ip[i] & mask[i]) != (local[i] & mask[i])) return false;
  }
  return true;
}

IPAddress station_broadcast() {
  const IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
  return IPAddress(ip[0] | uint8_t(~mask[0]), ip[1] | uint8_t(~mask[1]),
                   ip[2] | uint8_t(~mask[2]), ip[3] | uint8_t(~mask[3]));
}

int16_t rssi() { return station_connected() ? WiFi.RSSI() : 0; }

unsigned clients() {
  return local_router() ? 0U : WiFi.softAPgetStationNum();
}

}  // namespace network_service
