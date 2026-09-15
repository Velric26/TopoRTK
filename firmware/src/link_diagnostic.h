#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
class IPAddress;
constexpr size_t kDiagnosticCapacity=6144;
void diagnostic_begin();
void diagnostic_service(uint32_t now,bool rover,IPAddress peer,bool profile_busy);
bool diagnostic_busy();
bool diagnostic_request(const char *json);
bool diagnostic_snapshot(char *out,size_t capacity);
// Latest stored report per medium for the settings snapshot; never null-values
// a report that was not produced for that medium.
void diagnostic_tests_json(JsonObject out);
