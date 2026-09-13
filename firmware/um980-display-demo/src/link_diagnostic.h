#pragma once
#include <cstddef>
#include <cstdint>
class IPAddress;
constexpr size_t kDiagnosticCapacity=6144;
void diagnostic_begin();
void diagnostic_service(uint32_t now,bool rover,IPAddress peer,bool profile_busy);
bool diagnostic_busy();
bool diagnostic_request(const char *json);
bool diagnostic_snapshot(char *out,size_t capacity);
